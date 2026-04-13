#if defined(__APPLE__)

#import <xpc/xpc.h>
#import <dispatch/dispatch.h>
#import <uuid/uuid.h>

#include "xpc.hh"

#include "../../app.hh"
#include "../../bridge.hh"
#include "../../debug.hh"
#include "../../runtime.hh"
#include "../../bytes.hh"
#include "../../crypto.hh"
#include "../../string.hh"

#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <limits>

using oro::runtime::app::App;
using oro::runtime::crypto::rand64;
using oro::runtime::bytes::base64::encode;
using oro::runtime::bytes::base64::decode;

namespace oro::runtime::core::services {
  namespace {
    constexpr std::chrono::milliseconds defaultReplyTimeout {30000};

    struct PendingMessage {
      XPC::MessageID id = 0;
      XPC::ConnectionID connectionId = 0;
      xpc_object_t message = nullptr;
      bool expectsReply = false;
    };

    struct Connection {
      XPC::ConnectionID id = 0;
      XPC::ConnectionID parentId = 0;
      xpc_connection_t handle = nullptr;
      dispatch_queue_t queue = nullptr;
      bool isListener = false;
      bool closed = false;
      String serviceName;
      String label;
      XPC* owner = nullptr;
      std::chrono::milliseconds replyTimeout {defaultReplyTimeout};
    };

    static inline JSON::Object makeError(const char* source, const char* type, const String& message) {
      return JSON::Object::Entries {
        {"source", String(source)},
        {"err", JSON::Object::Entries {
          {"type", String(type)},
          {"message", message}
        }}
      };
    }

    static inline JSON::Object makeOk(const char* source, const JSON::Any& data) {
      return JSON::Object::Entries {
        {"source", String(source)},
        {"data", data}
      };
    }

    static inline JSON::Object makeStatePayload(XPC::ConnectionID id, const char* state, const String& reason = "") {
      JSON::Object::Entries payload {
        {"connectionId", String(std::to_string(id))}
      };

      if (state != nullptr) {
        payload.emplace("state", String(state));
      }

      if (reason.size() > 0) {
        payload.emplace("reason", reason);
      }

      return JSON::Object(payload);
    }

    class AppleXPCBackend final : public XPC::Backend, public std::enable_shared_from_this<AppleXPCBackend> {
      public:
        explicit AppleXPCBackend(XPC& svc)
          : service(svc)
        {}

        ~AppleXPCBackend() override {
          this->shutdown();
        }

        JSON::Object availability() const override {
          return JSON::Object::Entries {
            {"source", String("xpc.availability")},
            {"data", JSON::Object::Entries {
              {"available", true},
              {"reason", String()}
            }}
          };
        }

        void connect(const String& seq, const JSON::Any& optionsAny, const XPC::Callback cb) override {
          auto parseOptions = [](const JSON::Any& any, ConnectOptions& out, String& error) -> bool {
            if (any.type == JSON::Type::Null) {
              error = "options must be an object";
              return false;
            }

            if (any.type != JSON::Type::Object) {
              error = "options must be an object";
              return false;
            }

            const auto& object = any.as<JSON::Object>();

            if (object.has("service")) {
              const auto& value = object.get("service");
              if (value.type != JSON::Type::String || value.str().size() == 0) {
                error = "options.service must be a non-empty string";
                return false;
              }
              out.service = value.str();
            }

            if (object.has("type")) {
              const auto& value = object.get("type");
              const auto type = value.str();
              if (type != "mach-service" && type != "named") {
                error = "options.type must be 'mach-service' or 'named'";
                return false;
              }
              out.type = type;
            }

            if (object.has("listener")) {
              const auto& value = object.get("listener");
              if (value.type == JSON::Type::Boolean) {
                out.listener = value.as<JSON::Boolean>().value();
              } else {
                out.listener = value.str() == "true" || value.str() == "1";
              }
            }

            if (object.has("privileged")) {
              const auto& value = object.get("privileged");
              if (value.type == JSON::Type::Boolean) {
                out.privileged = value.as<JSON::Boolean>().value();
              } else {
                out.privileged = value.str() == "true" || value.str() == "1";
              }
            }

            if (object.has("flags")) {
              const auto& value = object.get("flags");
              try {
                if (value.type == JSON::Type::Number) {
                  const auto numeric = value.as<JSON::Number>().value();
                  if (numeric < 0) {
                    error = "options.flags must be a non-negative integer";
                    return false;
                  }
                  out.flags = static_cast<uint64_t>(numeric);
                } else {
                  out.flags = static_cast<uint64_t>(std::stoull(value.str()));
                }
              } catch (...) {
                error = "options.flags must be a non-negative integer";
                return false;
              }
            }

            if (object.has("label")) {
              const auto& value = object.get("label");
              out.label = value.str();
            }

            if (object.has("pendingReplyTimeout")) {
              const auto& value = object.get("pendingReplyTimeout");
              uint64_t timeoutMs = 0;
              try {
                if (value.type == JSON::Type::Number) {
                  const auto numeric = value.as<JSON::Number>().value();
                  if (numeric < 0) {
                    error = "options.pendingReplyTimeout must be a non-negative integer";
                    return false;
                  }
                  timeoutMs = static_cast<uint64_t>(numeric);
                } else {
                  timeoutMs = static_cast<uint64_t>(std::stoull(value.str()));
                }
              } catch (...) {
                error = "options.pendingReplyTimeout must be a non-negative integer";
                return false;
              }

              if (timeoutMs > maxPendingReplyTimeoutMs) {
                error = "options.pendingReplyTimeout exceeds maximum supported duration";
                return false;
              }

              out.replyTimeout = std::chrono::milliseconds(timeoutMs);
            }

            if (out.service.size() == 0) {
              error = "options.service is required";
              return false;
            }

            if (out.listener && out.type != "mach-service") {
              error = "options.listener requires type == 'mach-service'";
              return false;
            }

            return true;
          };

          JSON::Any parsedOptions;
          String parseError;
          if (!this->parseRawJson(optionsAny, parsedOptions, parseError)) {
            cb(seq, makeError("xpc.connect", "SyntaxError", parseError), QueuedResponse{});
            return;
          }

          ConnectOptions options;
          String error;
          if (!parseOptions(parsedOptions, options, error)) {
            cb(seq, makeError("xpc.connect", "TypeError", error), QueuedResponse{});
            return;
          }

          auto connection = this->createConnection(options);
          if (!connection) {
            cb(seq, makeError("xpc.connect", "InternalError", "Failed to create XPC connection"), QueuedResponse{});
            return;
          }

          const auto idStr = String(std::to_string(connection->id));
          JSON::Object::Entries data {
            {"id", idStr},
            {"connectionId", idStr},
            {"listener", connection->isListener ? "true" : "false"},
            {"service", connection->serviceName}
          };

          if (connection->label.size() > 0) {
            data.emplace("label", connection->label);
          }

          cb(seq, makeOk("xpc.connect", JSON::Object(data)), QueuedResponse{});
        }

