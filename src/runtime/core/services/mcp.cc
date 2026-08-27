#include "mcp.hh"

#include "../../runtime.hh"
#include "../../version.hh"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <future>
#include <limits>

namespace oro::runtime::core::services {
  namespace {
    static constexpr size_t kMaxPendingMcpRequests = 1024;

    thread_local bool suppressSynchronousTransportSend = false;

    struct SynchronousResponseGuard {
      bool previous = false;

      SynchronousResponseGuard ()
        : previous(suppressSynchronousTransportSend) {
        suppressSynchronousTransportSend = true;
      }

      ~SynchronousResponseGuard() {
        suppressSynchronousTransportSend = this->previous;
      }
    };

    bool isModernRequest(const nlohmann::json& request) {
      if (!request.is_object() || !request.contains("params") || !request["params"].is_object()) {
        return false;
      }
      const auto& params = request["params"];
      if (!params.contains("_meta") || !params["_meta"].is_object()) {
        return false;
      }
      const auto version = params["_meta"].find("io.modelcontextprotocol/protocolVersion");
      return version != params["_meta"].end() &&
        version->is_string() &&
        mcp::isModernProtocolVersion(version->get<String>());
    }

    void finalizeModernResponse(nlohmann::json& response, const nlohmann::json& request) {
      if (!response.is_object() || !response.contains("result") || !response["result"].is_object()) {
        return;
      }
      response["result"]["resultType"] = "complete";
      if (!response["result"].contains("_meta") || !response["result"]["_meta"].is_object()) {
        response["result"]["_meta"] = nlohmann::json::object();
      }
      response["result"]["_meta"]["io.modelcontextprotocol/serverInfo"] = {
        {"name", "oro.runtime"},
        {"version", runtime::version::VERSION_STRING}
      };
      const auto method = request.value("method", "");
      if (method == "tools/list" ||
          method == "resources/list" ||
          method == "resources/read" ||
          method == "prompts/list") {
        response["result"]["ttlMs"] = 5000;
        response["result"]["cacheScope"] = "private";
      }
    }

    template <typename Counter>
    uint64_t nextIdentifier(Counter& counter) {
      auto value = counter.fetch_add(1, std::memory_order_relaxed);
      if (value == 0) {
        value = counter.fetch_add(1, std::memory_order_relaxed);
      }
      return value;
    }

    String requestIdentifier(const nlohmann::json& id) {
      return id.is_string() ? id.get<String>() : id.dump();
    }
  }

  MCP::MCP(const Options& options)
    : core::Service(options) {
    if (auto* runtime = this->context.getRuntime()) {
      const auto& userConfig = runtime->userConfig;
      const auto tokenIt = userConfig.find("mcp_token");
      if (tokenIt != userConfig.end()) {
        this->staticAuthToken = tokenIt->second;
      }

      const auto timeoutIt = userConfig.find("mcp_auth_timeout_ms");
      if (timeoutIt != userConfig.end()) {
        try {
          const auto parsed = std::stoul(timeoutIt->second);
          if (parsed > 0 && parsed <= std::numeric_limits<uint32_t>::max()) {
            this->authorizationTimeoutMs = static_cast<uint32_t>(parsed);
          }
        } catch (...) {}
      }
    }
  }

  bool MCP::start() {
    if (!this->enabled) {
      return true;
    }
    return core::Service::start();
  }

  bool MCP::stop() {
    if (!this->enabled) {
      return true;
    }

    this->stopServer();
    this->rejectAllPendingAuthorizations();

    {
      Lock lock(this->mutex);
      this->toolRegistry.clear();
      this->resourceRegistry.clear();
      this->pendingInvocations.clear();
      this->pendingResourceReads.clear();
      this->sessionState.clear();
      this->resourceSubscriptions.clear();
      this->sessionSubscriptions.clear();
      this->modernSubscriptions.clear();
    }

    return core::Service::stop();
  }

  MCP::ToolId MCP::registerTool(const mcp::Tool& tool, const Callback& callback) {
    auto toolEntry = SharedPointer<RegisteredTool>(new RegisteredTool());
    toolEntry->id = nextIdentifier(this->toolCounter);
    toolEntry->definition = tool;
    toolEntry->callback = callback;

    {
      Lock lock(this->mutex);
      this->toolRegistry.erase(tool.name);
      this->toolRegistry[tool.name] = toolEntry;
    }

    this->notifyModernListChange("toolsListChanged", "notifications/tools/list_changed");

    return toolEntry->id;
  }

  bool MCP::unregisterTool(const String& name) {
    bool removed = false;
    Vector<String> pendingIds;
    {
      Lock lock(this->mutex);
      removed = this->toolRegistry.erase(name) > 0;

      if (removed) {
        for (const auto& entry : this->pendingInvocations) {
          if (entry.second && entry.second->toolName == name) {
            pendingIds.push_back(entry.first);
          }
        }
      }
    }

    if (removed) {
      for (const auto& id : pendingIds) {
        this->rejectInvocation(
          id,
          mcp::Error(
            mcp::ErrorCode::InternalError,
            "Tool was unregistered before its invocation completed"
          )
        );
      }
      this->notifyModernListChange("toolsListChanged", "notifications/tools/list_changed");
    }

    return removed;
  }

  Vector<mcp::Tool> MCP::listTools() const {
    Vector<mcp::Tool> tools;
    Lock lock(this->mutex);
    tools.reserve(this->toolRegistry.size());
    for (const auto& entry : this->toolRegistry) {
      if (entry.second) {
        tools.push_back(entry.second->definition);
      }
    }
    return tools;
  }

  MCP::ResourceId MCP::registerResource(const mcp::ResourceDescriptor& descriptor, const Callback& callback) {
    auto resourceEntry = SharedPointer<RegisteredResource>(new RegisteredResource());
    resourceEntry->id = nextIdentifier(this->resourceCounter);
    resourceEntry->descriptor = descriptor;
    resourceEntry->callback = callback;

    {
      Lock lock(this->mutex);
      this->resourceRegistry[descriptor.uri] = resourceEntry;
    }

    this->notifyModernListChange("resourcesListChanged", "notifications/resources/list_changed");

    return resourceEntry->id;
  }

