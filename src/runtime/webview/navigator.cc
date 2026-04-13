#include "../filesystem.hh"
#include "../runtime.hh"
#include "../config.hh"
#include "../bridge.hh"
#include "../string.hh"
#include "../bytes.hh"
#include "../app.hh"

#include "../webview.hh"
#include "tls_pins.hh"
#include "../scheme.hh"

using namespace oro::runtime;

using oro::runtime::config::getDevHost;
using oro::runtime::string::parseStringList;
using oro::runtime::string::replace;
using oro::runtime::string::split;
using oro::runtime::string::trim;
using oro::runtime::string::toLowerCase;
using oro::runtime::app::App;
using oro::runtime::webview::TlsPinMap;
using oro::runtime::webview::parseTlsPinConfig;
using oro::runtime::webview::isCertificateAllowedForHost;
using oro::runtime::webview::hasPinsForHost;

#if ORO_RUNTIME_PLATFORM_APPLE
#include <Security/Security.h>
#include <CommonCrypto/CommonDigest.h>

@implementation ORONavigationDelegate
- (void) webView: (WKWebView*) webview
  didFailNavigation: (WKNavigation*) navigation
          withError: (NSError*) error {
  // TODO(@jwerle)
}

- (void) webView: (WKWebView*) webview
  didFailProvisionalNavigation: (WKNavigation*) navigation
                     withError: (NSError*) error {
  // TODO(@jwerle)
}

- (void) webView: (WKWebView*) webview
    decidePolicyForNavigationAction: (WKNavigationAction*) navigationAction
                    decisionHandler: (void (^)(WKNavigationActionPolicy)) decisionHandler {
  using namespace oro::runtime;
  if (
    webview != nullptr &&
    webview.URL != nullptr &&
    webview.URL.absoluteString.UTF8String != nullptr &&
    navigationAction != nullptr &&
    navigationAction.request.URL.absoluteString.UTF8String != nullptr
  ) {
    const auto currentURL = String(webview.URL.absoluteString.UTF8String);
    const auto requestedURL = String(navigationAction.request.URL.absoluteString.UTF8String);

    if (!self.navigator->handleNavigationRequest(currentURL, requestedURL)) {
      return decisionHandler(WKNavigationActionPolicyCancel);
    }
  }

  decisionHandler(WKNavigationActionPolicyAllow);
}

- (void) webView: (WKWebView*) webview
  decidePolicyForNavigationResponse: (WKNavigationResponse*) navigationResponse
                    decisionHandler: (void (^)(WKNavigationResponsePolicy)) decisionHandler {
  decisionHandler(WKNavigationResponsePolicyAllow);
}

- (void) webView: (WKWebView*) webView
didReceiveAuthenticationChallenge: (NSURLAuthenticationChallenge*) challenge
 completionHandler: (void (^)(NSURLSessionAuthChallengeDisposition disposition, NSURLCredential* _Nullable credential)) completionHandler {
  using namespace oro::runtime;

  if (challenge == nil || challenge.protectionSpace == nil) {
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    return;
  }

  if (![challenge.protectionSpace.authenticationMethod isEqualToString: NSURLAuthenticationMethodServerTrust]) {
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    return;
  }

  auto navigator = self.navigator;
  if (!navigator) {
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    return;
  }

  auto& userConfig = navigator->bridge.userConfig;
  auto it = userConfig.find("webview_tls_pins");
  if (it == userConfig.end() || it->second.empty()) {
    it = userConfig.find("webview.tls_pins");
    if (it == userConfig.end() || it->second.empty()) {
      completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
      return;
    }
  }

  const auto pins = parseTlsPinConfig(it->second, true);

  String host;
  @autoreleasepool {
    NSString* hostString = challenge.protectionSpace.host;
    if (hostString != nil && hostString.UTF8String != nullptr) {
      host = toLowerCase(String(hostString.UTF8String));
    }
  }

  const auto protectionPort = static_cast<int>(challenge.protectionSpace.port);
  String pinHost = host;
  if (!host.empty() && protectionPort > 0) {
    if (host.find(':') != String::npos && host.front() != '[') {
      pinHost = "[" + host + "]:" + std::to_string(protectionPort);
    } else {
      pinHost = host + ":" + std::to_string(protectionPort);
    }
  }

  // Preserve the platform default behaviour when no pins apply to this host.
  if (!hasPinsForHost(pins, pinHost)) {
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    return;
  }

  SecTrustRef trust = challenge.protectionSpace.serverTrust;
  if (!trust) {
    completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    return;
  }

  SecCertificateRef certificate = SecTrustGetCertificateAtIndex(trust, 0);
  if (!certificate) {
    completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    return;
  }

  CFDataRef data = SecCertificateCopyData(certificate);
  if (!data) {
    completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    return;
  }

  Vector<uint8_t> digest;
  digest.reserve(CC_SHA256_DIGEST_LENGTH);

  const UInt8* bytes = CFDataGetBytePtr(data);
  const CFIndex length = CFDataGetLength(data);

  if (bytes && length > 0) {
    unsigned char output[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes, (CC_LONG) length, output);
    digest.assign(output, output + CC_SHA256_DIGEST_LENGTH);
  }

  CFRelease(data);

  if (digest.empty()) {
    completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    return;
  }

  if (!isCertificateAllowedForHost(pins, pinHost, digest)) {
    completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    return;
  }

  NSURLCredential* credential = [NSURLCredential credentialForTrust: trust];
  completionHandler(NSURLSessionAuthChallengeUseCredential, credential);
}