        void disconnect(const String& seq, XPC::ConnectionID id, const XPC::Callback cb) override {
          auto conn = this->getConnection(id);
          if (!conn) {
            cb(seq, makeError("xpc.disconnect", "InvalidStateError", "Unknown XPC connection id"), QueuedResponse{});
            return;
          }

          const auto closed = this->closeConnection(conn);
          cb(seq, makeOk("xpc.disconnect", JSON::Object::Entries {
            {"connectionId", String(std::to_string(id))}
          }), QueuedResponse{});

          for (const auto& closedConn : closed) {
            if (!closedConn) {
              continue;
            }
            const auto reason = closedConn->id == id
              ? String("Connection closed by request")
              : String("Connection closed with parent listener");
            this->emitState(closedConn->id, "closed", reason);
          }
        }

        void send(
          const String& seq,
          XPC::ConnectionID id,
          const JSON::Any& messageAny,
          bool expectReply,
          uint64_t timeoutMs,
          const XPC::Callback cb
        ) override {
          auto conn = this->getConnection(id);
          if (!conn) {
            cb(seq, makeError("xpc.send", "InvalidStateError", "Unknown XPC connection id"), QueuedResponse{});
            return;
          }

          JSON::Any parsedMessage;
          String parseError;
          if (!this->parseRawJson(messageAny, parsedMessage, parseError)) {
            cb(seq, makeError("xpc.send", "SyntaxError", parseError), QueuedResponse{});
            return;
          }

          String error;
          auto message = this->decodeValue(parsedMessage, error);
          if (!message) {
            cb(seq, makeError("xpc.send", "TypeError", error.size() > 0 ? error : String("Invalid XPC message payload")), QueuedResponse{});
            return;
          }

          if (expectReply && timeoutMs > 0) {
            constexpr uint64_t maxTimeoutMs = static_cast<uint64_t>(std::numeric_limits<int64_t>::max() / 1000000ULL);
            if (timeoutMs > maxTimeoutMs) {
              cb(seq, makeError("xpc.send", "RangeError", "timeout exceeds maximum supported duration"), QueuedResponse{});
              xpc_release(message);
              return;
            }
          }

          if (expectReply) {
            const auto seqCopy = seq;
            std::weak_ptr<AppleXPCBackend> weakSelf = this->weak_from_this();
            auto replyHandled = std::make_shared<std::atomic<bool>>(false);
            auto connectionId = conn->id;
            auto callbackCopy = cb;
            auto deliverReply = [weakSelf, seqCopy, callbackCopy](JSON::Object json) {
              auto invoke = [callbackCopy, seqCopy, json = std::move(json)]() mutable {
                callbackCopy(seqCopy, json, QueuedResponse{});
              };

              if (auto backend = weakSelf.lock()) {
                if (!backend->service.dispatch(invoke)) {
                  invoke();
                }
              } else {
                invoke();
              }
            };

            xpc_connection_send_message_with_reply(
              conn->handle,
              message,
              conn->queue,
              ^(xpc_object_t response) {
                if (!response || *replyHandled) {
                  return;
                }

                *replyHandled = true;

                JSON::Object json;

                if (response && xpc_get_type(response) == XPC_TYPE_ERROR) {
                  if (auto backend = weakSelf.lock()) {
                    json = backend->encodeErrorPayload(connectionId, response);
                  } else {
                    json = makeError("xpc.send", "InvalidStateError", "XPC backend unavailable");
                  }
                } else {
                  if (auto backend = weakSelf.lock()) {
                    json = backend->encodeReplyPayload(connectionId, response);
                  } else {
                    json = makeError("xpc.send", "InvalidStateError", "XPC backend unavailable");
                  }
                }

                deliverReply(std::move(json));
              }
            );
            xpc_release(message);

            if (timeoutMs > 0) {
              const int64_t timeoutNs = static_cast<int64_t>(timeoutMs) * 1000000LL;
              dispatch_after(
                dispatch_time(DISPATCH_TIME_NOW, timeoutNs),
                conn->queue,
                ^{
                  if (*replyHandled) {
                    return;
                  }
                  *replyHandled = true;
                  deliverReply(makeError("xpc.send", "TimeoutError", "Timed out waiting for XPC reply"));
                }
              );
            }
          } else {
            xpc_connection_send_message(conn->handle, message);
            xpc_release(message);
            cb(seq, makeOk("xpc.send", JSON::Object::Entries {
              {"connectionId", String(std::to_string(conn->id))},
              {"status", String("ok")}
            }), QueuedResponse{});
          }
        }

