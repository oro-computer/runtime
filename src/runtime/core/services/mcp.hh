#ifndef ORO_RUNTIME_CORE_SERVICES_MCP_H
#define ORO_RUNTIME_CORE_SERVICES_MCP_H

#include "../../core.hh"
#include "../../ipc.hh"
#include "../../mcp/protocol.hh"
#include "../../mcp/tool.hh"
#include "../../mcp/resource.hh"
#include "../../mcp/http_server.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <future>
#include <optional>

namespace oro::runtime::core::services {
  class MCP : public core::Service {
    public:
      using ToolId = uint64_t;
      using ResourceId = uint64_t;
      using Callback = core::Service::Callback;

      struct RegisteredTool {
        ToolId id = 0;
        mcp::Tool definition;
        Callback callback = nullptr;
      };

      struct RegisteredResource {
        ResourceId id = 0;
        mcp::ResourceDescriptor descriptor;
        Callback callback = nullptr;
      };

      MCP(const Options& options);
      ~MCP() override;

      bool start() override;
      bool stop() override;

      ToolId registerTool(const mcp::Tool& tool, const Callback& callback);
      bool unregisterTool(const String& name);
      Vector<mcp::Tool> listTools() const;

      ResourceId registerResource(const mcp::ResourceDescriptor& descriptor, const Callback& callback);
      bool unregisterResource(const String& uri);
      Vector<mcp::ResourceDescriptor> listResources() const;

      bool startServer(const mcp::HTTPServer::Config&);
      void stopServer();
      bool serverRunning() const;
      mcp::HTTPServer::Config getServerConfig() const;
      bool publishResourceUpdate(const String& uri,
                                 const String& resultJson,
                                 const std::optional<String>& sessionId,
                                 const std::optional<String>& subscriptionId);
      void setAuthorizationHandler(const Callback& callback);
      void clearAuthorizationHandler();
      bool resolveAuthorization(const String& id,
                                bool allow,
                                const std::optional<int>& statusOverride,
                                const std::optional<String>& messageOverride);

      bool invokeTool(const String& seq,
                      const String& name,
                      const String& sessionId,
                      const String& argumentsJson,
                      bool modern,
                      const Callback& reply);

      bool resolveInvocation(const String& invocationId, const String& resultJson);
      bool rejectInvocation(const String& invocationId, const mcp::Error& error);
      bool resolveResourceRead(const String& readId, const String& resultJson);
      bool rejectResourceRead(const String& readId, const mcp::Error& error);

    private:
      mutable Mutex mutex;
      Map<String, SharedPointer<RegisteredTool>> toolRegistry;
      Map<String, SharedPointer<RegisteredResource>> resourceRegistry;
      std::atomic<ToolId> toolCounter {1};
      std::atomic<ResourceId> resourceCounter {1};
      std::atomic<uint64_t> invocationCounter {1};
      std::unique_ptr<mcp::HTTPServer> server;
      mcp::HTTPServer::Config serverConfig;
      bool serverActive = false;
      struct ServerAdapter;
      std::unique_ptr<ServerAdapter> serverAdapter;
      Map<String, bool> sessionState;

      struct PendingInvocation {
        String id;
        String toolName;
        String sessionId;
        String seq;
        mcp::Tool definition;
        bool modern = false;
        Callback reply = nullptr;
      };

      Map<String, SharedPointer<PendingInvocation>> pendingInvocations;

      struct PendingResourceRead {
        String id;
        String resourceUri;
        String sessionId;
        String seq;
        Callback reply = nullptr;
        nlohmann::json requestId = nullptr;
        bool modern = false;
      };

      Map<String, SharedPointer<PendingResourceRead>> pendingResourceReads;

      struct ResourceSubscription {
        String id;
        String resourceUri;
        String sessionId;
      };

      Map<String, SharedPointer<ResourceSubscription>> resourceSubscriptions;
      Map<String, Vector<String>> sessionSubscriptions;

      struct ModernSubscription {
        nlohmann::json requestId = nullptr;
        nlohmann::json accepted = nlohmann::json::object();
        Vector<String> resourceUris;
        bool toolsListChanged = false;
        bool resourcesListChanged = false;
        bool started = false;
      };

      Map<String, SharedPointer<ModernSubscription>> modernSubscriptions;
      struct AuthorizationDecision {
        bool allow = false;
        int status = 401;
        String message = "Unauthorized";
      };

      struct PendingAuthorization {
        std::promise<AuthorizationDecision> promise;
        bool completed = false;
      };

      Callback authCallback = nullptr;
      std::atomic<uint64_t> authCounter {1};
      Map<String, SharedPointer<PendingAuthorization>> pendingAuthorizations;
      uint32_t authorizationTimeoutMs = 5000;
      String staticAuthToken;

      SharedPointer<RegisteredTool> getTool(const String& name) const;
      SharedPointer<RegisteredResource> getResource(const String& uri) const;
      String generateInvocationId();
      bool sendJsonRpcNotification(const String& sessionId, const nlohmann::json& message);
      void notifyModernListChange(const String& filter, const String& method);
      void notifyModernSubscriptionStarted(const String& sessionId);
      std::optional<nlohmann::json> handleJsonRpcRequest(const String& sessionId, const nlohmann::json& request);
      void removeSessionSubscriptions(const String& sessionId,
                                      const std::optional<String>& reason = std::nullopt);
      std::optional<bool> authorizeRequest(const httplib::Request& req, httplib::Response& res);
      void rejectAllPendingAuthorizations();
    };
}

#endif
