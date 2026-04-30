#include "../platform.hh"
#include "../window.hh"
#include "../bridge.hh"
#include "../config.hh"
#include "../debug.hh"
#include "../http.hh"
#include "../app.hh"

#include "../webview.hh"
#include "../scheme.hh"
#include "cookies.hh"

#include <atomic>
#if ORO_RUNTIME_PLATFORM_WINDOWS
#include <vector>
#endif

using namespace oro::runtime;
using namespace oro::runtime::webview;
using oro::runtime::url::decodeURIComponent;
using oro::runtime::config::isDebugEnabled;
using oro::runtime::config::getUserConfig;
using oro::runtime::config::getDevHost;
using oro::runtime::http::toHeaderCase;
using oro::runtime::string::toUpperCase;
using oro::runtime::string::toLowerCase;
using oro::runtime::string::split;
using oro::runtime::string::trim;
using oro::runtime::string::tmpl;
using oro::runtime::app::App;

#if ORO_RUNTIME_PLATFORM_LINUX
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <unistd.h>
#endif

#if ORO_RUNTIME_PLATFORM_APPLE
using Task = id<WKURLSchemeTask>;

static inline String stringFromNSString (NSString* value) {
  if (value == nil || value.UTF8String == nullptr) {
    return "";
  }

  return String(value.UTF8String);
}

@class OROWebView;
@interface OROInternalWKURLSchemeHandler : NSObject<WKURLSchemeHandler>
@property (nonatomic) oro::runtime::webview::SchemeHandlers* handlers;

-     (void) webView: (OROWebView*) webview
  startURLSchemeTask: (id<WKURLSchemeTask>) task;

-    (void) webView: (OROWebView*) webview
  stopURLSchemeTask: (id<WKURLSchemeTask>) task;
@end

@implementation OROInternalWKURLSchemeHandler {
  Mutex mutex;
  UnorderedMap<Task, uint64_t> tasks;
}

- (void) enqueueTask: (Task) task
       withRequestID: (uint64_t) id {
  Lock lock(mutex);
  if (task != nullptr && !tasks.contains(task)) {
    tasks.emplace(task, id);
  }
}

- (void) finalizeTask: (Task) task {
  Lock lock(mutex);
  if (task != nullptr && tasks.contains(task)) {
    tasks.erase(task);
  }
}

- (bool) waitingForTask: (Task) task {
  Lock lock(mutex);
  return task != nullptr && tasks.contains(task);
}

- (bool) getRequestID: (uint64_t*) id
              forTask: (Task) task {
  if (id == nullptr || task == nullptr) {
    return false;
  }

  Lock lock(mutex);
  auto it = tasks.find(task);
  if (it == tasks.end()) {
    return false;
  }

  *id = it->second;
  return true;
}

- (void) webView: (OROWebView*) webview
  stopURLSchemeTask: (Task) task {
  uint64_t id = 0;
  if ([self getRequestID: &id forTask: task] && self.handlers != nullptr) {
    if (self.handlers->isRequestActive(id)) {
      SharedPointer<SchemeHandlers::Request> request = nullptr;
      {
        Lock lock(self.handlers->mutex);
        auto it = self.handlers->activeRequests.find(id);
        if (it != self.handlers->activeRequests.end()) {
          request = it->second;
        }
      }

      if (request != nullptr) {
        request->cancelled = true;
      }

      if (request != nullptr && request->callbacks.cancel != nullptr) {
        request->callbacks.cancel();
      }
    }
  }

  [self finalizeTask: task];
}

- (void) webView: (OROWebView*) webview
  startURLSchemeTask: (Task) task {
  if (self.handlers == nullptr) {
    const auto bundleIdentifier = String("oro.runtime");

    [task didFailWithError: [NSError
         errorWithDomain: @(bundleIdentifier.c_str())
                    code: 1
                userInfo: @{NSLocalizedDescriptionKey: @("SchemeHandlers::Response: Request is in an invalid state")}
    ]];
    return;
  }

  auto method = stringFromNSString(task.request.HTTPMethod);
  if (method.size() == 0) {
    method = "GET";
  }

  auto request = SchemeHandlers::Request::Builder(self.handlers, task)
    .setMethod(toUpperCase(method))
    // copies all headers
    .setHeaders(task.request.allHTTPHeaderFields)
    // copies request body
    .setBody(task.request.HTTPBody)
    .build();

  [self enqueueTask: task withRequestID: request->id];
  const auto handled = self.handlers->handleRequest(request, [=](const auto& response) {
    [self finalizeTask: task];
  });

  if (!handled) {
    auto response = SchemeHandlers::Response(request, 404);
    response.finish();
    [self finalizeTask: task];
  }
}
@end
#elif ORO_RUNTIME_PLATFORM_LINUX
static const auto MAX_URI_SCHEME_REQUEST_BODY_BYTES = 4 * 1024 * 1024;
static void onURISchemeRequest (WebKitURISchemeRequest* schemeRequest, gpointer userData) {
  static auto globalUserConfig = getUserConfig();
  static auto app = App::sharedApplication();

  if (!app) {
    const auto quark = g_quark_from_string(globalUserConfig["meta_bundle_identifier"].c_str());
    const auto error = g_error_new(quark, 1, "SchemeHandlers::Request: Missing WindowManager in request");
    webkit_uri_scheme_request_finish_error(schemeRequest, error);
    return;
  }

  auto webview = webkit_uri_scheme_request_get_web_view(schemeRequest);
  auto window = app->runtime.windowManager.getWindowForWebView(webview);

  if (!window) {
    const auto quark = g_quark_from_string(globalUserConfig["meta_bundle_identifier"].c_str());
    const auto error = g_error_new(quark, 1, "SchemeHandlers::Request: Missing Window in request");
    webkit_uri_scheme_request_finish_error(schemeRequest, error);
    return;
  }

  auto bridge = window->bridge;
  auto request = oro::runtime::webview::SchemeHandlers::Request::Builder(&bridge->schemeHandlers, schemeRequest)
    .setMethod(String(webkit_uri_scheme_request_get_http_method(schemeRequest)))
    // copies all request soup headers
    .setHeaders(webkit_uri_scheme_request_get_http_headers(schemeRequest))
    // reads and copies request stream body
    .setBody(webkit_uri_scheme_request_get_http_body(schemeRequest))
    .build();

  const auto handled = bridge->schemeHandlers.handleRequest(request, [=](const auto& response) mutable {
  });

  if (!handled) {
    auto response = SchemeHandlers::Response(request, 404);
    response.finish();
  }
}
#elif ORO_RUNTIME_PLATFORM_ANDROID
extern "C" {
  jboolean ANDROID_EXTERNAL(webview, SchemeHandlers, handleRequest) (
    JNIEnv* env,
    jobject self,
    jint index,
    jobject requestObject
  ) {
    auto app = app::App::sharedApplication();

    if (!app) {
      ANDROID_THROW(env, "Missing 'App' in environment");
      return false;
    }

    const auto window = app->runtime.windowManager.getWindow(index);

    if (!window) {
      ANDROID_THROW(env, "Invalid window requested");
      return false;
    }

    const auto method = android::StringWrap(env, (jstring) CallClassMethodFromAndroidEnvironment(
      env,
      Object,
      requestObject,
      "getMethod",
      "()Ljava/lang/String;"
    )).str();

    const auto headers = android::StringWrap(env, (jstring) CallClassMethodFromAndroidEnvironment(
      env,
      Object,
      requestObject,
      "getHeaders",
      "()Ljava/lang/String;"
    )).str();

    const auto requestBodyByteArray = (jbyteArray) CallClassMethodFromAndroidEnvironment(
      env,
      Object,
      requestObject,
      "getBody",
      "()[B"
    );

    const auto requestBodySize = requestBodyByteArray != nullptr
      ? env->GetArrayLength(requestBodyByteArray)
      : 0;

    const auto bytes = requestBodySize > 0
      ? new unsigned char[requestBodySize]{0}
      : nullptr;

    if (requestBodyByteArray) {
      env->GetByteArrayRegion(
        requestBodyByteArray,
        0,
        requestBodySize,
        (jbyte*) bytes
      );
    }

    const auto requestObjectRef = env->NewGlobalRef(requestObject);
    const auto request = webview::SchemeHandlers::Request::Builder(
      &window->bridge->schemeHandlers,
      requestObjectRef
    )
      .setMethod(method)
      // copies all request soup headers
      .setHeaders(headers)
      // reads and copies request stream body
      .setBody(requestBodySize, bytes)
      .build();

    const auto handled = window->bridge->schemeHandlers.handleRequest(request, [=](const auto& response) {
      if (bytes) {
        delete [] bytes;
      }

      const auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);
      attachment.env->DeleteGlobalRef(requestObjectRef);
    });

    if (!handled) {
      env->DeleteGlobalRef(requestObjectRef);
    }

    return handled;
  }

  jboolean ANDROID_EXTERNAL(webview, SchemeHandlers, hasHandlerForScheme) (
    JNIEnv* env,
    jobject self,
    jint index,
    jstring schemeString
  ) {
    auto app = app::App::sharedApplication();

    if (!app) {
      ANDROID_THROW(env, "Missing 'App' in environment");
      return false;
    }

    const auto window = app->runtime.windowManager.getWindow(index);

    if (!window) {
      ANDROID_THROW(env, "Invalid window requested");
      return false;
    }

    const auto scheme = android::StringWrap(env, schemeString).str();
    return window->bridge->schemeHandlers.hasHandlerForScheme(scheme);
  }
}
#endif

namespace oro::runtime::webview {
  class SchemeHandlersInternals {
    public:
      SchemeHandlers* handlers = nullptr;
      SharedPointer<Atomic<bool>> alive = std::make_shared<Atomic<bool>>(true);
    #if ORO_RUNTIME_PLATFORM_APPLE
      OROInternalWKURLSchemeHandler* schemeHandler = nullptr;
    #endif

      SchemeHandlersInternals (SchemeHandlers* handlers) {
        this->handlers = handlers;
      #if ORO_RUNTIME_PLATFORM_APPLE
        this->schemeHandler = [OROInternalWKURLSchemeHandler new];
        this->schemeHandler.handlers = handlers;
      #endif
      }