        void respond(
          const String& seq,
          XPC::MessageID id,
          bool isError,
          const JSON::Any& messageAny,
          const XPC::Callback cb
        ) override {
          PendingMessage pending;
          {
            Lock lock(this->pendingMutex);
            auto it = this->pendingMessages.find(id);
            if (it == this->pendingMessages.end()) {
              cb(seq, makeError(isError ? "xpc.respondError" : "xpc.respond", "InvalidStateError", "Unknown XPC message id"), QueuedResponse{});
              return;
            }
            pending = it->second;
            this->pendingMessages.erase(it);
          }

          auto conn = this->getConnection(pending.connectionId);
          if (!conn) {
            cb(seq, makeError(isError ? "xpc.respondError" : "xpc.respond", "InvalidStateError", "Connection for pending message no longer exists"), QueuedResponse{});
            if (pending.message) {
              xpc_release(pending.message);
            }
            return;
          }

          if (!pending.message || !pending.expectsReply) {
            if (pending.message) {
              xpc_release(pending.message);
            }
            cb(seq, makeError(isError ? "xpc.respondError" : "xpc.respond", "InvalidStateError", "Message does not accept replies"), QueuedResponse{});
            return;
          }

          JSON::Any parsedPayload;
          String parseError;
          if (!this->parseRawJson(messageAny, parsedPayload, parseError)) {
            if (pending.message) {
              xpc_release(pending.message);
            }
            cb(seq, makeError(isError ? "xpc.respondError" : "xpc.respond", "SyntaxError", parseError), QueuedResponse{});
            return;
          }

          String error;
          auto payload = this->decodeValue(parsedPayload, error);
          if (!payload) {
            if (pending.message) {
              xpc_release(pending.message);
            }
            cb(seq, makeError(isError ? "xpc.respondError" : "xpc.respond", "TypeError", error.size() > 0 ? error : String("Invalid XPC reply payload")), QueuedResponse{});
            return;
          }

          auto reply = xpc_dictionary_create_reply(pending.message);
          if (!reply) {
            if (pending.message) {
              xpc_release(pending.message);
            }
            cb(seq, makeError(isError ? "xpc.respondError" : "xpc.respond", "InvalidStateError", "Unable to create XPC reply message"), QueuedResponse{});
            return;
          }

          if (payload) {
            this->copyDecodedToDictionary(reply, payload);
            xpc_release(payload);
          }

          if (isError) {
            xpc_dictionary_set_bool(reply, "error", true);
          }

          xpc_connection_send_message(conn->handle, reply);
          xpc_release(reply);

          if (pending.message) {
            xpc_release(pending.message);
          }

          cb(seq, makeOk(isError ? "xpc.respondError" : "xpc.respond", JSON::Object::Entries {
            {"messageId", String(std::to_string(id))},
            {"status", String("sent")}
          }), QueuedResponse{});
        }

        void suspend(const String& seq, XPC::ConnectionID id, const XPC::Callback cb) override {
          auto conn = this->getConnection(id);
          if (!conn) {
            cb(seq, makeError("xpc.suspend", "InvalidStateError", "Unknown XPC connection id"), QueuedResponse{});
            return;
          }
          xpc_connection_suspend(conn->handle);
          cb(seq, makeOk("xpc.suspend", JSON::Object::Entries {
            {"connectionId", String(std::to_string(id))},
            {"state", String("suspended")}
          }), QueuedResponse{});
        }

        void resume(const String& seq, XPC::ConnectionID id, const XPC::Callback cb) override {
          auto conn = this->getConnection(id);
          if (!conn) {
            cb(seq, makeError("xpc.resume", "InvalidStateError", "Unknown XPC connection id"), QueuedResponse{});
            return;
          }
          xpc_connection_resume(conn->handle);
          cb(seq, makeOk("xpc.resume", JSON::Object::Entries {
            {"connectionId", String(std::to_string(id))},
            {"state", String("active")}
          }), QueuedResponse{});
        }