  bool MCP::unregisterResource(const String& uri) {
    bool removed = false;
    SharedPointer<RegisteredResource> removedResource = nullptr;
    Vector<String> pendingIds;
    Vector<SharedPointer<ResourceSubscription>> removedSubscriptions;
    Vector<std::pair<String, nlohmann::json>> removedModernSubscriptions;
    {
      Lock lock(this->mutex);
      const auto resourceIt = this->resourceRegistry.find(uri);
      if (resourceIt != this->resourceRegistry.end()) {
        removedResource = resourceIt->second;
        this->resourceRegistry.erase(resourceIt);
        removed = true;
      }
      if (removed) {
        for (const auto& entry : this->pendingResourceReads) {
          if (entry.second && entry.second->resourceUri == uri) {
            pendingIds.push_back(entry.first);
          }
        }

        for (auto it = this->resourceSubscriptions.begin();
             it != this->resourceSubscriptions.end();) {
          if (it->second && it->second->resourceUri == uri) {
            removedSubscriptions.push_back(it->second);
            it = this->resourceSubscriptions.erase(it);
          } else {
            ++it;
          }
        }
        for (auto it = this->sessionSubscriptions.begin();
             it != this->sessionSubscriptions.end();) {
          auto& ids = it->second;
          ids.erase(
            std::remove_if(ids.begin(), ids.end(), [&removedSubscriptions](const String& id) {
              return std::any_of(
                removedSubscriptions.begin(),
                removedSubscriptions.end(),
                [&id](const auto& subscription) {
                  return subscription && subscription->id == id;
                }
              );
            }),
            ids.end()
          );
          if (ids.empty()) {
            it = this->sessionSubscriptions.erase(it);
          } else {
            ++it;
          }
        }

        for (const auto& entry : this->modernSubscriptions) {
          const auto& subscription = entry.second;
          if (!subscription) {
            continue;
          }
          const auto resourceIt = std::find(
            subscription->resourceUris.begin(),
            subscription->resourceUris.end(),
            uri
          );
          if (resourceIt == subscription->resourceUris.end()) {
            continue;
          }
          if (subscription->started) {
            removedModernSubscriptions.push_back({entry.first, subscription->requestId});
          }
          subscription->resourceUris.erase(resourceIt);
          subscription->accepted["resourceSubscriptions"] = subscription->resourceUris;
        }
      }
    }
    if (removed) {
      for (const auto& id : pendingIds) {
        this->rejectResourceRead(
          id,
          mcp::Error(
            mcp::ErrorCode::InternalError,
            "Resource was unregistered before its read completed"
          )
        );
      }

      if (removedResource && removedResource->callback) {
        const auto notifyUnsubscribed = [&removedResource, &uri](
          const String& id,
          const String& sessionId
        ) {
          nlohmann::json paramsJson {
            {"reason", "resource-unregistered"}
          };
          removedResource->callback("-1", JSON::Object(JSON::Object::Entries {
            {"source", "mcp.resource.unsubscribe"},
            {"data", JSON::Object(JSON::Object::Entries {
              {"id", JSON::String(id)},
              {"resource", JSON::String(uri)},
              {"sessionId", JSON::String(sessionId)},
              {"descriptor", removedResource->descriptor.toJSON()},
              {"params", JSON::String(paramsJson.dump())}
            })}
          }), QueuedResponse{});
        };

        for (const auto& subscription : removedSubscriptions) {
          if (subscription) {
            notifyUnsubscribed(subscription->id, subscription->sessionId);
          }
        }
        for (const auto& subscription : removedModernSubscriptions) {
          notifyUnsubscribed(
            requestIdentifier(subscription.second),
            subscription.first
          );
        }
      }
      this->notifyModernListChange("resourcesListChanged", "notifications/resources/list_changed");
    }
    return removed;
  }

  Vector<mcp::ResourceDescriptor> MCP::listResources() const {
    Vector<mcp::ResourceDescriptor> resources;
    Lock lock(this->mutex);
    resources.reserve(this->resourceRegistry.size());
    for (const auto& entry : this->resourceRegistry) {
      if (entry.second) {
        resources.push_back(entry.second->descriptor);
      }
    }
    return resources;
  }

  SharedPointer<MCP::RegisteredTool> MCP::getTool(const String& name) const {
    Lock lock(this->mutex);
    auto it = this->toolRegistry.find(name);
    if (it != this->toolRegistry.end()) {
      return it->second;
    }
    return nullptr;
  }

  SharedPointer<MCP::RegisteredResource> MCP::getResource(const String& uri) const {
    Lock lock(this->mutex);
    auto it = this->resourceRegistry.find(uri);
    if (it != this->resourceRegistry.end()) {
      return it->second;
    }
    return nullptr;
  }

  String MCP::generateInvocationId() {
    auto value = this->invocationCounter.fetch_add(1, std::memory_order_relaxed);
    if (value == 0) {
      value = this->invocationCounter.fetch_add(1, std::memory_order_relaxed);
    }
    return std::to_string(value);
  }