      ~SchemeHandlersInternals () {
      #if ORO_RUNTIME_PLATFORM_APPLE
        if (this->schemeHandler != nullptr) {
          this->schemeHandler.handlers = nullptr;
        #if !__has_feature(objc_arc)
          [this->schemeHandler release];
        #endif
          this->schemeHandler = nullptr;
        }
      #endif
      }
  };
}

namespace oro::runtime::webview {
#if ORO_RUNTIME_PLATFORM_LINUX
  static Map<String, Set<String>> globallyRegisteredSchemesForLinux;
#endif

  static constexpr size_t MAX_CDP_SCHEME_RESPONSE_BODY_BYTES = 5ULL * 1024ULL * 1024ULL;
  static constexpr size_t MAX_CDP_SCHEME_POSTDATA_BYTES = 256ULL * 1024ULL;

  static JSON::Object::Entries cdpHeadersToJSON (const http::Headers& headers) {
    JSON::Object::Entries out;
    for (const auto& h : headers) {
      out.insert_or_assign(toLowerCase(h.name), h.value.str());
    }
    return out;
  }

  static bool cdpMimeTypeLooksText (const String& mimeType) {
    const auto mt = toLowerCase(mimeType);
    if (mt.starts_with("text/")) return true;
    if (mt.find("json") != String::npos) return true;
    if (mt.find("javascript") != String::npos) return true;
    if (mt.find("ecmascript") != String::npos) return true;
    if (mt.find("xml") != String::npos) return true;
    if (mt.find("html") != String::npos) return true;
    return false;
  }

  static String cdpInferResourceType (const SchemeHandlers::Request& request) {
    const auto dest = toLowerCase(request.headers.get("sec-fetch-dest").value.str());
    if (dest == "document" || dest == "iframe" || dest == "frame") return "Document";
    if (dest == "script" || dest == "worker") return "Script";
    if (dest == "style") return "Stylesheet";
    if (dest == "image") return "Image";
    if (dest == "font") return "Font";

    if (request.method != "GET" && request.method != "HEAD") {
      return "Fetch";
    }

    const auto path = toLowerCase(request.pathname);
    if (path.ends_with(".html") || path.ends_with(".htm")) return "Document";
    if (path.ends_with(".js") || path.ends_with(".mjs") || path.ends_with(".cjs")) return "Script";
    if (path.ends_with(".css")) return "Stylesheet";
    if (path.ends_with(".json")) return "Fetch";
    if (
      path.ends_with(".png") ||
      path.ends_with(".jpg") ||
      path.ends_with(".jpeg") ||
      path.ends_with(".gif") ||
      path.ends_with(".webp") ||
      path.ends_with(".svg") ||
      path.ends_with(".ico")
    ) return "Image";
    if (
      path.ends_with(".woff") ||
      path.ends_with(".woff2") ||
      path.ends_with(".ttf") ||
      path.ends_with(".otf")
    ) return "Font";

    return "Other";
  }

  static bool cdpShouldInstrument (const SharedPointer<SchemeHandlers::Request>& request) {
    if (!request || request->cdpWindowIndex < 0 || request->cdpRequestId.empty()) {
      return false;
    }

    if (!request->handlers) {
      return false;
    }

    auto runtime = request->handlers->bridge.getRuntime();
    if (!runtime) {
      return false;
    }

    return runtime->services.cdp.isListening();
  }

  static void cdpEmitResponseReceived (SchemeHandlers::Response* response) {
    if (!response || !response->request) {
      return;
    }

    auto request = response->request;
    if (!cdpShouldInstrument(request)) {
      return;
    }

    if (request->cdpResponseReceived.exchange(true)) {
      return;
    }

    auto runtime = request->handlers->bridge.getRuntime();
    if (!runtime) {
      return;
    }

    const auto headersJson = cdpHeadersToJSON(response->headers);
    const auto mimeType = response->headers.get("content-type").value.str();
    const auto type = request->cdpResourceType.size() ? request->cdpResourceType : String("Other");

    runtime->services.cdp.onNetworkEvent(request->cdpWindowIndex, JSON::Object::Entries {
      {"method", "Network.responseReceived"},
      {"params", JSON::Object::Entries {
        {"requestId", request->cdpRequestId},
        {"type", type},
        {"response", JSON::Object::Entries {
          {"url", request->str()},
          {"status", JSON::Number(static_cast<double>(response->statusCode))},
          {"statusText", http::getStatusText(response->statusCode)},
          {"headers", headersJson},
          {"mimeType", mimeType}
        }}
      }}
    });
  }

  static void cdpEmitDataReceived (SchemeHandlers::Response* response, size_t dataLength) {
    if (!response || !response->request || dataLength == 0) {
      return;
    }

    auto request = response->request;
    if (!cdpShouldInstrument(request)) {
      return;
    }

    cdpEmitResponseReceived(response);

    auto runtime = request->handlers->bridge.getRuntime();
    if (!runtime) {
      return;
    }

    runtime->services.cdp.onNetworkEvent(request->cdpWindowIndex, JSON::Object::Entries {
      {"method", "Network.dataReceived"},
      {"params", JSON::Object::Entries {
        {"requestId", request->cdpRequestId},
        {"dataLength", JSON::Number(static_cast<double>(dataLength))},
        {"encodedDataLength", JSON::Number(static_cast<double>(dataLength))}
      }}
    });
  }

  static void cdpEmitLoadingFinished (SchemeHandlers::Response* response) {
    if (!response || !response->request) {
      return;
    }

    auto request = response->request;
    if (!cdpShouldInstrument(request)) {
      return;
    }

    if (request->cdpLoadingFinished.exchange(true)) {
      return;
    }

    cdpEmitResponseReceived(response);

    auto runtime = request->handlers->bridge.getRuntime();
    if (!runtime) {
      return;
    }

    runtime->services.cdp.onNetworkEvent(request->cdpWindowIndex, JSON::Object::Entries {
      {"method", "Network.loadingFinished"},
      {"params", JSON::Object::Entries {
        {"requestId", request->cdpRequestId},
        {"encodedDataLength", JSON::Number(static_cast<double>(response->bytesWritten))}
      }}
    });
  }

  static void cdpEmitLoadingFailed (SchemeHandlers::Response* response, const String& errorText, bool canceled) {
    if (!response || !response->request) {
      return;
    }

    auto request = response->request;
    if (!cdpShouldInstrument(request)) {
      return;
    }

    auto runtime = request->handlers->bridge.getRuntime();
    if (!runtime) {
      return;
    }

    runtime->services.cdp.onNetworkEvent(request->cdpWindowIndex, JSON::Object::Entries {
      {"method", "Network.loadingFailed"},
      {"params", JSON::Object::Entries {
        {"requestId", request->cdpRequestId},
        {"errorText", errorText},
        {"canceled", JSON::Boolean(canceled)}
      }}
    });
  }

  SchemeHandlers::SchemeHandlers (bridge::Bridge& bridge)
    : bridge(bridge) {
    this->internals = new SchemeHandlersInternals(this);
  }

  void SchemeHandlers::close () {
    if (this->internals != nullptr && this->internals->alive != nullptr) {
      this->internals->alive->exchange(false, std::memory_order_acq_rel);
    }

  #if ORO_RUNTIME_PLATFORM_APPLE
    this->configuration.webview = nullptr;
    if (this->internals != nullptr && this->internals->schemeHandler != nullptr) {
      this->internals->schemeHandler.handlers = nullptr;
    }
  #endif

    {
      Lock lock(this->mutex);
      for (auto& entry : this->activeRequests) {
        if (entry.second != nullptr) {
          entry.second->cancelled = true;
          entry.second->callbacks.cancel = nullptr;
          entry.second->callbacks.finish = nullptr;
          entry.second->callbacks.fail = nullptr;
          entry.second->handlers = nullptr;
          entry.second->platformRequest = nullptr;
        }
      }
      this->activeRequests.clear();
      this->handlers.clear();
    }
  }

  SchemeHandlers::~SchemeHandlers () {
    this->close();

    if (this->internals != nullptr) {
      delete this->internals;
      this->internals = nullptr;
    }
  }

  void SchemeHandlers::init () {}

  bool SchemeHandlers::isAlive () const {
    return (
      this->internals != nullptr &&
      this->internals->alive != nullptr &&
      this->internals->alive->load(std::memory_order_acquire)
    );
  }

  void SchemeHandlers::configure (const Configuration& configuration) {
    static const auto devHost = getDevHost();
    this->configuration = configuration;
  }

  bool SchemeHandlers::hasHandlerForScheme (const String& scheme) {
    if (!this->isAlive()) {
      return false;
    }

    Lock lock(this->mutex);
    return this->handlers.contains(scheme);
  }

  SchemeHandlers::Handler SchemeHandlers::getHandlerForScheme (const String& scheme) {
    if (!this->isAlive()) {
      return SchemeHandlers::Handler {};
    }

    Lock lock(this->mutex);
    return this->handlers.contains(scheme)
      ? this->handlers.at(scheme)
      : SchemeHandlers::Handler {};
  }