      private:
        using ConnectionID = XPC::ConnectionID;
        static constexpr std::chrono::milliseconds defaultPendingReplyTimeout {defaultReplyTimeout};
        static constexpr uint64_t maxPendingReplyTimeoutMs = static_cast<uint64_t>(std::numeric_limits<int64_t>::max() / 1000000ULL);
        static constexpr size_t maxPendingPerConnection = 64;

        XPC& service;
        struct ConnectOptions {
          String service;
          String type = "mach-service";
          bool listener = false;
          bool privileged = false;
          uint64_t flags = 0;
          String label;
          std::chrono::milliseconds replyTimeout = defaultPendingReplyTimeout;
        };

        mutable Mutex connectionsMutex;
        std::unordered_map<ConnectionID, std::shared_ptr<Connection>> connections;
        mutable Mutex pendingMutex;
        std::unordered_map<XPC::MessageID, PendingMessage> pendingMessages;
        std::atomic<ConnectionID> nextConnectionId {1};
        std::atomic<XPC::MessageID> nextMessageId {1};

        void shutdown() {
          std::vector<std::shared_ptr<Connection>> toClose;
          {
            Lock lock(this->connectionsMutex);
            for (auto& entry : this->connections) {
              if (entry.second) {
                toClose.push_back(entry.second);
              }
            }
            this->connections.clear();
          }

          for (const auto& connection : toClose) {
            if (!connection) {
              continue;
            }
            this->clearPendingForConnection(connection->id);
            this->tearDownConnectionResources(connection);
          }

          std::vector<PendingMessage> remainingPending;
          {
            Lock pendingLock(this->pendingMutex);
            for (auto& entry : this->pendingMessages) {
              remainingPending.push_back(entry.second);
            }
            this->pendingMessages.clear();
          }

          for (auto& pending : remainingPending) {
            if (pending.message) {
              xpc_release(pending.message);
              pending.message = nullptr;
            }
          }
        }

        std::shared_ptr<Connection> createConnection(const ConnectOptions& options) {
          uint64_t flags = options.flags;
          if (options.listener) {
            flags |= XPC_CONNECTION_MACH_SERVICE_LISTENER;
          }
          if (options.privileged) {
            flags |= XPC_CONNECTION_MACH_SERVICE_PRIVILEGED;
          }

          xpc_connection_t handle = nullptr;
          dispatch_queue_t queue = dispatch_queue_create("oro.runtime.xpc.connection", DISPATCH_QUEUE_SERIAL);

          if (options.type == "mach-service") {
            handle = xpc_connection_create_mach_service(
              options.service.c_str(),
              queue,
              flags
            );
          } else {
            handle = xpc_connection_create(
              options.service.c_str(),
              queue
            );
          }

          if (!handle) {
#if !__has_feature(objc_arc)
            if (queue) {
              dispatch_release(queue);
            }
#endif
            return nullptr;
          }

          auto connection = std::make_shared<Connection>();
          connection->id = this->nextConnectionId.fetch_add(1);
          connection->handle = handle;
          connection->queue = queue;
          connection->isListener = options.listener;
          connection->serviceName = options.service;
          connection->label = options.label.size() > 0 ? options.label : options.service;
          connection->owner = &this->service;
          connection->replyTimeout = options.replyTimeout;

          this->registerConnection(connection);

          return connection;
        }

        void registerConnection(const std::shared_ptr<Connection>& connection) {
          if (!connection) {
            return;
          }

          {
            Lock lock(this->connectionsMutex);
            this->connections[connection->id] = connection;
          }

          std::weak_ptr<Connection> weakConnection = connection;
          std::weak_ptr<AppleXPCBackend> weakBackend;
          try {
            weakBackend = this->shared_from_this();
          } catch (const std::bad_weak_ptr&) {
            debug("xpc: backend must be managed by std::shared_ptr before registering connection");
            this->closeConnection(connection);
            return;
          }

          xpc_connection_set_event_handler(connection->handle, ^(xpc_object_t event) {
            auto shared = weakConnection.lock();
            auto backend = weakBackend.lock();
            if (!shared || !backend) return;
            backend->handleEvent(shared, event);
          });

          xpc_connection_resume(connection->handle);
        }

        std::shared_ptr<Connection> getConnection(ConnectionID id) const {
          Lock lock(this->connectionsMutex);
          auto it = this->connections.find(id);
          if (it == this->connections.end()) {
            return nullptr;
          }
          return it->second;
        }

        std::vector<std::shared_ptr<Connection>> closeConnection(const std::shared_ptr<Connection>& connection) {
          std::vector<std::shared_ptr<Connection>> closed;
          if (!connection) {
            return closed;
          }

          {
            Lock lock(this->connectionsMutex);
            this->collectConnectionAndChildrenLocked(connection->id, closed);
          }

          for (const auto& connToClose : closed) {
            if (!connToClose) {
              continue;
            }
            this->clearPendingForConnection(connToClose->id);
            this->tearDownConnectionResources(connToClose);
          }

          return closed;
        }

        void collectConnectionAndChildrenLocked(ConnectionID id, std::vector<std::shared_ptr<Connection>>& out) {
          std::vector<ConnectionID> children;
          children.reserve(this->connections.size());

          for (const auto& entry : this->connections) {
            const auto& child = entry.second;
            if (child && child->parentId == id) {
              children.push_back(entry.first);
            }
          }

          for (const auto childId : children) {
            this->collectConnectionAndChildrenLocked(childId, out);
          }

          auto it = this->connections.find(id);
          if (it == this->connections.end()) {
            return;
          }

          out.push_back(it->second);
          this->connections.erase(it);
        }