#if ORO_RUNTIME_PLATFORM_IOS
- (void) webView: (WKWebView*) webview
  didFinishNavigation: (WKNavigation*) navigation {
  auto app = App::sharedApplication();
  if (webview.scrollView.refreshControl && webview.scrollView.refreshControl.refreshing) {
    app->dispatch([webview]() {
      [webview.scrollView.refreshControl endRefreshing];
      [UIView animateWithDuration: 0.25 animations:^{
        const auto topInset = webview.superview.safeAreaInsets.top;
        auto insets = webview.scrollView.contentInset;
        auto offset = webview.scrollView.contentOffset;
        insets.top = topInset;
        offset.y = -topInset;
        webview.scrollView.contentInset = insets;
        webview.scrollView.contentOffset = offset;
      }];
    });
  }
}
#endif
@end
#endif

namespace oro::runtime::webview {
  Navigator::Location::Location (Navigator& navigator)
    : navigator(navigator),
      URL()
  {}

  void Navigator::Location::init () {}

  /**
   * .
   * ├── a-conflict-index
   * │             └── index.html
   * ├── a-conflict-index.html
   * ├── an-index-file
   * │             ├── a-html-file.html
   * │             └── index.html
   * ├── another-file.html
   * └── index.html
   *
   * Subtleties:
   * Direct file navigation always wins
   * /foo/index.html have precedent over foo.html
   * /foo redirects to /foo/ when there is a /foo/index.html
   *
   * '/' -> '/index.html'
   * '/index.html' -> '/index.html'
   * '/a-conflict-index' -> redirect to '/a-conflict-index/'
   * '/another-file' -> '/another-file.html'
   * '/another-file.html' -> '/another-file.html'
   * '/an-index-file/' -> '/an-index-file/index.html'
   * '/an-index-file' -> redirect to '/an-index-file/'
   * '/an-index-file/a-html-file' -> '/an-index-file/a-html-file.html'
   **/
  static const Navigator::Location::Resolution resolveLocationPathname (
    const String& pathname,
    const String& dirname
  ) {
    auto result = pathname;

    if (result.starts_with("/")) {
      result = result.substr(1);
    }

    // Resolve the full path
    const auto filename = (fs::path(dirname) / fs::path(result)).make_preferred();

    // 1. Try the given path if it's a file
    if (filesystem::Resource::isFile(filename)) {
      return Navigator::Location::Resolution {
        .pathname = "/" + replace(fs::relative(filename, dirname).string(), "\\\\", "/")
      };
    }

    // 2. Try appending a `/` to the path and checking for an index.html
    const auto index = filename / fs::path("index.html");
    if (filesystem::Resource::isFile(index)) {
      if (filename.string().ends_with("\\") || filename.string().ends_with("/")) {
        return Navigator::Location::Resolution {
          .pathname = "/" + replace(fs::relative(index, dirname).string(), "\\\\", "/"),
          .redirect = false
        };
      } else {
        return Navigator::Location::Resolution {
          .pathname = "/" + replace(fs::relative(filename, dirname).string(), "\\\\", "/") + "/",
          .redirect = true
        };
      }
    }

    // 3. Check if appending a .html file extension gives a valid file
    const auto html = Path(filename).replace_extension(".html");
    if (filesystem::Resource::isFile(html)) {
      return Navigator::Location::Resolution {
        .pathname = "/" + replace(fs::relative(html, dirname).string(), "\\\\", "/")
      };
    }

    // If no valid path is found, return empty string
    return Navigator::Location::Resolution {};
  }

  const Navigator::Location::Resolution Navigator::Location::resolve (const Path& pathname, const Path& dirname) {
    return this->resolve(pathname.string(), dirname.string());
  }