  bool SchemeHandlers::registerSchemeHandler (const String& scheme, const Handler& handler) {
    if (!this->isAlive()) {
      return false;
    }

    if (scheme.size() == 0 || this->hasHandlerForScheme(scheme)) {
      return false;
    }

  #if ORO_RUNTIME_PLATFORM_APPLE
    if (this->configuration.webview == nullptr) {
      return false;
    }

    [this->configuration.webview
      setURLSchemeHandler: this->internals->schemeHandler
             forURLScheme: @(scheme.c_str())
    ];

  #elif ORO_RUNTIME_PLATFORM_LINUX
    const auto bundleIdentifier = this->bridge.userConfig["meta_bundle_identifier"];
    if (!globallyRegisteredSchemesForLinux.contains(bundleIdentifier)) {
      globallyRegisteredSchemesForLinux[bundleIdentifier] = Set<String> {};
    }

    auto& schemes = globallyRegisteredSchemesForLinux.at(bundleIdentifier);
    // schemes are registered for the globally shared defaut context,
    // despite the `SchemeHandlers` instance bound to a `Router`
    // we'll select the correct `Router` in the callback which will give
    // access to the `SchemeHandlers` that should handle the
    // request and provide a response
    if (
      std::find(
        schemes.begin(),
        schemes.end(),
        scheme
      ) == schemes.end()
    ) {
      schemes.insert(scheme);
      auto context = this->bridge.webContext;
      auto security = webkit_web_context_get_security_manager(context);
      webkit_web_context_register_uri_scheme(
        this->bridge.webContext,
        scheme.c_str(),
        onURISchemeRequest,
        nullptr,
        nullptr
      );
      webkit_security_manager_register_uri_scheme_as_secure(
        security,
        scheme.c_str()
      );
      webkit_security_manager_register_uri_scheme_as_cors_enabled(
        security,
        scheme.c_str()
      );
    }
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    // WebView2 requires custom schemes to be registered on the environment
    // options before environment creation, otherwise navigations and module
    // loads may be rejected (often leaving `about:blank`).
    if (this->configuration.webview.Get() != nullptr) {
      Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions4> options4;

      if (SUCCEEDED(this->configuration.webview.As(&options4)) && options4 != nullptr) {
        UINT32 existingCount = 0;
        ICoreWebView2CustomSchemeRegistration** existing = nullptr;
        std::vector<Microsoft::WRL::ComPtr<ICoreWebView2CustomSchemeRegistration>> registrations;

        if (SUCCEEDED(options4->GetCustomSchemeRegistrations(&existingCount, &existing)) && existing != nullptr) {
          registrations.reserve(existingCount + 1);
          for (UINT32 i = 0; i < existingCount; ++i) {
            registrations.emplace_back(existing[i]);
          }
          CoTaskMemFree(existing);
          existing = nullptr;
        } else {
          registrations.reserve(1);
        }

        // Avoid duplicate scheme entries (the WebView2 runtime may reject them).
        bool alreadyRegistered = false;
        for (const auto& entry : registrations) {
          if (entry == nullptr) {
            continue;
          }

          LPWSTR name = nullptr;
          if (SUCCEEDED(entry->get_SchemeName(&name)) && name != nullptr) {
            const auto schemeName = oro::runtime::string::convertWStringToString(name);
            CoTaskMemFree(name);
            name = nullptr;
            if (toLowerCase(schemeName) == toLowerCase(scheme)) {
              alreadyRegistered = true;
              break;
            }
          }
        }

        if (!alreadyRegistered) {
          const auto schemeW = oro::runtime::string::convertStringToWString(scheme);
          auto registrationImpl = Microsoft::WRL::Make<CoreWebView2CustomSchemeRegistration>(schemeW.c_str());
          Microsoft::WRL::ComPtr<ICoreWebView2CustomSchemeRegistration> registration;

          if (registrationImpl != nullptr && SUCCEEDED(registrationImpl.As(&registration)) && registration != nullptr) {
            registration->put_TreatAsSecure(TRUE);

            // `oro` is used for both hostful bundle URLs (`oro://<bundle>/...`)
            // and hostless module specifiers (`oro:<module>/...`). WebView2 treats
            // schemes with authorities as hostful, so we normalize requests at
            // the scheme handler layer when needed.
            const auto hasAuthority = (toLowerCase(scheme) != "node");
            registration->put_HasAuthorityComponent(hasAuthority ? TRUE : FALSE);

            registrations.emplace_back(registration);

            std::vector<ICoreWebView2CustomSchemeRegistration*> raw;
            raw.reserve(registrations.size());
            for (auto& reg : registrations) {
              if (reg != nullptr) {
                raw.push_back(reg.Get());
              }
            }

            if (raw.size() > 0) {
              options4->SetCustomSchemeRegistrations(static_cast<UINT32>(raw.size()), raw.data());
            }
          }
        }
      }
    }
  #endif

    this->handlers.insert_or_assign(scheme, handler);
    return true;
  }