        void tearDownConnectionResources(const std::shared_ptr<Connection>& connection) {
          if (!connection) {
            return;
          }

          if (connection->handle && !connection->closed) {
            xpc_connection_cancel(connection->handle);
            connection->closed = true;
          }

          if (connection->handle) {
            xpc_release(connection->handle);
            connection->handle = nullptr;
          }

          if (connection->queue) {
#if !__has_feature(objc_arc)
            dispatch_release(connection->queue);
#endif
            connection->queue = nullptr;
          }
        }

        void handleEvent(const std::shared_ptr<Connection>& connection, xpc_object_t event) {
          if (!connection || !event) {
            return;
          }

          if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
            this->emitState(connection->id, "interrupted", "");
            return;
          }

          if (event == XPC_ERROR_CONNECTION_INVALID) {
            this->emitState(connection->id, "invalid", "");
            const auto closed = this->closeConnection(connection);
            for (const auto& child : closed) {
              if (!child || child->id == connection->id) {
                continue;
              }
              this->emitState(child->id, "invalid", "Parent connection became invalid");
            }
            return;
          }

          if (event == XPC_ERROR_TERMINATION_IMMINENT) {
            this->emitState(connection->id, "terminationImminent", "");
            return;
          }

          const auto type = xpc_get_type(event);

          if (type == XPC_TYPE_DICTIONARY) {
            this->handleMessage(connection, event);
          } else if (type == XPC_TYPE_CONNECTION) {
            this->handleIncomingConnection(connection, event);
          } else if (type == XPC_TYPE_ERROR) {
            this->emitEvent("xpc.error", this->encodeErrorPayload(connection->id, event));
          } else {
            auto payload = JSON::Object::Entries {
              {"connectionId", String(std::to_string(connection->id))},
              {"type", String("unknown")},
              {"description", String("Unsupported XPC event type")}
            };
            this->emitEvent("xpc.error", JSON::Object::Entries {
              {"source", String("xpc.error")},
              {"data", JSON::Object(payload)}
            });
          }
        }

        void handleIncomingConnection(const std::shared_ptr<Connection>& parent, xpc_object_t event) {
          if (!parent || !parent->isListener || !event) {
            return;
          }

          auto queue = dispatch_queue_create("oro.runtime.xpc.connection.peer", DISPATCH_QUEUE_SERIAL);
          xpc_connection_set_target_queue(event, queue);

          auto child = std::make_shared<Connection>();
          child->id = this->nextConnectionId.fetch_add(1);
          child->parentId = parent->id;
          child->handle = event;
          child->queue = queue;
          child->isListener = false;
          child->serviceName = parent->serviceName;
          child->label = parent->label;
          child->owner = &this->service;
          child->replyTimeout = parent->replyTimeout;

          xpc_retain(event);

          {
            Lock lock(this->connectionsMutex);
            this->connections[child->id] = child;
          }

          std::weak_ptr<Connection> weakChild = child;
          std::weak_ptr<AppleXPCBackend> weakBackend;
          try {
            weakBackend = this->shared_from_this();
          } catch (const std::bad_weak_ptr&) {
            debug("xpc: backend must be managed by std::shared_ptr before registering listener peer");
            this->closeConnection(child);
            return;
          }

          xpc_connection_set_event_handler(child->handle, ^(xpc_object_t message) {
            auto shared = weakChild.lock();
            auto backend = weakBackend.lock();
            if (!shared || !backend) return;
            backend->handleEvent(shared, message);
          });

          xpc_connection_resume(child->handle);

          JSON::Object::Entries data {
            {"listenerId", String(std::to_string(parent->id))},
            {"connectionId", String(std::to_string(child->id))},
            {"service", child->serviceName},
            {"label", child->label}
          };

          this->emitEvent("xpc.listener.connection", JSON::Object::Entries {
            {"source", String("xpc.listener.connection")},
            {"data", JSON::Object(data)}
          });
        }

        void handleMessage(const std::shared_ptr<Connection>& connection, xpc_object_t message) {
          if (!connection || !message) {
            return;
          }

          bool expectsReply = false;
          if (auto replyProbe = xpc_dictionary_create_reply(message)) {
            expectsReply = true;
            xpc_release(replyProbe);
          }
          XPC::MessageID messageId = 0;

          if (expectsReply) {
            messageId = this->nextMessageId.fetch_add(1);
            xpc_retain(message);
            PendingMessage pending;
            pending.id = messageId;
            pending.connectionId = connection->id;
            pending.message = message;
            pending.expectsReply = true;

            if (!this->addPendingMessage(pending)) {
              this->sendImmediateErrorResponse(connection, message, "Too many pending XPC replies awaiting handling");
              xpc_release(message);
              this->emitMessageDropped(connection->id, String("Exceeded pending XPC reply limit"));
              return;
            }

            this->schedulePendingTimeout(messageId, connection->id, connection->queue, connection->replyTimeout);
          }

          const auto encoded = this->encodeValue(message);
          JSON::Object::Entries data {
            {"connectionId", String(std::to_string(connection->id))},
            {"message", encoded}
          };

          if (expectsReply) {
            data.emplace("messageId", String(std::to_string(messageId)));
            data.emplace("expectsReply", "true");
          } else {
            data.emplace("expectsReply", "false");
          }

          if (connection->parentId > 0) {
            data.emplace("listenerId", String(std::to_string(connection->parentId)));
          }

          this->emitEvent("xpc.message", JSON::Object::Entries {
            {"source", String("xpc.message")},
            {"data", JSON::Object(data)}
          });
        }