  const Navigator::Location::Resolution Navigator::Location::resolve (const String& pathname, const String& dirname) {
    for (const auto& entry : this->mounts) {
      if (pathname.starts_with(entry.second)) {
        const auto relative = replace(pathname, entry.second, "");
        auto resolution = resolveLocationPathname(relative, entry.first);
        if (resolution.pathname.size() > 0) {
          const auto filename = Path(entry.first) / resolution.pathname.substr(1);
          resolution.type = Navigator::Location::Resolution::Type::Mount;
          resolution.mount.filename = filename.string();
          return resolution;
        }
      }
    }

    auto resolution = resolveLocationPathname(pathname, dirname);
    if (resolution.pathname.size() > 0) {
      resolution.type = Navigator::Location::Resolution::Type::Resource;
    }
    return resolution;
  }

  bool Navigator::Location::Resolution::isUnknown () const {
    return this->type == Navigator::Location::Resolution::Type::Unknown;
  }

  bool Navigator::Location::Resolution::isResource () const {
    return this->type == Navigator::Location::Resolution::Type::Resource;
  }

  bool Navigator::Location::Resolution::isMount () const {
    return this->type == Navigator::Location::Resolution::Type::Mount;
  }

  void Navigator::Location::assign (const String& url) {
    this->set(url);
    this->navigator.bridge.navigate(url);
  }

  void Navigator::Location::set (const URL& url) {
    URL::set(url.href());
  }

  Navigator::Navigator (bridge::Bridge& bridge)
    : bridge(bridge),
      location(*this) {
  #if ORO_RUNTIME_PLATFORM_APPLE
    this->navigationDelegate = [ORONavigationDelegate new];
    this->navigationDelegate.navigator = this;
  #endif
  }

  Navigator::~Navigator () {
  #if ORO_RUNTIME_PLATFORM_APPLE
    if (this->navigationDelegate) {
      this->navigationDelegate.navigator = nullptr;

    #if !__has_feature(objc_arc)
      [this->navigationDelegate release];
    #endif
    }

    this->navigationDelegate = nullptr;
  #endif
  }

  void Navigator::init () {
    this->location.init();
  }