  bool SchemeHandlers::handleRequest (
    SharedPointer<Request> request,
    const HandlerCallback callback
  ) {
    if (!this->isAlive()) {
      return false;
    }

    // request was not finalized, likely not from a `Request::Builder`
    if (request == nullptr || !request->finalized) {
      return false;
    }

    if (this->isRequestActive(request->id)) {
      return false;
    }

    // Only intercept schemes we explicitly registered. On platforms like
    // WebView2 (where we may listen for all WebResourceRequested events),
    // returning false allows the platform request pipeline to proceed.
    if (!this->hasHandlerForScheme(request->scheme)) {
      return false;
    }

    if (request->hostname.size() > 0 && !request->headers.has("cookie")) {
      if (
        this->bridge.userConfig.contains("permissions_allow_cookies") &&
        this->bridge.userConfig.at("permissions_allow_cookies") == "false"
      ) {
        request->headers.set("cookie", "");
        return this->handleRequest(request, callback);
      }

      // Avoid blocking the very first navigation/boot sequence on cookies.
      // In practice the initial request often has no need for a Cookie header
      // (it serves local app resources), and blocking here can leave the
      // WebView stuck at `about:blank` if the cookie store isn't ready yet.
      if (auto app = app::App::sharedApplication()) {
        if (!app->runtime.services.platform.wasFirstDOMContentLoadedEventDispatched) {
          request->headers.set("cookie", "");
          // proceed without waiting for cookies
        } else {
          std::weak_ptr<oro::runtime::bridge::Bridge> weakBridge;
          auto window = app->runtime.windowManager.getWindowForBridge(&this->bridge);
          if (window != nullptr && window->bridge != nullptr) {
            weakBridge = window->bridge;
          }

          if (weakBridge.expired()) {
            request->headers.set("cookie", "");
            return this->handleRequest(request, callback);
          }

          const auto runtime = this->bridge.getRuntime();
          if (runtime == nullptr) {
            request->headers.set("cookie", "");
            return this->handleRequest(request, callback);
          }

          auto continued = std::make_shared<std::atomic_bool>(false);
          auto timeoutId = std::make_shared<std::atomic_uint64_t>(0);
          auto continueRequest = [weakBridge, request, callback, continued, timeoutId](String cookieValue) mutable {
            if (continued->exchange(true)) {
              return;
            }

            auto bridge = weakBridge.lock();
            if (!bridge) {
              return;
            }

            bridge->dispatch([bridge, request, callback, cookieValue, timeoutId]() mutable {
              const auto id = timeoutId->exchange(0);
              if (id != 0) {
                if (auto runtime = bridge->getRuntime()) {
                  runtime->services.timers.clearTimeout(id);
                }
              }

              if (request == nullptr || request->isCancelled()) {
                return;
              }

              request->headers.set("cookie", cookieValue);
              bridge->schemeHandlers.handleRequest(request, callback);
            });
          };

          const auto id = runtime->services.timers.setTimeout(4000, [continueRequest, timeoutId]() mutable {
            timeoutId->store(0);
            continueRequest("");
          });
          timeoutId->store(id);

          cookies::get(this->bridge, request->str(), [continueRequest](const auto& value, const auto&) mutable {
            continueRequest(value);
          });
          return true;
        }
      } else {
        request->headers.set("cookie", "");
        // proceed without waiting for cookies
      }
    }

    // CDP Network.* events for runtime-handled scheme requests.
    {
      auto runtime = this->bridge.getRuntime();
      auto app = app::App::sharedApplication();
      if (runtime && app && runtime->services.cdp.isListening()) {
        auto window = app->runtime.windowManager.getWindowForBridge(&this->bridge);
        if (window != nullptr) {
          const int windowIndex = window->index;
          request->cdpWindowIndex = windowIndex;

          if (request->cdpRequestId.size() == 0) {
            request->cdpRequestId = std::to_string(windowIndex) + ":scheme:" + std::to_string(request->id);
          }

          if (request->cdpResourceType.size() == 0) {
            request->cdpResourceType = cdpInferResourceType(*request);
          }

          runtime->services.cdp.applyExtraHTTPHeadersForWindow(windowIndex, &request->headers);

          if (!request->cdpRequestWillBeSent.exchange(true)) {
            String postData = "";
            if (request->body.size() > 0 && request->body.size() <= MAX_CDP_SCHEME_POSTDATA_BYTES) {
              postData = request->body.str();
              runtime->services.cdp.storeNativeNetworkRequestPostData(request->cdpRequestId, postData);
            }

            auto requestHeaders = cdpHeadersToJSON(request->headers);
            auto req = JSON::Object::Entries {
              {"url", request->str()},
              {"method", request->method},
              {"headers", requestHeaders}
            };
            if (!postData.empty()) {
              req.insert_or_assign("postData", postData);
            }

            const auto type = request->cdpResourceType.size() ? request->cdpResourceType : String("Other");
            runtime->services.cdp.onNetworkEvent(windowIndex, JSON::Object::Entries {
              {"method", "Network.requestWillBeSent"},
              {"params", JSON::Object::Entries {
                {"requestId", request->cdpRequestId},
                {"type", type},
                {"request", req}
              }}
            });
          }
        }
      }
    }

    const auto handler = this->getHandlerForScheme(request->scheme);
    const auto id = request->id;

    // fail if there is somehow not a handler for this request scheme
    if (handler == nullptr) {
      return false;
    }

    do {
      Lock lock(this->mutex);
      this->activeRequests.emplace(request->id, request);
    } while (0);

    if (request->error != nullptr) {
      auto response = webview::SchemeHandlers::Response(request, 500);
      response.fail(request->error);
      do {
        Lock lock(this->mutex);
        this->activeRequests.erase(id);
      } while (0);
      return true;
    }

    auto span = request->tracer.span("handler");

    auto alive = this->internals != nullptr ? this->internals->alive : nullptr;
    auto complete = [this, id, span, request, callback, alive](SchemeHandlers::Response& response) mutable {
      if (alive == nullptr || !alive->load(std::memory_order_acquire)) {
        return;
      }

      span->end();

      // notify finished
      if (request->callbacks.finish != nullptr) {
        request->callbacks.finish();
      }

      // make sure the response was finished before
      // calling the `callback` function below
      response.finish();

      do {
        Lock lock(this->mutex);
        this->activeRequests.erase(id);
      } while (0);

      if (callback != nullptr) {
        callback(response);
      }
    };

    // Network.setBlockedURLs applies to runtime-handled scheme requests.
    {
      auto runtime = this->bridge.getRuntime();
      if (
        runtime &&
        request->cdpWindowIndex >= 0 &&
        runtime->services.cdp.shouldBlockURLForWindow(request->cdpWindowIndex, request->str())
      ) {
        auto response = webview::SchemeHandlers::Response(request, 403);
        response.fail("BlockedByClient");
        complete(response);
        return true;
      }
    }

    // Fetch.* interception for runtime-handled requests (SchemeHandlers).
    {
      auto runtime = this->bridge.getRuntime();
      if (
        runtime &&
        request->cdpWindowIndex >= 0 &&
        !request->cdpRequestId.empty() &&
        runtime->services.cdp.shouldPauseFetchRequest(request->cdpWindowIndex, request->str(), request->cdpResourceType)
      ) {
        const int windowIndex = request->cdpWindowIndex;
        const auto type = request->cdpResourceType.size() ? request->cdpResourceType : String("Other");

        const auto requestHeaders = cdpHeadersToJSON(request->headers);
        String postData = "";
        if (request->body.size() > 0 && request->body.size() <= MAX_CDP_SCHEME_POSTDATA_BYTES) {
          postData = request->body.str();
        }

        auto pausedRequest = JSON::Object::Entries {
          {"url", request->str()},
          {"method", request->method},
          {"headers", requestHeaders}
        };

        if (!postData.empty()) {
          pausedRequest.insert_or_assign("postData", postData);
        }

        const bool registered = runtime->services.cdp.registerFetchRequest(
          windowIndex,
          request->cdpRequestId,
          [this, handler, request, complete, windowIndex, alive](const JSON::Any& decision) mutable {
            if (alive == nullptr || !alive->load(std::memory_order_acquire)) {
              return;
            }

            this->bridge.dispatch([=, this]() mutable {
              if (alive == nullptr || !alive->load(std::memory_order_acquire)) {
                return;
              }

              if (request == nullptr || !request->isActive() || request->isCancelled()) {
                return;
              }

              auto getString = [](const JSON::Any& objAny, const char* key) -> String {
                if (!objAny.isObject()) return "";
                const auto& v = objAny.as<JSON::Object>().get(key);
                return v.isString() ? v.as<JSON::String>().value() : "";
              };

              auto getObject = [](const JSON::Any& objAny, const char* key) -> std::optional<JSON::Object> {
                if (!objAny.isObject()) return std::nullopt;
                const auto& v = objAny.as<JSON::Object>().get(key);
                if (!v.isObject()) return std::nullopt;
                return v.as<JSON::Object>();
              };

              const auto action = getString(decision, "action");

              if (action == "fail") {
                const auto reason = getString(decision, "errorReason");
                auto response = webview::SchemeHandlers::Response(request, 500);
                response.fail(reason.size() ? reason : String("Failed"));
                complete(response);
                return;
              }

              if (action == "fulfill") {
                int code = 200;
                if (decision.isObject()) {
                  const auto& v = decision.as<JSON::Object>().get("responseCode");
                  if (v.isNumber()) {
                    code = static_cast<int>(v.as<JSON::Number>().value());
                  } else if (v.isString()) {
                    try { code = std::stoi(v.as<JSON::String>().value()); } catch (...) {}
                  }
                }

                auto response = webview::SchemeHandlers::Response(request, code);

                if (const auto headersObjOpt = getObject(decision, "responseHeaders"); headersObjOpt.has_value()) {
                  for (const auto& kv : headersObjOpt.value().value()) {
                    if (kv.second.isString()) {
                      response.setHeader(kv.first, kv.second.as<JSON::String>().value());
                    }
                  }
                }

                const auto body = getString(decision, "body");
                if (!body.empty()) {
                  const auto decoded = bytes::base64::decode(body);
                  if (decoded.size() > 0) {
                    response.write(decoded.size(), reinterpret_cast<const unsigned char*>(decoded.data()));
                  }
                }

                complete(response);
                return;
              }

              // Continue (default): optionally apply overrides and run the handler.
              if (decision.isObject()) {
                const auto url = getString(decision, "url");
                if (!url.empty()) {
                  const auto parsed = URL::Components::parse(url);
                  if (parsed.scheme.size() > 0 && parsed.scheme == request->scheme) {
                    request->originalURL = url;
                    request->hostname = parsed.authority;
                    request->pathname = parsed.pathname;
                    request->query = parsed.query;
                    request->fragment = parsed.fragment;
                    request->origin = request->scheme + "://" + request->hostname;
                    request->params.clear();
                    for (const auto& entry : split(request->query, '&')) {
                      const auto parts = split(entry, '=');
                      if (parts.size() == 2) {
                        const auto key = decodeURIComponent(trim(parts[0]));
                        const auto value = decodeURIComponent(trim(parts[1]));
                        request->params.insert_or_assign(key, value);
                      }
                    }
                  }
                }

                const auto newMethod = getString(decision, "method");
                if (!newMethod.empty()) {
                  request->method = newMethod;
                }

                if (const auto headersObjOpt = getObject(decision, "headers"); headersObjOpt.has_value()) {
                  request->headers.clear();
                  for (const auto& kv : headersObjOpt.value().value()) {
                    if (kv.second.isString()) {
                      request->headers.set(kv.first, kv.second.as<JSON::String>().value());
                    }
                  }
                }

                const auto newPostData = getString(decision, "postData");
                if (!newPostData.empty() && newPostData.size() <= MAX_CDP_SCHEME_POSTDATA_BYTES) {
                  request->body = bytes::Buffer::from(newPostData);
                  if (auto runtime = this->bridge.getRuntime()) {
                    runtime->services.cdp.storeNativeNetworkRequestPostData(request->cdpRequestId, newPostData);
                  }
                }

                // Re-apply Network.setExtraHTTPHeaders after any header overrides so
                // interception doesn't accidentally drop them.
                if (auto runtime = this->bridge.getRuntime()) {
                  runtime->services.cdp.applyExtraHTTPHeadersForWindow(windowIndex, &request->headers);
                }
              }

              handler(request, this->bridge, &request->callbacks, complete);
            });
          }
        );

        if (registered) {
          runtime->services.cdp.onNetworkEvent(windowIndex, JSON::Object::Entries {
            {"method", "Fetch.requestPaused"},
            {"params", JSON::Object::Entries {
              {"requestId", request->cdpRequestId},
              {"networkId", request->cdpRequestId},
              {"request", pausedRequest},
              {"resourceType", type},
              {"requestStage", "Request"}
            }}
          });
          return true;
        }
      }
    }

  #if ORO_RUNTIME_PLATFORM_ANDROID || ORO_RUNTIME_PLATFORM_LINUX
    this->bridge.dispatch([=, this]() mutable {
  #endif
      if (request != nullptr && request->isActive() && !request->isCancelled()) {
        handler(request, this->bridge, &request->callbacks, complete);
      }
  #if ORO_RUNTIME_PLATFORM_ANDROID || ORO_RUNTIME_PLATFORM_LINUX
    });
  #endif

    return true;
  }

  bool SchemeHandlers::isRequestActive (uint64_t id) {
    if (!this->isAlive()) {
      return false;
    }

    Lock lock(this->mutex);
    return this->activeRequests.contains(id);
  }

  bool SchemeHandlers::isRequestCancelled (uint64_t id) {
    if (!this->isAlive()) {
      return true;
    }

    Lock lock(this->mutex);
    return (
      id > 0 &&
      this->activeRequests.contains(id) &&
      this->activeRequests.at(id) != nullptr &&
      this->activeRequests.at(id)->cancelled
    );
  }

  SchemeHandlers::Request::Builder::Builder (
  #if ORO_RUNTIME_PLATFORM_WINDOWS
    SchemeHandlers* handlers,
    PlatformRequest platformRequest,
    ICoreWebView2Environment* env
  #else
    SchemeHandlers* handlers,
    PlatformRequest platformRequest
  #endif
  ) {
    const auto userConfig = handlers->bridge.userConfig;
    const auto bundleIdentifier = userConfig.contains("meta_bundle_identifier")
      ? userConfig.at("meta_bundle_identifier")
      : "";

  #if ORO_RUNTIME_PLATFORM_APPLE
    if (
      platformRequest != nullptr &&
      platformRequest.request != nullptr &&
      platformRequest.request.URL != nullptr
    ) {
      this->absoluteURL = stringFromNSString(platformRequest.request.URL.absoluteString);
    }
  #elif ORO_RUNTIME_PLATFORM_LINUX
    this->absoluteURL = webkit_uri_scheme_request_get_uri(platformRequest);
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    LPWSTR requestURI;
    platformRequest->get_Uri(&requestURI);
    this->absoluteURL = convertWStringToString(requestURI);
    auto normalizeRuntimeURL = [this, &bundleIdentifier] (const String& schemePrefix) {
      if (
        this->absoluteURL.starts_with(schemePrefix) &&
        !scheme::matchesBundleURL(this->absoluteURL, bundleIdentifier)
      ) {
        const auto offset = schemePrefix.size() > 0
          ? schemePrefix.size() - 1
          : 0;
        this->absoluteURL = schemePrefix + this->absoluteURL.substr(offset);
        if (this->absoluteURL.ends_with("/")) {
          this->absoluteURL = this->absoluteURL.substr(0, this->absoluteURL.size() - 1);
        }
      }
    };

    normalizeRuntimeURL("oro://");
    CoTaskMemFree(requestURI);
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    auto app = app::App::sharedApplication();
    auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);
    this->absoluteURL = android::StringWrap(attachment.env, (jstring) CallClassMethodFromAndroidEnvironment(
      attachment.env,
      Object,
      platformRequest,
      "getUrl",
      "()Ljava/lang/String;"
    )).str();
  #endif

    const auto url = URL::Components::parse(this->absoluteURL);

    this->request = std::make_shared<Request>(
      handlers,
      platformRequest,
      Request::Options {
        .scheme = url.scheme
      }
    );