        JSON::Object encodeReplyPayload(ConnectionID id, xpc_object_t reply) const {
          return JSON::Object::Entries {
            {"source", String("xpc.send")},
            {"data", JSON::Object::Entries {
              {"connectionId", String(std::to_string(id))},
              {"message", this->encodeValue(reply)},
              {"error", JSON::Any(false)}
            }}
          };
        }

        JSON::Object encodeErrorPayload(ConnectionID id, xpc_object_t errorObject) const {
          String reason = "Unknown XPC error";
          if (errorObject == XPC_ERROR_CONNECTION_INTERRUPTED) {
            reason = "Connection interrupted";
          } else if (errorObject == XPC_ERROR_CONNECTION_INVALID) {
            reason = "Connection invalid";
          } else if (errorObject == XPC_ERROR_TERMINATION_IMMINENT) {
            reason = "Termination imminent";
          } else if (xpc_get_type(errorObject) == XPC_TYPE_DICTIONARY) {
            const char* description = xpc_dictionary_get_string(errorObject, XPC_ERROR_KEY_DESCRIPTION);
            if (description) {
              reason = description;
            }
          }

          return JSON::Object::Entries {
            {"source", String("xpc.error")},
            {"data", JSON::Object::Entries {
              {"connectionId", String(std::to_string(id))},
              {"message", reason},
              {"reason", reason},
              {"error", JSON::Any(true)}
            }}
          };
        }

        void emitState(ConnectionID id, const char* state, const String& reason) {
          JSON::Object::Entries payload {
            {"source", String("xpc.state")},
            {"data", makeStatePayload(id, state, reason)}
          };

          this->emitEvent("xpc.state", JSON::Object(payload));
        }

        void emitEvent(const char* eventName, const JSON::Object& payload) const {
          auto json = payload.str();
          auto name = String(eventName);

          this->service.dispatch([json, name]() {
            auto app = App::sharedApplication();
            if (!app) {
              return;
            }
            auto* runtime = &app->runtime;
            for (const auto& window : runtime->windowManager.windows) {
              if (window && window->bridge) {
                window->bridge->emit(name, json);
              }
            }
          });
        }

        bool addPendingMessage(const PendingMessage& pending) {
          Lock lock(this->pendingMutex);

          size_t count = 0;
          for (const auto& entry : this->pendingMessages) {
            if (entry.second.connectionId == pending.connectionId) {
              count++;
            }
          }

          if (count >= maxPendingPerConnection) {
            return false;
          }

          this->pendingMessages.emplace(pending.id, pending);
          return true;
        }

        void schedulePendingTimeout(XPC::MessageID id, ConnectionID connectionId, dispatch_queue_t queue, std::chrono::milliseconds timeout) {
          if (!queue || timeout.count() <= 0) {
            return;
          }

          std::weak_ptr<AppleXPCBackend> weakSelf = this->weak_from_this();
          const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();

          dispatch_after(
            dispatch_time(DISPATCH_TIME_NOW, nanoseconds),
            queue,
            ^{
              auto backend = weakSelf.lock();
              if (!backend) {
                return;
              }
              backend->handlePendingTimeout(id, connectionId);
            }
          );
        }

        void handlePendingTimeout(XPC::MessageID id, ConnectionID connectionId) {
          PendingMessage pending;
          bool found = false;

          {
            Lock lock(this->pendingMutex);
            auto it = this->pendingMessages.find(id);
            if (it != this->pendingMessages.end() && it->second.connectionId == connectionId) {
              pending = it->second;
              this->pendingMessages.erase(it);
              found = true;
            }
          }

          if (!found) {
            return;
          }

          this->resolvePendingWithError(pending, "TimeoutError", String("Timed out waiting for XPC reply"));
          this->emitMessageTimeout(connectionId, id, String("Timed out waiting for XPC reply"));
        }

        void resolvePendingWithError(const PendingMessage& pending, const char* type, const String& reason) {
          auto conn = this->getConnection(pending.connectionId);

          if (conn && pending.message) {
            auto reply = xpc_dictionary_create_reply(pending.message);
            if (reply) {
              xpc_dictionary_set_bool(reply, "error", true);
              if (type && std::strlen(type) > 0) {
                xpc_dictionary_set_string(reply, "type", type);
              }
              if (reason.size() > 0) {
                xpc_dictionary_set_string(reply, "reason", reason.c_str());
              }
              xpc_connection_send_message(conn->handle, reply);
              xpc_release(reply);
            }
          }

          if (pending.message) {
            xpc_release(pending.message);
          }
        }

