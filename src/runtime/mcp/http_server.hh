#ifndef ORO_RUNTIME_MCP_HTTP_SERVER_H
#define ORO_RUNTIME_MCP_HTTP_SERVER_H

#include "../platform/types.hh"
#include "../core.hh"
#include "../bytes.hh"
#include "../crypto.hh"

#include <chrono>
#include <future>
#include <optional>
#include <queue>

#include <cpp-httplib/httplib.h>

namespace oro::runtime::mcp {
  class HTTPServer {
    public:
      struct OAuthConfig {
        bool enabled = false;
        String issuer;
        String authorizePath = "/oauth/authorize";
        String tokenPath = "/oauth/token";
        String metadataPath = "/.well-known/oauth-authorization-server";
        uint32_t codeLifetimeSeconds = 300;
        uint32_t tokenLifetimeSeconds = 3600;
        String defaultClientId;
        String defaultScope;
        String screenHtml;
        String screenFile;
      };

      struct Config {
        String host = "127.0.0.1";
        int port = 8000;
        String endpoint = "/mcp";
        String token;
        uint32_t retryMilliseconds = 5000;
        // How long to retain sessions without an active SSE stream.
        // Helps prevent unbounded growth when clients don't reuse session ids.
        uint32_t sessionTtlSeconds = 600;
        // When enabled, a new GET SSE connection for an existing session id will
        // terminate the previous stream and take over the session.
        bool replaceSseStreamOnReconnect = false;
        OAuthConfig oauth;
      };

      struct Delegate {
        virtual void onSessionStarted(const String& sessionId) = 0;
        virtual void onSessionStopped(const String& sessionId) = 0;
        virtual std::optional<String> onJsonRpcRequest(const String& sessionId, const String& payload) = 0;
        virtual void onPing(const String& sessionId) = 0;
        virtual std::optional<bool> authorize(const httplib::Request& req, httplib::Response& res) {
          (void)req;
          (void)res;
          return std::nullopt;
        }
        virtual ~Delegate() = default;
      };

      HTTPServer();
      ~HTTPServer();

      bool start(const Config& config, Delegate* delegate);
      void stop();
      bool isRunning() const;

      bool sendEvent(const String& sessionId, const String& eventName, const String& data);
      Config getConfig() const;

    private:
      class EventDispatcher {
        public:
          bool wait(httplib::DataSink& sink);
          void send(const String& payload);
          void close();

        private:
          Mutex mutex;
          ConditionVariableAny cond;
          std::queue<String> queue;
          bool closed = false;
      };
      struct Session {
        String id;
        std::shared_ptr<EventDispatcher> dispatcher;
        std::chrono::steady_clock::time_point lastActivity = std::chrono::steady_clock::now();
        Atomic<bool> closed = false;
        Atomic<bool> hasStream = false;
      };

      Config config;
      Delegate* delegate = nullptr;
      std::unique_ptr<httplib::Server> server;
      std::unique_ptr<std::thread> serverThread;
      Map<String, std::shared_ptr<Session>> sessions;
      mutable Mutex mutex;

      struct OAuthAuthorizationCode {
        String code;
        String clientId;
        String redirectUri;
        String scope;
        String state;
        String codeChallenge;
        String codeChallengeMethod;
        std::chrono::steady_clock::time_point expiresAt;
      };

      struct OAuthAccessToken {
        String token;
        String clientId;
        String scope;
        std::chrono::steady_clock::time_point expiresAt;
      };

      mutable Map<String, OAuthAuthorizationCode> oauthAuthorizationCodes;
      mutable Map<String, OAuthAccessToken> oauthAccessTokens;

      void handleMessage(const httplib::Request& req, httplib::Response& res);
      void handleSSE(const httplib::Request& req, httplib::Response& res);
      void handleOAuthAuthorize(const httplib::Request& req, httplib::Response& res);
      void handleOAuthToken(const httplib::Request& req, httplib::Response& res);
      void handleOAuthMetadata(const httplib::Request& req, httplib::Response& res);
      void handlePing(const String& sessionId);
      void closeSession(const String& sessionId);
      void pruneExpiredSessions(std::chrono::steady_clock::time_point now);
      bool authorize(const httplib::Request& req, httplib::Response& res) const;
      bool validateAccessToken(const String& token) const;
      void pruneExpiredOAuthArtifacts(std::chrono::steady_clock::time_point now) const;
      void setCorsHeaders(httplib::Response& res) const;
      String renderAuthorizationPage(const httplib::Request& req,
                                     const OAuthConfig& oauthConfig,
                                     const String& clientId,
                                     const String& redirectUri,
                                     const String& scope,
                                     const String& state,
                                     const String& codeChallenge,
                                     const String& codeChallengeMethod) const;
      bool readScreenTemplate(const String& path, String& out) const;
      std::optional<String> validateSessionId(const httplib::Request& req) const;
      std::optional<String> createSessionForRequest(const String& requestedId = "");
      void attachSessionHeader(httplib::Response& res, const String& sessionId) const;
  };
}

#endif