  #if ORO_RUNTIME_PLATFORM_WINDOWS
    this->request->env = env;
  #endif

    this->request->client = handlers->bridge.client;

    // build request URL components from parsed URL components
    this->request->originalURL = this->absoluteURL;
    this->request->hostname = url.authority;
    this->request->pathname = url.pathname;
    this->request->query = url.query;
    this->request->fragment = url.fragment;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setScheme (const String& scheme) {
    this->request->scheme = scheme;
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setMethod (const String& method) {
    this->request->method = method;
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setHostname (const String& hostname) {
    this->request->hostname = hostname;
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setPathname (const String& pathname) {
    this->request->pathname = pathname;
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setQuery (const String& query) {
    this->request->query = query;
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setFragment (const String& fragment) {
    this->request->fragment = fragment;
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setHeader (
    const String& name,
    const Headers::Value& value
  ) {
    this->request->headers.set(name, value.string);
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setHeaders (const Headers& headers) {
    for (const auto& entry : headers) {
      this->request->headers.set(entry);
    }
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setHeaders (
    const Map<String, String>& headers
  ) {
    for (const auto& entry : headers) {
      this->request->headers.set(entry.first, entry.second);
    }
    return *this;
  }

#if ORO_RUNTIME_PLATFORM_APPLE
  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setHeaders (
    const NSDictionary<NSString*, NSString*>* headers
  ) {
    if (headers == nullptr) {
      return *this;
    }

    for (NSString* key in headers) {
      const auto value = [headers objectForKey: key];
      const auto keyString = stringFromNSString(key);
      const auto valueString = stringFromNSString(value);
      if (keyString.size() > 0) {
        this->request->headers.set(keyString, valueString);
      }
    }

    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setBody (const NSData* data) {
    if (data != nullptr && data.length > 0 && data.bytes != nullptr) {
      return this->setBody(data.length, reinterpret_cast<const unsigned char*>(data.bytes));
    }
    return *this;
  }
#elif ORO_RUNTIME_PLATFORM_LINUX
  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setHeaders (
    const SoupMessageHeaders* headers
  ) {
    if (headers) {
      soup_message_headers_foreach(
        const_cast<SoupMessageHeaders*>(headers),
        [](const char* name, const char* value, gpointer userData) {
          auto request = reinterpret_cast<SchemeHandlers::Request*>(userData);
          request->headers.set(name, value);
        },
        this->request.get()
      );
    }

    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setBody (
    GInputStream* stream
  ) {
    if (stream == nullptr) {
      return *this;
    }

    if (
      this->request->method == "POST" ||
      this->request->method == "PUT" ||
      this->request->method == "PATCH"
    ) {
      GError* error = nullptr;
      unsigned char tmp[MAX_URI_SCHEME_REQUEST_BODY_BYTES] = {0};
      size_t size = 0;
      const auto success = g_input_stream_read_all(
        stream,
        reinterpret_cast<gchar*>(tmp),
        MAX_URI_SCHEME_REQUEST_BODY_BYTES,
        &size,
        nullptr,
        &this->error
      );
      this->request->body = bytes::Buffer::from(tmp, size);
    }
    return *this;
  }
#endif

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setBody (const bytes::Buffer& body) {
    if (
      this->request->method == "POST" ||
      this->request->method == "PUT" ||
      this->request->method == "PATCH"
    ) {
      this->request->body = body;
    }
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setBody (size_t size, const unsigned char* bytes) {
    if (this->request->method == "POST" || this->request->method == "PUT" || this->request->method == "PATCH") {
      if (size > 0 && bytes != nullptr) {
        this->request->body = bytes::Buffer::from(bytes, size);
      }
    }
    return *this;
  }

  SchemeHandlers::Request::Builder& SchemeHandlers::Request::Builder::setCallbacks (const RequestCallbacks& callbacks) {
    this->request->callbacks = callbacks;
    return *this;
  }

  SharedPointer<SchemeHandlers::Request> SchemeHandlers::Request::Builder::build () {
    this->request->error = this->error;
    // Normalize runtime scheme URLs that may be emitted by platform URL parsers
    // in non-canonical forms.
    //
    // - Runtime bundle/document URLs should be hostful: `oro://<bundle>/<path>`
    // - Runtime module specifiers should be hostless: `oro:<module>/<path>`
    //
    // Some engines surface hostless module specifiers as `oro://internal/...`
    // (treating the first path segment as authority). Some also surface
    // hostless bundle URLs as `oro:/<path>` or `oro:///...` (missing authority).
    //
    // We use fetch metadata headers to decide whether to coerce a URL into a
    // bundle URL (document-ish) or a module specifier (script-ish).
    if (this->request && scheme::isRuntimeScheme(this->request->scheme)) {
      const auto& userConfig = this->request->handlers->bridge.userConfig;
      const auto bundleIdentifier = userConfig.contains("meta_bundle_identifier")
        ? userConfig.at("meta_bundle_identifier")
        : "";
      const auto globalConfig = getUserConfig();
      const auto globalBundleIdentifier = globalConfig.contains("meta_bundle_identifier")
        ? globalConfig.at("meta_bundle_identifier")
        : "";

      if (!bundleIdentifier.empty() || !globalBundleIdentifier.empty()) {
        const auto loweredBundleIdentifier = toLowerCase(bundleIdentifier);
        const auto loweredGlobalBundleIdentifier = toLowerCase(globalBundleIdentifier);
        const auto fetchDest = toLowerCase(trim(this->request->headers.get("sec-fetch-dest").value.str()));
        const auto accept = toLowerCase(trim(this->request->headers.get("accept").value.str()));

        bool isDocumentLike = (
          fetchDest == "document" ||
          fetchDest == "iframe" ||
          fetchDest == "frame"
        );

        if (!isDocumentLike) {
          if (
            accept.find("text/html") != String::npos ||
            accept.find("application/xhtml+xml") != String::npos
          ) {
            isDocumentLike = true;
          }
        }

        if (!isDocumentLike) {
          const auto pathnameLower = toLowerCase(this->request->pathname);
          if (
            pathnameLower == "/" ||
            pathnameLower.ends_with(".html") ||
            pathnameLower.ends_with(".htm") ||
            pathnameLower.ends_with(".xhtml") ||
            pathnameLower.ends_with(".xht")
          ) {
            isDocumentLike = true;
          }
        }

        auto hostnameMatchesBundle = [&] (const String& hostname) -> bool {
          if (hostname.empty()) {
            return false;
          }

          const auto loweredHostname = toLowerCase(hostname);
          if (!loweredBundleIdentifier.empty() && loweredHostname == loweredBundleIdentifier) {
            return true;
          }
          if (!loweredGlobalBundleIdentifier.empty() && loweredHostname == loweredGlobalBundleIdentifier) {
            return true;
          }
          return false;
        };

        // 1) Convert `oro://internal/globals` -> `oro:internal/globals` when the
        // authority is not the app bundle.
        //
        // Some engines (notably WebView2) surface hostless module specifiers as
        // hostful URLs by treating the first segment as authority. This can also
        // happen for module imports executed via `ExecuteScript`, where fetch
        // metadata headers may not be reliable.
        if (
          this->request->hostname.size() > 0 &&
          !hostnameMatchesBundle(this->request->hostname)
        ) {
          String hostlessPath = "/" + this->request->hostname;
          if (this->request->pathname.size() == 0) {
            hostlessPath += "/";
          } else if (this->request->pathname.starts_with("/")) {
            hostlessPath += this->request->pathname;
          } else {
            hostlessPath += "/" + this->request->pathname;
          }

          this->absoluteURL = this->request->scheme + ":" + hostlessPath.substr(1);
          if (this->request->query.size() > 0) {
            this->absoluteURL += "?" + this->request->query;
          }
          if (this->request->fragment.size() > 0) {
            this->absoluteURL += "#" + this->request->fragment;
          }
          const auto components = URL::Components::parse(this->absoluteURL);
          this->request->originalURL = this->absoluteURL;
          this->request->hostname = components.authority;
          this->request->pathname = components.pathname;
          this->request->query = components.query;
          this->request->fragment = components.fragment;
        }

        // 2) Convert `oro:/index.html` or `oro:///index.html` -> `oro://<bundle>/index.html`
        // for document-like requests missing an authority.
        if (isDocumentLike && this->request->hostname.empty()) {
          const auto defaultBundleIdentifier = !bundleIdentifier.empty()
            ? bundleIdentifier
            : globalBundleIdentifier;

          if (defaultBundleIdentifier.empty()) {
            this->request->finalize();
            return std::move(this->request);
          }

          auto selectedBundleIdentifier = defaultBundleIdentifier;
          auto rest = this->request->pathname;

          auto stripBundlePrefix = [&rest] (const String& id) -> bool {
            const auto prefix = "/" + id;
            if (!rest.starts_with(prefix)) {
              return false;
            }

            const auto n = prefix.size();
            if (rest.size() == n) {
              rest = "/";
              return true;
            }

            if (rest[n] == '/') {
              rest = rest.substr(n);
              if (rest.empty()) {
                rest = "/";
              }
              return true;
            }

            return false;
          };

          if (
            !bundleIdentifier.empty() &&
            (stripBundlePrefix(bundleIdentifier) || stripBundlePrefix(loweredBundleIdentifier))
          ) {
            selectedBundleIdentifier = bundleIdentifier;
          } else if (
            !globalBundleIdentifier.empty() &&
            (stripBundlePrefix(globalBundleIdentifier) || stripBundlePrefix(loweredGlobalBundleIdentifier))
          ) {
            selectedBundleIdentifier = globalBundleIdentifier;
          }

          if (rest.empty()) {
            rest = "/";
          } else if (!rest.starts_with("/")) {
            rest = "/" + rest;
          }

          this->absoluteURL = this->request->scheme + "://" + selectedBundleIdentifier + rest;
          if (this->request->query.size() > 0) {
            this->absoluteURL += "?" + this->request->query;
          }
          if (this->request->fragment.size() > 0) {
            this->absoluteURL += "#" + this->request->fragment;
          }

          const auto components = URL::Components::parse(this->absoluteURL);
          this->request->originalURL = this->absoluteURL;
          this->request->hostname = components.authority;
          this->request->pathname = components.pathname;
          this->request->query = components.query;
          this->request->fragment = components.fragment;
        }
      }
    }
    this->request->finalize();
    return std::move(this->request);
  }

  SchemeHandlers::Request::Request (
    SchemeHandlers* handlers,
    PlatformRequest platformRequest,
    const Options& options
  )
    : handlers(handlers),
      scheme(options.scheme),
      method(options.method),
      hostname(options.hostname),
      pathname(options.pathname),
      query(options.query),
      fragment(options.fragment),
      headers(options.headers),
      tracer("webview::SchemeHandlers::Request") {
    this->platformRequest = platformRequest;
    if (this->platformRequest) {
    #if ORO_RUNTIME_PLATFORM_LINUX
      g_object_ref(this->platformRequest);
    #endif
    }
  }

  SchemeHandlers::Request::~Request () {
    if (this->platformRequest) {
    #if ORO_RUNTIME_PLATFORM_LINUX
      g_object_unref(this->platformRequest);
    #endif
    }
  }

  bool SchemeHandlers::Request::hasHeader (const String& name) const {
    if (this->headers.has(name)) {
      return true;
    }

    return false;
  }

  const String SchemeHandlers::Request::getHeader (const String& name) const {
    return this->headers.get(name).value.string;
  }

  const String SchemeHandlers::Request::url () const {
    return this->str();
  }

  const String SchemeHandlers::Request::str () const {
    if (this->hostname.size() > 0) {
      return trim(
        this->scheme +
        "://" +
        this->hostname +
        this->pathname +
        (this->query.size() ? "?" + this->query : "") +
        (this->fragment.size() ? "#" + this->fragment : "")
      );
    }

    return trim(
      this->scheme +
      ":" +
      this->pathname.substr(1) +
      (this->query.size() ? "?" + this->query : "") +
      (this->fragment.size() ? "#" + this->fragment : "")
    );
  }

  bool SchemeHandlers::Request::finalize () {
    if (this->finalized) {
      return false;
    }

    if (this->hasHeader("runtime-client-id")) {
      try {
        this->client.id = std::stoull(this->getHeader("runtime-client-id"));
      } catch (...) {}
    }

    for (const auto& entry : split(this->query, '&')) {
      const auto parts = split(entry, '=');
      if (parts.size() == 2) {
        const auto key = decodeURIComponent(trim(parts[0]));
        const auto value = decodeURIComponent(trim(parts[1]));
        this->params.insert_or_assign(key, value);
      }
    }

    this->finalized = true;
    this->origin = this->scheme + "://" + this->hostname;
    return true;
  }

  bool SchemeHandlers::Request::isActive () const {
    auto app = app::App::sharedApplication();
    if (this->handlers == nullptr) {
      return false;
    }

    if (app == nullptr) {
      return false;
    }

    auto window = app->runtime.windowManager.getWindowForBridge(&this->handlers->bridge);

    // only a scheme handler owned by this bridge and attached to a
    // window should be considered "active"
    // scheme handlers SHOULD only work windows that have a navigator
    if (window != nullptr && this->handlers != nullptr) {
      return this->handlers->isRequestActive(this->id);
    }

    return false;
  }

  bool SchemeHandlers::Request::isCancelled () const {
    auto app = app::App::sharedApplication();
    if (app == nullptr || this->handlers == nullptr) {
      return true;
    }

    auto window = app->runtime.windowManager.getWindowForBridge(&this->handlers->bridge);

    if (window != nullptr && this->handlers != nullptr) {
      return this->handlers->isRequestCancelled(this->id);
    }

    return true;
  }

  JSON::Object SchemeHandlers::Request::json () const {
    return JSON::Object::Entries {
      {"scheme", this->scheme},
      {"method", this->method},
      {"hostname", this->hostname},
      {"pathname", this->pathname},
      {"query", this->query},
      {"fragment", this->fragment},
      {"headers", this->headers.json()},
      {"client", JSON::Object::Entries {
        {"id", this->client.id}
      }}
    };
  }

  SchemeHandlers::Response::Response (
    SharedPointer<Request> request,
    int statusCode,
    const Headers headers
  ) : request(request),
      handlers(request->handlers),
      client(request->client),
      id(request->id),
      tracer("webview::SchemeHandlers::Response") {
    const auto defaultHeaders = split(
      this->request->handlers->bridge.userConfig.contains("webview_headers")
        ? this->request->handlers->bridge.userConfig.at("webview_headers")
        : "",
      '\n'
    );

    if (isDebugEnabled()) {
      this->setHeader("cache-control", "no-cache");
    }

    // Baseline security hardening headers
    this->setHeader("connection", "keep-alive");
    this->setHeader("x-content-type-options", "nosniff");

    // Optional CSP and referrer policy from config
    const auto& userConfig = this->request->handlers->bridge.userConfig;
    if (userConfig.contains("webview_csp") && userConfig.at("webview_csp").size() > 0) {
      this->setHeader("content-security-policy", userConfig.at("webview_csp"));
    }
    if (userConfig.contains("webview_referrer_policy") && userConfig.at("webview_referrer_policy").size() > 0) {
      this->setHeader("referrer-policy", userConfig.at("webview_referrer_policy"));
    }

    // Configurable CORS posture; default remains permissive for app UX.
    const auto corsAllowAll = (
      userConfig.contains("webview_cors_allow_all")
        ? (userConfig.at("webview_cors_allow_all") != "false")
        : true
    );
    const auto corsAllowCredentials = (
      userConfig.contains("webview_cors_allow_credentials")
        ? (userConfig.at("webview_cors_allow_credentials") == "true")
        : true
    );
    const auto corsAllowHeaders = (
      userConfig.contains("webview_cors_allow_headers")
        ? userConfig.at("webview_cors_allow_headers")
        : String("*")
    );
    const auto corsAllowMethods = (
      userConfig.contains("webview_cors_allow_methods")
        ? userConfig.at("webview_cors_allow_methods")
        : String("GET, POST, PATCH, PUT, DELETE, HEAD, OPTIONS")
    );

    if (corsAllowAll) {
      this->setHeader("access-control-allow-origin", "*");
      this->setHeader("access-control-allow-headers", corsAllowHeaders);
      this->setHeader("access-control-allow-methods", corsAllowMethods);
      this->setHeader("access-control-allow-credentials", corsAllowCredentials ? "true" : "false");
    } else {
      // If allow-all is disabled, reflect Origin when present and allowlisted
      const auto originHeader = this->request->headers.get("origin");
      const auto origins = userConfig.contains("webview_cors_allowed_origins")
        ? split(trim(userConfig.at("webview_cors_allowed_origins")), ' ')
        : Vector<String>{};
      if (originHeader.size() > 0 && origins.size() > 0) {
        const auto o = originHeader.value.str();
        if (std::find(origins.begin(), origins.end(), o) != origins.end()) {
          this->setHeader("access-control-allow-origin", o);
          this->setHeader("vary", "Origin");
          this->setHeader("access-control-allow-headers", corsAllowHeaders);
          this->setHeader("access-control-allow-methods", corsAllowMethods);
          this->setHeader("access-control-allow-credentials", corsAllowCredentials ? "true" : "false");
        }
      }
    }

    if (request->method == "OPTIONS") {
      this->setHeader("allow", "GET, POST, PATCH, PUT, DELETE, HEAD");
    }

    for (const auto& entry : defaultHeaders) {
      const auto parts = split(trim(entry), ':');
      this->setHeader(parts[0], parts[1]);
    }
  }

  bool SchemeHandlers::Response::writeHead (int statusCode, const Headers headers) {
    Lock lock(this->mutex);
    // fail if already finished
    if (this->finished) {
      debug("SchemeHandlers::Response: Failed to write head. Already finished");
      return false;
    }

    if (
      !this->handlers->isRequestActive(this->id) ||
      this->handlers->isRequestCancelled(this->id)
    ) {
      return false;
    }

    // fail if head of response is already created
    if (this->platformResponse != nullptr) {
      debug("SchemeHandlers::Response: Failed to write head as it was already written");
      return false;
    }

    if (this->request->platformRequest == nullptr) {
      debug("SchemeHandlers::Response: Failed to write head. Request is in an invalid state");
      return false;
    }

    if (statusCode >= 100 && statusCode < 600) {
      this->statusCode = statusCode;
    }

    for (const auto& header : headers) {
      this->setHeader(header);
    }

  #if ORO_RUNTIME_PLATFORM_APPLE || ORO_RUNTIME_PLATFORM_LINUX || ORO_RUNTIME_PLATFORM_ANDROID
    // webkit status codes cannot be in the range of 300 >= statusCode < 400
    if (this->statusCode >= 300 && this->statusCode < 400) {
      this->statusCode = 200;
    }
  #endif

  #if ORO_RUNTIME_PLATFORM_APPLE
    auto headerFields = [NSMutableDictionary dictionary];
    for (const auto& entry : this->headers) {
      headerFields[@(entry.name.c_str())] = @(entry.value.c_str());
    }

    if (
      !this->handlers->isRequestActive(this->id) ||
      this->handlers->isRequestCancelled(this->id)
    ) {
      return false;
    }

    auto platformRequest = this->request->platformRequest;
    if (platformRequest != nullptr && platformRequest.request != nullptr) {
      const auto url = platformRequest.request.URL;
      if (url != nullptr) {
        @try {
          this->platformResponse = [[NSHTTPURLResponse alloc]
            initWithURL: platformRequest.request.URL
             statusCode: this->statusCode
            HTTPVersion: @"HTTP/1.1"
           headerFields: headerFields
          ];
        } @catch (::id) {
          return false;
        }

        [platformRequest didReceiveResponse: this->platformResponse];
        cdpEmitResponseReceived(this);
        return true;
      }
    }
  #elif ORO_RUNTIME_PLATFORM_LINUX
    // Determine streaming mode
    const auto te = toLowerCase(this->headers.get("transfer-encoding").value.string);
    const auto ct = toLowerCase(this->headers.get("content-type").value.string);
    const bool isChunked = te == "chunked";
    const bool isSSE = ct.find("text/event-stream") != String::npos;

    gint64 size = -1;
    if (!isChunked && !isSSE) {
      const auto contentLength = this->getHeader("content-length");
      if (contentLength.size() > 0) {
        try { size = std::stol(contentLength); } catch (...) {}
      }
    }

    if (this->platformResponseStream == nullptr) {
      if (isChunked || isSSE) {
        int fds[2] = {-1, -1};
        if (pipe(fds) == 0) {
          this->platformResponseStream = G_INPUT_STREAM(g_unix_input_stream_new(fds[0], TRUE));
          this->platformResponseOutput = G_OUTPUT_STREAM(g_unix_output_stream_new(fds[1], TRUE));
          size = -1; // unknown length for streaming
        } else {
          // fallback to memory stream if pipe fails
          this->platformResponseStream = g_memory_input_stream_new();
        }
      } else {
        this->platformResponseStream = g_memory_input_stream_new();
      }
    }

    this->platformResponse = webkit_uri_scheme_response_new(this->platformResponseStream, size);

    auto requestHeaders = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
    for (const auto& entry : this->headers) {
      soup_message_headers_append(requestHeaders, entry.name.c_str(), entry.value.c_str());
    }

    webkit_uri_scheme_response_set_http_headers(this->platformResponse, requestHeaders);

    if (this->hasHeader("content-type")) {
      const auto contentType = this->getHeader("content-type");
      if (contentType.size() > 0) {
        webkit_uri_scheme_response_set_content_type(this->platformResponse, contentType.c_str());
      }
    }

    const auto statusText = http::getStatusText(this->statusCode);

    webkit_uri_scheme_response_set_status(
      this->platformResponse,
      this->statusCode,
      statusText.size() > 0 ? statusText.c_str() : nullptr
    );

    cdpEmitResponseReceived(this);
    return true;
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    const auto statusText = http::getStatusText(this->statusCode);
    this->platformResponseStream = SHCreateMemStream(nullptr, 0);
    const auto result = this->request->env->CreateWebResourceResponse(
      this->platformResponseStream,
      this->statusCode,
      convertStringToWString(statusText).c_str(),
      convertStringToWString(this->headers.str()).c_str(),
      &this->platformResponse
    );

    if (result == S_OK) {
      cdpEmitResponseReceived(this);
    }
    return result == S_OK;
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    const auto app = app::App::sharedApplication();
    const auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);

    this->platformResponse = attachment.env->NewGlobalRef(
      CallClassMethodFromAndroidEnvironment(
        attachment.env,
        Object,
        this->request->platformRequest,
        "getResponse",
        "()Loro/runtime/webview/SchemeHandlers$Response;"
      )
    );

    for (const auto& header : this->headers) {
      const auto name = attachment.env->NewStringUTF(toHeaderCase(header.name).c_str());
      const auto value = attachment.env->NewStringUTF(header.value.c_str());

      CallVoidClassMethodFromAndroidEnvironment(
        attachment.env,
        this->platformResponse,
        "setHeader",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        name,
        value
      );

      attachment.env->DeleteLocalRef(name);
      attachment.env->DeleteLocalRef(value);
    }

    const auto statusText = attachment.env->NewStringUTF(http::getStatusText(this->statusCode).c_str());

    CallVoidClassMethodFromAndroidEnvironment(
      attachment.env,
      this->platformResponse,
      "setStatus",
      "(ILjava/lang/String;)V",
      this->statusCode,
      statusText
    );

    attachment.env->DeleteLocalRef(statusText);

    cdpEmitResponseReceived(this);
    return true;
  #endif
    return false;
  }

  bool SchemeHandlers::Response::write (const bytes::Buffer& buffer) {
    return this->write(buffer.size(), buffer.shared());
  }

  bool SchemeHandlers::Response::write (
    size_t size,
    SharedPointer<unsigned char[]> bytes
  ) {
    if (
      !this->handlers->isRequestActive(this->id) ||
      this->handlers->isRequestCancelled(this->id)
    ) {
      debug("SchemeHandlers::Response: Write attemped for request that is no longer active or cancelled");
      return false;
    }

    if (!this->hasHeader("content-type")) {
      this->setHeader("content-type", "application/octet-stream");
    }

    do {
      Lock lock(this->mutex);
      if (!this->platformResponse) {
        // For streaming responses (chunked or SSE), do not set content-length
        const auto ct = toLowerCase(this->getHeader("content-type"));
        const bool isSSE = ct.find("text/event-stream") != String::npos;
        const bool isChunked = toLowerCase(this->getHeader("transfer-encoding")) == "chunked";
        if (!isSSE && !isChunked) {
          // set 'content-length' header if response was not created
          this->setHeader("content-length", size);
        }
        if (!this->writeHead()) {
          debug(
            "SchemeHandlers::Response: Failed to write head for %s",
            this->request->str().c_str()
          );
          return false;
        }
      }
    } while (0);

    if (size > 0 && bytes != nullptr) {
      bool success = false;
      size_t writtenBytes = size;

      Lock lock(this->mutex);
    #if ORO_RUNTIME_PLATFORM_APPLE
      const auto data = [NSData dataWithBytes: bytes.get() length: size];
      @try {
        [this->request->platformRequest didReceiveData: data];
        success = true;
      } @catch (::id) {
        success = false;
      }
    #elif ORO_RUNTIME_PLATFORM_LINUX
      if (this->platformResponseOutput != nullptr) {
        GError* error = nullptr;
        gsize written = 0;
        const gboolean wroteAll = g_output_stream_write_all(
          this->platformResponseOutput,
          reinterpret_cast<const void*>(bytes.get()),
          (gsize) size,
          &written,
          nullptr,
          &error
        );
        if (!wroteAll || error != nullptr) {
          if (error) g_error_free(error);
          success = false;
        } else {
          success = written == (gsize) size;
          writtenBytes = (size_t) written;
        }
      } else {
        const auto tmp = new unsigned char[size]{0};
        memcpy(tmp, bytes.get(), size);
        g_memory_input_stream_add_data(
          reinterpret_cast<GMemoryInputStream*>(this->platformResponseStream),
          reinterpret_cast<const void*>(tmp),
          (gssize) size,
          [](auto p) {
            const auto tmp = reinterpret_cast<unsigned char*>(p);
            delete [] tmp;
          }
        );
        success = true;
      }
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      success = S_OK == this->platformResponseStream->Write(
        reinterpret_cast<const void*>(bytes.get()),
        (ULONG) size,
        nullptr
      );
      writtenBytes = success ? size : 0;
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      const auto app = app::App::sharedApplication();
      const auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);
      const auto byteArray = attachment.env->NewByteArray(size);

      attachment.env->SetByteArrayRegion(
        byteArray,
        0,
        size,
        (jbyte *) bytes.get()
      );

      CallVoidClassMethodFromAndroidEnvironment(
        attachment.env,
        this->platformResponse,
        "write",
        "([B)V",
        byteArray
      );

      if (byteArray != nullptr) {
        attachment.env->DeleteLocalRef(byteArray);
      }

      success = true;
    #endif

      if (success && writtenBytes > 0) {
        this->bytesWritten += writtenBytes;

        // Capture response bodies for CDP (bounded) so Network.getResponseBody works
        // for runtime-handled requests.
        if (!this->cdpBodyCaptureTruncated && this->request && cdpShouldInstrument(this->request)) {
          if (this->cdpBodyCaptureBytes < MAX_CDP_SCHEME_RESPONSE_BODY_BYTES) {
            const size_t remaining = MAX_CDP_SCHEME_RESPONSE_BODY_BYTES - this->cdpBodyCaptureBytes;
            const size_t take = writtenBytes <= remaining ? writtenBytes : remaining;

            if (take == writtenBytes) {
              this->cdpBodyCapture.push(bytes, take);
            } else if (take > 0) {
              auto partial = std::make_shared<unsigned char[]>(take);
              memcpy(partial.get(), bytes.get(), take);
              this->cdpBodyCapture.push(partial, take);
              this->cdpBodyCaptureTruncated = true;
            } else {
              this->cdpBodyCaptureTruncated = true;
            }

            this->cdpBodyCaptureBytes += take;
            if (writtenBytes > take) {
              this->cdpBodyCaptureTruncated = true;
            }
          } else {
            this->cdpBodyCaptureTruncated = true;
          }
        }

        cdpEmitDataReceived(this, writtenBytes);
      }

      return success;
    }

    return false;
  }

  bool SchemeHandlers::Response::write (const String& source) {
    const auto size = source.size();

    if (size == 0) {
      return false;
    }

    auto bytes = std::make_shared<unsigned char[]>(size);
    memset(bytes.get(), 0, size);
    memcpy(bytes.get(), source.data(), size);
    return this->write(size, bytes);
  }

  bool SchemeHandlers::Response::write (size_t size, const unsigned char* input) {
    if (size == 0) {
      return false;
    }

    auto bytes = std::make_shared<unsigned char[]>(size);
    memcpy(bytes.get(), input, size);
    return this->write(size, bytes);
  }

  bool SchemeHandlers::Response::write (const JSON::Any& json) {
    this->setHeader("content-type", "application/json");
    return this->write(json.str());
  }

  bool SchemeHandlers::Response::write (const filesystem::Resource& resource) {
    auto responseResource = filesystem::Resource(resource);
    auto app = app::App::sharedApplication();

    const auto contentLength = responseResource.size();
    const auto contentType = responseResource.mimeType();

    if (contentType.size() > 0 && !this->hasHeader("content-type")) {
      this->setHeader("content-type", contentType);
    }

    if (contentLength > 0) {
      this->setHeader("content-length", contentLength);
    }

    if (contentLength > 0) {
      this->writeHead();
      return this->write(contentLength, responseResource.read());
    }

    return false;
  }

  bool SchemeHandlers::Response::write (
    const filesystem::Resource::ReadStream::Buffer& buffer
  ) {
    return this->write(buffer.size, buffer.bytes);
  }

  bool SchemeHandlers::Response::send (const String& source) {
    return this->write(source);
  }

  bool SchemeHandlers::Response::send (const JSON::Any& json) {
    return this->write(json);
  }

  bool SchemeHandlers::Response::send (const filesystem::Resource& resource) {
    return this->write(resource) && this->finish();
  }

  bool SchemeHandlers::Response::finish () {
    // fail if already finished
    if (this->finished) {
      return false;
    }

    if (
      !this->handlers->isRequestActive(this->id) ||
      this->handlers->isRequestCancelled(this->id)
    ) {
      return false;
    }

    if (!this->platformResponse) {
      if (!this->writeHead() || !this->platformResponse) {
        return false;
      }
    }

    if (
      !this->handlers->isRequestActive(this->id) ||
      this->handlers->isRequestCancelled(this->id)
    ) {
      return false;
    }

    Lock lock(this->mutex);
  #if ORO_RUNTIME_PLATFORM_APPLE
    @try {
      [this->request->platformRequest didFinish];
    } @catch (::id) {}
  #if !__has_feature(objc_arc)
    [this->platformResponse release];
  #endif
    this->platformResponse = nullptr;
  #elif ORO_RUNTIME_PLATFORM_LINUX
    if (this->request && this->request->platformRequest && this->platformResponse) {
      webkit_uri_scheme_request_finish_with_response(
        this->request->platformRequest,
        this->platformResponse
      );

      this->request->platformRequest = nullptr;

      // Close streams
      if (this->platformResponseOutput != nullptr) {
        g_output_stream_close(this->platformResponseOutput, nullptr, nullptr);
        g_object_unref(this->platformResponseOutput);
        this->platformResponseOutput = nullptr;
      }
      if (this->platformResponseStream != nullptr) {
        g_input_stream_close(this->platformResponseStream, nullptr, nullptr);
        g_object_unref(this->platformResponseStream);
        this->platformResponseStream = nullptr;
      }
      this->platformResponse = nullptr;
    }
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    this->platformResponseStream = nullptr;
    // TODO(@jwerle): move more `WebResourceRequested` logic to here
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    if (this->platformResponse != nullptr) {
      auto app = app::App::sharedApplication();
      auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);

      CallVoidClassMethodFromAndroidEnvironment(
        attachment.env,
        this->platformResponse,
        "finish",
        "()V"
      );

      this->platformResponse = nullptr;
      attachment.env->DeleteGlobalRef(this->platformResponse);
    }
  #else
    this->platformResponse = nullptr;
  #endif

    // Persist captured body (bounded) for Network.getResponseBody and emit a
    // matching Network.loadingFinished.
    if (this->request && cdpShouldInstrument(this->request)) {
      const auto mimeType = this->headers.get("content-type").value.str();
      const bool looksText = cdpMimeTypeLooksText(mimeType);

      String body = "";
      bool base64Encoded = false;
      size_t bodyBytes = this->cdpBodyCaptureBytes;

      if (looksText) {
        body = this->cdpBodyCapture.str(bytes::Buffer::Encoding::UTF8);
        base64Encoded = false;
      } else {
        base64Encoded = true;
        if (this->cdpBodyCaptureTruncated) {
          body = "";
          bodyBytes = 0;
        } else {
          body = this->cdpBodyCapture.str(bytes::Buffer::Encoding::BASE64);
        }
      }

      auto runtime = this->request->handlers->bridge.getRuntime();
      if (runtime) {
        runtime->services.cdp.storeNativeNetworkResponseBody(
          this->request->cdpRequestId,
          body,
          base64Encoded,
          bodyBytes
        );
      }

      this->cdpBodyCapture.reset();
      this->cdpBodyCaptureBytes = 0;

      cdpEmitLoadingFinished(this);
    }

    this->finished = true;
    return true;
  }

  void SchemeHandlers::Response::setHeader (const String& name, const Headers::Value& value) {
    auto app = App::sharedApplication();
    const auto bridge = &this->request->handlers->bridge;
    if (toLowerCase(name) == "set-cookie") {
      const auto cookieValue = trim(value.string);
      if (!cookieValue.empty()) {
        if (
          !bridge->userConfig.contains("permissions_allow_cookies") ||
          bridge->userConfig.at("permissions_allow_cookies") != "false"
        ) {
          cookies::set(
            this->request->handlers->bridge,
            this->request->url(),
            cookieValue,
            [](bool, const auto&) {}
          );
        }
      }
    }
    if (toLowerCase(name) == "referer") {
      if (bridge->navigator.location.workers.contains(value.string)) {
        const auto workerLocation = bridge->navigator.location.workers[value.string];
        this->headers.set(name, workerLocation);
        return;
      } else {
        for (const auto& entry : app->runtime.serviceWorkerManager.servers) {
          if (entry.second->bridge->navigator.location.workers.contains(value.string)) {
            const auto workerLocation = entry.second->bridge->navigator.location.workers[value.string];
            this->headers.set(name, workerLocation);
            return;
          }
        }
      }
    }

    this->headers.set(name, value.string);
  }

  void SchemeHandlers::Response::setHeader (const Headers::Header& header) {
    this->setHeader(header.name, header.value.string);
  }

  void SchemeHandlers::Response::setHeader (const String& name, size_t value) {
    this->setHeader(name, std::to_string(value));
  }

  void SchemeHandlers::Response::setHeader (const String& name, int64_t value) {
    this->setHeader(name, std::to_string(value));
  }

#if !ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_PLATFORM_ANDROID && !ORO_RUNTIME_PLATFORM_WINDOWS
  void SchemeHandlers::Response::setHeader (const String& name, uint64_t value) {
    this->setHeader(name, std::to_string(value));
  }
#endif

  void SchemeHandlers::Response::setHeaders (const Headers& headers) {
    for (const auto& header : headers) {
      this->setHeader(header);
    }
  }

  const String SchemeHandlers::Response::getHeader (const String& name) const {
    return this->headers.get(name).value.string;
  }

  bool SchemeHandlers::Response::hasHeader (const String& name) const {
    return this->headers.has(name);
  }

  bool SchemeHandlers::Response::fail (const Error* error) {
  #if ORO_RUNTIME_PLATFORM_APPLE
    if (error != nullptr) {
      const auto description = stringFromNSString(error.localizedDescription);
      if (description.size() > 0) {
        return this->fail(description);
      }

      const auto reason = stringFromNSString(error.localizedFailureReason);
      if (reason.size() > 0) {
        return this->fail(reason);
      }
    }
  #elif ORO_RUNTIME_PLATFORM_LINUX
    if (error != nullptr && error->message != nullptr) {
      return this->fail(error->message);
    }
  #endif

    return this->fail("Request failed for an unknown reason");
  }

  bool SchemeHandlers::Response::fail (const String& reason) {
    const auto bundleIdentifier = this->request->handlers->bridge.userConfig.contains("meta_bundle_identifier")
      ? this->request->handlers->bridge.userConfig.at("meta_bundle_identifier")
      : "";

    if (
      this->finished ||
      !this->handlers->isRequestActive(this->id) ||
      this->handlers->isRequestCancelled(this->id)
    ) {
      return false;
    }

    cdpEmitLoadingFailed(this, reason, false);

  #if ORO_RUNTIME_PLATFORM_APPLE
    const auto error = [NSError
      errorWithDomain: @(bundleIdentifier.c_str())
                code: 1
            userInfo: @{NSLocalizedDescriptionKey: @(reason.c_str())}
    ];

    @try {
      [this->request->platformRequest didFailWithError: error];
    } @catch (::id e) {
      // ignore possible 'NSInternalInconsistencyException'
      return false;
    }

    // notify fail callback
    if (this->request->callbacks.fail != nullptr) {
      this->request->callbacks.fail(error);
    }
  #elif ORO_RUNTIME_PLATFORM_LINUX
    const auto quark = g_quark_from_string(bundleIdentifier.c_str());
    if (!quark) {
      return false;
    }

    const auto error = g_error_new(quark, 1, "%s", reason.c_str());

    if (error == nullptr) {
      return false;
    }

    if (this->request && WEBKIT_IS_URI_SCHEME_REQUEST(this->request->platformRequest)) {
      webkit_uri_scheme_request_finish_error(this->request->platformRequest, error);
    } else {
      return false;
    }

    // notify fail callback
    if (this->request->callbacks.fail != nullptr) {
      this->request->callbacks.fail(error);
    }
  #else
    // XXX(@jwerle): there doesn't appear to be a way to notify a failure for all platforms
    this->finished = true;
    return false;
  #endif

    this->finished = true;
    return true;
  }

  bool SchemeHandlers::Response::redirect (const String& location, int statusCode) {
    static constexpr auto redirectSourceTemplate = R"S(
      <meta http-equiv="refresh" content="0; url='{{url}}'" />
    )S";

    // if head was already written, then we cannot perform a redirect
    if (this->platformResponse) {
      return false;
    }

    if (location.starts_with("/")) {
      this->setHeader("location", this->request->origin + location);
    } else if (location.starts_with(".")) {
      this->setHeader("location", this->request->origin + location.substr(1));
    } else {
      this->setHeader("location", location);
    }

    if (!this->writeHead(statusCode)) {
      return false;
    }

    if (this->request->method != "HEAD" && this->request->method != "OPTIONS") {
      const auto content = tmpl(
        redirectSourceTemplate,
        {{"url", location}}
      );

      if (!this->write(content)) {
        return false;
      }
    }

    return this->finish();
  }

  const String SchemeHandlers::Response::Event::str () const noexcept {
    if (this->name.size() > 0 && this->data.size() > 0) {
      return (
        String("event: ") + this->name + "\n" +
        String("data: ") + this->data + "\n"
        "\n"
      );
    }

    if (this->name.size() > 0) {
      return  String("event: ") + this->name + "\n\n";
    }

    if (this->data.size() > 0) {
      return  String("data: ") + this->data + "\n\n";
    }

    return "";
  }

  size_t SchemeHandlers::Response::Event::count () const noexcept {
    if (this->name.size() > 0 && this->data.size() > 0) {
      return 2;
    } else if (this->name.size() > 0 || this->data.size() > 0) {
      return 1;
    } else {
      return 0;
    }
  }
}