        void emitMessageTimeout(ConnectionID id, XPC::MessageID messageId, const String& reason) {
          JSON::Object::Entries data {
            {"connectionId", String(std::to_string(id))},
            {"messageId", String(std::to_string(messageId))}
          };

          if (reason.size() > 0) {
            data.emplace("reason", reason);
          }

          this->emitEvent("xpc.message.timeout", JSON::Object::Entries {
            {"source", String("xpc.message.timeout")},
            {"data", JSON::Object(data)}
          });
        }

        void emitMessageDropped(ConnectionID id, const String& reason) {
          JSON::Object::Entries data {
            {"connectionId", String(std::to_string(id))}
          };

          if (reason.size() > 0) {
            data.emplace("reason", reason);
          }

          this->emitEvent("xpc.message.dropped", JSON::Object::Entries {
            {"source", String("xpc.message.dropped")},
            {"data", JSON::Object(data)}
          });
        }

        void sendImmediateErrorResponse(const std::shared_ptr<Connection>& connection, xpc_object_t message, const char* reason) {
          if (!connection || !message) {
            return;
          }

          auto reply = xpc_dictionary_create_reply(message);
          if (!reply) {
            return;
          }

          xpc_dictionary_set_bool(reply, "error", true);
          if (reason && std::strlen(reason) > 0) {
            xpc_dictionary_set_string(reply, "reason", reason);
          }

          xpc_connection_send_message(connection->handle, reply);
          xpc_release(reply);
        }

        bool parseRawJson(const JSON::Any& input, JSON::Any& output, String& error) const {
          output = input;

          if (output.type == JSON::Type::Raw || output.type == JSON::Type::String) {
            const auto source = output.str();
            if (!source.empty()) {
              try {
                output = JSON::parse(source);
              } catch (const JSON::Error& jsonError) {
                error = String("Failed to parse JSON payload: ") + jsonError.what();
                return false;
              } catch (const std::exception& stdError) {
                error = String("Failed to parse JSON payload: ") + stdError.what();
                return false;
              }
            } else {
              output = nullptr;
            }
          }

          return true;
        }

        JSON::Object encodeValue(xpc_object_t value) const {
          if (!value) {
            return JSON::Object::Entries {
              {"type", String("null")}
            };
          }

          const auto type = xpc_get_type(value);

          if (type == XPC_TYPE_DICTIONARY) {
            __block JSON::Object::Entries entries;
            xpc_dictionary_apply(value, ^bool(const char* key, xpc_object_t val) {
              entries.emplace(String(key), this->encodeAny(val));
              return true;
            });
            return JSON::Object::Entries {
              {"type", String("dictionary")},
              {"value", JSON::Object(entries)}
            };
          }

          if (type == XPC_TYPE_ARRAY) {
            JSON::Array::Entries entries;
            const size_t count = xpc_array_get_count(value);
            for (size_t index = 0; index < count; ++index) {
              entries.push_back(this->encodeAny(xpc_array_get_value(value, index)));
            }

            return JSON::Object::Entries {
              {"type", String("array")},
              {"value", JSON::Array(entries)}
            };
          }

          return this->encodeAny(value).template as<JSON::Object>();
        }

        JSON::Any encodeAny(xpc_object_t value) const {
          const auto type = xpc_get_type(value);

          if (type == XPC_TYPE_BOOL) {
            return JSON::Object::Entries {
              {"type", String("bool")},
              {"value", value == XPC_BOOL_TRUE ? "true" : "false"}
            };
          }

          if (type == XPC_TYPE_INT64) {
            const auto number = xpc_int64_get_value(value);
            return JSON::Object::Entries {
              {"type", String("int64")},
              {"value", String(std::to_string(number))}
            };
          }

          if (type == XPC_TYPE_UINT64) {
            const auto number = xpc_uint64_get_value(value);
            return JSON::Object::Entries {
              {"type", String("uint64")},
              {"value", String(std::to_string(number))}
            };
          }

          if (type == XPC_TYPE_DOUBLE) {
            const auto number = xpc_double_get_value(value);
            return JSON::Object::Entries {
              {"type", String("double")},
              {"value", JSON::Number(number)}
            };
          }

          if (type == XPC_TYPE_STRING) {
            const char* str = xpc_string_get_string_ptr(value);
            return JSON::Object::Entries {
              {"type", String("string")},
              {"value", String(str ? str : "")}
            };
          }

          if (type == XPC_TYPE_DATA) {
            const size_t length = xpc_data_get_length(value);
            const auto* bytes = static_cast<const uint8_t*>(xpc_data_get_bytes_ptr(value));
            Vector<uint8_t> buffer(length);
            if (bytes && length > 0) {
              memcpy(buffer.data(), bytes, length);
            }
            return JSON::Object::Entries {
              {"type", String("data")},
              {"encoding", String("base64")},
              {"value", encode(buffer)}
            };
          }

          if (type == XPC_TYPE_UUID) {
            uuid_t uuid;
            const auto* bytes = xpc_uuid_get_bytes(value);
            if (bytes) {
              memcpy(uuid, bytes, sizeof(uuid_t));
            } else {
              memset(uuid, 0, sizeof(uuid_t));
            }
            char buffer[37];
            uuid_unparse(uuid, buffer);
            return JSON::Object::Entries {
              {"type", String("uuid")},
              {"value", String(buffer)}
            };
          }

          if (type == XPC_TYPE_NULL) {
            return JSON::Object::Entries {
              {"type", String("null")}
            };
          }

          return JSON::Object::Entries {
            {"type", String("unsupported")},
            {"value", String("Unsupported XPC type")}
          };
        }

