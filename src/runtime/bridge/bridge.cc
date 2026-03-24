#include "../serviceworker.hh"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include "../filesystem.hh"
#include "../javascript.hh"
#include "../runtime.hh"
#include "../version.hh"
#include "../webview.hh"
#include "../string.hh"
#include "../window.hh"
#include "../env.hh"
#include "../ipc.hh"
#include "../url.hh"
#include "../app.hh"
#include "../http.hh"
#include "../bytes.hh"

#include "../bridge.hh"

using oro::runtime::javascript::getResolveToRenderProcessJavaScript;
using oro::runtime::javascript::getEmitToRenderProcessJavaScript;
using oro::runtime::url::encodeURIComponent;
using oro::runtime::webview::SchemeHandlers;

using oro::runtime::config::isDebugEnabled;
using oro::runtime::config::getUserConfig;

using oro::runtime::string::parseStringList;
using oro::runtime::string::toLowerCase;
using oro::runtime::string::replace;
using oro::runtime::string::split;
using oro::runtime::string::tmpl;
using oro::runtime::string::trim;
using oro::runtime::string::join;

using oro::runtime::crypto::rand64;

using oro::runtime::app::App;

namespace oro::runtime::bridge {
  // The `ESM_IMPORT_PROXY_TEMPLATE` is used to provide an ESM module as
  // a proxy to a canonical URL for a module import.
  static constexpr auto ESM_IMPORT_PROXY_TEMPLATE_WITH_DEFAULT_EXPORT = R"S(
/**
 * This module exists to provide a proxy to a canonical URL for a module
 * so `{{protocol}}:{{specifier}}` and `{{protocol}}://{bundle_identifier}/legacy-runtime/{{pathname}}`
 * resolve to the exact same module instance.
 * @see {@link https://github.com/oro-computer/legacy-runtime/blob/{{commit}}/api{{pathname}}}
 */
import module from '{{url}}'
export * from '{{url}}'
export default module
)S";

  static constexpr auto ESM_IMPORT_PROXY_TEMPLATE_WITHOUT_DEFAULT_EXPORT = R"S(
/**
 * This module exists to provide a proxy to a canonical URL for a module
 * so `{{protocol}}:{{specifier}}` and `{{protocol}}://{bundle_identifier}/legacy-runtime/{{pathname}}`
 * resolve to the exact same module instance.
 * @see {@link https://github.com/oro-computer/legacy-runtime/blob/{{commit}}/api{{pathname}}}
 */
