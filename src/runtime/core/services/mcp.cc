#include "mcp.hh"

#include "../../runtime.hh"
#include "../../version.hh"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <future>

namespace oro::runtime::core::services {
  namespace {
    template <typename Counter>
    uint64_t nextIdentifier(Counter& counter) {
      auto value = counter.fetch_add(1, std::memory_order_relaxed);
      if (value == 0) {
        value = counter.fetch_add(1, std::memory_order_relaxed);
      }
      return value;
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
          if (parsed > 0) {
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

    return toolEntry->id;
  }

  bool MCP::unregisterTool(const String& name) {
    bool removed = false;
    {
      Lock lock(this->mutex);
      removed = this->toolRegistry.erase(name) > 0;

      auto it = this->pendingInvocations.begin();
      while (it != this->pendingInvocations.end()) {
        if (it->second && it->second->toolName == name) {
          it = this->pendingInvocations.erase(it);
        } else {
          ++it;
        }
      }
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

    return resourceEntry->id;
  }

  bool MCP::unregisterResource(const String& uri) {
    Lock lock(this->mutex);
    const bool removed = this->resourceRegistry.erase(uri) > 0;
    if (removed) {
      auto it = this->pendingResourceReads.begin();
      while (it != this->pendingResourceReads.end()) {
        if (it->second && it->second->resourceUri == uri) {
          it = this->pendingResourceReads.erase(it);
        } else {
          ++it;
        }
      }
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

    const auto invocationId = this->generateInvocationId();
    auto pending = SharedPointer<PendingInvocation>(new PendingInvocation());
    pending->id = invocationId;
    pending->toolName = name;
    pending->sessionId = sessionId;
    pending->seq = seq;
    pending->reply = reply;

    {
      Lock lock(this->mutex);
      this->pendingInvocations[invocationId] = pending;
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

    JSON::Object::Entries data {
      {"id", invocationId},
      {"result", JSON::String(resultJson)}
    };

    pending->reply(pending->seq, JSON::Object(JSON::Object::Entries {{"data", JSON::Object(data)}}), QueuedResponse{});
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

    JSON::Object::Entries errEntries {
      {"id", invocationId},
      {"error", error.toJSON()}
    };

    pending->reply(pending->seq, JSON::Object(JSON::Object::Entries {{"err", JSON::Object(errEntries)}}), QueuedResponse{});
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
      message["result"] = parsed;
    } catch (const std::exception& err) {
      message["error"] = {
        {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
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

    std::optional<String> onJsonRpcRequest(const String& sessionId, const String& payload) override {
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
        return result->dump();
      }
      return std::nullopt;
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
      this->resourceSubscriptions.clear();
      this->sessionSubscriptions.clear();
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

    nlohmann::json parsed;
    try {
      parsed = resultJson.empty() ? nlohmann::json::object() : nlohmann::json::parse(resultJson);
      if (!parsed.is_object()) {
        nlohmann::json wrapper;
        wrapper["contents"] = parsed;
        parsed = wrapper;
      }
      if (!parsed.contains("contents")) {
        parsed["contents"] = nlohmann::json::array();
      }
    } catch (...) {
      return false;
    }

    Vector<SharedPointer<ResourceSubscription>> targets;
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
    }

    if (targets.empty()) {
      return false;
    }

    for (const auto& subscription : targets) {
      if (!subscription) {
        continue;
      }

      nlohmann::json message;
      message["jsonrpc"] = "2.0";
      message["method"] = "resources/update";

      nlohmann::json params = parsed;
      params["subscription"] = {
        {"id", subscription->id},
        {"resource", subscription->resourceUri}
      };
      params["resource"] = subscription->resourceUri;

      message["params"] = params;
      this->sendJsonRpcNotification(subscription->sessionId, message);
    }

    return true;
  }

  void MCP::removeSessionSubscriptions(const String& sessionId,
                                       const std::optional<String>& reason) {
    Vector<SharedPointer<ResourceSubscription>> removed;

    {
      Lock lock(this->mutex);
      auto it = this->sessionSubscriptions.find(sessionId);
      if (it == this->sessionSubscriptions.end()) {
        return;
      }

      for (const auto& id : it->second) {
        auto subIt = this->resourceSubscriptions.find(id);
        if (subIt != this->resourceSubscriptions.end()) {
          removed.push_back(subIt->second);
          this->resourceSubscriptions.erase(subIt);
        }
      }

      this->sessionSubscriptions.erase(it);
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
  }

  void MCP::sendJsonRpcNotification(const String& sessionId, const nlohmann::json& message) {
    if (!this->server || !this->server->isRunning()) {
      return;
    }

    this->server->sendEvent(sessionId, "message", message.dump());
  }

  std::optional<nlohmann::json> MCP::handleJsonRpcRequest(const String& sessionId, const nlohmann::json& request) {
    if (!request.is_object()) {
      return std::nullopt;
    }

    const auto idIt = request.find("id");
    const bool isNotification = idIt == request.end();
    nlohmann::json response;
    response["jsonrpc"] = "2.0";
    if (idIt != request.end()) {
      response["id"] = *idIt;
    }

    const auto method = request.value("method", "");
    const auto params = request.value("params", nlohmann::json::object());

    bool initialized = false;
    {
      Lock lock(this->mutex);
      const auto it = this->sessionState.find(sessionId);
      if (it != this->sessionState.end()) {
        initialized = it->second;
      }
    }

    if (method == "initialize") {
      const auto requestedVersion = params.value("protocolVersion", "");
      if (requestedVersion != mcp::kProtocolVersion) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Unsupported protocol version"}
        };
      } else {
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

        if (!registeredResources.empty()) {
          nlohmann::json resourceCaps = nlohmann::json::object();
          resourceCaps["subscribe"] = true;
          capabilities["resources"] = resourceCaps;
        }

        if (!registeredTools.empty()) {
          nlohmann::json toolCaps = nlohmann::json::object();
          capabilities["tools"] = toolCaps;
        }

        nlohmann::json resultPayload;
        resultPayload["protocolVersion"] = mcp::kProtocolVersion;
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

    if (method == "ping") {
      response["result"] = nlohmann::json::object();
      this->sendJsonRpcNotification(sessionId, response);
      return response;
    }

    if (method == "notifications/initialized") {
      return std::nullopt;
    }

    if (!initialized && method != "initialize" && method != "ping") {
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
        tools.push_back(nlohmann::json::parse(tool.toJSON().str()));
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

    if (method == "resources/subscribe") {
      if (idIt == request.end()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
          {"message", "Request id required"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      const auto uri = params.value("uri", "");
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
          {"code", static_cast<int>(mcp::ErrorCode::MethodNotFound)},
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

      nlohmann::json resultJson;
      resultJson["subscription"] = {
        {"id", subscriptionId},
        {"resource", uri}
      };

      response["result"] = resultJson;
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

      String subscriptionId;
      if (params.contains("subscription")) {
        const auto& subNode = params["subscription"];
        if (subNode.is_object()) {
          subscriptionId = subNode.value("id", String());
        } else if (subNode.is_string()) {
          subscriptionId = subNode.get<std::string>();
        }
      }
      if (subscriptionId.empty()) {
        subscriptionId = params.value("id", String());
      }

      if (subscriptionId.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Missing subscription id"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      SharedPointer<ResourceSubscription> subscription = nullptr;
      {
        Lock lock(this->mutex);
        auto it = this->resourceSubscriptions.find(subscriptionId);
        if (it == this->resourceSubscriptions.end()) {
          subscription = nullptr;
        } else {
          subscription = it->second;
          if (!subscription || subscription->sessionId != sessionId) {
            response["error"] = {
              {"code", static_cast<int>(mcp::ErrorCode::InvalidRequest)},
              {"message", "Subscription not active on this session"}
            };
            this->sendJsonRpcNotification(sessionId, response);
            return response;
          }
          this->resourceSubscriptions.erase(it);
          auto sessionIt = this->sessionSubscriptions.find(sessionId);
          if (sessionIt != this->sessionSubscriptions.end()) {
            auto& ids = sessionIt->second;
            ids.erase(std::remove(ids.begin(), ids.end(), subscriptionId), ids.end());
            if (ids.empty()) {
              this->sessionSubscriptions.erase(sessionIt);
            }
          }
        }
      }

      if (subscription == nullptr) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::MethodNotFound)},
          {"message", "Subscription not found"}
        };
        this->sendJsonRpcNotification(sessionId, response);
        return response;
      }

      auto resource = this->getResource(subscription->resourceUri);
      if (resource && resource->callback) {
        JSON::Object::Entries dataEntries {
          {"id", JSON::String(subscription->id)},
          {"resource", JSON::String(subscription->resourceUri)},
          {"sessionId", JSON::String(subscription->sessionId)},
          {"descriptor", resource->descriptor.toJSON()}
        };
        const auto paramsDump = params.is_null() ? std::string() : params.dump();
        if (!paramsDump.empty() && paramsDump != "null") {
          dataEntries.insert({"params", JSON::String(paramsDump)});
        }

        resource->callback("-1", JSON::Object(JSON::Object::Entries {
          {"source", "mcp.resource.unsubscribe"},
          {"data", JSON::Object(dataEntries)}
        }), QueuedResponse{});
      }

      nlohmann::json resultJson;
      resultJson["subscription"] = {
        {"id", subscription->id},
        {"resource", subscription->resourceUri}
      };
      response["result"] = resultJson;
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

      const auto uri = params.value("uri", "");
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
          {"code", static_cast<int>(mcp::ErrorCode::MethodNotFound)},
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

      {
        Lock lock(this->mutex);
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
      const auto name = params.value("name", "");
      const auto argumentsJson = params.contains("arguments") ? params["arguments"].dump() : std::string();

      if (name.empty()) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::InvalidParams)},
          {"message", "Missing tool name"}
        };
        this->sendJsonRpcNotification(sessionId, response);
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

      if (!this->invokeTool(idString, name, sessionId, argumentsJson, callback)) {
        response["error"] = {
          {"code", static_cast<int>(mcp::ErrorCode::MethodNotFound)},
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