        xpc_object_t decodeValue(const JSON::Any& value, String& error) {
          if (value.type != JSON::Type::Object) {
            error = "Value must be an object with a type field";
            return nullptr;
          }

          const auto& object = value.as<JSON::Object>();
          if (!object.has("type")) {
            error = "Missing type field in XPC value";
            return nullptr;
          }

          const auto type = object.get("type").str();

          if (type == "dictionary") {
            if (!object.has("value") || object.get("value").type != JSON::Type::Object) {
              error = "Dictionary value must be an object";
              return nullptr;
            }

            const auto& objectValue = object.get("value").as<JSON::Object>();
            auto dict = xpc_dictionary_create(nullptr, nullptr, 0);
            for (auto it = objectValue.begin(); it != objectValue.end(); ++it) {
              const auto& key = it->first;
              const auto& any = it->second;
              String nestedError;
              auto nested = this->decodeValue(any, nestedError);
              if (!nested) {
                if (nestedError.size() == 0) {
                  nestedError = String("Invalid XPC value for key '") + key + String("'");
                }
                xpc_release(dict);
                error = nestedError;
                return nullptr;
              }
              xpc_dictionary_set_value(dict, key.c_str(), nested);
              xpc_release(nested);
            }
            return dict;
          }

          if (type == "array") {
            if (!object.has("value") || object.get("value").type != JSON::Type::Array) {
              error = "Array value must be an array";
              return nullptr;
            }

            const auto& arrayValue = object.get("value").as<JSON::Array>();
            auto xpcArray = xpc_array_create(nullptr, 0);

            for (size_t index = 0; index < arrayValue.size(); ++index) {
              const auto& entry = arrayValue[index];
              String nestedError;
              auto nested = this->decodeValue(entry, nestedError);
              if (!nested) {
                if (nestedError.size() == 0) {
                  const auto indexString = std::to_string(index);
                  nestedError = String("Invalid XPC value at index ") + String(indexString.c_str());
                }
                xpc_release(xpcArray);
                error = nestedError;
                return nullptr;
              }
              xpc_array_set_value(xpcArray, XPC_ARRAY_APPEND, nested);
              xpc_release(nested);
            }

            return xpcArray;
          }

          if (type == "string") {
            auto str = object.get("value").str();
            return xpc_string_create(str.c_str());
          }

          if (type == "bool") {
            auto str = object.get("value").str();
            const bool valueBool = (str == "true" || str == "1");
            return valueBool ? XPC_BOOL_TRUE : XPC_BOOL_FALSE;
          }

          if (type == "int64") {
            auto raw = object.get("value").str();
            try {
              const auto number = std::stoll(raw);
              return xpc_int64_create(number);
            } catch (...) {
              error = "Invalid int64 value";
              return nullptr;
            }
          }

          if (type == "uint64") {
            auto raw = object.get("value").str();
            try {
              const auto number = std::stoull(raw);
              return xpc_uint64_create(number);
            } catch (...) {
              error = "Invalid uint64 value";
              return nullptr;
            }
          }

          if (type == "double") {
            auto raw = object.get("value").str();
            try {
              const auto number = std::stod(raw);
              return xpc_double_create(number);
            } catch (...) {
              error = "Invalid double value";
              return nullptr;
            }
          }

          if (type == "data") {
            auto encodedData = object.get("value").str();
            auto decoded = decode(encodedData);
            return xpc_data_create(decoded.data(), decoded.size());
          }

          if (type == "uuid") {
            auto raw = object.get("value").str();
            uuid_t uuid;
            if (uuid_parse(raw.c_str(), uuid) != 0) {
              error = "Invalid UUID value";
              return nullptr;
            }
            return xpc_uuid_create(uuid);
          }

          if (type == "null") {
            return xpc_null_create();
          }

          error = "Unsupported XPC value type";
          return nullptr;
        }

        void copyDecodedToDictionary(xpc_object_t dictionary, xpc_object_t payload) {
          if (!dictionary || !payload) {
            return;
          }

          const auto type = xpc_get_type(payload);
          if (type == XPC_TYPE_DICTIONARY) {
            xpc_dictionary_apply(payload, ^bool(const char* key, xpc_object_t value) {
              xpc_dictionary_set_value(dictionary, key, value);
              return true;
            });
          } else {
            xpc_dictionary_set_value(dictionary, "value", payload);
          }
        }

        void clearPendingForConnection(ConnectionID id) {
          Lock lock(this->pendingMutex);
          for (auto it = this->pendingMessages.begin(); it != this->pendingMessages.end();) {
            if (it->second.connectionId == id) {
              if (it->second.message) {
                xpc_release(it->second.message);
              }
              it = this->pendingMessages.erase(it);
            } else {
              ++it;
            }
          }
        }
    };

  }

  std::shared_ptr<XPC::Backend> makeXPCBackend(XPC& svc) {
    return std::make_shared<AppleXPCBackend>(svc);
  }
}

#endif
