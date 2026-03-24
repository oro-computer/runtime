#include "dbus.hh"

#include "../../debug.hh"
#include "../../env.hh"
#include "../../app.hh"
#include "../../runtime.hh"
#include "../../bridge.hh"
#include "../../string.hh"
#include "../../queued_response.hh"
#include "../../crypto.hh"

#include "../../bytes.hh"

#if ORO_RUNTIME_HAVE_DBUS
#  include <dbus/dbus.h>
#endif

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <exception>

using oro::runtime::crypto::rand64;
using oro::runtime::app::App;

namespace oro::runtime::core::services {

#if ORO_RUNTIME_HAVE_DBUS
  static JSON::Any encodeValue(DBusMessageIter*);
  static bool appendValuesFromJSON(const String& signature, const JSON::Any& value, DBusMessageIter* iter, String& errorMessage);
#endif

  namespace {
    inline JSON::Object makeError(const char* type, const String& message) {
      return JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String(type)},
          {"message", message}
        }}}
      }};
    }

    inline JSON::Object notSupported(const String& message) {
      return makeError("NotSupportedError", message);
    }

    inline JSON::Object invalidState(const String& message) {
      return makeError("InvalidStateError", message);
    }

    inline JSON::Object typeError(const String& message) {
      return makeError("TypeError", message);
    }

    inline JSON::Object invalidAccess(const String& message) {
      return makeError("InvalidAccessError", message);
    }

    inline String jsonStringValue(const JSON::Any& value) {
      if (value.type == JSON::Type::String) {
        return value.as<JSON::String>().value();
      }

      if (value.type == JSON::Type::Raw) {
        return value.as<JSON::Raw>().value();
      }

      return value.str();
    }
  }

  struct DBus::Implementation {
    DBus& service;
    std::atomic<bool> started {false};

    #if ORO_RUNTIME_HAVE_DBUS
    struct PendingReply;
    struct PendingMethod;

    static DBusHandlerResult connectionFilter(DBusConnection*, DBusMessage*, void*);
    static DBusHandlerResult objectPathFilter(DBusConnection*, DBusMessage*, void*);

    struct Connection {
      ConnectionID id = 0;
      DBusConnection* handle = nullptr;
      std::atomic<bool> running {false};
      std::thread pump;
      std::mutex mutex;
      struct ExportedObject {
        ExportID id = 0;
        String path;
        String interface;
        DBusObjectPathVTable vtable {};
        Connection* connection = nullptr;
        std::unordered_set<String> methods;
      };

      std::unordered_map<DBusPendingCall*, PendingReply*> replies;
      std::unordered_map<MatchID, String> matches;
      std::unordered_map<ExportID, SharedPointer<ExportedObject>> exports;
      std::unordered_map<String, ExportID> exportsByPath;
      std::unordered_map<CallID, PendingMethod*> pendingMethods;
      Implementation* owner = nullptr;
      bool isPrivate = false;
    };

    struct PendingReply {
      DBus* service = nullptr;
      ConnectionID connectionId = 0;
      String seq;
      Callback callback;
      std::atomic<bool> completed {false};
      std::mutex mutex;
    };

    struct PendingMethod {
      DBus* service = nullptr;
      ConnectionID connectionId = 0;
      CallID id = 0;
      String path;
      String interface;
      String member;
      DBusMessage* message = nullptr;
    };

    mutable std::mutex connectionsMutex;
    std::unordered_map<ConnectionID, SharedPointer<Connection>> connections;
    std::atomic<bool> threadsInitialized {false};
    #endif

    explicit Implementation(DBus& svc)
      : service(svc)
    {}

    ~Implementation() {
      shutdown();
    }

    bool available() const {
    #if ORO_RUNTIME_HAVE_DBUS
      return true;
    #else
      return false;
    #endif
    }

    void shutdown();
#if ORO_RUNTIME_HAVE_DBUS
    DBusHandlerResult handleMessage(Connection&, DBusConnection*, DBusMessage*);
    void emitEvent(const String& event, const JSON::Object& payload);
    DBusHandlerResult handleExportedMessage(Connection&, Connection::ExportedObject&, DBusConnection*, DBusMessage*);
    void handlePendingReply(DBusPendingCall*, PendingReply*);
#endif
  };

  DBus::DBus(const Options& options)
    : core::Service(options),
      impl(std::make_unique<Implementation>(*this))
  {}

  DBus::~DBus() = default;

  bool DBus::start() {
    if (!this->enabled.load()) {
      return true;
    }
    if (!impl->available()) {
      this->enabled.store(false);
      return true;
    }
    return core::Service::start();
  }

  bool DBus::stop() {
    if (impl) {
      impl->shutdown();
    }
    return core::Service::stop();
  }

  JSON::Object DBus::availability() const {
    if (!impl->available()) {
      return JSON::Object::Entries {{
        {"available", false},
        {"reason", String("DBus support not compiled in")}
      }};
    }

    if (!this->enabled.load()) {
      return JSON::Object::Entries {{
        {"available", false},
        {"reason", String("DBus service disabled")}
      }};
    }

    return JSON::Object::Entries {{
      {"available", true}
    }};
  }

  void DBus::Implementation::shutdown() {
  #if ORO_RUNTIME_HAVE_DBUS
    std::unordered_map<ConnectionID, SharedPointer<Connection>> snapshot;
    {
      std::scoped_lock lock(connectionsMutex);
      snapshot = connections;
      connections.clear();
    }

    for (auto& entry : snapshot) {
      auto& connection = entry.second;
      if (!connection) continue;
      connection->running.store(false);
      if (connection->handle) {
        for (const auto& entry : connection->exports) {
          if (entry.second && !entry.second->path.empty()) {
            dbus_connection_unregister_object_path(connection->handle, entry.second->path.c_str());
          }
        }
        connection->exports.clear();
        connection->exportsByPath.clear();
        dbus_connection_remove_filter(connection->handle, Implementation::connectionFilter, connection.get());
        if (connection->isPrivate) {
          dbus_connection_close(connection->handle);
        }
      }

      {
        std::scoped_lock inner(connection->mutex);
        for (auto& pendingEntry : connection->pendingMethods) {
          auto pending = pendingEntry.second;
          if (pending) {
            if (pending->message) {
              dbus_message_unref(pending->message);
              pending->message = nullptr;
            }
            delete pending;
          }
        }
        connection->pendingMethods.clear();
      }
      if (connection->pump.joinable()) {
        connection->pump.join();
      }
      if (connection->handle) {
        dbus_connection_unref(connection->handle);
        connection->handle = nullptr;
      }
    }
  #endif
  }

  void DBus::connect(const String& seq, const JSON::Any& optionsAny, const Callback cb) {
    if (!this->enabled.load()) {
      cb(seq, invalidState("DBus service disabled"), QueuedResponse{});
      return;
    }

    if (!impl->available()) {
      cb(seq, notSupported("DBus support not available on this platform"), QueuedResponse{});
      return;
    }

#if ORO_RUNTIME_HAVE_DBUS
    ConnectOptions options;

    JSON::Any parsedOptions = optionsAny;

    if (parsedOptions.type == JSON::Type::Raw || parsedOptions.type == JSON::Type::String) {
      const auto source = parsedOptions.str();
      if (!source.empty()) {
        try {
          parsedOptions = JSON::parse(source);
        } catch (const JSON::Error& error) {
          const auto name = error.name.empty() ? String("SyntaxError") : error.name;
          const auto message = String("Failed to parse DBus options: ") + error.what();
          cb(seq, makeError(name.c_str(), message), QueuedResponse{});
          return;
        } catch (const std::exception& error) {
          const auto message = String("Failed to parse DBus options: ") + error.what();
          cb(seq, makeError("SyntaxError", message), QueuedResponse{});
          return;
        }
      } else {
        parsedOptions = nullptr;
      }
    }

    if (parsedOptions.type == JSON::Type::Object) {
      const auto& obj = parsedOptions.as<JSON::Object>();
      auto type = obj.has("bus") ? jsonStringValue(obj.get("bus")) : String("session");
      if (type == "session") {
        options.bus = ConnectOptions::BusType::Session;
      } else if (type == "system") {
        options.bus = ConnectOptions::BusType::System;
      } else if (type == "starter") {
        options.bus = ConnectOptions::BusType::Starter;
      } else if (type == "address") {
        options.bus = ConnectOptions::BusType::Address;
        if (obj.has("address")) {
          options.address = jsonStringValue(obj.get("address"));
        }
      } else {
        cb(seq, typeError("options.bus must be one of 'session', 'system', 'starter', or 'address'"), QueuedResponse{});
        return;
      }

      if (obj.has("address")) {
        options.address = jsonStringValue(obj.get("address"));
      }
      if (obj.has("private")) {
        options.privateConnection = static_cast<bool>(obj.get("private"));
      }
      if (obj.has("allowPeerAuthentication")) {
        options.allowPeerAuthentication = static_cast<bool>(obj.get("allowPeerAuthentication"));
      }
    } else if (
      parsedOptions.type != JSON::Type::Null &&
      parsedOptions.type != JSON::Type::Empty
    ) {
      cb(seq, typeError("options must be an object"), QueuedResponse{});
      return;
    }

    if (this->queue.destroyed()) {
      cb(seq, invalidState("DBus service shutting down"), QueuedResponse{});
      return;
    }

    this->queue.push([=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      DBusError error;
      dbus_error_init(&error);

      if (options.bus == ConnectOptions::BusType::Address && options.address.empty()) {
        respond(typeError("address must be provided when bus is 'address'"));
        return;
      }

      if (!impl->threadsInitialized.exchange(true)) {
        dbus_threads_init_default();
      }

      DBusConnection* connection = nullptr;
      switch (options.bus) {
        case ConnectOptions::BusType::Session:
          connection = options.privateConnection
            ? dbus_bus_get_private(DBUS_BUS_SESSION, &error)
            : dbus_bus_get(DBUS_BUS_SESSION, &error);
          break;
        case ConnectOptions::BusType::System:
          connection = options.privateConnection
            ? dbus_bus_get_private(DBUS_BUS_SYSTEM, &error)
            : dbus_bus_get(DBUS_BUS_SYSTEM, &error);
          break;
        case ConnectOptions::BusType::Starter:
          connection = options.privateConnection
            ? dbus_bus_get_private(DBUS_BUS_STARTER, &error)
            : dbus_bus_get(DBUS_BUS_STARTER, &error);
          break;
        case ConnectOptions::BusType::Address:
          connection = options.privateConnection
            ? dbus_connection_open_private(options.address.c_str(), &error)
            : dbus_connection_open(options.address.c_str(), &error);
          break;
      }

      if (!connection) {
        const String message = error.message ? String(error.message) : String("Failed to connect to DBus");
        dbus_error_free(&error);
        respond(makeError("DBusError", message));
        return;
      }

      if (dbus_error_is_set(&error)) {
        const String message = error.message ? String(error.message) : String("Failed to connect to DBus");
        dbus_error_free(&error);
        if (options.privateConnection || options.bus == ConnectOptions::BusType::Address) {
          dbus_connection_close(connection);
        }
        dbus_connection_unref(connection);
        respond(makeError("DBusError", message));
        return;
      }

      if (options.allowPeerAuthentication) {
        dbus_connection_set_allow_anonymous(connection, static_cast<dbus_bool_t>(true));
      }

      if (options.bus == ConnectOptions::BusType::Address) {
        DBusError registerError;
        dbus_error_init(&registerError);
        if (!dbus_bus_register(connection, &registerError)) {
          const String message = registerError.message
            ? String(registerError.message)
            : String("Failed to register DBus connection");
          dbus_error_free(&registerError);
          if (options.privateConnection || options.bus == ConnectOptions::BusType::Address) {
            dbus_connection_close(connection);
          }
          dbus_connection_unref(connection);
          respond(makeError("DBusError", message));
          return;
        }
        if (dbus_error_is_set(&registerError)) {
          const String message = registerError.message
            ? String(registerError.message)
            : String("Failed to register DBus connection");
          dbus_error_free(&registerError);
          if (options.privateConnection || options.bus == ConnectOptions::BusType::Address) {
            dbus_connection_close(connection);
          }
          dbus_connection_unref(connection);
          respond(makeError("DBusError", message));
          return;
        }
        dbus_error_free(&registerError);
      }

      dbus_connection_set_exit_on_disconnect(connection, false);

      auto ctx = std::make_shared<Implementation::Connection>();
      ctx->id = rand64();
      ctx->handle = connection;
      ctx->running.store(true);
      ctx->owner = impl.get();
      ctx->isPrivate = options.privateConnection || options.bus == ConnectOptions::BusType::Address;

      {
        std::scoped_lock lock(impl->connectionsMutex);
        impl->connections[ctx->id] = ctx;
      }

      dbus_connection_add_filter(connection, Implementation::connectionFilter, ctx.get(), nullptr);

      ctx->pump = std::thread([connection, ctx, svc = this]() {
        while (ctx->running.load()) {
          if (!dbus_connection_read_write(connection, 10)) {
            ctx->running.store(false);
            break;
          }

          while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS) {}
        }
      });

      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.connect")},
        {"data", JSON::Object::Entries {{
          {"id", std::to_string(ctx->id)}
        }}}
      }};

      respond(json);
    });
  #else
    (void)optionsAny;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::disconnect(const String& seq, ConnectionID id, const Callback cb) {
    if (!impl->available()) {
      cb(seq, notSupported("DBus support not available"), QueuedResponse{});
      return;
    }

  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      auto it = impl->connections.find(id);
      if (it != impl->connections.end()) {
        connection = it->second;
        impl->connections.erase(it);
      }
    }

    if (!connection) {
      cb(seq, invalidState("Unknown DBus connection id"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      connection->running.store(false);
      if (connection->handle) {
        for (const auto& entry : connection->exports) {
          if (entry.second && !entry.second->path.empty()) {
            dbus_connection_unregister_object_path(connection->handle, entry.second->path.c_str());
          }
        }
        connection->exports.clear();
        connection->exportsByPath.clear();
        dbus_connection_remove_filter(connection->handle, Implementation::connectionFilter, connection.get());
        if (connection->isPrivate) {
          dbus_connection_close(connection->handle);
        }
      }

      {
        std::scoped_lock inner(connection->mutex);
        for (auto& entry : connection->pendingMethods) {
          auto pending = entry.second;
          if (pending) {
            if (pending->message) {
              dbus_message_unref(pending->message);
              pending->message = nullptr;
            }
            delete pending;
          }
        }
        connection->pendingMethods.clear();
      }
      if (connection->pump.joinable()) {
        if (std::this_thread::get_id() == connection->pump.get_id()) {
          connection->pump.detach();
        } else {
          connection->pump.join();
        }
      }
      if (connection->handle) {
        dbus_connection_unref(connection->handle);
        connection->handle = nullptr;
      }
      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.disconnect")},
        {"data", JSON::Object::Entries {{
          {"id", std::to_string(id)}
        }}}
      }};
      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)id;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::requestName(const String& seq, ConnectionID id, const String& name, uint32_t flags, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      auto it = impl->connections.find(id);
      if (it != impl->connections.end()) connection = it->second;
    }

    if (!connection || !connection->handle) {
      cb(seq, invalidState("Invalid DBus connection"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      DBusError error;
      dbus_error_init(&error);
      debug("before request name");
      const auto reply = dbus_bus_request_name(connection->handle, name.c_str(), flags, &error);
      debug("after request name");
      if (dbus_error_is_set(&error)) {
        const String message = error.message ? String(error.message) : String("Failed to request name");
        dbus_error_free(&error);
        respond(makeError("DBusError", message));
        return;
      }
      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.requestName")},
        {"data", JSON::Object::Entries {{
          {"id", std::to_string(connection->id)},
          {"name", name},
          {"reply", reply}
        }}}
      }};
      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::releaseName(const String& seq, ConnectionID id, const String& name, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      auto it = impl->connections.find(id);
      if (it != impl->connections.end()) connection = it->second;
    }

    if (!connection || !connection->handle) {
      cb(seq, invalidState("Invalid DBus connection"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      DBusError error;
      dbus_error_init(&error);
      const auto reply = dbus_bus_release_name(connection->handle, name.c_str(), &error);
      if (dbus_error_is_set(&error)) {
        const String message = error.message ? String(error.message) : String("Failed to release name");
        dbus_error_free(&error);
        respond(makeError("DBusError", message));
        return;
      }
      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.releaseName")},
        {"data", JSON::Object::Entries {{
          {"id", std::to_string(connection->id)},
          {"name", name},
          {"reply", reply}
        }}}
      }};
      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)id; (void)name;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::call(const String& seq, const CallOptions& options, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      auto it = impl->connections.find(options.connectionId);
      if (it != impl->connections.end()) connection = it->second;
    }

    if (!connection || !connection->handle) {
      cb(seq, invalidState("Invalid DBus connection"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      auto message = dbus_message_new_method_call(
        options.destination.empty() ? nullptr : options.destination.c_str(),
        options.path.empty() ? nullptr : options.path.c_str(),
        options.interface.empty() ? nullptr : options.interface.c_str(),
        options.member.empty() ? nullptr : options.member.c_str()
      );

      if (!message) {
        respond(makeError("DBusError", "Failed to allocate method call"));
        return;
      }

      if (!options.signature.empty()) {
        DBusMessageIter iter;
        dbus_message_iter_init_append(message, &iter);
        String errorMessage;
        if (!appendValuesFromJSON(options.signature, options.body, &iter, errorMessage)) {
          dbus_message_unref(message);
          respond(makeError("TypeError", errorMessage));
          return;
        }
      }

      if (options.noReply) {
        dbus_message_set_no_reply(message, true);
        dbus_connection_send(connection->handle, message, nullptr);
        dbus_connection_flush(connection->handle);
        dbus_message_unref(message);
        const auto json = JSON::Object::Entries {{
          {"source", String("dbus.call")},
          {"data", JSON::Object::Entries {{
            {"id", std::to_string(connection->id)},
            {"noReply", true}
          }}}
        }};
        respond(json);
        return;
      }

      DBusPendingCall* pending = nullptr;
      if (!dbus_connection_send_with_reply(connection->handle, message, &pending, options.timeoutMs)) {
        dbus_message_unref(message);
        respond(makeError("DBusError", "Failed to send method call"));
        return;
      }

      dbus_message_unref(message);

      dbus_connection_flush(connection->handle);

      if (!pending) {
        respond(makeError("DBusError", "Failed to create pending call"));
        return;
      }

      auto pendingReply = new Implementation::PendingReply();
      pendingReply->service = this;
      pendingReply->connectionId = connection->id;
      pendingReply->seq = seq;
      pendingReply->callback = cb;

      {
        std::scoped_lock lock(connection->mutex);
        connection->replies[pending] = pendingReply;
      }

      dbus_pending_call_set_notify(
        pending,
        [](DBusPendingCall* call, void* user) {
          auto ctx = static_cast<Implementation::PendingReply*>(user);
          if (!ctx) return;
          if (ctx->service && ctx->service->impl) {
            ctx->service->impl->handlePendingReply(call, ctx);
          }
        },
        pendingReply,
        [](void* user) {
          auto ctx = static_cast<Implementation::PendingReply*>(user);
          delete ctx;
        }
      );
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)options;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::emitSignal(const String& seq, const SignalOptions& options, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      auto it = impl->connections.find(options.connectionId);
      if (it != impl->connections.end()) connection = it->second;
    }

    if (!connection || !connection->handle) {
      cb(seq, invalidState("Invalid DBus connection"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      auto message = dbus_message_new_signal(
        options.path.empty() ? nullptr : options.path.c_str(),
        options.interface.empty() ? nullptr : options.interface.c_str(),
        options.name.empty() ? nullptr : options.name.c_str()
      );

      if (!message) {
        respond(makeError("DBusError", "Failed to allocate signal"));
        return;
      }

      if (!options.signature.empty()) {
        DBusMessageIter iter;
        dbus_message_iter_init_append(message, &iter);
        String errorMessage;
        if (!appendValuesFromJSON(options.signature, options.body, &iter, errorMessage)) {
          dbus_message_unref(message);
          respond(makeError("TypeError", errorMessage));
          return;
        }
      }

      if (!dbus_connection_send(connection->handle, message, nullptr)) {
        dbus_message_unref(message);
        respond(makeError("DBusError", "Failed to send signal"));
        return;
      }

      dbus_connection_flush(connection->handle);
      dbus_message_unref(message);

      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.signal")},
        {"data", JSON::Object::Entries {{
          {"id", std::to_string(connection->id)}
        }}}
      }};
      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)options;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::addMatch(const String& seq, const MatchRule& rule, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      auto it = impl->connections.find(rule.connectionId);
      if (it != impl->connections.end()) connection = it->second;
    }

    if (!connection || !connection->handle) {
      cb(seq, invalidState("Invalid DBus connection"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      DBusError error;
      dbus_error_init(&error);
      dbus_bus_add_match(connection->handle, rule.rule.c_str(), &error);
      dbus_connection_flush(connection->handle);

      if (dbus_error_is_set(&error)) {
        const String message = error.message ? String(error.message) : String("Failed to add match rule");
        dbus_error_free(&error);
        respond(makeError("DBusError", message));
        return;
      }

      MatchID matchId = rand64();
      {
        std::scoped_lock lock(connection->mutex);
        connection->matches[matchId] = rule.rule;
      }

      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.addMatch")},
        {"data", JSON::Object::Entries {{
          {"id", std::to_string(connection->id)},
          {"matchId", std::to_string(matchId)}
        }}}
      }};
      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)rule;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::removeMatch(const String& seq, MatchID id, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    String rule;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      for (auto& entry : impl->connections) {
        auto& conn = entry.second;
        if (!conn) continue;
        std::scoped_lock connLock(conn->mutex);
        auto it = conn->matches.find(id);
        if (it != conn->matches.end()) {
          rule = it->second;
          conn->matches.erase(it);
          connection = conn;
          break;
        }
      }
    }

    if (!connection || !connection->handle) {
      cb(seq, invalidState("Match rule not found"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      DBusError error;
      dbus_error_init(&error);
      dbus_bus_remove_match(connection->handle, rule.c_str(), &error);
      dbus_connection_flush(connection->handle);

      if (dbus_error_is_set(&error)) {
        const String message = error.message ? String(error.message) : String("Failed to remove match rule");
        dbus_error_free(&error);
        respond(makeError("DBusError", message));
        return;
      }

      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.removeMatch")},
        {"data", JSON::Object::Entries {{
          {"matchId", std::to_string(id)}
        }}}
      }};
      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)id;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::exportObject(const String& seq, const ExportOptions& options, const JSON::Any& definition, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    if (options.path.empty()) {
      cb(seq, typeError("options.path must be provided"), QueuedResponse{});
      return;
    }

    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      auto it = impl->connections.find(options.connectionId);
      if (it != impl->connections.end()) connection = it->second;
    }

    if (!connection || !connection->handle) {
      cb(seq, invalidState("Invalid DBus connection"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      std::scoped_lock lock(connection->mutex);
      if (connection->exportsByPath.contains(options.path)) {
        respond(invalidState("DBus object path already exported"));
        return;
      }

      auto exported = std::make_shared<Implementation::Connection::ExportedObject>();
      exported->id = rand64();
      exported->path = options.path;
      exported->interface = options.interface;
      exported->connection = connection.get();
      exported->vtable.message_function = Implementation::objectPathFilter;

      if (definition.type == JSON::Type::Object) {
        const auto& obj = definition.as<JSON::Object>();
        if (obj.has("methods")) {
          const auto& methodsAny = obj.get("methods");
          if (methodsAny.type == JSON::Type::Array) {
            const auto& methods = methodsAny.as<JSON::Array>();
            for (const auto& methodAny : methods) {
              if (methodAny.type == JSON::Type::String) {
                exported->methods.insert(methodAny.str());
              }
            }
          }
        }
      }

      if (!dbus_connection_register_object_path(
            connection->handle,
            exported->path.c_str(),
            &exported->vtable,
            exported.get()
          )) {
        respond(makeError("DBusError", "Failed to register object path"));
        return;
      }

      connection->exports[exported->id] = exported;
      connection->exportsByPath[exported->path] = exported->id;

      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.exportObject")},
        {"data", JSON::Object::Entries {{
          {"exportId", std::to_string(exported->id)},
          {"path", exported->path}
        }}}
      }};

      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)options; (void)definition;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::unexportObject(const String& seq, ExportID id, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    SharedPointer<Implementation::Connection::ExportedObject> exported;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      for (auto& entry : impl->connections) {
        auto& conn = entry.second;
        if (!conn) continue;
        std::scoped_lock connLock(conn->mutex);
        auto it = conn->exports.find(id);
        if (it != conn->exports.end()) {
          connection = conn;
          exported = it->second;
          conn->exports.erase(it);
          conn->exportsByPath.erase(exported->path);
          break;
        }
      }
    }

    if (!connection || !exported) {
      cb(seq, invalidState("Unknown export id"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      if (connection->handle) {
        dbus_connection_unregister_object_path(connection->handle, exported->path.c_str());
      }
      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.unexportObject")},
        {"data", JSON::Object::Entries {{
          {"exportId", std::to_string(exported->id)}
        }}}
      }};
      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)id;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  void DBus::respond(const String& seq, CallID id, bool isError, const String& name, const String& signature, const JSON::Any& body, const Callback cb) {
  #if ORO_RUNTIME_HAVE_DBUS
    SharedPointer<Implementation::Connection> connection;
    Implementation::PendingMethod* pending = nullptr;
    {
      std::scoped_lock lock(impl->connectionsMutex);
      for (auto& entry : impl->connections) {
        auto& conn = entry.second;
        if (!conn) continue;
        std::scoped_lock connLock(conn->mutex);
        auto it = conn->pendingMethods.find(id);
        if (it != conn->pendingMethods.end()) {
          pending = it->second;
          connection = conn;
          conn->pendingMethods.erase(it);
          break;
        }
      }
    }

    if (!connection || !pending || !pending->message) {
      cb(seq, invalidState("Unknown method call id"), QueuedResponse{});
      return;
    }

    auto work = [=, this]() {
      auto respond = [=, this](const JSON::Any& json) {
        this->dispatch([=]() { cb(seq, json, QueuedResponse{}); });
      };

      auto message = pending->message;
      pending->message = nullptr;

      if (!connection->handle) {
        dbus_message_unref(message);
        delete pending;
        respond(invalidState("DBus connection closed"));
        return;
      }

      DBusMessage* reply = nullptr;

      if (isError) {
        const char* errorName = name.empty() ? DBUS_ERROR_FAILED : name.c_str();
        const char* errorMessage = signature.c_str();
        reply = dbus_message_new_error(message, errorName, errorMessage);
      } else {
        reply = dbus_message_new_method_return(message);
        if (!signature.empty()) {
          DBusMessageIter iter;
          dbus_message_iter_init_append(reply, &iter);
          String errorMessage;
          if (!appendValuesFromJSON(signature, body, &iter, errorMessage)) {
            dbus_message_unref(reply);
            dbus_message_unref(message);
            delete pending;
            respond(makeError("TypeError", errorMessage));
            return;
          }
        }
      }

      if (!reply) {
        dbus_message_unref(message);
        delete pending;
        respond(makeError("DBusError", "Failed to create reply message"));
        return;
      }

      if (!dbus_connection_send(connection->handle, reply, nullptr)) {
        dbus_message_unref(reply);
        dbus_message_unref(message);
        delete pending;
        respond(makeError("DBusError", "Failed to send reply"));
        return;
      }

      dbus_connection_flush(connection->handle);
      dbus_message_unref(reply);
      dbus_message_unref(message);
      delete pending;

      const auto json = JSON::Object::Entries {{
        {"source", String("dbus.respond")},
        {"data", JSON::Object::Entries {{
          {"callId", std::to_string(id)}
        }}}
      }};

      respond(json);
    };

    if (this->queue.destroyed()) {
      work();
      return;
    }

    this->queue.push(work);
  #else
    (void)id; (void)isError; (void)name; (void)signature; (void)body;
    cb(seq, notSupported("DBus support not available"), QueuedResponse{});
  #endif
  }

  #if ORO_RUNTIME_HAVE_DBUS

  static JSON::Any encodeValue(DBusMessageIter* iter);
  static bool appendValuesFromJSON(const String& signature, const JSON::Any& value, DBusMessageIter* iter, String& errorMessage);
  static JSON::Any encodeBody(DBusMessage* message);

#if ORO_RUNTIME_HAVE_DBUS
  DBusHandlerResult DBus::Implementation::connectionFilter(DBusConnection* connection, DBusMessage* message, void* user) {
    auto* ctx = static_cast<Connection*>(user);
    if (!ctx || !ctx->owner) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return ctx->owner->handleMessage(*ctx, connection, message);
  }

  DBusHandlerResult DBus::Implementation::objectPathFilter(DBusConnection* connection, DBusMessage* message, void* user) {
    auto* exported = static_cast<Connection::ExportedObject*>(user);
    if (!exported || !exported->connection || !exported->connection->owner) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return exported->connection->owner->handleExportedMessage(*exported->connection, *exported, connection, message);
  }
#endif

  static JSON::Any encodeBody(DBusMessage* message) {
    if (!message) return JSON::Object::Entries {{
      {"signature", String("")},
      {"values", JSON::Array {}}
    }};

    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter)) {
      return JSON::Object::Entries {{
        {"signature", String("")},
        {"values", JSON::Array {}}
      }};
    }

    const char* signature = dbus_message_get_signature(message);
    String sig = signature ? String(signature) : String("");

    JSON::Array values;
    do {
      values.push(encodeValue(&iter));
    } while (dbus_message_iter_next(&iter));

    return JSON::Object::Entries {{
      {"signature", sig},
      {"values", values}
    }};
  }

  void DBus::Implementation::handlePendingReply(DBusPendingCall* pending, PendingReply* ctx) {
    if (!pending || !ctx) return;

    DBusMessage* reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);

    JSON::Object response;

    if (!reply) {
      response = makeError("DBusError", "No reply received");
    } else {
      const int type = dbus_message_get_type(reply);
      if (type == DBUS_MESSAGE_TYPE_ERROR) {
        auto namePtr = dbus_message_get_error_name(reply);
        JSON::Object::Entries errEntries;
        errEntries["type"] = String("DBusError");
        errEntries["name"] = namePtr ? String(namePtr) : String("");
        DBusMessageIter iter;
        if (dbus_message_iter_init(reply, &iter)) {
          auto payload = encodeBody(reply);
          errEntries["body"] = payload;
        }
        response = JSON::Object::Entries {{
          {"err", JSON::Object::Entries(errEntries)}
        }};
      } else {
        auto payload = encodeBody(reply);
        response = JSON::Object::Entries {{
          {"source", String("dbus.call")},
          {"data", JSON::Object::Entries {{
            {"connectionId", std::to_string(ctx->connectionId)},
            {"body", payload}
          }}}
        }};
      }
      dbus_message_unref(reply);
    }

    if (ctx->callback) {
      bool dispatched = false;
      if (ctx->service) {
        JSON::Object payload = response;
        dispatched = ctx->service->dispatch([callback = ctx->callback, seq = ctx->seq, payload = std::move(payload)]() mutable {
          if (callback) {
            callback(seq, payload, QueuedResponse{});
          }
        });
      }

      if (!dispatched) {
        ctx->callback(ctx->seq, response, QueuedResponse{});
      }
    }

    SharedPointer<Implementation::Connection> connection;
    {
      std::scoped_lock lock(connectionsMutex);
      auto it = connections.find(ctx->connectionId);
      if (it != connections.end()) connection = it->second;
    }

    if (connection) {
      std::scoped_lock lock(connection->mutex);
      connection->replies.erase(pending);
    }
  }

  void DBus::Implementation::emitEvent(const String& event, const JSON::Object& payload) {
    auto app = App::sharedApplication();
    if (!app) {
      return;
    }

    const String json = payload.str();

    for (const auto& window : app->runtime.windowManager.windows) {
      if (window && window->bridge) {
        window->bridge->emit(event, json);
      }
    }
  }

  DBusHandlerResult DBus::Implementation::handleExportedMessage(Connection& connection, Connection::ExportedObject& exported, DBusConnection*, DBusMessage* message) {
    if (!message) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    const char* iface = dbus_message_get_interface(message);
    if (!exported.interface.empty() && (!iface || exported.interface != iface)) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    const char* member = dbus_message_get_member(message);
    if (!member || std::strlen(member) == 0) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!exported.methods.empty() && !exported.methods.count(member)) {
      auto error = dbus_message_new_error(
        message,
        DBUS_ERROR_UNKNOWN_METHOD,
        "Method not exported"
      );
      if (error) {
        dbus_connection_send(connection.handle, error, nullptr);
        dbus_message_unref(error);
        dbus_connection_flush(connection.handle);
      }
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    auto pending = new PendingMethod();
    pending->service = &this->service;
    pending->connectionId = connection.id;
    pending->id = rand64();
    pending->path = exported.path;
    pending->interface = exported.interface.empty() && iface ? String(iface) : exported.interface;
    pending->member = member;
    pending->message = message;
    dbus_message_ref(message);

    {
      std::scoped_lock lock(connection.mutex);
      connection.pendingMethods[pending->id] = pending;
    }

    const char* sender = dbus_message_get_sender(message);
    auto body = encodeBody(message);

    JSON::Object::Entries data {
      {"connectionId", std::to_string(connection.id)},
      {"callId", std::to_string(pending->id)},
      {"path", exported.path},
      {"interface", pending->interface},
      {"member", pending->member},
      {"sender", String(sender ? sender : "")},
      {"body", body}
    };

    JSON::Object event(JSON::Object::Entries {{
      {"source", String("dbus.methodCall")},
      {"data", data}
    }});

    emitEvent("dbus.methodCall", event);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  DBusHandlerResult DBus::Implementation::handleMessage(Connection& connection, DBusConnection*, DBusMessage* message) {
    if (!message) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    const int type = dbus_message_get_type(message);

    if (type == DBUS_MESSAGE_TYPE_SIGNAL) {
      const char* path = dbus_message_get_path(message);
      const char* interface = dbus_message_get_interface(message);
      const char* member = dbus_message_get_member(message);
      const char* sender = dbus_message_get_sender(message);

      auto payload = encodeBody(message);

      JSON::Object::Entries entries {
        {"connectionId", std::to_string(connection.id)},
        {"path", String(path ? path : "")},
        {"interface", String(interface ? interface : "")},
        {"member", String(member ? member : "")},
        {"sender", String(sender ? sender : "")},
        {"body", payload}
      };

      JSON::Object event(JSON::Object::Entries {{
        {"source", String("dbus.signal")},
        {"data", entries}
      }});

      emitEvent("dbus.signal", event);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  static JSON::Any encodeBasic(DBusMessageIter* iter, int type) {
    switch (type) {
      case DBUS_TYPE_STRING:
      case DBUS_TYPE_OBJECT_PATH:
      case DBUS_TYPE_SIGNATURE:
#ifdef DBUS_TYPE_G_VARIANT
      case DBUS_TYPE_G_VARIANT:
#endif
      {
        const char* value = "";
        dbus_message_iter_get_basic(iter, &value);
        return String(value ? value : "");
      }
      case DBUS_TYPE_BOOLEAN:
      {
        dbus_bool_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return static_cast<bool>(value);
      }
      case DBUS_TYPE_BYTE:
      {
        uint8_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return static_cast<int>(value);
      }
      case DBUS_TYPE_INT16:
      {
        int16_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return static_cast<int>(value);
      }
      case DBUS_TYPE_UINT16:
      {
        uint16_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return static_cast<int>(value);
      }
      case DBUS_TYPE_INT32:
      {
        int32_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return static_cast<int>(value);
      }
      case DBUS_TYPE_UINT32:
      {
        uint32_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return static_cast<uint64_t>(value);
      }
      case DBUS_TYPE_INT64:
      {
        int64_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return String(std::to_string(value));
      }
      case DBUS_TYPE_UINT64:
      {
        uint64_t value = 0;
        dbus_message_iter_get_basic(iter, &value);
        return String(std::to_string(value));
      }
      case DBUS_TYPE_DOUBLE:
      {
        double value = 0.0;
        dbus_message_iter_get_basic(iter, &value);
        return value;
      }
      case DBUS_TYPE_UNIX_FD:
      {
        int value = -1;
        dbus_message_iter_get_basic(iter, &value);
        return value;
      }
      default:
        return nullptr;
    }
  }

  static JSON::Any encodeArray(DBusMessageIter* iter) {
    DBusMessageIter subIter;
    dbus_message_iter_recurse(iter, &subIter);
    auto elementType = dbus_message_iter_get_arg_type(&subIter);
    JSON::Array array;

    while (elementType != DBUS_TYPE_INVALID) {
      array.push(encodeValue(&subIter));
      dbus_message_iter_next(&subIter);
      elementType = dbus_message_iter_get_arg_type(&subIter);
    }

    return array;
  }

  static JSON::Any encodeVariant(DBusMessageIter* iter) {
    DBusMessageIter varIter;
    dbus_message_iter_recurse(iter, &varIter);
    char* sig = dbus_message_iter_get_signature(&varIter);
    String signature = sig ? String(sig) : String("");
    if (sig) dbus_free(sig);
    JSON::Any value = encodeValue(&varIter);
    return JSON::Object::Entries {{
      {"signature", signature},
      {"value", value}
    }};
  }

  static JSON::Any encodeDictEntry(DBusMessageIter* iter) {
    DBusMessageIter entryIter;
    dbus_message_iter_recurse(iter, &entryIter);
    JSON::Any key = encodeValue(&entryIter);
    dbus_message_iter_next(&entryIter);
    JSON::Any value = encodeValue(&entryIter);
    return JSON::Object::Entries {{
      {"key", key},
      {"value", value}
    }};
  }

  static JSON::Any encodeStruct(DBusMessageIter* iter) {
    DBusMessageIter subIter;
    dbus_message_iter_recurse(iter, &subIter);
    JSON::Array values;
    int type = dbus_message_iter_get_arg_type(&subIter);
    while (type != DBUS_TYPE_INVALID) {
      values.push(encodeValue(&subIter));
      dbus_message_iter_next(&subIter);
      type = dbus_message_iter_get_arg_type(&subIter);
    }
    return values;
  }

  static JSON::Any encodeValue(DBusMessageIter* iter) {
    const int type = dbus_message_iter_get_arg_type(iter);
    if (type == DBUS_TYPE_ARRAY) {
      if (dbus_message_iter_get_element_type(iter) == DBUS_TYPE_DICT_ENTRY) {
        return encodeArray(iter);
      }
      return encodeArray(iter);
    }
    if (type == DBUS_TYPE_VARIANT) {
      return encodeVariant(iter);
    }
    if (type == DBUS_TYPE_STRUCT) {
      return encodeStruct(iter);
    }
    if (type == DBUS_TYPE_DICT_ENTRY) {
      return encodeDictEntry(iter);
    }
    return encodeBasic(iter, type);
  }

  static bool appendValueForSignature(DBusMessageIter* iter, DBusSignatureIter* signatureIter, const JSON::Any& value, String& errorMessage) {
    const int type = dbus_signature_iter_get_current_type(signatureIter);
    if (type == DBUS_TYPE_INVALID) {
      errorMessage = "Signature mismatch";
      return false;
    }

    switch (type) {
      case DBUS_TYPE_STRING:
      case DBUS_TYPE_OBJECT_PATH:
      case DBUS_TYPE_SIGNATURE:
      {
        const String str = value.str();
        const char* cstr = str.c_str();
        return dbus_message_iter_append_basic(iter, type, &cstr);
      }
      case DBUS_TYPE_BOOLEAN:
      {
        dbus_bool_t b = static_cast<bool>(value) ? 1 : 0;
        return dbus_message_iter_append_basic(iter, type, &b);
      }
      case DBUS_TYPE_BYTE:
      {
        if (value.type != JSON::Type::Number) {
          errorMessage = "Expected number for DBus byte";
          return false;
        }
        auto num = static_cast<uint8_t>(value.as<JSON::Number>().value());
        return dbus_message_iter_append_basic(iter, type, &num);
      }
      case DBUS_TYPE_INT16:
      case DBUS_TYPE_INT32:
      {
        int32_t num = 0;
        if (value.type == JSON::Type::Number) {
          num = static_cast<int32_t>(value.as<JSON::Number>().value());
        } else if (value.type == JSON::Type::String) {
          num = std::stoi(value.str());
        } else {
          errorMessage = "Expected number for integer";
          return false;
        }
        return dbus_message_iter_append_basic(iter, type, &num);
      }
      case DBUS_TYPE_UINT16:
      case DBUS_TYPE_UINT32:
      {
        uint32_t num = 0;
        if (value.type == JSON::Type::Number) {
          num = static_cast<uint32_t>(value.as<JSON::Number>().value());
        } else if (value.type == JSON::Type::String) {
          num = static_cast<uint32_t>(std::stoul(value.str()));
        } else {
          errorMessage = "Expected number for unsigned integer";
          return false;
        }
        return dbus_message_iter_append_basic(iter, type, &num);
      }
      case DBUS_TYPE_INT64:
      case DBUS_TYPE_UINT64:
      {
        int64_t num = 0;
        if (value.type == JSON::Type::String) {
          num = std::stoll(value.str());
        } else if (value.type == JSON::Type::Number) {
          num = static_cast<int64_t>(value.as<JSON::Number>().value());
        } else {
          errorMessage = "Expected string or number for 64-bit integer";
          return false;
        }
        if (type == DBUS_TYPE_UINT64 && num < 0) {
          errorMessage = "Expected unsigned integer";
          return false;
        }
        if (type == DBUS_TYPE_UINT64) {
          uint64_t unum = static_cast<uint64_t>(num);
          return dbus_message_iter_append_basic(iter, type, &unum);
        }
        return dbus_message_iter_append_basic(iter, type, &num);
      }
      case DBUS_TYPE_DOUBLE:
      {
        double d = 0.0;
        if (value.type == JSON::Type::Number) {
          d = value.as<JSON::Number>().value();
        } else if (value.type == JSON::Type::String) {
          d = std::stod(value.str());
        } else {
          errorMessage = "Expected number for double";
          return false;
        }
        return dbus_message_iter_append_basic(iter, type, &d);
      }
      case DBUS_TYPE_ARRAY:
      {
        if (value.type != JSON::Type::Array) {
          errorMessage = "Expected array value";
          return false;
        }
        DBusSignatureIter elementIter;
        dbus_signature_iter_recurse(signatureIter, &elementIter);
        DBusMessageIter arrayIter;
        char* elementSig = dbus_signature_iter_get_signature(&elementIter);
        bool ok = dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, elementSig, &arrayIter);
        dbus_free(elementSig);
        if (!ok) {
          errorMessage = "Failed to open DBus array container";
          return false;
        }
        const auto& arr = value.as<JSON::Array>();
        for (const auto& entry : arr) {
          DBusSignatureIter elementIterCopy;
          dbus_signature_iter_recurse(signatureIter, &elementIterCopy);
          if (!appendValueForSignature(&arrayIter, &elementIterCopy, entry, errorMessage)) {
            dbus_message_iter_close_container(iter, &arrayIter);
            return false;
          }
        }
        return dbus_message_iter_close_container(iter, &arrayIter);
      }
      case DBUS_TYPE_STRUCT:
      {
        if (value.type != JSON::Type::Array) {
          errorMessage = "Expected array for struct value";
          return false;
        }
        DBusMessageIter structIter;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, nullptr, &structIter)) {
          errorMessage = "Failed to open struct container";
          return false;
        }
        const auto& arr = value.as<JSON::Array>();
        DBusSignatureIter subSig;
        dbus_signature_iter_recurse(signatureIter, &subSig);
        for (const auto& entry : arr) {
          if (!appendValueForSignature(&structIter, &subSig, entry, errorMessage)) {
            dbus_message_iter_close_container(iter, &structIter);
            return false;
          }
          dbus_signature_iter_next(&subSig);
        }
        return dbus_message_iter_close_container(iter, &structIter);
      }
      case DBUS_TYPE_VARIANT:
      {
        if (value.type != JSON::Type::Object) {
          errorMessage = "Variant requires object with signature and value";
          return false;
        }
        const auto& obj = value.as<JSON::Object>();
        const auto signature = obj.get("signature");
        const auto variantValue = obj.get("value");

        if (signature.type != JSON::Type::String) {
          errorMessage = "Variant signature must be string";
          return false;
        }

        const String signatureStr = signature.str();

        DBusMessageIter variantIter;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, signatureStr.c_str(), &variantIter)) {
          errorMessage = "Failed to open variant container";
          return false;
        }

        DBusSignatureIter variantSig;
        dbus_signature_iter_init(&variantSig, signatureStr.c_str());
        if (!appendValueForSignature(&variantIter, &variantSig, variantValue, errorMessage)) {
          dbus_message_iter_close_container(iter, &variantIter);
          return false;
        }

        return dbus_message_iter_close_container(iter, &variantIter);
      }
      case DBUS_TYPE_DICT_ENTRY:
      {
        if (value.type != JSON::Type::Object) {
          errorMessage = "Dict entries require object with key/value";
          return false;
        }

        const auto& obj = value.as<JSON::Object>();
        if (!obj.has("key") || !obj.has("value")) {
          errorMessage = "Dict entry missing key/value";
          return false;
        }

        const auto keyValue = obj.get("key");
        const auto valueValue = obj.get("value");

        DBusMessageIter dictIter;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_DICT_ENTRY, nullptr, &dictIter)) {
          errorMessage = "Failed to open dict entry container";
          return false;
        }

        DBusSignatureIter dictSig;
        dbus_signature_iter_recurse(signatureIter, &dictSig);

        DBusSignatureIter keySig = dictSig;
        if (!appendValueForSignature(&dictIter, &keySig, keyValue, errorMessage)) {
          dbus_message_iter_close_container(iter, &dictIter);
          return false;
        }
        DBusSignatureIter valueSig = dictSig;
        dbus_signature_iter_next(&valueSig);
        if (!appendValueForSignature(&dictIter, &valueSig, valueValue, errorMessage)) {
          dbus_message_iter_close_container(iter, &dictIter);
          return false;
        }

        return dbus_message_iter_close_container(iter, &dictIter);
      }
      default:
        errorMessage = "Unsupported DBus type";
        return false;
    }
  }

  static bool appendValuesFromJSON(const String& signature, const JSON::Any& body, DBusMessageIter* iter, String& errorMessage) {
    if (signature.empty()) {
      return true;
    }

    JSON::Any resolved = body;

    if (resolved.type == JSON::Type::Raw) {
      const auto source = resolved.as<JSON::Raw>().value();
      if (!source.empty()) {
        try {
          resolved = JSON::parse(source);
        } catch (const JSON::Error& error) {
          errorMessage = String("Invalid body JSON: ") + error.what();
          return false;
        } catch (const std::exception& error) {
          errorMessage = String("Invalid body JSON: ") + error.what();
          return false;
        }
      } else {
        resolved = nullptr;
      }
    } else if (resolved.type == JSON::Type::String) {
      const auto source = resolved.as<JSON::String>().value();
      if (!source.empty()) {
        try {
          resolved = JSON::parse(source);
        } catch (const JSON::Error& error) {
          errorMessage = String("Invalid body JSON: ") + error.what();
          return false;
        } catch (const std::exception& error) {
          errorMessage = String("Invalid body JSON: ") + error.what();
          return false;
        }
      } else {
        resolved = nullptr;
      }
    }

    if (resolved.type == JSON::Type::Null || resolved.type == JSON::Type::Empty) {
      return true;
    }

    if (resolved.type != JSON::Type::Array && resolved.type != JSON::Type::Object) {
      errorMessage = "Body must be array";
      return false;
    }

    JSON::Array values;
    if (resolved.type == JSON::Type::Array) {
      values = resolved.as<JSON::Array>();
    } else {
      if (!resolved.as<JSON::Object>().has("values")) {
        errorMessage = "Body missing values";
        return false;
      }
      values = resolved.as<JSON::Object>().get("values").as<JSON::Array>();
    }

    DBusSignatureIter sigIter;
    dbus_signature_iter_init(&sigIter, signature.c_str());
    auto sigCopy = sigIter;

    size_t index = 0;
    for (const auto& value : values) {
      if (!appendValueForSignature(iter, &sigCopy, value, errorMessage)) {
        errorMessage = String("Failed to append argument at index ") + std::to_string(index) + ": " + errorMessage;
        return false;
      }
      dbus_signature_iter_next(&sigCopy);
      index++;
    }

    return true;
  }

  #endif

}