export * from '{{url}}'
)S";

  static const Vector<String> allowedNodeCoreModules = {
    "async_hooks",
    "assert",
    "buffer",
    "console",
    "constants",
    "child_process",
    "crypto",
    "dgram",
    "dns",
    "dns/promises",
    "events",
    "fs",
    "fs/constants",
    "fs/promises",
    "http",
    "https",
    "ip",
    "module",
    "net",
    "os",
    "os/constants",
    "path",
    "path/posix",
    "path/win32",
    "perf_hooks",
    "process",
    "querystring",
    "stream",
    "stream/web",
    "string_decoder",
    "sys",
    "test",
    "timers",
    "timers/promises",
    "tty",
    "url",
    "util",
    "vm",
    "worker_threads"
  };

  Bridge::Bridge (
    const Options& options
  ) : window::IBridge(options.dispatcher, options.context, options.client, options.userConfig),
      schemeHandlers(*this),
      navigator(*this),
      router(*this) {
    // '-1' may mean the bridge is running as a WebKit web process extension
    if (options.client.index >= 0) {
      const auto windowClientConfigKey = String("window.") + std::to_string(options.client.index) + ".client";

      // handle user defined window client id
      if (this->userConfig[windowClientConfigKey + ".id"].size() > 0) {
        try {
          this->client.id = std::stoull(this->userConfig[windowClientConfigKey + ".id"]);
        } catch (...) {
          debug("Invalid window client ID given in '[window.%d.client] id'", options.client.index);
        }
      }
    }

    // Bluetooth removed; no-op handlers previously attached here.
  }

  Bridge::~Bridge () {
    auto app = App::sharedApplication();
    if (app) {
      auto &svc = app->runtime.services.bluetooth;
      {
        Lock lock(this->bluetoothMutex);
        for (auto& entry : this->bluetoothSubscriptions) {
          svc.removeCharacteristicObserver(entry.second.observerId);
        }
        this->bluetoothSubscriptions.clear();
      }
    }
    if (app && !app->shouldExit) {
      // remove observers
      app->runtime.services.geolocation.removePermissionChangeObserver(this->geolocationPermissionChangeObserver);
      app->runtime.services.networkStatus.removeObserver(this->networkStatusObserver);
      app->runtime.services.notifications.removePermissionChangeObserver(this->notificationsPermissionChangeObserver);
      app->runtime.services.notifications.removeNotificationResponseObserver(this->notificationResponseObserver);
      app->runtime.services.notifications.removeNotificationPresentedObserver(this->notificationPresentedObserver);
    }
  }

  void Bridge::init () {
    auto& runtime = static_cast<runtime::Runtime&>(this->context);

    runtime.services.networkStatus.addObserver(this->networkStatusObserver, [this](auto json) {
      if (json.has("name")) {
        this->emit(json["name"].str(), json.str());
      }
    });

    runtime.services.networkStatus.start();

    runtime.services.geolocation.addPermissionChangeObserver(this->geolocationPermissionChangeObserver, [this] (auto json) {
      JSON::Object event = JSON::Object::Entries {
        {"name", "geolocation"},
        {"state", json["state"]}
      };
      this->emit("permissionchange", event.str());
    });

    // on Linux, much of the Notification API is supported so these observers
    // below are not needed as those events already occur in the webview
    // we are patching for the other platforms
  #if !ORO_RUNTIME_PLATFORM_LINUX
    runtime.services.notifications.addPermissionChangeObserver(this->notificationsPermissionChangeObserver, [this](auto json) {
      JSON::Object event = JSON::Object::Entries {
        {"name", "notifications"},
        {"state", json["state"]}
      };
      this->emit("permissionchange", event.str());
    });

    if (this->userConfig["permissions_allow_notifications"] != "false") {
      runtime.services.notifications.addNotificationResponseObserver(this->notificationResponseObserver, [this](auto json) {
        this->emit("notificationresponse", json.str());
      });

      runtime.services.notifications.addNotificationPresentedObserver(this->notificationPresentedObserver, [this](auto json) {
        this->emit("notificationpresented", json.str());
      });
    }
  #endif
    this->router.init();
    this->navigator.init();
    this->schemeHandlers.init();
  }

  void Bridge::configureWebView (webview::WebView* webview) {
    this->navigator.configureWebView(webview);
  }

  bool Bridge::evaluateJavaScript (const String& source) {
    if (this->evaluateJavaScriptHandler != nullptr) {
      this->evaluateJavaScriptHandler(source);
      return true;
    }

    return false;
  }

  bool Bridge::dispatch (const context::DispatchCallback callback) {
    if (this->dispatchHandler != nullptr) {
      this->dispatchHandler(callback);
      return true;
    }
    return static_cast<Runtime&>(this->context).dispatch(callback);
  }

  bool Bridge::navigate (const String& url) {
    if (this->navigateHandler != nullptr) {
      this->navigateHandler(url);
      return true;
    }

    return false;
  }

  bool Bridge::route (const String& uri, SharedPointer<unsigned char[]> bytes, size_t size) {
    return this->route(uri, bytes, size, nullptr);
  }

  bool Bridge::route (
    const String& uri,
    SharedPointer<unsigned char[]> bytes,
    size_t size,
    ipc::Router::ResultCallback callback
  ) {
    if (callback != nullptr) {
      return this->router.invoke(uri, bytes, size, callback);
    } else {
      return this->router.invoke(uri, bytes, size);
    }
  }

  bool Bridge::route (const String& uri, const bytes::Buffer& buffer) {
    return this->route(uri, buffer, nullptr);
  }

  bool Bridge::route (const String& uri, const bytes::Buffer& buffer, const ipc::Router::ResultCallback callback) {
    return this->route(
      uri,
      buffer.shared(),
      buffer.size(),
      callback
    );
  }

  bool Bridge::send (
    const ipc::Message::Seq& seq,
    const String& data,
    const QueuedResponse& queuedResponse
  ) {
    if (queuedResponse.body != nullptr || seq == "-1") {
      const auto script = this->context.createQueuedResponse(seq, data, queuedResponse);
      return this->evaluateJavaScript(script);
    }

    const auto value = encodeURIComponent(data);
    const auto script = getResolveToRenderProcessJavaScript(
      seq.size() == 0 ? "-1" : seq,
      "0",
      value
    );

    return this->evaluateJavaScript(script);
  }

  bool Bridge::send (const ipc::Message::Seq& seq, const JSON::Any& json, const QueuedResponse& queuedResponse) {
    return this->send(seq, json.str(), queuedResponse);
  }

  bool Bridge::emit (const String& name, const String& data) {
    const auto value = encodeURIComponent(data);
    const auto script = getEmitToRenderProcessJavaScript(name, value);
    return this->evaluateJavaScript(script);
  }

  bool Bridge::emit (const String& name, const JSON::Any& json) {
    return this->emit(name, json.str());
  }

  void Bridge::configureSchemeHandlers (
    const SchemeHandlers::Configuration& configuration
  ) {
    this->schemeHandlers.configure(configuration);
    this->schemeHandlers.registerSchemeHandler("ipc", [this](
      const auto request,
      const auto& bridge,
      auto callbacks,
      auto callback
    ) {
      auto message = ipc::Message(request->url());

      if (request->method == "OPTIONS") {
        auto response = SchemeHandlers::Response(request, 204);
        return callback(response);
      }

      message.isHTTP = true;
      message.cancel = std::make_shared<ipc::MessageCancellation>();

      callbacks->cancel = [message] () {
        if (message.cancel->handler != nullptr) {
          message.cancel->handler(message.cancel->data);
        }
      };

      const auto size = request->body.size();
      const auto invoked = this->router.invoke(message, request->body.shared(), size, [=](ipc::Result result) {
        if (!request->isActive()) {
          return;
        }

        auto response = new SchemeHandlers::Response(request);

        response->setHeaders(result.headers);

        // handle event source streams
        if (result.queuedResponse.eventStreamCallback != nullptr) {
          response->setHeader("content-type", "text/event-stream; charset=utf-8");
          response->setHeader("cache-control", "no-store");
          // Coalesce small SSE events to reduce per-write overhead
          auto sseBuffer = std::make_shared<String>();
          static constexpr size_t kFlushThreshold = 4096; // 4KB
          *result.queuedResponse.eventStreamCallback = [request, response, message, callback, sseBuffer](
            const char* name,
            const unsigned char* data,
            bool finished
          ) mutable {
            if (response == nullptr) {
              return false;
            }

            if (request->isCancelled()) {
              if (message.cancel->handler != nullptr) {
                message.cancel->handler(message.cancel->data);
              }
              // finalize and cleanup response on cancellation
              if (response != nullptr) {
                callback(*response);
                delete response;
                response = nullptr;
              }
              return false;
            }

            response->writeHead(200);

            const auto event = SchemeHandlers::Response::Event { name, reinterpret_cast<const char*>(data) };

            if (event.count() > 0) {
              // Accumulate into a small buffer, then flush when threshold reached
              sseBuffer->append(event.str());
              if (sseBuffer->size() >= kFlushThreshold) {
                response->write(*sseBuffer);
                sseBuffer->clear();
              }
            }

            if (finished) {
              if (!sseBuffer->empty()) {
                response->write(*sseBuffer);
                sseBuffer->clear();
              }
              callback(*response);
              delete response;
              response = nullptr;
            }

            return true;
          };
          return;
        }

        // handle chunk streams
        if (result.queuedResponse.chunkStreamCallback != nullptr) {
          response->setHeader("transfer-encoding", "chunked");
          // Coalesce small chunks into a larger buffer to reduce write calls
          auto chunkBuffer = std::make_shared<Vector<unsigned char>>();
          static constexpr size_t kChunkFlushThreshold = 16 * 1024; // 16KB
          *result.queuedResponse.chunkStreamCallback = [request, response, message, callback, chunkBuffer](
            const unsigned char* chunk,
            size_t size,
            bool finished
          ) mutable {
            if (response == nullptr) {
              return false;
            }

            if (request->isCancelled()) {
              if (message.cancel->handler != nullptr) {
                message.cancel->handler(message.cancel->data);
              }
              // finalize and cleanup response on cancellation
              if (response != nullptr) {
                callback(*response);
                delete response;
                response = nullptr;
              }
              return false;
            }

            response->writeHead(200);
            if (chunk && size > 0) {
              const auto start = chunkBuffer->size();
              chunkBuffer->resize(start + size);
              memcpy(chunkBuffer->data() + start, chunk, size);
              if (chunkBuffer->size() >= kChunkFlushThreshold) {
                response->write(chunkBuffer->size(), chunkBuffer->data());
                chunkBuffer->clear();
              }
            }

            if (finished) {
              if (!chunkBuffer->empty()) {
                response->write(chunkBuffer->size(), chunkBuffer->data());
                chunkBuffer->clear();
              }
              callback(*response);
              delete response;
              response = nullptr;
            }

            return true;
          };
          return;
        }

        if (result.queuedResponse.body != nullptr) {
          response->write(result.queuedResponse.length, result.queuedResponse.body);
        } else {
          response->write(result.json());
        }

        callback(*response);
        delete response;
        response = nullptr;
      });

      if (!invoked) {
        auto response = SchemeHandlers::Response(request, 404);
        response.send(JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Not found"},
            {"type", "NotFoundError"},
            {"url", request->url()}
          }}
        });

        return callback(response);
      }
    });

    const auto runtimeSchemeHandler = [this](
      const auto request,
      const auto& bridge,
      auto callbacks,
      auto callback
    ) {
      auto app = App::sharedApplication();
      auto globalConfig = getUserConfig();
      auto userConfig = this->userConfig;
      auto bundleIdentifier = userConfig["meta_bundle_identifier"];
      auto globalBundleIdentifier = globalConfig["meta_bundle_identifier"];
      auto window = app->runtime.windowManager.getWindowForBridge(&bridge);

      // if there was no window, then this is a bad request as scheme
      // handlers should only be handled directly in a window with
      // a navigator and a connected IPC bridge
      if (window == nullptr) {
        auto response = SchemeHandlers::Response(request);
        response.writeHead(400);
        callback(response);
        return;
      }

      if (request->method == "OPTIONS") {
        auto response = SchemeHandlers::Response(request);
        response.writeHead(204);
        callback(response);
        return;
      }

      const auto shouldUseAppResourcesDirectory = (
        request->hostname.size() > 0 &&
        window->getOptions().resourcesDirectory.size() > 0 &&
        !request->pathname.starts_with("/oro") &&
        !request->pathname.starts_with("/api")
      );

      // the location of static application resources
      const auto applicationResources = shouldUseAppResourcesDirectory
        ? fs::absolute(window->getOptions().resourcesDirectory).string()
        : filesystem::Resource::getResourcesPath().string();

      // default response is 404
      auto response = SchemeHandlers::Response(request, 404);

      // the resouce path that may be request
      String resourcePath;

      // the content location relative to the request origin
      String contentLocation;

      // application resource or service worker request at `oro://<bundle_identifier>/*`
      if (request->hostname.size() > 0) {
        auto origin = webview::Origin(request->scheme + "://" + request->hostname);
        auto serviceWorker = app->runtime.serviceWorkerManager.get(origin.name());

        if (!serviceWorker) {
          serviceWorker = this->navigator.serviceWorkerServer;
        }

        auto handleLlamaServer = [&]() -> bool {
          auto& aiService = app->runtime.services.ai;
          if (!aiService.enabled.load(std::memory_order_relaxed)) {
            return false;
          }

          String routePrefix = aiService.serverOptions.routePrefix.size() > 0
            ? aiService.serverOptions.routePrefix
            : String("/ai/llama");

          if (routePrefix.front() != '/') {
            routePrefix.insert(routePrefix.begin(), '/');
          }

          if (routePrefix.size() > 1 && routePrefix.back() == '/') {
            routePrefix.pop_back();
          }

          if (request->pathname.rfind(routePrefix, 0) != 0) {
            return false;
          }

          if (request->pathname.size() > routePrefix.size()) {
            const char next = request->pathname[routePrefix.size()];
            if (next != '/') {
              return false;
            }
          }

          // Construct request forwarded to the Llama server
          serviceworker::Request aiReq;
          aiReq.method = request->method;
          aiReq.scheme = request->scheme;
          aiReq.url.scheme = request->scheme;
          aiReq.url.hostname = request->hostname;
          aiReq.url.pathname = request->pathname;
          aiReq.url.searchParams.set(request->query);
          aiReq.url.search = request->query;
          aiReq.headers = request->headers;
          aiReq.body = request->body;
          aiReq.client = request->client;

          const auto acceptRaw = aiReq.headers.has("accept")
            ? toLowerCase(trim(aiReq.headers.get("accept").value.str()))
            : String("");
          const auto contentTypeRaw = aiReq.headers.has("content-type")
            ? toLowerCase(trim(aiReq.headers.get("content-type").value.str()))
            : String("");

          bool wantsStream = false;
          const auto streamParam = toLowerCase(aiReq.url.searchParams.get("stream").str());
          if (streamParam == "true" || streamParam == "1" || streamParam == "yes") {
            wantsStream = true;
          }

          if (!wantsStream && acceptRaw.find("text/event-stream") != String::npos) {
            wantsStream = true;
          }

          nlohmann::json bodyJson;
          bool bodyParsed = false;
          constexpr size_t kMaxBodyInspectBytes = 1024 * 1024; // 1 MiB
          if (!wantsStream && aiReq.body.size() > 0 && aiReq.body.size() <= kMaxBodyInspectBytes) {
            if (contentTypeRaw.find("json") != String::npos) {
              try {
                bodyJson = nlohmann::json::parse(aiReq.body.str());
                bodyParsed = true;
              } catch (...) {
                bodyParsed = false;
              }
            }
          }

          if (!wantsStream && bodyParsed && bodyJson.contains("stream")) {
            try {
              if (bodyJson["stream"].is_boolean()) {
                wantsStream = bodyJson["stream"].get<bool>();
              } else if (bodyJson["stream"].is_string()) {
                const auto v = toLowerCase(bodyJson["stream"].get<std::string>());
                wantsStream = (v == "1" || v == "true" || v == "yes");
              }
            } catch (...) {}
          }

          String suffix = request->pathname.size() > routePrefix.size()
            ? request->pathname.substr(routePrefix.size())
            : String("");
          if (suffix.size() == 0) {
            suffix = String("/");
          }
          if (suffix.front() != '/') {
            suffix.insert(suffix.begin(), '/');
          }

          auto stats = aiService.getServerStats(origin.name(), routePrefix);
          auto windowStats = aiService.getServerStatsForWindow(origin.name(), routePrefix, request->client.id);

          bool inflightIncremented = false;
          auto releaseInflight = [&]() {
            if (inflightIncremented) {
              stats->inflight.fetch_sub(1, std::memory_order_acq_rel);
              windowStats->inflight.fetch_sub(1, std::memory_order_acq_rel);
              inflightIncremented = false;
            }
          };

          const int maxConcurrent = std::max(1, aiService.serverOptions.maxConcurrent);
          const int inflightNow = stats->inflight.fetch_add(1, std::memory_order_acq_rel) + 1;
          windowStats->inflight.fetch_add(1, std::memory_order_acq_rel);
          inflightIncremented = true;

          auto finishError = [&](int status, const char* type, const char* message) {
            http::Headers errHeaders;
            errHeaders.set("content-type", "application/json; charset=utf-8");
            errHeaders.set("runtime-preload-injection", "disabled");
            response.writeHead(status, errHeaders);
            response.write(String("{\"error\":{\"type\":\"") + type + "\",\"message\":\"" + message + "\"}}");
            response.finish();
            callback(response);
          };

          if (inflightNow > maxConcurrent) {
            stats->errors.fetch_add(1, std::memory_order_relaxed);
            windowStats->errors.fetch_add(1, std::memory_order_relaxed);
            releaseInflight();
            finishError(429, "rate_limit_error", "Server is busy");
            return true;
          }

          if (!stats->tryConsumeToken(aiService.serverOptions.rateRPS, aiService.serverOptions.rateBurst)) {
            stats->rateLimited.fetch_add(1, std::memory_order_relaxed);
            windowStats->rateLimited.fetch_add(1, std::memory_order_relaxed);
            releaseInflight();
            finishError(429, "rate_limit_error", "Rate limit exceeded");
            return true;
          }

          const bool isChatEndpoint = (
            suffix == "/v1/chat/completions" ||
            suffix == "/chat/completions" ||
            suffix == "/api/chat"
          );
          const bool isCompletionEndpoint = (
            suffix == "/v1/completions" ||
            suffix == "/completions" ||
            suffix == "/completion"
          );
          const bool isEmbeddingsEndpoint = (
            suffix == "/v1/embeddings" ||
            suffix == "/embeddings" ||
            suffix == "/embedding"
          );

          const bool supportsStream = isChatEndpoint || isCompletionEndpoint;
          const bool doStream = wantsStream && supportsStream;

          using AIServerStats = oro::runtime::core::services::AI::ServerStats;

          auto bumpCounter = [&](std::atomic<uint64_t> AIServerStats::*member) {
            (stats.get()->*member).fetch_add(1, std::memory_order_relaxed);
            (windowStats.get()->*member).fetch_add(1, std::memory_order_relaxed);
          };
          auto bumpLatency = [&](std::atomic<long long> AIServerStats::*sum,
                                 std::atomic<long long> AIServerStats::*count,
                                 long long value) {
            (stats.get()->*sum).fetch_add(value, std::memory_order_relaxed);
            (stats.get()->*count).fetch_add(1, std::memory_order_relaxed);
            (windowStats.get()->*sum).fetch_add(value, std::memory_order_relaxed);
            (windowStats.get()->*count).fetch_add(1, std::memory_order_relaxed);
          };

          auto markError = [&]() {
            stats->errors.fetch_add(1, std::memory_order_relaxed);
            windowStats->errors.fetch_add(1, std::memory_order_relaxed);
          };

          const auto start = std::chrono::steady_clock::now();

          if (doStream && isChatEndpoint) {
            http::Headers streamHeaders;
            streamHeaders.set("content-type", "text/event-stream; charset=utf-8");
            streamHeaders.set("cache-control", "no-cache");
            streamHeaders.set("connection", "keep-alive");
            streamHeaders.set("transfer-encoding", "chunked");
            streamHeaders.set("runtime-preload-injection", "disabled");
            response.writeHead(200, streamHeaders);

            auto emit = [&, finishedCalled = false](const String& event, const String& data, bool finished) mutable -> bool {
              String chunk;
              if (event.size() > 0 && event != "message") {
                chunk += "event: " + event + "\n";
              }
              auto lines = split(data, "\n");
              if (lines.empty()) {
                chunk += "data:\n";
              } else {
                for (const auto& line : lines) {
                  chunk += "data: " + line + "\n";
                }
              }
              chunk += "\n";
              if (!response.write(chunk)) {
                return false;
              }
              if (finished && !finishedCalled) {
                response.finish();
                finishedCalled = true;
              }
              return true;
            };

            const bool ok = aiService.server.streamChatCompletionsV1(aiReq, aiService.llm.manager, emit);
            releaseInflight();

            const auto end = std::chrono::steady_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bumpCounter(&AIServerStats::reqChatStream);
            bumpLatency(&AIServerStats::latChatStreamSum, &AIServerStats::latChatStreamCount, duration);

            if (!ok) {
              markError();
            }

            callback(response);
            return true;
          }

          if (doStream && isCompletionEndpoint) {
            http::Headers streamHeaders;
            streamHeaders.set("content-type", "text/event-stream; charset=utf-8");
            streamHeaders.set("cache-control", "no-cache");
            streamHeaders.set("connection", "keep-alive");
            streamHeaders.set("transfer-encoding", "chunked");
            streamHeaders.set("runtime-preload-injection", "disabled");
            response.writeHead(200, streamHeaders);

            auto emit = [&, finishedCalled = false](const String& event, const String& data, bool finished) mutable -> bool {
              String chunk;
              if (event.size() > 0 && event != "message") {
                chunk += "event: " + event + "\n";
              }
              auto lines = split(data, "\n");
              if (lines.empty()) {
                chunk += "data:\n";
              } else {
                for (const auto& line : lines) {
                  chunk += "data: " + line + "\n";
                }
              }
              chunk += "\n";
              if (!response.write(chunk)) {
                return false;
              }
              if (finished && !finishedCalled) {
                response.finish();
                finishedCalled = true;
              }
              return true;
            };

            const bool ok = aiService.server.streamCompletionsV1(aiReq, aiService.llm.manager, emit);
            releaseInflight();

            const auto end = std::chrono::steady_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bumpCounter(&AIServerStats::reqTextStream);
            bumpLatency(&AIServerStats::latTextStreamSum, &AIServerStats::latTextStreamCount, duration);

            if (!ok) {
              markError();
            }

            callback(response);
            return true;
          }

          int statusCode = 200;
          http::Headers headersOut;
          bytes::Buffer bodyOut;

          bool handled = aiService.server.handle(aiReq, statusCode, headersOut, bodyOut, aiService.llm.manager);

          releaseInflight();

          const auto end = std::chrono::steady_clock::now();
          const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

          if (!handled) {
            markError();
            finishError(404, "not_found_error", "Not Found");
            return true;
          }

          if (!headersOut.has("runtime-preload-injection")) {
            headersOut.set("runtime-preload-injection", "disabled");
          }

          response.writeHead(statusCode, headersOut);
          if (bodyOut.size() > 0) {
            response.write(bodyOut);
          }
          response.finish();

          if (isChatEndpoint) {
            bumpCounter(&AIServerStats::reqChatNonStream);
            bumpLatency(&AIServerStats::latChatNonStreamSum, &AIServerStats::latChatNonStreamCount, duration);
          } else if (isCompletionEndpoint) {
            bumpCounter(&AIServerStats::reqTextNonStream);
            bumpLatency(&AIServerStats::latTextNonStreamSum, &AIServerStats::latTextNonStreamCount, duration);
          } else if (isEmbeddingsEndpoint) {
            bumpCounter(&AIServerStats::reqEmbeddings);
            bumpLatency(&AIServerStats::latEmbeddingsSum, &AIServerStats::latEmbeddingsCount, duration);
          }

          if (statusCode >= 400) {
            if (statusCode == 413) {
              stats->tooLarge.fetch_add(1, std::memory_order_relaxed);
              windowStats->tooLarge.fetch_add(1, std::memory_order_relaxed);
            } else if (statusCode == 408) {
              stats->timeouts.fetch_add(1, std::memory_order_relaxed);
              windowStats->timeouts.fetch_add(1, std::memory_order_relaxed);
            } else {
              markError();
            }
          }

          callback(response);
          return true;
        };

        if (handleLlamaServer()) {
          return;
        }

        if (
          serviceWorker != nullptr &&
          request->hostname != globalBundleIdentifier &&
          window->getOptions().shouldPreferServiceWorker &&
          serviceWorker->container.registrations.size() > 0
        ) {
          auto fetch = serviceworker::Request();
          fetch.method = request->method;
          fetch.scheme = request->scheme;
          fetch.url.scheme = request->scheme;
          fetch.url.hostname = request->hostname;
          fetch.url.pathname = request->pathname;
          fetch.url.searchParams.set(request->query);
          fetch.url.search = request->query;
          fetch.headers = request->headers;
          fetch.body = request->body;
          fetch.client = request->client;

          if (!fetch.headers.has("origin")) {
            fetch.headers.set("origin", this->navigator.location.origin);
          }

          const auto app = App::sharedApplication();
          const auto options = serviceworker::Fetch::Options { request->client };
          const auto fetched = serviceWorker->fetch(fetch, options, [=, this] (auto res) mutable {
            if (!request ||  !request->isActive()) {
              return;
            }

            auto response = SchemeHandlers::Response(request, 404);

            if (res.statusCode == 0) {
              response.fail("ServiceWorker request failed");
            } else if (res.statusCode != 404) {
              response.writeHead(res.statusCode, res.headers);
              response.write(res.body.buffer);
            } else {
              const auto resolved = this->navigator.location.resolve(request->pathname, applicationResources);

              if (resolved.redirect) {
                if (request->method == "GET") {
                  auto location = resolved.pathname;
                  if (request->query.size() > 0) {
                    location += "?" + request->query;
                  }

                  if (request->fragment.size() > 0) {
                    location += "#" + request->fragment;
                  }

                  response.redirect(location);
                  return callback(response);
                }
              } else if (resolved.isResource()) {
                resourcePath = applicationResources + resolved.pathname;
              } else if (resolved.isMount()) {
                resourcePath = resolved.mount.filename;
              } else {
                // SPA-style fallback: prefer explicit default_index; otherwise,
                // if allow_any_route is enabled, fall back to "/index.html".
                String fallback = "";
                if (userConfig.contains("webview_default_index")) {
                  fallback = userConfig["webview_default_index"];
                } else {
                  auto it = userConfig.find("webview_allow_any_route");
                  if (it != userConfig.end()) {
                    const auto v = toLowerCase(trim(it->second));
                    const bool allow = (v == "true" || v == "1" || v == "on" || v == "yes");
                    if (allow) fallback = "/index.html";
                  }
                }

                if (fallback.size() > 0) {
                  resourcePath = fallback;
                  if (resourcePath.starts_with("./")) {
                    resourcePath = applicationResources + resourcePath.substr(1);
                  } else if (resourcePath.starts_with("/")) {
                    resourcePath = applicationResources + resourcePath;
                  } else {
                    resourcePath = applicationResources + "/" + resourcePath;
                  }
                }
              }

              if (resourcePath.size() == 0 && resolved.pathname.size() > 0) {
                resourcePath = applicationResources + resolved.pathname;
              }

              // handle HEAD and GET requests for a file resource
              if (resourcePath.size() > 0) {
                if (resourcePath.starts_with(applicationResources)) {
                  contentLocation = resourcePath.substr(applicationResources.size(), resourcePath.size());
                }

                auto resource = filesystem::Resource(resourcePath);
                bool resourceExists = resource.exists();
                bool resourceIsDirectory = resourceExists && filesystem::Resource::isDirectory(resourcePath);
                const bool requestLooksLikeDirectory = request->pathname.size() == 0 || request->pathname.ends_with("/");
                const bool autoIndexEnabled = [&]() {
                  auto it = userConfig.find("webview_autoindex");
                  if (it == userConfig.end()) return false;
                  const auto v = toLowerCase(trim(it->second));
                  return (v == "true" || v == "1" || v == "on" || v == "yes");
                }();
                const bool requestHasExtension = [&]() {
                  if (request->pathname.size() == 0) return false;
                  if (request->pathname.ends_with("/")) return false;
                  try {
                    return fs::path(request->pathname).has_extension();
                  } catch (...) {
                    return false;
                  }
                }();
                bool recoveredDirectoryFromMissingHtml = false;
                const bool requestTargetsRoot = (request->pathname.size() == 0 || request->pathname == "/");

                auto tryRecoverDirectoryFromMissingHtml = [&]() -> bool {
                  if (!autoIndexEnabled) return false;
                  if (request->method != "GET") return false;
                  if (!request->pathname.ends_with(".html")) return false;

                  const String indexSuffix = "/index.html";
                  const String htmlSuffix = ".html";
                  String base = request->pathname;

                  if (base.ends_with(indexSuffix)) {
                    base = base.substr(0, base.size() - indexSuffix.size());
                  } else {
                    base = base.substr(0, base.size() - htmlSuffix.size());
                  }

                  if (base.size() == 0) base = "/";
                  if (!base.starts_with("/")) base = "/" + base;

                  String normalizedBase = base;
                  if (normalizedBase.size() > 1 && normalizedBase.ends_with("/")) {
                    normalizedBase = normalizedBase.substr(0, normalizedBase.size() - 1);
                  }

                  auto directoryPath = fs::path(applicationResources);
                  if (normalizedBase.size() > 1) {
                    directoryPath /= normalizedBase.substr(1);
                  }

                  if (!filesystem::Resource::isDirectory(directoryPath)) {
                    return false;
                  }

                  resourcePath = directoryPath.string();
                  resource = filesystem::Resource(resourcePath);
                  resourceExists = resource.exists();
                  resourceIsDirectory = filesystem::Resource::isDirectory(resourcePath);

                  if (resourcePath.starts_with(applicationResources)) {
                    contentLocation = resourcePath.substr(applicationResources.size(), resourcePath.size());
                  }

                  if (contentLocation.size() == 0) {
                    if (normalizedBase == "/") {
                      contentLocation = "/";
                    } else {
                      contentLocation = normalizedBase;
                    }
                  } else if (!contentLocation.starts_with("/")) {
                    contentLocation = "/" + contentLocation;
                  }

                  return resourceIsDirectory;
                };

                auto attemptServiceWorkerFetch = [&, this](bool waitForRegistration = true) -> bool {
                  if (!serviceWorker) {
                    return false;
                  }

                  auto swFetch = serviceworker::Request();
                  swFetch.method = request->method;
                  swFetch.scheme = request->scheme;
                  swFetch.url.scheme = request->scheme;
                  swFetch.url.hostname = request->hostname;
                  swFetch.url.pathname = request->pathname;
                  swFetch.url.searchParams.set(request->query);
                  swFetch.url.search = request->query;
                  swFetch.headers = request->headers;
                  swFetch.body = request->body;
                  swFetch.client = request->client;
                  if (!swFetch.headers.has("origin")) {
                    swFetch.headers.set("origin", this->navigator.location.origin);
                  }

                  const auto options = serviceworker::Fetch::Options {
                    .client = request->client,
                    .waitForRegistrationToFinish = waitForRegistration
                  };

                  const bool fetched = serviceWorker->fetch(swFetch, options, [request, callback, this](auto res) mutable {
                    if (!request->isActive()) return;
                    auto resp = SchemeHandlers::Response(request);
                    if (res.statusCode == 0) {
                      resp.fail("ServiceWorker request failed");
                    } else {
                      resp.writeHead(res.statusCode, res.headers);
                      resp.write(res.body.buffer);
                    }
                    callback(resp);

                    auto normalizedQuery = request->query;
                    if (normalizedQuery.size() > 0 && normalizedQuery[0] == '?') {
                      normalizedQuery = normalizedQuery.substr(1);
                    }
                    const auto key = std::to_string(request->client.id) + "|" + request->scheme + "|" + request->hostname + "|" + request->pathname + "|" + normalizedQuery;
                    if (this->swPendingRequest.contains(key)) {
                      this->swPendingRequest.erase(key);
                    }
                    if (this->swPendingCallback.contains(key)) {
                      this->swPendingCallback.erase(key);
                    }
                  });

                  if (fetched) {
                    this->getRuntime()->services.timers.setTimeout(32000, [request] () mutable {
                      if (request->isActive()) {
                        auto resp = SchemeHandlers::Response(request, 408);
                        resp.fail("ServiceWorker request timed out.");
                      }
                    });

                    auto normalizedQuery = request->query;
                    if (normalizedQuery.size() > 0 && normalizedQuery[0] == '?') {
                      normalizedQuery = normalizedQuery.substr(1);
                    }
                    const auto key = std::to_string(request->client.id) + "|" + request->scheme + "|" + request->hostname + "|" + request->pathname + "|" + normalizedQuery;
                    this->swPendingRequest.insert_or_assign(key, request);
                    this->swPendingCallback.insert_or_assign(key, callback);
                  }

                  return fetched;
                };

                bool handled = false;

                if (!resourceExists) {
                  if (attemptServiceWorkerFetch()) {
                    return;
                  }
                  if (requestTargetsRoot && autoIndexEnabled) {
                    resourcePath = applicationResources;
                    resource = filesystem::Resource(resourcePath);
                    resourceExists = resource.exists();
                    resourceIsDirectory = resourceExists && filesystem::Resource::isDirectory(resourcePath);
                    if (resourceExists && contentLocation.size() == 0) {
                      contentLocation = "/";
                    }
                  }
                  if (!resourceExists) {
                    recoveredDirectoryFromMissingHtml = tryRecoverDirectoryFromMissingHtml();
                    if (!recoveredDirectoryFromMissingHtml) {
                      response.writeHead(404);
                      handled = true;
                    }
                  }
                } else {
                  if (resourceIsDirectory && !requestLooksLikeDirectory) {
                    if (attemptServiceWorkerFetch()) {
                      return;
                    }
                  }
                }

                if (resourceIsDirectory) {
                  const bool renderAutoIndex = autoIndexEnabled && (!requestHasExtension || recoveredDirectoryFromMissingHtml);

                  if (renderAutoIndex && request->method == "GET") {
                    String basePath = contentLocation;
                    if (basePath.size() == 0 && resourcePath.starts_with(applicationResources)) {
                      basePath = resourcePath.substr(applicationResources.size(), resourcePath.size());
                    }
                    if (basePath.size() == 0) basePath = "/";
                    if (!basePath.ends_with("/")) basePath += "/";

                    String title = String("Index of ") + basePath;
                    String body;
                    body += "<!doctype html><html><head><meta charset=\"utf-8\">";
                    body += "<title>" + title + "</title>";
                    body += "<style>body{font-family:system-ui,Arial,sans-serif;margin:1rem;}table{border-collapse:collapse;}td,th{padding:.25rem .5rem;border-bottom:1px solid #eee;}a{text-decoration:none;}a:hover{text-decoration:underline;}</style>";
                    body += "</head><body>";
                    body += "<h1>" + title + "</h1>";
                    if (basePath != "/") {
                      auto parent = basePath;
                      if (parent.ends_with("/")) parent = parent.substr(0, parent.size() - 1);
                      auto pos = parent.rfind('/');
                      parent = pos == String::npos ? String("/") : parent.substr(0, pos + 1);
                      body += String("<div><a href=\"") + parent + "\">../</a></div>";
                    }
                    body += "<table><thead><tr><th>Name</th><th>Size</th></tr></thead><tbody>";
                    try {
                      const auto parentName = fs::path(resourcePath).filename().string();
                      for (const auto& entry : fs::directory_iterator(resourcePath)) {
                        const auto name = entry.path().filename().string();
                        if (name == "." || name == "..") continue;
                        if (name == "oro") continue;
                        if (entry.is_directory() && parentName == "lib" && name == "extensions") continue;

                        const bool isDir = entry.is_directory();
                        const auto href = basePath + name + (isDir ? "/" : "");
                        String sizeStr = isDir ? String("-") : std::to_string(entry.is_regular_file() ? (uint64_t)fs::file_size(entry) : 0);
                        body += String("<tr><td><a href=\"") + href + "\">" + name + (isDir ? "/" : "") + "</a></td><td style=\"text-align:right\">" + sizeStr + "</td></tr>";
                      }
                    } catch (...) {}
                    body += "</tbody></table></body></html>";

                    response.setHeader("content-type", "text/html; charset=utf-8");
                    response.setHeader("content-length", body.size());
                    if (!userConfig["webview_cache-control"].empty()) {
                      response.setHeader("cache-control", userConfig["webview_cache-control"]);
                    } else {
                      response.setHeader("cache-control", "no-cache");
                    }
                    response.writeHead(200);
                    response.write(body);
                    return callback(response);
                  }

                  if (requestHasExtension && !recoveredDirectoryFromMissingHtml && !handled) {
                    response.writeHead(404);
                    handled = true;
                  }
                }

                if (!handled) {
                  if (contentLocation.size() > 0) {
                    response.setHeader("content-location", contentLocation);
                  }

                  if (request->method == "OPTIONS") {
                    response.writeHead(204);
                  }

                  if (request->method == "HEAD") {
                    const auto contentType = resource.mimeType();
                    const auto contentLength = resource.size();

                    if (contentType.size() > 0) {
                      response.setHeader("content-type", contentType);
                    }

                    if (contentLength > 0) {
                      response.setHeader("content-length", contentLength);
                    }

                    response.writeHead(200);
                  }

                  if (request->method == "GET") {
                    const auto serviceWorkerHeader = request->headers.get("service-worker");
                    const auto fetchDestHeader = request->headers.get("sec-fetch-dest");
                    const auto& serviceWorkerValue = serviceWorkerHeader.value.str();
                    const auto& fetchDestValue = fetchDestHeader.value.str();
                    if (
                      serviceWorkerValue == "script" ||
                      fetchDestValue == "serviceworker"
                    ) {
                      response.setHeader("service-worker-allowed", "/");
                    }

                    if (resource.mimeType() != "text/html") {
                      if (!userConfig["webview_cache-control"].empty()) {
                        response.setHeader("cache-control", userConfig["webview_cache-control"]);
                      } else {
                        response.setHeader("cache-control", "public");
                      }
                      response.send(resource);
                    } else {
                      const auto html = request->headers["runtime-preload-injection"] == "disabled"
                        ? resource.str()
                        : this->client.preload.insertIntoHTML(resource.str(), {
                            .protocolHandlerSchemes = serviceWorker->container.protocols.getSchemes()
                          });

                      response.setHeader("content-type", "text/html");
                      response.setHeader("content-length", html.size());
                      if (!userConfig["webview_cache-control"].empty()) {
                        response.setHeader("cache-control", userConfig["webview_cache-control"]);
                      } else {
                        response.setHeader("cache-control", "public");
                      }
                      response.writeHead(200);
                      response.write(html);
                    }
                  }
                }

                return callback(response);
              }
            }

            callback(response);
          });

          if (fetched) {
             this->getRuntime()->services.timers.setTimeout(32000, [request] () mutable {
               if (request->isActive()) {
                 auto response = SchemeHandlers::Response(request, 408);
                 response.fail("ServiceWorker request timed out.");
               }
             });
            return;
          }
        }

      const auto requestHostnameLower = toLowerCase(request->hostname);
      const auto userBundleIdentifierLower = toLowerCase(userConfig["meta_bundle_identifier"]);
      const auto globalBundleIdentifierLower = toLowerCase(globalConfig["meta_bundle_identifier"]);

      if (
        requestHostnameLower.size() > 0 &&
        (
          requestHostnameLower == userBundleIdentifierLower ||
          requestHostnameLower == globalBundleIdentifierLower
        )
      ) {
        const auto resolved = this->navigator.location.resolve(request->pathname, applicationResources);

        if (!serviceWorker) {
          serviceWorker = this->navigator.serviceWorkerServer;
        }

          if (resolved.redirect) {
            if (request->method == "GET") {
              auto location = resolved.pathname;
              if (request->query.size() > 0) {
                location += "?" + request->query;
              }

              if (request->fragment.size() > 0) {
                location += "#" + request->fragment;
              }

              response.redirect(location);
              return callback(response);
            }
          } else if (resolved.isResource()) {
            resourcePath = applicationResources + resolved.pathname;
          } else if (resolved.isMount()) {
            resourcePath = resolved.mount.filename;
          } else {
            // SPA-style fallback: prefer explicit default_index; otherwise,
            // if allow_any_route is enabled, fall back to "/index.html".
            String fallback = "";
            if (userConfig.contains("webview_default_index")) {
              fallback = userConfig["webview_default_index"];
            } else {
              auto it = userConfig.find("webview_allow_any_route");
              if (it != userConfig.end()) {
                const auto v = toLowerCase(trim(it->second));
                const bool allow = (v == "true" || v == "1" || v == "on" || v == "yes");
                if (allow) fallback = "/index.html";
              }
            }

            if (fallback.size() > 0) {
              resourcePath = fallback;
              if (resourcePath.starts_with("./")) {
                resourcePath = applicationResources + resourcePath.substr(1);
              } else if (resourcePath.starts_with("/")) {
                resourcePath = applicationResources + resourcePath;
              } else {
                resourcePath = applicationResources + "/" + resourcePath;
              }
            }
          }

          if (resourcePath.size() == 0 && resolved.pathname.size() > 0) {
            resourcePath = applicationResources + resolved.pathname;
          }

          // handle HEAD and GET requests for a file resource
          if (resourcePath.size() > 0) {
            if (resourcePath.starts_with(applicationResources)) {
              contentLocation = resourcePath.substr(applicationResources.size(), resourcePath.size());
            }

            auto resource = filesystem::Resource(resourcePath);
            bool resourceExists = resource.exists();
            bool resourceIsDirectory = resourceExists && filesystem::Resource::isDirectory(resourcePath);
            const bool requestLooksLikeDirectory = request->pathname.size() == 0 || request->pathname.ends_with("/");
            const bool autoIndexEnabled = [&]() {
              auto it = userConfig.find("webview_autoindex");
              if (it == userConfig.end()) return false;
              const auto v = toLowerCase(trim(it->second));
              return (v == "true" || v == "1" || v == "on" || v == "yes");
            }();
            const bool requestHasExtension = [&]() {
              if (request->pathname.size() == 0) return false;
              if (request->pathname.ends_with("/")) return false;
              try {
                return fs::path(request->pathname).has_extension();
              } catch (...) {
                return false;
              }
            }();
            bool recoveredDirectoryFromMissingHtml = false;
            const bool requestTargetsRoot = (request->pathname.size() == 0 || request->pathname == "/");

            auto tryRecoverDirectoryFromMissingHtml = [&]() -> bool {
              if (!autoIndexEnabled) return false;
              if (request->method != "GET") return false;
              if (!request->pathname.ends_with(".html")) return false;

              const String indexSuffix = "/index.html";
              const String htmlSuffix = ".html";
              String base = request->pathname;

              if (base.ends_with(indexSuffix)) {
                base = base.substr(0, base.size() - indexSuffix.size());
              } else {
                base = base.substr(0, base.size() - htmlSuffix.size());
              }

              if (base.size() == 0) base = "/";
              if (!base.starts_with("/")) base = "/" + base;

              String normalizedBase = base;
              if (normalizedBase.size() > 1 && normalizedBase.ends_with("/")) {
                normalizedBase = normalizedBase.substr(0, normalizedBase.size() - 1);
              }

              auto directoryPath = fs::path(applicationResources);
              if (normalizedBase.size() > 1) {
                directoryPath /= normalizedBase.substr(1);
              }

              if (!filesystem::Resource::isDirectory(directoryPath)) {
                return false;
              }

              resourcePath = directoryPath.string();
              resource = filesystem::Resource(resourcePath);
              resourceExists = resource.exists();
              resourceIsDirectory = filesystem::Resource::isDirectory(resourcePath);

              if (resourcePath.starts_with(applicationResources)) {
                contentLocation = resourcePath.substr(applicationResources.size(), resourcePath.size());
              }

              if (contentLocation.size() == 0) {
                if (normalizedBase == "/") {
                  contentLocation = "/";
                } else {
                  contentLocation = normalizedBase;
                }
              } else if (!contentLocation.starts_with("/")) {
                contentLocation = "/" + contentLocation;
              }

              return resourceIsDirectory;
            };

            auto attemptServiceWorkerFetch = [&, this](bool waitForRegistration = true) -> bool {
              if (!serviceWorker) {
                return false;
              }

              auto swFetch = serviceworker::Request();
              swFetch.method = request->method;
              swFetch.scheme = request->scheme;
              swFetch.url.scheme = request->scheme;
              swFetch.url.hostname = request->hostname;
              swFetch.url.pathname = request->pathname;
              swFetch.url.searchParams.set(request->query);
              swFetch.url.search = request->query;
              swFetch.headers = request->headers;
              swFetch.body = request->body;
              swFetch.client = request->client;
              if (!swFetch.headers.has("origin")) {
                swFetch.headers.set("origin", this->navigator.location.origin);
              }

              const auto options = serviceworker::Fetch::Options {
                .client = request->client,
                .waitForRegistrationToFinish = waitForRegistration
              };

              const bool fetched = serviceWorker->fetch(swFetch, options, [request, callback, this](auto res) mutable {
                if (!request->isActive()) return;
                auto resp = SchemeHandlers::Response(request);
                if (res.statusCode == 0) {
                  resp.fail("ServiceWorker request failed");
                } else {
                  resp.writeHead(res.statusCode, res.headers);
                  resp.write(res.body.buffer);
                }
                callback(resp);

                auto normalizedQuery = request->query;
                if (normalizedQuery.size() > 0 && normalizedQuery[0] == '?') {
                  normalizedQuery = normalizedQuery.substr(1);
                }
                const auto key = std::to_string(request->client.id) + "|" + request->scheme + "|" + request->hostname + "|" + request->pathname + "|" + normalizedQuery;
                if (this->swPendingRequest.contains(key)) {
                  this->swPendingRequest.erase(key);
                }
                if (this->swPendingCallback.contains(key)) {
                  this->swPendingCallback.erase(key);
                }
              });

              if (fetched) {
                this->getRuntime()->services.timers.setTimeout(32000, [request] () mutable {
                  if (request->isActive()) {
                    auto resp = SchemeHandlers::Response(request, 408);
                    resp.fail("ServiceWorker request timed out.");
                  }
                });

                auto normalizedQuery = request->query;
                if (normalizedQuery.size() > 0 && normalizedQuery[0] == '?') {
                  normalizedQuery = normalizedQuery.substr(1);
                }
                const auto key = std::to_string(request->client.id) + "|" + request->scheme + "|" + request->hostname + "|" + request->pathname + "|" + normalizedQuery;
                this->swPendingRequest.insert_or_assign(key, request);
                this->swPendingCallback.insert_or_assign(key, callback);
              }

              return fetched;
            };

            bool handled = false;

            if (!resourceExists) {
              if (attemptServiceWorkerFetch()) {
                return;
              }
              if (requestTargetsRoot && autoIndexEnabled) {
                resourcePath = applicationResources;
                resource = filesystem::Resource(resourcePath);
                resourceExists = resource.exists();
                resourceIsDirectory = resourceExists && filesystem::Resource::isDirectory(resourcePath);
                if (resourceExists && contentLocation.size() == 0) {
                  contentLocation = "/";
                }
              }
              if (!resourceExists) {
                recoveredDirectoryFromMissingHtml = tryRecoverDirectoryFromMissingHtml();
                if (!recoveredDirectoryFromMissingHtml) {
                  response.writeHead(404);
                  handled = true;
                }
              }
            } else {
              if (resourceIsDirectory && !requestLooksLikeDirectory) {
                if (attemptServiceWorkerFetch()) {
                  return;
                }
              }
            }

            if (resourceIsDirectory) {
              const bool renderAutoIndex = autoIndexEnabled && (!requestHasExtension || recoveredDirectoryFromMissingHtml);

              if (renderAutoIndex && request->method == "GET") {
                String basePath = contentLocation;
                if (basePath.size() == 0 && resourcePath.starts_with(applicationResources)) {
                  basePath = resourcePath.substr(applicationResources.size(), resourcePath.size());
                }
                if (basePath.size() == 0) basePath = "/";
                if (!basePath.ends_with("/")) basePath += "/";

                String title = String("Index of ") + basePath;
                String body;
                body += "<!doctype html><html><head><meta charset=\"utf-8\">";
                body += "<title>" + title + "</title>";
                body += "<style>body{font-family:system-ui,Arial,sans-serif;margin:1rem;}table{border-collapse:collapse;}td,th{padding:.25rem .5rem;border-bottom:1px solid #eee;}a{text-decoration:none;}a:hover{text-decoration:underline;}</style>";
                body += "</head><body>";
                body += "<h1>" + title + "</h1>";
                if (basePath != "/") {
                  auto parent = basePath;
                  if (parent.ends_with("/")) parent = parent.substr(0, parent.size() - 1);
                  auto pos = parent.rfind('/');
                  parent = pos == String::npos ? String("/") : parent.substr(0, pos + 1);
                  body += String("<div><a href=\"") + parent + "\">../</a></div>";
                }
                body += "<table><thead><tr><th>Name</th><th>Size</th></tr></thead><tbody>";
                try {
                  const auto parentName = fs::path(resourcePath).filename().string();
                  for (const auto& entry : fs::directory_iterator(resourcePath)) {
                    const auto name = entry.path().filename().string();
                    if (name == "." || name == "..") continue;
                    if (name == "oro") continue;
                    if (entry.is_directory() && parentName == "lib" && name == "extensions") continue;

                    const bool isDir = entry.is_directory();
                    const auto href = basePath + name + (isDir ? "/" : "");
                    String sizeStr = isDir ? String("-") : std::to_string(entry.is_regular_file() ? (uint64_t)fs::file_size(entry) : 0);
                    body += String("<tr><td><a href=\"") + href + "\">" + name + (isDir ? "/" : "") + "</a></td><td style=\"text-align:right\">" + sizeStr + "</td></tr>";
                  }
                } catch (...) {}
                body += "</tbody></table></body></html>";

                response.setHeader("content-type", "text/html; charset=utf-8");
                response.setHeader("content-length", body.size());
                if (!userConfig["webview_cache-control"].empty()) {
                  response.setHeader("cache-control", userConfig["webview_cache-control"]);
                } else {
                  response.setHeader("cache-control", "no-cache");
                }
                response.writeHead(200);
                response.write(body);
                return callback(response);
              }

              if (requestHasExtension && !recoveredDirectoryFromMissingHtml && !handled) {
                response.writeHead(404);
                handled = true;
              }
            }

            if (!handled) {
              if (contentLocation.size() > 0) {
                response.setHeader("content-location", contentLocation);
              }

              if (request->method == "OPTIONS") {
                response.writeHead(204);
              }

              if (request->method == "HEAD") {
                const auto contentType = resource.mimeType();
                const auto contentLength = resource.size();

                if (contentType.size() > 0) {
                  response.setHeader("content-type", contentType);
                }

                if (contentLength > 0) {
                  response.setHeader("content-length", contentLength);
                }

                response.writeHead(200);
              }

              if (request->method == "GET") {
                const auto serviceWorkerHeader = request->headers.get("service-worker");
                const auto fetchDestHeader = request->headers.get("sec-fetch-dest");
                const auto& serviceWorkerValue = serviceWorkerHeader.value.str();
                const auto& fetchDestValue = fetchDestHeader.value.str();
                if (
                  serviceWorkerValue == "script" ||
                  fetchDestValue == "serviceworker"
                ) {
                  response.setHeader("service-worker-allowed", "/");
                }

                if (resource.mimeType() != "text/html") {
                  if (!userConfig["webview_cache-control"].empty()) {
                    response.setHeader("cache-control", userConfig["webview_cache-control"]);
                  } else {
                    response.setHeader("cache-control", "public");
                  }
                  response.send(resource);
                } else {
                  const auto html = request->headers["runtime-preload-injection"] == "disabled"
                    ? resource.str()
                    : this->client.preload.insertIntoHTML(resource.str(), {
                        .protocolHandlerSchemes = serviceWorker
                          ? serviceWorker->container.protocols.getSchemes()
                          : Vector<String>()
                        });

                  response.setHeader("content-type", "text/html");
                  response.setHeader("content-length", html.size());
                  if (!userConfig["webview_cache-control"].empty()) {
                    response.setHeader("cache-control", userConfig["webview_cache-control"]);
                  } else {
                    response.setHeader("cache-control", "public");
                  }
                  response.writeHead(200);
                  response.write(html);
                }
              }
            }

            return callback(response);
          }
        }

        if (serviceWorker && serviceWorker->container.registrations.size() > 0) {
          auto fetch = serviceworker::Request();
          fetch.method = request->method;
          fetch.scheme = request->scheme;
          fetch.url.scheme = request->scheme;
          fetch.url.hostname = request->hostname;
          fetch.url.pathname = request->pathname;
          fetch.url.searchParams.set(request->query);
          fetch.url.search = request->query;
          fetch.headers = request->headers;
          fetch.body = request->body;
          fetch.client = request->client;

          if (!fetch.headers.has("origin")) {
            fetch.headers.set("origin", this->navigator.location.origin);
          }

          const auto options = serviceworker::Fetch::Options { request->client };
          const auto fetched = serviceWorker->fetch(fetch, options, [request, callback] (auto res) mutable {
            if (!request->isActive()) {
              return;
            }

            auto response = SchemeHandlers::Response(request, 404);

            if (res.statusCode == 0) {
              response.fail("ServiceWorker request failed");
            } else {
              response.writeHead(res.statusCode, res.headers);
              response.write(res.body.buffer);
            }

            callback(response);
          });

          if (fetched) {
            this->getRuntime()->services.timers.setTimeout(32000, [request] () mutable {
              if (request->isActive()) {
                auto response = SchemeHandlers::Response(request, 408);
                response.fail("ServiceWorker request timed out.");
              }
            });
            return;
          }
        }

        response.writeHead(404);
        return callback(response);
      }

      // module or stdlib import/fetch `oro:<module>/<path>` which will just
      // proxy an import into a normal resource request above
      if (request->hostname.size() == 0) {
        auto pathname = request->pathname;

        if (pathname.ends_with("/")) {
          pathname = pathname.substr(0, pathname.size() - 1);
        }

        String specifier;

        if (pathname.size() > 0) {
          if (pathname.front() == '/') {
            if (pathname.size() > 1) {
              specifier = pathname.substr(1);
            }
          } else {
            specifier = pathname;
          }
        }

        if (specifier.empty()) {
          response.writeHead(404);
          return callback(response);
        }

        if (!pathname.ends_with(".js")) {
          pathname += ".js";
        }

        if (!pathname.starts_with("/")) {
          pathname = "/" + pathname;
        }

        resourcePath = applicationResources + "/oro" + pathname;
        contentLocation = "/oro" + pathname;

        auto resource = filesystem::Resource(resourcePath, { .cache = true });

        if (resource.exists()) {
          auto url = URL();
          #if ORO_RUNTIME_PLATFORM_ANDROID
          url.scheme = "https";
          #else
          url.scheme = "oro";
          #endif
          url.hostname = toLowerCase(bundleIdentifier);
          url.pathname = contentLocation;
          url.search = request->query;

          const auto moduleImportProxy = tmpl(
            String(reinterpret_cast<const char*>(resource.read())).find("export default") != String::npos
              ? ESM_IMPORT_PROXY_TEMPLATE_WITH_DEFAULT_EXPORT
              : ESM_IMPORT_PROXY_TEMPLATE_WITHOUT_DEFAULT_EXPORT,
            Map<String, String> {
              {"url", url.str()},
              {"commit", version::VERSION_HASH_STRING},
              {"protocol", "oro"},
              {"pathname", pathname},
              {"specifier", specifier},
              {"bundle_identifier", toLowerCase(bundleIdentifier)}
            }
          );

          const auto contentType = resource.mimeType();

          if (contentType.size() > 0) {
            response.setHeader("content-type", contentType);
          }

          response.setHeader("content-length", moduleImportProxy.size());

          if (contentLocation.size() > 0) {
            response.setHeader("content-location", contentLocation);
          }

          response.writeHead(200);
          response.write(moduleImportProxy);
          return callback(response);
        }
        response.setHeader("content-type", "text/javascript");
      }

      response.writeHead(404);
      callback(response);
    };

    this->schemeHandlers.registerSchemeHandler("oro", runtimeSchemeHandler);

    this->schemeHandlers.registerSchemeHandler("node", [this](
      const auto request,
      const auto& bridge,
      auto callbacks,
      auto callback
    ) {
      if (request->method == "OPTIONS") {
        auto response = SchemeHandlers::Response(request);
        response.writeHead(204);
        callback(response);
        return;
      }

      auto globalUserConfig = getUserConfig();
      const auto bundleIdentifier = this->userConfig["meta_bundle_identifier"];
      // the location of static application resources
      const auto applicationResources = filesystem::Resource::getResourcesPath().string();
      // default response is 404
      auto response = SchemeHandlers::Response(request, 404);

      // the resouce path that may be request
      String resourcePath;

      // the content location relative to the request origin
      String contentLocation;

      // module or stdlib import/fetch `oro:<module>/<path>` which will just
      // proxy an import into a normal resource request above
      if (request->hostname.size() == 0) {
        auto rawPathname = request->pathname;
        String nodeSpecifier;

        if (rawPathname.size() > 0) {
          if (rawPathname.front() == '/') {
            if (rawPathname.size() > 1) {
              nodeSpecifier = rawPathname.substr(1);
            }
          } else {
            nodeSpecifier = rawPathname;
          }
        }

        if (nodeSpecifier.empty()) {
          response.writeHead(404);
          return callback(response);
        }

        const auto isAllowedNodeCoreModule = allowedNodeCoreModules.end() != std::find(
          allowedNodeCoreModules.begin(),
          allowedNodeCoreModules.end(),
          nodeSpecifier
        );

        if (!isAllowedNodeCoreModule) {
          response.writeHead(404);
          return callback(response);
        }

        auto pathname = request->pathname;

        if (!pathname.ends_with(".js")) {
          pathname += ".js";
        }

        if (!pathname.starts_with("/")) {
          pathname = "/" + pathname;
        }

        contentLocation = "/oro" + pathname;
        resourcePath = applicationResources + contentLocation;

        auto resource = filesystem::Resource(resourcePath, { .cache = true });

        if (!resource.exists()) {
          if (!pathname.ends_with(".js")) {
            pathname = request->pathname;

            if (!pathname.starts_with("/")) {
              pathname = "/" + pathname;
            }

            if (pathname.ends_with("/")) {
              pathname = pathname.substr(0, pathname.size() - 1);
            }

            contentLocation = "/oro" + pathname + "/index.js";
            resourcePath = applicationResources + contentLocation;
          }

          resource = filesystem::Resource(resourcePath, { .cache = true });
        }

        if (resource.exists()) {
          auto url = URL();
          #if ORO_RUNTIME_PLATFORM_ANDROID
          url.scheme = "https";
          #else
          url.scheme = "oro";
          #endif
          url.hostname = toLowerCase(bundleIdentifier);
          url.pathname = contentLocation;
          url.search = request->query;
          const auto moduleImportProxy = tmpl(
            String(reinterpret_cast<const char*>(resource.read())).find("export default") != String::npos
              ? ESM_IMPORT_PROXY_TEMPLATE_WITH_DEFAULT_EXPORT
              : ESM_IMPORT_PROXY_TEMPLATE_WITHOUT_DEFAULT_EXPORT,
            Map<String, String> {
              {"url", url.str()},
              {"commit", version::VERSION_HASH_STRING},
              {"protocol", "node"},
              {"pathname", pathname},
              {"specifier", pathname.size() > 1 ? pathname.substr(1) : String("")},
              {"bundle_identifier", bundleIdentifier}
            }
          );

          const auto contentType = resource.mimeType();

          if (contentType.size() > 0) {
            response.setHeader("content-type", contentType);
          }

          response.setHeader("content-length", moduleImportProxy.size());

          if (contentLocation.size() > 0) {
            response.setHeader("content-location", contentLocation);
          }

          response.writeHead(200);
          response.write(trim(moduleImportProxy));
        }

        return callback(response);
      }

      response.writeHead(404);
      callback(response);
    });

    Set<String> globalProtocolHandlers = { "npm" };
    Map<String, String> protocolHandlers = {};
    auto globalUserConfig = getUserConfig();
    protocolHandlers.insert({"npm", "/oro/npm/service-worker.js"});

    for (const auto& entry : split(globalUserConfig["webview_protocol-handlers"], " ")) {
      const auto scheme = replace(trim(entry), ":", "");
      if (this->navigator.serviceWorkerServer->container.protocols.registerHandler(scheme)) {
        protocolHandlers.insert_or_assign(scheme, "");
        globalProtocolHandlers.insert(scheme);
      }
    }

    for (const auto& entry : globalUserConfig) {
      const auto& key = entry.first;
      if (key.starts_with("webview_protocol-handlers_")) {
        const auto scheme = replace(replace(trim(key), "webview_protocol-handlers_", ""), ":", "");;
        const auto data = entry.second;
        if (this->navigator.serviceWorkerServer->container.protocols.registerHandler(scheme, { data })) {
          protocolHandlers.insert_or_assign(scheme, data);
          globalProtocolHandlers.insert(scheme);
        }
      }
    }

    for (const auto& entry : split(this->userConfig["webview_protocol-handlers"], " ")) {
      const auto scheme = replace(trim(entry), ":", "");
      if (this->navigator.serviceWorkerServer->container.protocols.registerHandler(scheme)) {
        protocolHandlers.insert_or_assign(scheme, "");
      }
    }

    for (const auto& entry : this->userConfig) {
      const auto& key = entry.first;
      if (key.starts_with("webview_protocol-handlers_")) {
        const auto scheme = replace(replace(trim(key), "webview_protocol-handlers_", ""), ":", "");;
        const auto data = entry.second;
        if (this->navigator.serviceWorkerServer->container.protocols.registerHandler(scheme, { data })) {
          protocolHandlers.insert_or_assign(scheme, data);
        }
      }
    }

    for (const auto& entry : protocolHandlers) {
      const auto& scheme = entry.first;
      const auto id = rand64();

      if (
        globalUserConfig["meta_bundle_identifier"] == this->userConfig["meta_bundle_identifier"] ||
        !globalProtocolHandlers.contains(scheme)
      ) {
        auto scriptURL = trim(entry.second);

        if (scriptURL.size() == 0) {
          continue;
        }

        if (!scriptURL.starts_with(".") && !scriptURL.starts_with("/")) {
          continue;
        }

        if (scriptURL.starts_with(".")) {
          scriptURL = scriptURL.substr(1, scriptURL.size());
        }

        String scope = "/";

        auto scopeParts = split(scriptURL, "/");
        if (scopeParts.size() > 0) {
          scopeParts = Vector<String>(scopeParts.begin(), scopeParts.end() - 1);
          scope = join(scopeParts, "/");
        }

        scriptURL = (
        #if ORO_RUNTIME_PLATFORM_ANDROID
          "https://" +
        #else
          "oro://" +
        #endif
          this->userConfig["meta_bundle_identifier"] +
          scriptURL
        );

        auto env = JSON::Object::Entries {};
        for (const auto& entry : this->userConfig) {
          if (entry.first.starts_with("env_")) {
            env[entry.first.substr(4)] = entry.second;
          } else if (entry.first == "build_env") {
            const auto keys = parseStringList(entry.second, { ',', ' ' });
            for (const auto& key : keys) {
              env[key] = env::get(key);
            }
          }
        }

        if (scheme == "npm") {
          if (globalUserConfig["meta_bundle_identifier"] == this->userConfig["meta_bundle_identifier"]) {
            this->navigator.serviceWorkerServer->container.registerServiceWorker({
              .type = serviceworker::Registration::Options::Type::Module,
              .scriptURL = scriptURL,
              .scope = scope,
              .scheme = scheme,
              .serializedWorkerArgs = "",
              .id = id
            });
          }
        } else {
          this->navigator.serviceWorkerServer->container.registerServiceWorker({
            .type = serviceworker::Registration::Options::Type::Module,
            .scriptURL = scriptURL,
            .scope = scope,
            .scheme = scheme,
            .serializedWorkerArgs = encodeURIComponent(JSON::Object(JSON::Object::Entries {
              {"index", this->client.index},
              {"argv", JSON::Array {}},
              {"env", env},
              {"debug", isDebugEnabled()},
              {"headless", this->userConfig["build_headless"] == "true"},
              {"config", this->userConfig},
              {"conduit", JSON::Object::Entries {
              {"port", this->getRuntime()->services.conduit.port},
              {"hostname", this->getRuntime()->services.conduit.hostname},
              {"sharedKey", this->getRuntime()->services.conduit.sharedKey}
              }}
            }).str()),
            .id = id
          });
        }
      }

      this->schemeHandlers.registerSchemeHandler(scheme, [this](
        auto request,
        const auto& bridge,
        auto callbacks,
        auto callback
      ) {
        auto app = App::sharedApplication();
        auto window = app->runtime.windowManager.getWindowForBridge(&bridge);

        auto fetch = serviceworker::Request();
        SharedPointer<serviceworker::Server> serviceWorkerServer = nullptr;

        if (window == nullptr) {
          auto response = SchemeHandlers::Response(request);
          response.writeHead(400);
          callback(response);
          return;
        }

        if (request->method == "OPTIONS") {
          auto response = SchemeHandlers::Response(request);
          response.writeHead(204);
          callback(response);
          return;
        }

        fetch.method = request->method;
        fetch.scheme = request->scheme;
        fetch.url.scheme = request->scheme;
        fetch.url.hostname = request->hostname;
        fetch.url.pathname = request->pathname;
        fetch.url.search = "?" + request->query;
        fetch.headers = request->headers;
        fetch.body = request->body;
        fetch.client = request->client;

        if (request->scheme == "npm") {
          auto app = App::sharedApplication();
          static auto userConfig = getUserConfig();
          const auto bundleIdentifier = userConfig["meta_bundle_identifier"];

          auto pathname = request->pathname;   // may be "/", "", or "/sub/path"
          auto hostname = request->hostname;   // npm module name (e.g., "lit")

          // Build npm spec path as "/<module>[/<subpath>]"
          String npmPath = "/";
          if (hostname.size() > 0) {
            // normalize incoming pathname
            if (pathname.size() > 0 && pathname[0] != '/') {
              pathname = "/" + pathname;
            }
            if (pathname == "/") {
              pathname.clear();
            }
            npmPath = "/" + hostname + pathname;
          } else {
            npmPath = (pathname.size() > 0 ? pathname : "/");
          }

          fetch.url.pathname = npmPath;
          // For npm requests, SW runs under the app origin; use bundle id as hostname
          fetch.url.hostname = bundleIdentifier;
        }

        if (!fetch.headers.has("origin")) {
          fetch.headers.set("origin", this->navigator.location.origin);
        }

        const auto options = serviceworker::Fetch::Options {
          .client = request->client,
          // Ensure SW is fully registered/activated before dispatching fetch
          .waitForRegistrationToFinish = true
        };

        auto origin = webview::Origin(fetch.url.str());
        origin.scheme = "oro";
        serviceWorkerServer = app->runtime.serviceWorkerManager.get(origin.name());
        if (!serviceWorkerServer) {
          serviceWorkerServer = this->navigator.serviceWorkerServer;
        }

        const auto scope = serviceWorkerServer->container.protocols.getServiceWorkerScope(request->scheme);

        if (scope.size() > 0) {
          fetch.url.pathname = scope + fetch.url.pathname;
        }

        // register pending mapping for SW streaming correlation
        {
          auto normalizedQuery = fetch.url.search;
          if (normalizedQuery.size() > 0 && normalizedQuery[0] == '?') {
            normalizedQuery = normalizedQuery.substr(1);
          }

          // For reserved scheme 'npm', the SW container strips the scope prefix
          // (e.g., '/oro/npm') when correlating streaming responses. Mirror
          // that here to ensure keys match.
          auto keyPath = fetch.url.pathname;
          if (request->scheme == "npm" && scope.size() > 0) {
            // 'scope' is a path-like string (e.g., '/oro/npm')
            if (keyPath.starts_with(scope)) {
              keyPath = keyPath.substr(scope.size());
              if (keyPath.size() == 0) keyPath = "/";
            }
          }

          const auto key = std::to_string(request->client.id) + "|" + fetch.url.scheme + "|" + fetch.url.hostname + "|" + keyPath + "|" + normalizedQuery;
          this->swPendingRequest.insert_or_assign(key, request);
          this->swPendingCallback.insert_or_assign(key, callback);
        }

        const auto fetched = serviceWorkerServer->fetch(fetch, options, [this, request, callback] (auto res) mutable {
          if (!request->isActive()) {
            return;
          }

          auto response = SchemeHandlers::Response(request);
          if (res.statusCode == 0) {
            response.fail("ServiceWorker request failed");
          } else {
            response.writeHead(res.statusCode, res.headers);
            response.write(res.body.buffer);
          }

          callback(response);

          // cleanup any pending mapping (non-streaming path)
          // Use the same correlation key shape as registration above.
          String cleanupHostname = request->hostname;
          String cleanupPath = request->pathname;
          if (request->scheme == "npm") {
            static auto userConfig = getUserConfig();
            cleanupHostname = userConfig["meta_bundle_identifier"];
            // mirror npm path construction used during pending mapping
            auto host = request->hostname;
            if (cleanupPath.size() > 0 && cleanupPath[0] != '/') {
              cleanupPath = "/" + cleanupPath;
            }
            if (cleanupPath == "/") {
              cleanupPath.clear();
            }
            cleanupPath = "/" + host + cleanupPath;
          }

          const auto cleanupKey = std::to_string(request->client.id) + "|" + request->scheme + "|" + cleanupHostname + "|" + cleanupPath + "|" + request->query;
          if (this->swPendingRequest.contains(cleanupKey)) this->swPendingRequest.erase(cleanupKey);
          if (this->swPendingCallback.contains(cleanupKey)) this->swPendingCallback.erase(cleanupKey);
        });

        if (fetched) {
          // FIXME(@jwerle): revisit timeout
          //this->core->setTimeout(32000, [request] () mutable {
          //if (request->isActive()) {
          //auto response = SchemeHandlers::Response(request, 408);
          //response.fail("Protocol handler ServiceWorker request timed out.");
          //}
          //});
          return;
        }

        auto response = SchemeHandlers::Response(request);
        response.writeHead(404);
        callback(response);
      });
    }
  #if ORO_RUNTIME_PLATFORM_WINDOWS
    static const int MAX_ALLOWED_SCHEME_ORIGINS = 64;
    static const int MAX_CUSTOM_SCHEME_REGISTRATIONS = 64;
    Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions4> options;
    Vector<SharedPointer<WString>> schemes;
    Vector<SharedPointer<WString>> origins;

    if (this->schemeHandlers.configuration.webview.As(&options) == S_OK) {
      ICoreWebView2CustomSchemeRegistration* registrations[MAX_CUSTOM_SCHEME_REGISTRATIONS] = {};
      const WCHAR* allowedOrigins[MAX_ALLOWED_SCHEME_ORIGINS] = {};

      int registrationsCount = 0;
      int allowedOriginsCount = 0;

      allowedOrigins[allowedOriginsCount++] = L"about://*";
      allowedOrigins[allowedOriginsCount++] = L"https://*";

      for (const auto& entry : this->schemeHandlers.handlers) {
        const auto origin = entry.first + "://*";
        origins.push_back(std::make_shared<WString>(convertStringToWString(origin)));
        allowedOrigins[allowedOriginsCount++] = origins.back()->c_str();
      }

    	// store registratino refs here
      Set<Microsoft::WRL::ComPtr<CoreWebView2CustomSchemeRegistration>> registrationsSet;

      for (const auto& entry : this->schemeHandlers.handlers) {
        schemes.push_back(std::make_shared<WString>(convertStringToWString(entry.first)));
        auto registration = Microsoft::WRL::Make<CoreWebView2CustomSchemeRegistration>(
          schemes.back()->c_str()
        );

        registration->SetAllowedOrigins(allowedOriginsCount, allowedOrigins);
        if (entry.first != "npm") {
          registration->put_HasAuthorityComponent(true);
        }
        registration->put_TreatAsSecure(true);
        registrations[registrationsCount++] = registration.Get();
        registrationsSet.insert(registration);
      }

      options->SetCustomSchemeRegistrations(
        registrationsCount,
        static_cast<ICoreWebView2CustomSchemeRegistration**>(registrations)
      );
    }
  #endif
  }

  void Bridge::configureNavigatorMounts () {
    this->navigator.configureMounts();
  }

  Runtime* Bridge::getRuntime () {
    return this->context.getRuntime();
  }

  const Runtime* Bridge::getRuntime () const {
    return this->context.getRuntime();
  }

  bool Bridge::active () const {
    return this->getRuntime()->loop.alive();
  }
}

#if ORO_RUNTIME_PLATFORM_ANDROID
extern "C" {
  jboolean ANDROID_EXTERNAL(bridge, Bridge, emit) (
    JNIEnv* env,
    jobject self,
    jint index,
    jstring eventString,
    jstring dataString
  ) {
    using namespace oro::runtime;
    auto app = App::sharedApplication();

    if (!app) {
      ANDROID_THROW(env, "Missing 'App' in environment");
      return false;
    }

    const auto window = app->runtime.windowManager.getWindow(index);

    if (!window) {
      ANDROID_THROW(env, "Invalid window requested");
      return false;
    }

    const auto event = android::StringWrap(env, eventString).str();
    const auto data = android::StringWrap(env, dataString).str();
    return window->bridge->emit(event, data);
  }
}
#endif
