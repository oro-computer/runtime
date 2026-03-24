#ifndef ORO_RUNTIME_CORE_SERVICES_CDP_H
#define ORO_RUNTIME_CORE_SERVICES_CDP_H

#include "../../core.hh"
#include "../../http.hh"
#include "../../json.hh"
#include "../../string.hh"
#include "../../uuid.hh"

#include <uv.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace oro::runtime::core::services {
  class CDP : public core::Service {
    public:
      struct ListenOptions {
        String hostname = "127.0.0.1";
        int port = 0; // 0 = random port
      };

      struct Status {
        bool listening = false;
        String hostname;
        int port = 0;
        String browserId;
        String wsEndpoint;
        String httpEndpoint;
      };

      CDP (const Options& options);
      ~CDP () noexcept override;

      bool start () override;
      bool stop () override;

      // IPC surface
      void listen (const String& seq, const ListenOptions& options, const Callback cb);
      void close (const String& seq, const Callback cb);
      void getStatus (const String& seq, const Callback cb);

      // Internal notifications used to emit CDP events.
      void onWindowCreated (int windowIndex);
      void onWindowDestroyed (int windowIndex);
      void onWindowNavigated (int windowIndex, const String& url);
      void onWindowBeforeRuntimeInit (int windowIndex);
      void onBindingCalled (int windowIndex, const String& name, const String& payload, int executionContextId);
      void onWindowReadyStateChanged (int windowIndex, const String& state);
      void onWindowDOMContentLoaded (int windowIndex);
      void onNetworkEvent (int windowIndex, const JSON::Any& event);

      // Native network payload storage for runtime-instrumented requests
      // (e.g., SchemeHandlers). This supports Network.getResponseBody and
      // Network.getRequestPostData without relying on in-page instrumentation.
      void storeNativeNetworkResponseBody (
        const String& requestId,
        const String& body,
        bool base64Encoded,
        size_t bodyBytes
      );
      void storeNativeNetworkRequestPostData (const String& requestId, const String& postData);
      void applyExtraHTTPHeadersForWindow (int windowIndex, http::Headers* headers) const;
      bool shouldBlockURLForWindow (int windowIndex, const String& url) const;

      bool isFetchEnabledForWindow (int windowIndex) const;
      bool shouldPauseFetchRequest (int windowIndex, const String& url, const String& resourceType) const;
      bool registerFetchRequest (
        int windowIndex,
        const String& requestId,
        const Function<void(const JSON::Any& decision)>& resolve
      );

      bool isListening () const;
      Status status () const;

    private:
      struct Client;

      struct TargetInfo {
        String targetId;
        int windowIndex = -1;
        String type = "page";
        String title = "";
        String url = "";
        bool attached = false;
        // For Playwright/Puppeteer compatibility, targets always surface a
        // browserContextId: the default context uses "default-<browserId>".
        // Non-default contexts are created via Target.createBrowserContext.
        String browserContextId = "";
      };

      struct SessionInfo {
        String sessionId;
        String targetId;
        bool flatten = true;
        String parentSessionId;
      };

      struct ScriptToEvaluateOnNewDocument {
        String identifier;
        String source;
        String worldName = "";
      };

      struct AutoAttachOptions {
        bool autoAttach = false;
        bool waitForDebuggerOnStart = false;
        bool flatten = true;
        struct TargetFilterRule {
          bool exclude = false;
          std::optional<String> type;
        };

        Vector<TargetFilterRule> filter;
      };

      struct WriteRequest {
        uv_write_t req;
        std::vector<char> data;
        bool closeAfter = false;
        Function<void(int)> callback = nullptr;
      };

      struct Client {
        enum class Kind { HTTP, BrowserWS, PageWS };

        CDP* cdp = nullptr;
        uint64_t id = 0;
        uv_tcp_t* handle = nullptr;
        Kind kind = Kind::HTTP;
        bool handshakeDone = false;
        bool closing = false;
        bool closed = false;
        bool logEnabled = false;

        // HTTP/WebSocket parsing state
        std::string readBuffer;
        std::string wsMessageBuffer;

        // WebSocket connection state
        bool discoverTargets = false;
        Vector<AutoAttachOptions::TargetFilterRule> discoverFilter;

        // Auto attach configuration is session-scoped in CDP. An empty key
        // denotes the root browser session.
        std::unordered_map<std::string, AutoAttachOptions> autoAttachBySession;

        // When Kind::PageWS, this is the bound targetId
        String boundTargetId;

        // sessionId -> targetId mapping for flattened sessions
        std::unordered_map<std::string, SessionInfo> sessions;
      };

      // lifecycle helpers
      bool ensureListening (const ListenOptions& options);
      void stopListeningOnLoop ();
      Status computeStatus () const;
      void handleWindowBeforeRuntimeInitOnLoop (int windowIndex);
      void handleWindowNavigatedOnLoop (int windowIndex, const String& url);

      // target helpers
      std::vector<TargetInfo> snapshotTargets ();
      TargetInfo getOrCreateTargetForWindow (int windowIndex);
      std::optional<int> resolveWindowIndexForTarget (const String& targetId);

      // HTTP/WebSocket helpers (run on loop thread)
      void onConnection (uv_stream_t* server, int status);
      void onAlloc (uv_handle_t* handle, size_t suggested, uv_buf_t* buf);
      void onRead (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
      void onClientClosed (uv_handle_t* handle);

      void handleHTTP (Client* client);
      bool handleWebSocketHandshake (Client* client, const http::Request& request);
      void writeHTTP (Client* client, const http::Response& response, bool closeAfter);
      void writeHTTPAndClose (Client* client, const http::Response& response);

      void processWebSocketFrames (Client* client);
      void handleWebSocketMessage (Client* client, const std::string& text);
      void sendWebSocketFrame (Client* client, int opcode, const char* data, size_t length, const Function<void(int)>& cb = nullptr);
      void sendWebSocketText (Client* client, const std::string& text, const Function<void(int)>& cb = nullptr);
      void sendWebSocketJSON (Client* client, const JSON::Any& message);
      void sendCDPResult (Client* client, int id, const JSON::Any& result, const String& sessionId = "");
      void sendCDPError (Client* client, int id, int code, const std::string& message, const String& sessionId = "");
      void sendCDPEvent (Client* client, const std::string& method, const JSON::Any& params, const String& sessionId = "");

      bool clientAttachedToTarget (const Client* client, const String& targetId) const;
      static Vector<AutoAttachOptions::TargetFilterRule> parseTargetFilter (const JSON::Any& filter);
      static bool targetMatchesFilter (const Vector<AutoAttachOptions::TargetFilterRule>& filter, const String& type);

      // CDP method handlers
      void handleCDPCommand (Client* client, const JSON::Any& msg);
      void handleTargetSetDiscoverTargets (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetGetTargets (Client* client, int id, const String& sessionId);
      void handleTargetSetAutoAttach (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetAttachToTarget (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetDetachFromTarget (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetSendMessageToTarget (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetGetTargetInfo (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetCreateTarget (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetCloseTarget (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTargetActivateTarget (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleBrowserGetWindowBounds (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageNavigate (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageSetDocumentContent (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageGetFrameTree (Client* client, int id, const String& sessionId);
      void handlePageGetResourceTree (Client* client, int id, const String& sessionId);
      void handlePageGetResourceContent (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageGetLayoutMetrics (Client* client, int id, const String& sessionId);
      void handlePageBringToFront (Client* client, int id, const String& sessionId);
      void handlePageReload (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageStopLoading (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageClose (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageCaptureScreenshot (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageGetNavigationHistory (Client* client, int id, const String& sessionId);
      void handlePageNavigateToHistoryEntry (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageAddScriptToEvaluateOnNewDocument (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handlePageRemoveScriptToEvaluateOnNewDocument (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleRuntimeEvaluate (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleRuntimeCallFunctionOn (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleRuntimeGetProperties (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleRuntimeReleaseObject (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleRuntimeReleaseObjectGroup (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleRuntimeAddBinding (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleRuntimeRemoveBinding (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleBrowserGetVersion (Client* client, int id, const String& sessionId);
      void handleBrowserGetWindowForTarget (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleBrowserSetContentsSize (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleBrowserSetWindowBounds (Client* client, int id, const JSON::Any& params, const String& sessionId);

      void handleNetworkGetResponseBody (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleNetworkGetRequestPostData (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleTakeResponseBodyAsStream (Client* client, int id, const JSON::Any& params, const String& sessionId, const char* requestIdKey);
      void handleIORead (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleIOClose (Client* client, int id, const JSON::Any& params, const String& sessionId);

      void handleDOMGetDocument (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMQuerySelector (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMQuerySelectorAll (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMDescribeNode (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMResolveNode (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMGetContentQuads (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMGetNodeForLocation (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMGetBoxModel (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMGetOuterHTML (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMSetOuterHTML (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMGetAttributes (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMRequestChildNodes (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMSetAttributeValue (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMRemoveAttribute (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMSetNodeValue (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMScrollIntoViewIfNeeded (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMFocus (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMGetFrameOwner (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleDOMSetFileInputFiles (Client* client, int id, const JSON::Any& params, const String& sessionId);

      void handleCSSGetComputedStyleForNode (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleCSSGetInlineStylesForNode (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleCSSGetMatchedStylesForNode (Client* client, int id, const JSON::Any& params, const String& sessionId);

      void handleInputDispatchMouseEvent (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleInputDispatchKeyEvent (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleInputInsertText (Client* client, int id, const JSON::Any& params, const String& sessionId);

      void handleEmulationSetDeviceMetricsOverride (Client* client, int id, const JSON::Any& params, const String& sessionId);
      void handleEmulationClearDeviceMetricsOverride (Client* client, int id, const String& sessionId);

      // event broadcast helpers
      void broadcastToAttachedTarget (
        const String& targetId,
        const Function<void(Client* client, const String& sessionId)>& fn
      );

      // state
      mutable Mutex mutex;
      Atomic<bool> listening = false;
      Atomic<bool> starting = false;
      Atomic<bool> injectedUserConfig = false;
      uv_tcp_t serverSocket {};
      struct sockaddr_in addr {};
      String hostname = "127.0.0.1";
      Atomic<int> port = 0;
      String browserId = uuid::v7();
      String defaultBrowserContextId = "";

      struct NavigationEntry {
        int id = 0;
        String url = "";
        String title = "";
      };

      // windowIndex <-> targetId
      std::unordered_map<int, std::string> windowTargets;
      std::unordered_map<std::string, int> targetWindows;

      // per-window execution contexts
      Atomic<int> nextExecutionContextId = 1;
      std::unordered_map<int, int> windowExecutionContexts;

      // per-window isolated world execution contexts (frameId -> worldName -> executionContextId)
      std::unordered_map<int, std::unordered_map<std::string, std::unordered_map<std::string, int>>>
        windowIsolatedWorldExecutionContexts;

      // per-window readyState tracking
      std::unordered_map<int, std::string> windowReadyState;

      // per-window Animation.setPlaybackRate compatibility state
      std::unordered_map<int, double> windowAnimationPlaybackRate;

      // per-window navigation history
      Atomic<int> nextNavigationEntryId = 1;
      std::unordered_map<int, std::vector<NavigationEntry>> windowNavigationHistory;
      std::unordered_map<int, int> windowNavigationHistoryIndex;
      std::unordered_map<int, std::string> windowPendingNavigationRequestId;
      std::unordered_map<int, std::string> windowLastNavigationRequestId;

      // per-window Network.setExtraHTTPHeaders state (applies to runtime-handled requests)
      std::unordered_map<int, http::Headers> windowExtraHTTPHeaders;

      // per-window Network.setBlockedURLs patterns
      std::unordered_map<int, std::vector<String>> windowBlockedURLPatterns;

      // Network.setRequestInterception compatibility (deprecated in CDP, but used
      // by some automation tooling). When enabled, Fetch.requestPaused events also
      // produce Network.requestIntercepted events for the same requests.
      std::unordered_map<int, bool> windowNetworkRequestInterceptionEnabled;

      // Browser contexts (Target.createBrowserContext) and per-window association.
      // For Playwright/Puppeteer compatibility, targets always include a
      // browserContextId: the default context uses a stable id
      // ("default-<browserId>") that is not returned by Target.getBrowserContexts.
      std::unordered_set<std::string> browserContextIds;
      std::unordered_map<int, std::string> windowBrowserContextId;

      struct FetchPattern {
        std::optional<String> urlPattern;
        std::optional<String> resourceType;
      };

      std::unordered_map<int, bool> windowFetchEnabled;
      std::unordered_map<int, std::vector<FetchPattern>> windowFetchPatterns;

      struct PendingFetchRequest {
        int windowIndex = -1;
        Function<void(const JSON::Any& decision)> resolve = nullptr;
      };

      std::unordered_map<std::string, PendingFetchRequest> pendingFetchRequests;

      struct StoredNetworkPayload {
        String body;
        bool base64Encoded = false;
        size_t bodyBytes = 0;
        String postData;
      };

      // Stored network payloads for native-instrumented requests (scheme handlers).
      std::unordered_map<std::string, StoredNetworkPayload> nativeNetworkPayloads;
      std::deque<std::string> nativeNetworkPayloadOrder;
      size_t nativeNetworkTotalBodyBytes = 0;

      struct IOStream {
        std::string data;
        bool base64Encoded = false;
        size_t offset = 0;
      };

      Atomic<uint64_t> nextIOStreamId = 1;
      std::unordered_map<std::string, IOStream> ioStreams;
      std::deque<std::string> ioStreamOrder;
      size_t ioStreamTotalBytes = 0;

      // per-target scripts/bindings to be installed on new documents
      std::unordered_map<std::string, std::vector<ScriptToEvaluateOnNewDocument>> targetNewDocumentScripts;
      std::unordered_map<std::string, std::unordered_set<std::string>> targetBindings;

      // active clients
      std::unordered_map<uv_stream_t*, Client*> clients;

      // telemetry for missing protocol surface
      std::unordered_set<std::string> loggedUnhandledMethods;
      bool loggedUnhandledMethodsSaturated = false;
  };
}

#endif