  void Navigator::configureWebView (WebView* webview) {
  #if ORO_RUNTIME_PLATFORM_APPLE
    webview.navigationDelegate = this->navigationDelegate;
  #elif ORO_RUNTIME_PLATFORM_LINUX
    g_signal_connect(
      G_OBJECT(webview),
      "decide-policy",
      G_CALLBACK((+[](
        WebKitWebView* webview,
        WebKitPolicyDecision* decision,
        WebKitPolicyDecisionType decisionType,
        gpointer userData
      ) {
        const auto navigator = reinterpret_cast<Navigator*>(userData);
        auto window = navigator->bridge.context.getRuntime()->windowManager.getWindowForWebView(webview);

        if (!window) {
          webkit_policy_decision_ignore(decision);
          return false;
        }

        if (decisionType != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
          webkit_policy_decision_use(decision);
          return true;
        }

        const auto navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        const auto action = webkit_navigation_policy_decision_get_navigation_action(navigation);
        const auto type = webkit_navigation_action_get_navigation_type(action);
        const auto request = webkit_navigation_action_get_request(action);
        const auto currentURL = String(webkit_web_view_get_uri(webview));
        const auto requestedURL = String(webkit_uri_request_get_uri(request));

        if (!navigator->handleNavigationRequest(currentURL, requestedURL)) {
          webkit_policy_decision_ignore(decision);
          return false;
        }

        webkit_policy_decision_use(decision);
        return true;
      })),
      this
    );
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    EventRegistrationToken token;
    webview->add_NavigationStarting(
      Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
        [=, this](ICoreWebView2* webview, ICoreWebView2NavigationStartingEventArgs* args) {
          PWSTR source;
          PWSTR uri;

          args->get_Uri(&uri);
          webview->get_Source(&source);

          if (uri == nullptr || source == nullptr) {
            if (uri) CoTaskMemFree(uri);
            if (source) CoTaskMemFree(source);
            return E_POINTER;
          }

          const auto requestedURL = convertWStringToString(uri);
          const auto currentURL = convertWStringToString(source);

          if (!this->handleNavigationRequest(currentURL, requestedURL)) {
            args->put_Cancel(true);
          }

          CoTaskMemFree(uri);
          CoTaskMemFree(source);
          return S_OK;
        }
      ).Get(),
      &token
    );
  #endif
  }

  bool Navigator::handleNavigationRequest (
    const String& currentURL,
    const String& requestedURL
  ) {
    auto userConfig = this->bridge.userConfig;
    const auto links = parseStringList(userConfig["meta_application_links"], ' ');
    const auto window = this->bridge.context.getRuntime()->windowManager.getWindowForBridge(&this->bridge);
    const auto applinks = parseStringList(userConfig["meta_application_links"], ' ');
    const auto currentURLComponents = URL::Components::parse(currentURL);

    bool hasAppLink = false;
    if (applinks.size() > 0 && currentURLComponents.authority.size() > 0) {
      const auto host = currentURLComponents.authority;
      for (const auto& applink : applinks) {
        const auto parts = split(applink, '?');
        if (host == parts[0]) {
          hasAppLink = true;
          break;
        }
      }
    }

    if (hasAppLink) {
      if (window) {
        window->handleApplicationURL(requestedURL);
        return false;
      }

      // should be unreachable, but...
      return true;
    }

 	  if (
 	    userConfig["meta_application_protocol"].size() > 0 &&
 	    requestedURL.starts_with(userConfig["meta_application_protocol"]) &&
 	    !scheme::matchesBundleURL(requestedURL, userConfig["meta_bundle_identifier"])
 	  ) {
 	    if (window) {
        window->handleApplicationURL(requestedURL);
        return false;
      }

      // should be unreachable, but...
      return true;
    }

    if (!this->isNavigationRequestAllowed(currentURL, requestedURL)) {
      debug("IPC::Navigation: A navigation request was ignored for: %s", requestedURL.c_str());
      return false;
    }

    return true;
  }

  bool Navigator::isNavigationRequestAllowed (
    const String& currentURL,
    const String& requestedURL
  ) {
    static const auto devHost = getDevHost();
    auto userConfig = this->bridge.userConfig;
    const auto allowed = split(trim(userConfig["webview_navigator_policies_allowed"]), ' ');

    if (requestedURL == "about:blank") {
      return true;
    }

    for (const auto& entry : split(userConfig["webview_protocol-handlers"], " ")) {
      const auto scheme = replace(trim(entry), ":", "");
      if (requestedURL.starts_with(scheme + ":")) {
        return true;
      }
    }

    for (const auto& entry : userConfig) {
      const auto& key = entry.first;
      if (key.starts_with("webview_protocol-handlers_")) {
        const auto scheme = replace(replace(trim(key), "webview_protocol-handlers_", ""), ":", "");;
        if (requestedURL.starts_with(scheme + ":")) {
          return true;
        }
      }
    }

    for (const auto& entry : allowed) {
      String pattern = entry;
      pattern = replace(pattern, "\\.", "\\.");
      pattern = replace(pattern, "\\*", "(.*)");
      pattern = replace(pattern, "\\.\\.\\*", "(.*)");
      pattern = replace(pattern, "\\/", "\\/");

      try {
        std::regex regex(pattern);
        std::smatch match;

        if (std::regex_match(requestedURL, match, regex, std::regex_constants::match_any)) {
          return true;
        }
      } catch (...) {}
    }

    if (
      scheme::startsWithRuntimeSpecifier(requestedURL) ||
      requestedURL.starts_with("node:") ||
      requestedURL.starts_with("npm:") ||
      requestedURL.starts_with("ipc:") ||
      requestedURL.starts_with(devHost)
    ) {
      return true;
    }

    return false;
  }

  void Navigator::configureMounts () {
    static const auto wellKnownPaths = filesystem::Resource::getWellKnownPaths();
    this->location.mounts = filesystem::Resource::getMountedPaths();

    for (const auto& entry : this->location.mounts) {
      const auto& path = entry.first;
      #if ORO_RUNTIME_PLATFORM_LINUX
        auto webContext = this->bridge.webContext;
        if (path != wellKnownPaths.home.string()) {
          webkit_web_context_add_path_to_sandbox(webContext, path.c_str(), false);
        }
      #endif
    }

  #if ORO_RUNTIME_PLATFORM_LINUX
    auto webContext = this->bridge.webContext;
    for (const auto& entry : wellKnownPaths.entries()) {
      if (filesystem::Resource::isDirectory(entry) && entry != wellKnownPaths.home) {
        webkit_web_context_add_path_to_sandbox(webContext, entry.c_str(), false);
      }
    }
  #endif
  }
}

#if ORO_RUNTIME_PLATFORM_ANDROID
extern "C" {
  jboolean ANDROID_EXTERNAL(webview, Navigator, isNavigationRequestAllowed) (
    JNIEnv* env,
    jobject self,
    jint index,
    jstring currentURLString,
    jstring requestedURLString
  ) {
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

    const auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);
    const auto currentURL = android::StringWrap(attachment.env, currentURLString).str();
    const auto requestedURL = android::StringWrap(attachment.env, requestedURLString).str();

    return window->bridge->navigator.isNavigationRequestAllowed(currentURL, requestedURL);
  }
}
#endif