  bool MCP::invokeTool(
    const String& seq,
    const String& name,
    const String& sessionId,
    const String& argumentsJson,
    bool modern,
    const Callback& reply
  ) {
    auto tool = this->getTool(name);
    if (tool == nullptr) {
      auto json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "NotFoundError"},
          {"message", "Tool not registered"}
        }}
      };
      reply(seq, JSON::Object(json), QueuedResponse{});
      return false;
    }

    if (!tool->callback) {
      auto json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "NotSupportedError"},
          {"message", "Tool has no registered handler"}
        }}
      };
      reply(seq, JSON::Object(json), QueuedResponse{});
      return false;
    }

    String validationError;
    if (!tool->definition.validateArguments(argumentsJson, validationError)) {
      auto json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "TypeError"},
          {"message", validationError}
        }}
      };
      reply(seq, JSON::Object(json), QueuedResponse{});
      return false;
    }

    const auto invocationId = this->generateInvocationId();
    auto pending = SharedPointer<PendingInvocation>(new PendingInvocation());
    pending->id = invocationId;
    pending->toolName = name;
    pending->sessionId = sessionId;
    pending->seq = seq;
    pending->definition = tool->definition;
    pending->modern = modern;
    pending->reply = reply;

    bool atCapacity = false;
    {
      Lock lock(this->mutex);
      if (this->pendingInvocations.size() >= kMaxPendingMcpRequests) {
        atCapacity = true;
      } else {
        this->pendingInvocations[invocationId] = pending;
      }
    }
    if (atCapacity) {
      auto json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "CapacityError"},
          {"message", "Too many pending MCP tool invocations"}
        }}
      };
      reply(seq, JSON::Object(json), QueuedResponse{});
      return false;
    }

    JSON::Object::Entries payload {
      {"id", invocationId},
      {"tool", name},
      {"sessionId", sessionId}
    };

    if (!argumentsJson.empty()) {
      payload.insert({"arguments", JSON::String(argumentsJson)});
    }

    auto json = JSON::Object::Entries {
      {"source", "mcp.tool.invoke"},
      {"data", JSON::Object(payload)}
    };

    tool->callback("-1", JSON::Object(json), QueuedResponse{});
    return true;
  }

  bool MCP::resolveInvocation(const String& invocationId, const String& resultJson) {
    SharedPointer<PendingInvocation> pending = nullptr;

    {
      Lock lock(this->mutex);
      auto it = this->pendingInvocations.find(invocationId);
      if (it != this->pendingInvocations.end()) {
        pending = it->second;
        this->pendingInvocations.erase(it);
      }
    }

    if (pending == nullptr || !pending->reply) {
      return false;
    }

    try {
      String validationError;
      if (!pending->definition.validateResult(resultJson, validationError)) {
        throw std::runtime_error(validationError);
      }
      auto result = JSON::parse(resultJson.empty() ? "{}" : resultJson);
      if (!pending->modern && result.isObject()) {
        auto& object = result.as<JSON::Object>();
        if (object.contains("structuredContent") &&
            !object.get("structuredContent").isObject()) {
          const auto structuredContent = object.get("structuredContent");
          object.set("structuredContent", JSON::Object(JSON::Object::Entries {
            {"value", structuredContent}
          }));
        }
      } else if (pending->modern && result.isObject()) {
        auto& object = result.as<JSON::Object>();
        object.set("resultType", JSON::String("complete"));
        JSON::Object metadata(JSON::Object::Entries {});
        if (object.contains("_meta") && object.get("_meta").isObject()) {
          metadata = object.get("_meta").as<JSON::Object>();
        }
        metadata.set("io.modelcontextprotocol/serverInfo", JSON::Object(JSON::Object::Entries {
          {"name", JSON::String("oro.runtime")},
          {"version", JSON::String(runtime::version::VERSION_STRING)}
        }));
        object.set("_meta", metadata);
      }
      pending->reply(
        pending->seq,
        JSON::Object(JSON::Object::Entries {{"data", result}}),
        QueuedResponse{}
      );
    } catch (const std::exception& err) {
      JSON::Object::Entries error {
        {"code", JSON::Number(static_cast<int>(mcp::ErrorCode::InternalError))},
        {"message", JSON::String(err.what())}
      };
      pending->reply(
        pending->seq,
        JSON::Object(JSON::Object::Entries {{"err", JSON::Object(error)}}),
        QueuedResponse{}
      );
    }
    return true;
  }

  bool MCP::rejectInvocation(const String& invocationId, const mcp::Error& error) {
    SharedPointer<PendingInvocation> pending = nullptr;

    {
      Lock lock(this->mutex);
      auto it = this->pendingInvocations.find(invocationId);
      if (it != this->pendingInvocations.end()) {
        pending = it->second;
        this->pendingInvocations.erase(it);
      }
    }

    if (pending == nullptr || !pending->reply) {
      return false;
    }

    pending->reply(
      pending->seq,
      JSON::Object(JSON::Object::Entries {{"err", error.toJSON()}}),
      QueuedResponse{}
    );
    return true;
  }

  bool MCP::resolveResourceRead(const String& readId, const String& resultJson) {
    SharedPointer<PendingResourceRead> pending = nullptr;

    {
      Lock lock(this->mutex);
      auto it = this->pendingResourceReads.find(readId);
      if (it != this->pendingResourceReads.end()) {
        pending = it->second;
        this->pendingResourceReads.erase(it);
      }
    }

    if (pending == nullptr) {
      return false;
    }

    if (pending->reply) {
      JSON::Object::Entries data {
        {"id", readId},
        {"result", JSON::String(resultJson)}
      };
      pending->reply(pending->seq, JSON::Object(JSON::Object::Entries {{"data", JSON::Object(data)}}), QueuedResponse{});
      return true;
    }

    nlohmann::json message;
    message["jsonrpc"] = "2.0";
    if (!pending->requestId.is_null()) {
      message["id"] = pending->requestId;
    }

    try {
      nlohmann::json parsed = resultJson.empty() ? nlohmann::json::object() : nlohmann::json::parse(resultJson);
      if (!parsed.contains("contents")) {
        throw std::runtime_error("Resource response missing contents");
      }
      if (pending->modern) {
        parsed["resultType"] = "complete";
        parsed["ttlMs"] = 5000;
        parsed["cacheScope"] = "private";
        parsed["_meta"]["io.modelcontextprotocol/serverInfo"] = {
          {"name", "oro.runtime"},
          {"version", runtime::version::VERSION_STRING}
        };
      }
      message["result"] = parsed;
    } catch (const std::exception& err) {
      message["error"] = {
        {"code", static_cast<int>(mcp::ErrorCode::InternalError)},
        {"message", err.what()}
      };
    }

    this->sendJsonRpcNotification(pending->sessionId, message);
    return true;
  }

  bool MCP::rejectResourceRead(const String& readId, const mcp::Error& error) {
    SharedPointer<PendingResourceRead> pending = nullptr;

    {
      Lock lock(this->mutex);
      auto it = this->pendingResourceReads.find(readId);
      if (it != this->pendingResourceReads.end()) {
        pending = it->second;
        this->pendingResourceReads.erase(it);
      }
    }

    if (pending == nullptr) {
      return false;
    }

    if (pending->reply) {
      JSON::Object::Entries errEntries {
        {"id", readId},
        {"error", error.toJSON()}
      };
      pending->reply(pending->seq, JSON::Object(JSON::Object::Entries {{"err", JSON::Object(errEntries)}}), QueuedResponse{});
      return true;
    }

    nlohmann::json message;
    message["jsonrpc"] = "2.0";
    if (!pending->requestId.is_null()) {
      message["id"] = pending->requestId;
    }

    nlohmann::json errJson;
    errJson["code"] = static_cast<int>(error.code);
    errJson["message"] = std::string(error.message);
    try {
      if (!error.data.isNull()) {
        errJson["data"] = nlohmann::json::parse(error.data.str());
      }
    } catch (...) {
      errJson["data"] = error.data.str();
    }

    message["error"] = errJson;
    this->sendJsonRpcNotification(pending->sessionId, message);
    return true;
  }

  namespace {
    std::string jsonString(const JSON::Any& value) {
      try {
        return value.str();
      } catch (...) {
        return "{}";
      }
    }
  }

  struct MCP::ServerAdapter : public mcp::HTTPServer::Delegate {
    MCP& service;

    explicit ServerAdapter(MCP& svc) : service(svc) {}

    std::optional<bool> authorize(const httplib::Request& req, httplib::Response& res) override {
      return this->service.authorizeRequest(req, res);
    }

    void onSessionStarted(const String& sessionId) override {
      Lock lock(this->service.mutex);
      this->service.sessionState[sessionId] = false;
    }

    void onSessionStopped(const String& sessionId) override {
      {
        Lock lock(this->service.mutex);
        this->service.sessionState.erase(sessionId);
        auto invIt = this->service.pendingInvocations.begin();
        while (invIt != this->service.pendingInvocations.end()) {
          if (invIt->second && invIt->second->sessionId == sessionId) {
            invIt = this->service.pendingInvocations.erase(invIt);
          } else {
            ++invIt;
          }
        }

        auto resIt = this->service.pendingResourceReads.begin();
        while (resIt != this->service.pendingResourceReads.end()) {
          if (resIt->second && resIt->second->sessionId == sessionId) {
            resIt = this->service.pendingResourceReads.erase(resIt);
          } else {
            ++resIt;
          }
        }
      }

      this->service.removeSessionSubscriptions(sessionId);
    }

    bool getExpectedRequestHeaders(
      const String& payload,
      Vector<mcp::ToolHeader>& headers,
      String& error
    ) override {
      try {
        const auto request = nlohmann::json::parse(payload);
        if (request.value("method", "") != "tools/call") {
          return true;
        }
        const auto params = request.value("params", nlohmann::json::object());
        const auto name = params.value("name", "");
        auto tool = this->service.getTool(name);
        if (!tool) {
          return true;
        }
        const auto arguments = params.contains("arguments")
          ? params["arguments"].dump()
          : String("{}");
        return tool->definition.getExpectedHTTPHeaders(arguments, headers, error);
      } catch (const std::exception& exception) {
        error = String("Unable to validate MCP parameter headers: ") + exception.what();
        return false;
      }
    }

    std::optional<String> onJsonRpcRequest(const String& sessionId, const String& payload) override {
      SynchronousResponseGuard guard;
      nlohmann::json request;
      try {
        request = nlohmann::json::parse(payload);
      } catch (const std::exception& err) {
        nlohmann::json error = {
          {"jsonrpc", "2.0"},
          {"error", {
            {"code", static_cast<int>(mcp::ErrorCode::ParseError)},
            {"message", err.what()}
          }}
        };
        this->service.sendJsonRpcNotification(sessionId, error);
        return error.dump();
      }

      auto result = this->service.handleJsonRpcRequest(sessionId, request);
      if (result.has_value()) {
        if (isModernRequest(request)) {
          finalizeModernResponse(*result, request);
        }
        return result->dump();
      }
      return std::nullopt;
    }

    void onJsonRpcResponseQueued(const String& sessionId, const String& payload) override {
      try {
        const auto response = nlohmann::json::parse(payload);
        if (response.is_object() &&
            response.value("method", "") == "notifications/subscriptions/acknowledged") {
          this->service.notifyModernSubscriptionStarted(sessionId);
        }
      } catch (...) {}
    }

    void onPing(const String& sessionId) override {
      (void)sessionId;
    }
  };

  MCP::~MCP() = default;

  bool MCP::startServer(const mcp::HTTPServer::Config& cfg) {
    mcp::HTTPServer::Config config = cfg;
    {
      Lock lock(this->mutex);
      if (config.token.empty() && !this->staticAuthToken.empty()) {
        config.token = this->staticAuthToken;
      }

      if (this->serverActive) {
        return true;
      }

      this->server = std::make_unique<mcp::HTTPServer>();
      this->serverAdapter = std::make_unique<ServerAdapter>(*this);
      if (!this->server->start(config, this->serverAdapter.get())) {
        this->serverAdapter.reset();
        this->server.reset();
        return false;
      }

      this->serverConfig = this->server->getConfig();
      this->serverActive = true;
    }

    return true;
  }

  void MCP::stopServer() {
    Vector<String> sessions;
    std::unique_ptr<mcp::HTTPServer> localServer;
    std::unique_ptr<ServerAdapter> localAdapter;

    {
      Lock lock(this->mutex);
      if (!this->serverActive) {
        return;
      }

      for (const auto& entry : this->sessionSubscriptions) {
        sessions.push_back(entry.first);
      }
      for (const auto& entry : this->modernSubscriptions) {
        if (std::find(sessions.begin(), sessions.end(), entry.first) == sessions.end()) {
          sessions.push_back(entry.first);
        }
      }

      localServer = std::move(this->server);
      localAdapter = std::move(this->serverAdapter);
      this->serverActive = false;
    }

    if (localServer) {
      localServer->stop();
    }
    localAdapter.reset();

    for (const auto& sessionId : sessions) {
      this->removeSessionSubscriptions(sessionId, String("server-stopped"));
    }

    {
      Lock lock(this->mutex);
      this->sessionState.clear();
      this->pendingInvocations.clear();
      this->pendingResourceReads.clear();
      this->resourceSubscriptions.clear();
      this->sessionSubscriptions.clear();
      this->modernSubscriptions.clear();
      this->serverConfig = mcp::HTTPServer::Config();
    }

    this->rejectAllPendingAuthorizations();
  }

  void MCP::setAuthorizationHandler(const Callback& callback) {
    Lock lock(this->mutex);
    this->authCallback = callback;
  }

  void MCP::clearAuthorizationHandler() {
    {
      Lock lock(this->mutex);
      this->authCallback = nullptr;
    }
    this->rejectAllPendingAuthorizations();
  }

  bool MCP::resolveAuthorization(const String& id,
                                 bool allow,
                                 const std::optional<int>& statusOverride,
                                 const std::optional<String>& messageOverride) {
    SharedPointer<PendingAuthorization> pending;
    {
      Lock lock(this->mutex);
      auto it = this->pendingAuthorizations.find(id);
      if (it == this->pendingAuthorizations.end()) {
        return false;
      }
      pending = it->second;
      this->pendingAuthorizations.erase(it);
    }

    if (!pending || pending->completed) {
      return false;
    }

    AuthorizationDecision decision;
    decision.allow = allow;
    if (statusOverride.has_value()) {
      decision.status = statusOverride.value();
    }
    if (messageOverride.has_value()) {
      decision.message = *messageOverride;
    }

    pending->completed = true;
    try {
      pending->promise.set_value(decision);
    } catch (...) {
      return false;
    }

    return true;
  }

  std::optional<bool> MCP::authorizeRequest(const httplib::Request& req, httplib::Response& res) {
    Callback callback;
    uint32_t timeoutMs = 0;

    {
      Lock lock(this->mutex);
      callback = this->authCallback;
      timeoutMs = this->authorizationTimeoutMs;
    }

    if (callback == nullptr) {
      return std::nullopt;
    }

    auto pending = SharedPointer<PendingAuthorization>(new PendingAuthorization());
    const auto authId = std::to_string(this->authCounter.fetch_add(1, std::memory_order_relaxed));
    auto future = pending->promise.get_future();

    {
      Lock lock(this->mutex);
      this->pendingAuthorizations.insert_or_assign(authId, pending);
    }

    JSON::Array::Entries headerEntries;
    for (const auto& header : req.headers) {
      headerEntries.push_back(JSON::Object::Entries {
        {"name", JSON::String(header.first)},
        {"value", JSON::String(header.second)}
      });
    }

    JSON::Array::Entries queryEntries;
    for (const auto& param : req.params) {
      queryEntries.push_back(JSON::Object::Entries {
        {"name", JSON::String(param.first)},
        {"value", JSON::String(param.second)}
      });
    }

    JSON::Object::Entries dataEntries {
      {"id", JSON::String(authId)},
      {"method", JSON::String(req.method)},
      {"path", JSON::String(req.path)},
      {"remoteAddress", JSON::String(req.remote_addr)},
      {"remotePort", JSON::Number(static_cast<uint64_t>(req.remote_port >= 0 ? req.remote_port : 0))},
      {"headers", JSON::Array(headerEntries)},
      {"query", JSON::Array(queryEntries)}
    };

    const auto authHeader = req.get_header_value("Authorization");
    if (!authHeader.empty()) {
      dataEntries.insert({"authorization", JSON::String(authHeader)});
    }

    if (!req.body.empty()) {
      dataEntries.insert({"body", JSON::String(req.body)});
    }

    callback("-1", JSON::Object(JSON::Object::Entries {
      {"source", "mcp.server.authorize"},
      {"data", JSON::Object(dataEntries)}
    }), QueuedResponse{});

    AuthorizationDecision decision;
    bool decided = false;

    const auto waitDuration = std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : this->authorizationTimeoutMs);
    const auto status = future.wait_for(waitDuration);
    if (status == std::future_status::ready) {
      try {
        decision = future.get();
        decided = true;
      } catch (...) {
        decided = false;
      }
    }

    {
      Lock lock(this->mutex);
      this->pendingAuthorizations.erase(authId);
    }

    if (!decided) {
      decision.allow = false;
    }

    if (!decision.allow) {
      res.status = decision.status;
      if (decision.status == 401) {
        res.set_header("WWW-Authenticate", "Bearer");
      }
      res.set_content(decision.message.c_str(), "text/plain");
    }

    return decision.allow;
  }

  void MCP::rejectAllPendingAuthorizations() {
    Map<String, SharedPointer<PendingAuthorization>> pending;
    {
      Lock lock(this->mutex);
      if (this->pendingAuthorizations.empty()) {
        return;
      }
      pending.swap(this->pendingAuthorizations);
    }

    for (auto& entry : pending) {
      auto pendingAuth = entry.second;
      if (!pendingAuth || pendingAuth->completed) {
        continue;
      }

      AuthorizationDecision decision;
      decision.allow = false;
      try {
        pendingAuth->completed = true;
        pendingAuth->promise.set_value(decision);
      } catch (...) {}
    }
  }

  bool MCP::serverRunning() const {
    Lock lock(this->mutex);
    return this->serverActive;
  }

  mcp::HTTPServer::Config MCP::getServerConfig() const {
    Lock lock(this->mutex);
    return this->serverConfig;
  }

  bool MCP::publishResourceUpdate(const String& uri,
                                  const String& resultJson,
                                  const std::optional<String>& sessionFilter,
                                  const std::optional<String>& subscriptionFilter) {
    if (uri.empty()) {
      return false;
    }
    (void)resultJson;

    Vector<SharedPointer<ResourceSubscription>> targets;
    Vector<std::pair<String, nlohmann::json>> modernTargets;
    {
      Lock lock(this->mutex);
      if (subscriptionFilter.has_value()) {
        const auto& id = *subscriptionFilter;
        auto it = this->resourceSubscriptions.find(id);
        if (it != this->resourceSubscriptions.end() && it->second) {
          const auto& subscription = it->second;
          if (subscription->resourceUri == uri && (!sessionFilter.has_value() || subscription->sessionId == *sessionFilter)) {
            targets.push_back(subscription);
          }
        }
      } else {
        for (const auto& entry : this->resourceSubscriptions) {
          const auto& subscription = entry.second;
          if (!subscription) {
            continue;
          }
          if (subscription->resourceUri != uri) {
            continue;
          }
          if (sessionFilter.has_value() && subscription->sessionId != *sessionFilter) {
            continue;
          }
          targets.push_back(subscription);
        }
      }

      for (const auto& entry : this->modernSubscriptions) {
        const auto& sessionId = entry.first;
        const auto& subscription = entry.second;
        if (!subscription || !subscription->started) {
          continue;
        }
        if (sessionFilter.has_value() && sessionId != *sessionFilter) {
          continue;
        }
        if (subscriptionFilter.has_value() &&
            requestIdentifier(subscription->requestId) != *subscriptionFilter) {
          continue;
        }
        if (std::find(
              subscription->resourceUris.begin(),
              subscription->resourceUris.end(),
              uri) == subscription->resourceUris.end()) {
          continue;
        }
        modernTargets.push_back({sessionId, subscription->requestId});
      }
    }

    if (targets.empty() && modernTargets.empty()) {
      return false;
    }

    bool delivered = false;
    for (const auto& subscription : targets) {
      if (!subscription) {
        continue;
      }

      nlohmann::json message;
      message["jsonrpc"] = "2.0";
      message["method"] = "notifications/resources/updated";
      message["params"] = {{"uri", subscription->resourceUri}};
      delivered = this->sendJsonRpcNotification(subscription->sessionId, message) || delivered;
    }

    for (const auto& target : modernTargets) {
      nlohmann::json message;
      message["jsonrpc"] = "2.0";
      message["method"] = "notifications/resources/updated";
      message["params"] = {
        {"uri", uri},
        {"_meta", {
          {"io.modelcontextprotocol/subscriptionId", target.second}
        }}
      };
      delivered = this->sendJsonRpcNotification(target.first, message) || delivered;
    }

    return delivered;
  }

  void MCP::removeSessionSubscriptions(const String& sessionId,
                                       const std::optional<String>& reason) {
    Vector<SharedPointer<ResourceSubscription>> removed;
    SharedPointer<ModernSubscription> modernSubscription = nullptr;

    {
      Lock lock(this->mutex);
      const auto modernIt = this->modernSubscriptions.find(sessionId);
      if (modernIt != this->modernSubscriptions.end()) {
        modernSubscription = modernIt->second;
        this->modernSubscriptions.erase(modernIt);
      }
      auto it = this->sessionSubscriptions.find(sessionId);
      if (it == this->sessionSubscriptions.end()) {
        if (!modernSubscription) {
          return;
        }
      } else {
        for (const auto& id : it->second) {
          auto subIt = this->resourceSubscriptions.find(id);
          if (subIt != this->resourceSubscriptions.end()) {
            removed.push_back(subIt->second);
            this->resourceSubscriptions.erase(subIt);
          }
        }

        this->sessionSubscriptions.erase(it);
      }
    }

    for (const auto& subscription : removed) {
      if (!subscription) {
        continue;
      }

      auto resource = this->getResource(subscription->resourceUri);
      if (!resource || !resource->callback) {
        continue;
      }

      JSON::Object::Entries dataEntries {
        {"id", JSON::String(subscription->id)},
        {"resource", JSON::String(subscription->resourceUri)},
        {"sessionId", JSON::String(subscription->sessionId)},
        {"descriptor", resource->descriptor.toJSON()}
      };

      const String reasonValue = (reason.has_value() && !reason->empty())
        ? *reason
        : String("session-closed");
      nlohmann::json paramsJson;
      paramsJson["reason"] = reasonValue;
      const std::string paramsDump = paramsJson.dump();
      dataEntries.insert({"params", JSON::String(paramsDump)});

      resource->callback("-1", JSON::Object(JSON::Object::Entries {
        {"source", "mcp.resource.unsubscribe"},
        {"data", JSON::Object(dataEntries)}
      }), QueuedResponse{});
    }

    if (modernSubscription && modernSubscription->started) {
      for (const auto& uri : modernSubscription->resourceUris) {
        auto resource = this->getResource(uri);
        if (!resource || !resource->callback) {
          continue;
        }
        JSON::Object::Entries dataEntries {
          {"id", JSON::String(requestIdentifier(modernSubscription->requestId))},
          {"resource", JSON::String(uri)},
          {"sessionId", JSON::String(sessionId)},
          {"descriptor", resource->descriptor.toJSON()}
        };
        resource->callback("-1", JSON::Object(JSON::Object::Entries {
          {"source", "mcp.resource.unsubscribe"},
          {"data", JSON::Object(dataEntries)}
        }), QueuedResponse{});
      }
    }
  }

  void MCP::notifyModernListChange(const String& filter, const String& method) {
    Vector<std::pair<String, nlohmann::json>> targets;
    {
      Lock lock(this->mutex);
      for (const auto& entry : this->modernSubscriptions) {
        const auto& subscription = entry.second;
        if (!subscription || !subscription->started) {
          continue;
        }
        const bool enabled = filter == "toolsListChanged"
          ? subscription->toolsListChanged
          : subscription->resourcesListChanged;
        if (enabled) {
          targets.push_back({entry.first, subscription->requestId});
        }
      }
    }

    for (const auto& target : targets) {
      nlohmann::json message;
      message["jsonrpc"] = "2.0";
      message["method"] = method;
      message["params"] = {
        {"_meta", {
          {"io.modelcontextprotocol/subscriptionId", target.second}
        }}
      };
      this->sendJsonRpcNotification(target.first, message);
    }
  }

  bool MCP::sendJsonRpcNotification(const String& sessionId, const nlohmann::json& message) {
    if (suppressSynchronousTransportSend) {
      return false;
    }
    if (!this->server || !this->server->isRunning()) {
      return false;
    }

    return this->server->sendEvent(sessionId, "message", message.dump());
  }

  void MCP::notifyModernSubscriptionStarted(const String& sessionId) {
    nlohmann::json requestId;
    Vector<String> resourceUris;
    {
      Lock lock(this->mutex);
      const auto it = this->modernSubscriptions.find(sessionId);
      if (it == this->modernSubscriptions.end() || !it->second || it->second->started) {
        return;
      }
      it->second->started = true;
      requestId = it->second->requestId;
      resourceUris = it->second->resourceUris;
    }

    for (const auto& uri : resourceUris) {
      auto resource = this->getResource(uri);
      if (!resource || !resource->callback) {
        continue;
      }
      JSON::Object::Entries dataEntries {
        {"id", JSON::String(requestIdentifier(requestId))},
        {"resource", JSON::String(resource->descriptor.uri)},
        {"sessionId", JSON::String(sessionId)},
        {"descriptor", resource->descriptor.toJSON()}
      };
      resource->callback("-1", JSON::Object(JSON::Object::Entries {
        {"source", "mcp.resource.subscribe"},
        {"data", JSON::Object(dataEntries)}
      }), QueuedResponse{});
    }
  }

  std::optional<nlohmann::json> MCP::handleJsonRpcRequest(const String& sessionId, const nlohmann::json& request) {
    if (!request.is_object()) {
      return std::nullopt;
    }

    const auto idIt = request.find("id");
    const bool isNotification = idIt == request.end();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    if (idIt != request.end() && (idIt->is_string() || idIt->is_number())) {
      response["id"] = *idIt;
    }

    if (!request.contains("jsonrpc") ||
        !request["jsonrpc"].is_string() ||
        request["jsonrpc"].get<String>() != mcp::kJsonRpcVersion ||
        !request.contains("method") ||
        !request["method"].is_string() ||
        request["method"].get<String>().empty() ||
        (idIt != request.end() && !idIt->is_string() && !idIt->is_number())) {
      response["error"] = {
        {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
        {"message", "Invalid JSON-RPC 2.0 request"}
      };
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    const auto method = request["method"].get<String>();
    if (request.contains("params") && !request["params"].is_object()) {
      response["error"] = {
        {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
        {"message", "MCP request params must be an object"}
      };
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    const auto params = request.value("params", nlohmann::json::object());
    const bool modern = isModernRequest(request);

    if (modern &&
        (!params.contains("_meta") ||
         !params["_meta"].is_object() ||
         !params["_meta"].contains("io.modelcontextprotocol/clientCapabilities") ||
         !params["_meta"]["io.modelcontextprotocol/clientCapabilities"].is_object())) {
      response["error"] = {
        {"code", static_cast<int>(mcp::ErrorCode::MissingRequiredClientCapability)},
        {"message", "Request metadata must include clientCapabilities"},
        {"data", {{"requiredCapabilities", nlohmann::json::object()}}}
      };
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    bool initialized = false;
    {
      Lock lock(this->mutex);
      const auto it = this->sessionState.find(sessionId);
      if (it != this->sessionState.end()) {
        initialized = it->second;
      }
    }

    if (method == "initialize") {
      const auto requestedVersion = params.contains("protocolVersion") && params["protocolVersion"].is_string()
        ? params["protocolVersion"].get<String>()
        : String();
      if (isNotification) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
          {"message", "initialize requires a request id"}
        };
      } else if (modern) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::MethodNotFound)},
          {"message", "initialize is not available in the modern MCP protocol"}
        };
      } else if (initialized) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
          {"message", "MCP session is already initialized"}
        };
      } else if (requestedVersion.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Missing protocol version"}
        };
      } else if (!params.contains("capabilities") ||
                 !params["capabilities"].is_object() ||
                 !params.contains("clientInfo") ||
                 !params["clientInfo"].is_object() ||
                 !params["clientInfo"].contains("name") ||
                 !params["clientInfo"]["name"].is_string() ||
                 params["clientInfo"]["name"].get<String>().empty() ||
                 !params["clientInfo"].contains("version") ||
                 !params["clientInfo"]["version"].is_string() ||
                 params["clientInfo"]["version"].get<String>().empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "initialize requires capabilities and clientInfo with name and version"}
        };
      } else {
        const auto negotiatedVersion = mcp::isMcp2025ProtocolVersion(requestedVersion)
          ? requestedVersion
          : String(mcp::kMcp2025ProtocolVersion);
        {
          Lock lock(this->mutex);
          this->sessionState[sessionId] = true;
        }

        const auto registeredTools = this->listTools();
        const auto registeredResources = this->listResources();

        String serverName = "oro.runtime";
        String serverVersion = runtime::version::VERSION_STRING;
        String serverTitle;

        if (auto* runtime = this->context.getRuntime()) {
          const auto& userConfig = runtime->userConfig;

          const auto updateFromConfig = [&userConfig](const char* key, String& target) {
            const auto it = userConfig.find(key);
            if (it != userConfig.end() && !it->second.empty()) {
              target = it->second;
              return true;
            }
            return false;
          };

          updateFromConfig("meta_bundle_identifier", serverName) || updateFromConfig("build_name", serverName);
          updateFromConfig("meta_title", serverTitle) || updateFromConfig("build_name", serverTitle);
          updateFromConfig("meta_version", serverVersion) || updateFromConfig("build_version", serverVersion);
        }

        if (serverVersion.empty()) {
          serverVersion = "0.0.0";
        }

        nlohmann::json serverInfo;
        serverInfo["name"] = serverName.c_str();
        serverInfo["version"] = serverVersion.c_str();
        if (!serverTitle.empty()) {
          serverInfo["title"] = serverTitle.c_str();
        }

        nlohmann::json capabilities = nlohmann::json::object();

        const bool hasSubscribableResource = std::any_of(
          registeredResources.begin(),
          registeredResources.end(),
          [](const auto& resource) {
            return resource.subscribable;
          }
        );
        if (!registeredResources.empty()) {
          nlohmann::json resourceCaps = nlohmann::json::object();
          resourceCaps["subscribe"] = hasSubscribableResource;
          capabilities["resources"] = resourceCaps;
        }

        if (!registeredTools.empty()) {
          nlohmann::json toolCaps = nlohmann::json::object();
          capabilities["tools"] = toolCaps;
        }

        nlohmann::json resultPayload;
        resultPayload["protocolVersion"] = negotiatedVersion;
        resultPayload["capabilities"] = capabilities;
        resultPayload["serverInfo"] = serverInfo;

        if (auto* runtime = this->context.getRuntime()) {
          const auto& userConfig = runtime->userConfig;
          const auto instructionsIt = userConfig.find("mcp_instructions");
          if (instructionsIt != userConfig.end() && !instructionsIt->second.empty()) {
            resultPayload["instructions"] = instructionsIt->second.c_str();
          }
        }

        response["result"] = resultPayload;
      }

      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "server/discover") {
      if (!modern) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::UnsupportedProtocolVersion)},
          {"message", "server/discover requires MCP 2026-07-28 request metadata"},
          {"data", {
            {"supported", mcp::supportedProtocolVersions()},
            {"requested", ""}
          }}
        };
        return response;
      }

      const auto registeredResources = this->listResources();
      const bool hasSubscribableResource = std::any_of(
        registeredResources.begin(),
        registeredResources.end(),
        [](const auto& resource) {
          return resource.subscribable;
        }
      );
      nlohmann::json capabilities = {
        {"tools", {{"listChanged", true}}},
        {"resources", {
          {"listChanged", true},
          {"subscribe", hasSubscribableResource}
        }}
      };

      response["result"] = {
        {"supportedVersions", mcp::supportedProtocolVersions()},
        {"capabilities", capabilities},
        {"instructions", "Invoke registered application tools and resources using workspace-scoped inputs."},
        {"ttlMs", 60000},
        {"cacheScope", "private"},
        {"_meta", {
          {"io.modelcontextprotocol/serverInfo", {
            {"name", "oro.runtime"},
            {"version", runtime::version::VERSION_STRING}
          }}
        }}
      };
      return response;
    }

    if (method == "ping") {
      response["result"] = nlohmann::json::object();
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "notifications/initialized") {
      return std::nullopt;
    }

    if (!modern && !initialized && method != "initialize" && method != "ping") {
      response["error"] = {
        {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
        {"message", "Session not initialized"}
      };
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "tools/list") {
      nlohmann::json tools = nlohmann::json::array();
      for (const auto& tool : this->listTools()) {
        tools.push_back(nlohmann::json::parse(tool.toJSON(modern).str()));
      }
      response["result"] = {{"tools", tools}};
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "resources/list") {
      nlohmann::json resources = nlohmann::json::array();
      for (const auto& resource : this->listResources()) {
        resources.push_back(nlohmann::json::parse(resource.toJSON().str()));
      }
      response["result"] = {{"resources", resources}};
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "subscriptions/listen") {
      if (!modern) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::MethodNotFound)},
          {"message", "subscriptions/listen requires MCP 2026-07-28"}
        };
        return response;
      }
      if (isNotification) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
          {"message", "subscriptions/listen requires a request id"}
        };
        return response;
      }
      if (!params.contains("notifications") || !params["notifications"].is_object()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "subscriptions/listen requires a notifications filter"}
        };
        return response;
      }

      const auto& requested = params["notifications"];
      for (const auto* filter : {"toolsListChanged", "promptsListChanged", "resourcesListChanged"}) {
        if (requested.contains(filter) && !requested[filter].is_boolean()) {
          response["error"] = {
            {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
            {"message", String(filter) + " must be a boolean"}
          };
          return response;
        }
      }
      auto subscription = SharedPointer<ModernSubscription>(new ModernSubscription());
      subscription->requestId = *idIt;
      subscription->toolsListChanged = requested.value("toolsListChanged", false);
      subscription->resourcesListChanged = requested.value("resourcesListChanged", false);
      if (subscription->toolsListChanged) {
        subscription->accepted["toolsListChanged"] = true;
      }
      if (subscription->resourcesListChanged) {
        subscription->accepted["resourcesListChanged"] = true;
      }

      if (requested.contains("resourceSubscriptions")) {
        if (!requested["resourceSubscriptions"].is_array()) {
          response["error"] = {
            {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
            {"message", "resourceSubscriptions must be an array of resource URIs"}
          };
          return response;
        }
        for (const auto& requestedUri : requested["resourceSubscriptions"]) {
          if (!requestedUri.is_string()) {
            response["error"] = {
              {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
              {"message", "resourceSubscriptions entries must be strings"}
            };
            return response;
          }
          const auto uri = requestedUri.get<String>();
          auto resource = this->getResource(uri);
          if (!resource || !resource->descriptor.subscribable) {
            continue;
          }
          if (std::find(
                subscription->resourceUris.begin(),
                subscription->resourceUris.end(),
                uri) == subscription->resourceUris.end()) {
            subscription->resourceUris.push_back(uri);
          }
        }
        subscription->accepted["resourceSubscriptions"] = subscription->resourceUris;
      }

      {
        Lock lock(this->mutex);
        this->modernSubscriptions[sessionId] = subscription;
      }

      nlohmann::json acknowledgement;
      acknowledgement["jsonrpc"] = "2.0";
      acknowledgement["method"] = "notifications/subscriptions/acknowledged";
      acknowledgement["params"] = {
        {"notifications", subscription->accepted},
        {"_meta", {
          {"io.modelcontextprotocol/subscriptionId", subscription->requestId}
        }}
      };
      return acknowledgement;
    }

    if (method == "resources/subscribe") {
      if (modern) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "resources/subscribe is not available in MCP 2026-07-28"}
        };
        return response;
      }
      if (idIt == request.end()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
          {"message", "Request id required"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      const auto uri = params.contains("uri") && params["uri"].is_string()
        ? params["uri"].get<String>()
        : String();
      if (uri.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Missing resource uri"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      auto resource = this->getResource(uri);
      if (!resource) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Resource not available"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      if (!resource->descriptor.subscribable) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Resource does not support subscriptions"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      const auto subscriptionId = this->generateInvocationId();
      auto subscription = SharedPointer<ResourceSubscription>(new ResourceSubscription());
      subscription->id = subscriptionId;
      subscription->resourceUri = uri;
      subscription->sessionId = sessionId;

      {
        Lock lock(this->mutex);
        this->resourceSubscriptions[subscriptionId] = subscription;
        auto& sessionList = this->sessionSubscriptions[sessionId];
        sessionList.push_back(subscriptionId);
      }

      if (resource->callback) {
        JSON::Object::Entries dataEntries {
          {"id", JSON::String(subscriptionId)},
          {"resource", JSON::String(uri)},
          {"sessionId", JSON::String(sessionId)},
          {"descriptor", resource->descriptor.toJSON()}
        };

        const auto paramsDump = params.is_null() ? std::string() : params.dump();
        if (!paramsDump.empty() && paramsDump != "null") {
          dataEntries.insert({"params", JSON::String(paramsDump)});
        }

        resource->callback("-1", JSON::Object(JSON::Object::Entries {
          {"source", "mcp.resource.subscribe"},
          {"data", JSON::Object(dataEntries)}
        }), QueuedResponse{});
      }

      response["result"] = nlohmann::json::object();
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "resources/unsubscribe") {
      if (idIt == request.end()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
          {"message", "Request id required"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      const String uri = params.contains("uri") && params["uri"].is_string()
        ? params["uri"].get<String>()
        : String();
      if (uri.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Missing resource uri"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      Vector<SharedPointer<ResourceSubscription>> subscriptions;
      {
        Lock lock(this->mutex);
        auto it = this->resourceSubscriptions.begin();
        while (it != this->resourceSubscriptions.end()) {
          const auto& subscription = it->second;
          if (subscription && subscription->sessionId == sessionId && subscription->resourceUri == uri) {
            subscriptions.push_back(subscription);
            it = this->resourceSubscriptions.erase(it);
          } else {
            ++it;
          }
        }

        auto sessionIt = this->sessionSubscriptions.find(sessionId);
        if (sessionIt != this->sessionSubscriptions.end()) {
          auto& ids = sessionIt->second;
          for (const auto& subscription : subscriptions) {
            if (subscription) {
              ids.erase(std::remove(ids.begin(), ids.end(), subscription->id), ids.end());
            }
          }
          if (ids.empty()) {
            this->sessionSubscriptions.erase(sessionIt);
          }
        }
      }

      if (subscriptions.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Resource is not subscribed on this session"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      for (const auto& subscription : subscriptions) {
        if (!subscription) {
          continue;
        }
        auto resource = this->getResource(subscription->resourceUri);
        if (resource && resource->callback) {
          JSON::Object::Entries dataEntries {
            {"id", JSON::String(subscription->id)},
            {"resource", JSON::String(subscription->resourceUri)},
            {"sessionId", JSON::String(subscription->sessionId)},
            {"descriptor", resource->descriptor.toJSON()}
          };
          resource->callback("-1", JSON::Object(JSON::Object::Entries {
            {"source", "mcp.resource.unsubscribe"},
            {"data", JSON::Object(dataEntries)}
          }), QueuedResponse{});
        }
      }

      response["result"] = nlohmann::json::object();
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "resources/read") {
      if (idIt == request.end()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
          {"message", "Request id required"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      const auto uri = params.contains("uri") && params["uri"].is_string()
        ? params["uri"].get<String>()
        : String();
      if (uri.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Missing resource uri"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      auto resource = this->getResource(uri);
      if (!resource || !resource->callback) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Resource not available"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      auto pending = SharedPointer<PendingResourceRead>(new PendingResourceRead());
      pending->id = this->generateInvocationId();
      pending->resourceUri = uri;
      pending->sessionId = sessionId;
      pending->seq = String();
      pending->reply = nullptr;
      pending->requestId = *idIt;
      pending->modern = modern;

      {
        Lock lock(this->mutex);
        if (this->pendingResourceReads.size() >= kMaxPendingMcpRequests) {
          response["error"] = {
            {"code", static_cast<int>(mcp::ErrorCode::InternalError)},
            {"message", "Too many pending MCP resource reads"}
          };
          this->sendJsonRpcNotification(sessionId, response);
          return response;
        }
        this->pendingResourceReads[pending->id] = pending;
      }

      JSON::Object::Entries dataEntries {
        {"id", pending->id},
        {"resource", uri},
        {"sessionId", sessionId},
        {"descriptor", resource->descriptor.toJSON()}
      };

      const auto paramsDump = params.is_null() ? std::string() : params.dump();
      if (!paramsDump.empty() && paramsDump != "null") {
        dataEntries.insert({"params", JSON::String(paramsDump)});
      }

      resource->callback("-1", JSON::Object(JSON::Object::Entries {
        {"source", "mcp.resource.read"},
        {"data", JSON::Object(dataEntries)}
      }), QueuedResponse{});

      return std::nullopt;
    }

    if (method == "tools/call") {
      const auto name = params.contains("name") && params["name"].is_string()
        ? params["name"].get<String>()
        : String();
      const auto argumentsJson = params.contains("arguments") ? params["arguments"].dump() : std::string();

      if (name.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Missing tool name"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      auto registeredTool = this->getTool(name);
      if (!registeredTool || !registeredTool->callback) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Tool not available"}
        };
        return response;
      }
      String validationError;
      if (!registeredTool->definition.validateArguments(argumentsJson, validationError)) {
        response["result"] = {
          {"content", nlohmann::json::array({{
            {"type", "text"},
            {"text", validationError}
          }})},
          {"isError", true}
        };
        return response;
      }

      const auto idString = idIt != request.end() && idIt->is_string() ? idIt->get<std::string>() : idIt != request.end() ? idIt->dump() : std::string();

      auto callback = [this, sessionId, idValue = idIt != request.end() ? *idIt : nlohmann::json(nullptr)](const String&, JSON::Any payload, QueuedResponse) {
        nlohmann::json message;
        message["jsonrpc"] = "2.0";
        if (!idValue.is_null()) {
          message["id"] = idValue;
        }

        try {
          auto parsed = nlohmann::json::parse(jsonString(payload));
          if (parsed.contains("err")) {
            message["error"] = parsed["err"];
          } else if (parsed.contains("data")) {
            message["result"] = parsed["data"];
          } else {
            message["result"] = parsed;
          }
        } catch (...) {
          message["error"] = {
            {"code", static_cast<int>(mcp::ErrorCode::InternalError)},
            {"message", "Failed to decode tool response"}
          };
        }

        this->sendJsonRpcNotification(sessionId, message);
      };

      if (!this->invokeTool(idString, name, sessionId, argumentsJson, modern, callback)) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Tool not available"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }
      return std::nullopt;
    }

    if (!isNotification) {
      response["error"] = {
        {"code", static_cast<int>(mcp::ErrorCode::MethodNotFound)},
        {"message", "Method not found"}
      };
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    return std::nullopt;
  }

} // namespace oro::runtime::core::services
