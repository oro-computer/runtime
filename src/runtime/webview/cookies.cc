#include "cookies.hh"

#include "../app.hh"
#include "../bridge.hh"
#include "../runtime.hh"
#include "../scheme.hh"
#include "../string.hh"
#include "../url.hh"

#include "tls_pins.hh"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace oro::runtime::webview::cookies {
  using oro::runtime::string::toLowerCase;
  using oro::runtime::string::trim;
  using oro::runtime::webview::normaliseTlsPinHost;
#if ORO_RUNTIME_PLATFORM_WINDOWS
  using oro::runtime::string::convertStringToWString;
  using oro::runtime::string::convertWStringToString;
#endif

  namespace {
    struct Cookie {
      String name;
      String value;
      String domain;
      String path;
      bool secure = false;
      bool httpOnly = false;
      int64_t expiresMs = -1;
    };

    static webview::WebView* getWebViewForBridge (bridge::Bridge& bridge) {
      const auto runtime = bridge.getRuntime();
      if (runtime == nullptr) {
        return nullptr;
      }

      auto window = runtime->windowManager.getWindowForBridge(&bridge);
      if (window == nullptr) {
        window = runtime->windowManager.getWindow(0);
      }

      return window != nullptr ? window->webview : nullptr;
    }

    static bool hostMatchesDomain (const String& host, String domain) {
      if (host.empty() || domain.empty()) {
        return false;
      }

      const auto loweredHost = normaliseTlsPinHost(host);

      domain = toLowerCase(trim(domain));
      const bool isDomainCookie = !domain.empty() && domain.front() == '.';
      while (!domain.empty() && domain.front() == '.') {
        domain.erase(domain.begin());
      }
      while (!domain.empty() && domain.back() == '.') {
        domain.pop_back();
      }

      if (domain.empty() || loweredHost.empty()) {
        return false;
      }

      if (loweredHost == domain) {
        return true;
      }

      if (!isDomainCookie) {
        return false;
      }

      if (loweredHost.size() <= domain.size() + 1) {
        return false;
      }

      const auto suffix = String(".") + domain;
      return loweredHost.rfind(suffix) == loweredHost.size() - suffix.size();
    }

    static bool pathMatchesCookiePath (const String& requestPath, const String& cookiePath) {
      auto request = requestPath.empty() ? String("/") : requestPath;
      auto cookie = cookiePath.empty() ? String("/") : cookiePath;

      if (request.front() != '/') {
        request = "/" + request;
      }

      if (cookie.front() != '/') {
        cookie = "/" + cookie;
      }

      if (request.rfind(cookie, 0) != 0) {
        return false;
      }

      if (request.size() == cookie.size()) {
        return true;
      }

      if (!cookie.empty() && cookie.back() == '/') {
        return true;
      }

      return request.size() > cookie.size() && request[cookie.size()] == '/';
    }

    static String cookieHeaderFromList (Vector<Cookie> cookies) {
      if (cookies.empty()) {
        return "";
      }

      std::sort(cookies.begin(), cookies.end(), [](const Cookie& a, const Cookie& b) {
        if (a.path.size() != b.path.size()) {
          return a.path.size() > b.path.size();
        }
        return a.name < b.name;
      });

      String header = "";
      for (const auto& cookie : cookies) {
        if (cookie.name.empty()) {
          continue;
        }

        if (!header.empty()) {
          header += "; ";
        }

        header += cookie.name;
        header += "=";
        header += cookie.value;
      }

      return header;
    }

    static String normalizeCookieURLForStore (const bridge::Bridge& bridge, const String& url) {
      const auto urlString = trim(url);
      const auto bundleIdentifier = bridge.userConfig.contains("meta_bundle_identifier")
        ? bridge.userConfig.at("meta_bundle_identifier")
        : "";

      const auto components = URL::Components::parse(urlString);
      if (!scheme::isRuntimeScheme(toLowerCase(trim(components.scheme)))) {
        return urlString;
      }

      if (bundleIdentifier.empty()) {
        return urlString;
      }

      if (normaliseTlsPinHost(components.authority) != normaliseTlsPinHost(bundleIdentifier)) {
        return urlString;
      }

      String normalized = "https://";
      normalized += components.authority;
      normalized += components.pathname.size() ? components.pathname : "/";
      if (!components.query.empty()) {
        normalized += "?";
        normalized += components.query;
      }
      if (!components.fragment.empty()) {
        normalized += "#";
        normalized += components.fragment;
      }

      return normalized;
    }

#if ORO_RUNTIME_PLATFORM_ANDROID
    static void flushAndroidCookieManagerIfAvailable (JNIEnv* env, jobject cookieManager) {
      if (env == nullptr || cookieManager == nullptr) {
        return;
      }

      const auto cookieManagerClass = env->GetObjectClass(cookieManager);
      if (cookieManagerClass == nullptr) {
        if (env->ExceptionCheck()) {
          env->ExceptionClear();
        }
        return;
      }

      const auto flush = env->GetMethodID(cookieManagerClass, "flush", "()V");
      if (flush != nullptr) {
        env->CallVoidMethod(cookieManager, flush);
      }

      if (env->ExceptionCheck()) {
        env->ExceptionClear();
      }

      env->DeleteLocalRef(cookieManagerClass);
    }

    static bool removeAllAndroidCookies (JNIEnv* env, jobject cookieManager) {
      if (env == nullptr || cookieManager == nullptr) {
        return false;
      }

      const auto cookieManagerClass = env->GetObjectClass(cookieManager);
      if (cookieManagerClass == nullptr) {
        if (env->ExceptionCheck()) {
          env->ExceptionClear();
        }
        return false;
      }

      const auto removeAllCookies = env->GetMethodID(
        cookieManagerClass,
        "removeAllCookies",
        "(Landroid/webkit/ValueCallback;)V"
      );

      if (removeAllCookies != nullptr) {
        env->CallVoidMethod(cookieManager, removeAllCookies, (jobject) nullptr);

        if (env->ExceptionCheck()) {
          env->ExceptionClear();
        }

        env->DeleteLocalRef(cookieManagerClass);
        return true;
      }

      if (env->ExceptionCheck()) {
        env->ExceptionClear();
      }

      const auto removeAllCookie = env->GetMethodID(cookieManagerClass, "removeAllCookie", "()V");
      if (removeAllCookie != nullptr) {
        env->CallVoidMethod(cookieManager, removeAllCookie);

        if (env->ExceptionCheck()) {
          env->ExceptionClear();
        }

        env->DeleteLocalRef(cookieManagerClass);
        return true;
      }

      if (env->ExceptionCheck()) {
        env->ExceptionClear();
      }

      env->DeleteLocalRef(cookieManagerClass);
      return false;
    }
#endif

  #if ORO_RUNTIME_PLATFORM_WINDOWS
    struct ParsedSetCookie {
      String name;
      String value;
      String domain;
      String path;
      bool secure = false;
      bool httpOnly = false;
      std::optional<int64_t> expiresUnixSeconds;
      std::optional<COREWEBVIEW2_COOKIE_SAME_SITE_KIND> sameSite;
    };

    static String trimAscii (String value) {
      return trim(value);
    }

    static std::optional<int64_t> parseHttpDateUnixSeconds (const String& input) {
      // Common formats:
      // - "Wed, 21 Oct 2015 07:28:00 GMT"
      // - "Wed, 21-Oct-2015 07:28:00 GMT"
      // - "Wed Oct 21 07:28:00 2015"
      std::tm tm = {};

      auto parseWith = [&](const char* fmt) -> bool {
        std::istringstream ss(input);
        ss.imbue(std::locale::classic());
        ss >> std::get_time(&tm, fmt);
        return !ss.fail();
      };

      if (
        !parseWith("%a, %d %b %Y %H:%M:%S GMT") &&
        !parseWith("%a, %d-%b-%Y %H:%M:%S GMT") &&
        !parseWith("%a %b %d %H:%M:%S %Y")
      ) {
        return std::nullopt;
      }

      // _mkgmtime converts tm in UTC to time_t.
      const auto seconds = _mkgmtime(&tm);
      if (seconds < 0) {
        return std::nullopt;
      }

      return static_cast<int64_t>(seconds);
    }

    static bool parseSetCookieHeader (
      const String& urlString,
      const String& setCookie,
      ParsedSetCookie& out,
      String& outError
    ) {
      const auto urlValue = trimAscii(urlString);
      const auto components = URL::Components::parse(urlValue);
      const auto host = normaliseTlsPinHost(components.authority);
      if (host.empty()) {
        outError = "URL must include a host";
        return false;
      }

      const auto input = trimAscii(setCookie);
      if (input.empty()) {
        outError = "Set-Cookie value is empty";
        return false;
      }

      const auto requestPath = components.pathname.size() ? components.pathname : "/";
      auto defaultPath = String("/");
      if (!requestPath.empty() && requestPath.front() == '/' && requestPath.size() > 1) {
        const auto lastSlash = requestPath.rfind('/');
        if (lastSlash != String::npos && lastSlash > 0) {
          defaultPath = requestPath.substr(0, lastSlash);
        }
      }

      const auto semi = input.find(';');
      const auto first = semi == String::npos ? input : input.substr(0, semi);
      const auto eq = first.find('=');
      if (eq == String::npos || eq == 0) {
        outError = "Invalid Set-Cookie name/value pair";
        return false;
      }

      out.name = trimAscii(first.substr(0, eq));
      out.value = eq + 1 < first.size() ? first.substr(eq + 1) : "";
      out.domain = host;
      out.path = defaultPath;

      const auto now = std::chrono::system_clock::now();
      const auto nowUnixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
      ).count();
      bool sawDomain = false;
      bool sawMaxAge = false;

      size_t pos = semi == String::npos ? input.size() : semi + 1;
      while (pos < input.size()) {
        auto next = input.find(';', pos);
        if (next == String::npos) {
          next = input.size();
        }

        auto token = trimAscii(input.substr(pos, next - pos));
        pos = next + 1;

        if (token.empty()) {
          continue;
        }

        const auto attrEq = token.find('=');
        const auto key = toLowerCase(trimAscii(attrEq == String::npos ? token : token.substr(0, attrEq)));
        const auto value = attrEq == String::npos ? "" : trimAscii(token.substr(attrEq + 1));

        if (attrEq == String::npos) {
          if (key == "secure") {
            out.secure = true;
          } else if (key == "httponly") {
            out.httpOnly = true;
          }
          continue;
        }

        if (key == "domain" && !value.empty()) {
          auto domain = toLowerCase(value);
          while (!domain.empty() && domain.front() == '.') {
            domain.erase(domain.begin());
          }
          while (!domain.empty() && domain.back() == '.') {
            domain.pop_back();
          }
          if (!domain.empty()) {
            sawDomain = true;
            out.domain = "." + domain;
          }
        } else if (key == "path" && !value.empty()) {
          if (value.front() == '/') {
            out.path = value;
          }
        } else if (key == "max-age" && !value.empty()) {
          try {
            const auto seconds = std::stoll(value);
            const auto expires = now + std::chrono::seconds(seconds);
            const auto unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
              expires.time_since_epoch()
            ).count();
            sawMaxAge = true;
            out.expiresUnixSeconds = static_cast<int64_t>(unixSeconds);
          } catch (...) {}
        } else if (key == "expires" && !value.empty()) {
          if (!sawMaxAge) {
            const auto parsed = parseHttpDateUnixSeconds(value);
            if (parsed.has_value()) {
              out.expiresUnixSeconds = parsed.value();
            }
          }
        } else if (key == "samesite" && !value.empty()) {
          const auto policy = toLowerCase(value);
          if (policy == "lax") {
            out.sameSite = COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX;
          } else if (policy == "strict") {
            out.sameSite = COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT;
          } else if (policy == "none") {
            out.sameSite = COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE;
          }
        }
      }

      if (out.name.empty()) {
        outError = "Cookie name is empty";
        return false;
      }

      if (out.domain.empty()) {
        outError = "Cookie domain is empty";
        return false;
      }

      if (sawDomain && !hostMatchesDomain(host, out.domain)) {
        outError = "Cookie domain does not match URL host";
        return false;
      }

      if (sawDomain) {
        auto domainCore = out.domain;
        while (!domainCore.empty() && domainCore.front() == '.') {
          domainCore.erase(domainCore.begin());
        }

        const bool hostHasDot = host.find('.') != String::npos;
        const bool domainHasDot = domainCore.find('.') != String::npos;

        if (hostHasDot && !domainHasDot) {
          outError = "Cookie domain must include a dot";
          return false;
        }
      }

      if (out.path.empty()) {
        out.path = defaultPath;
      }

      if (out.name.rfind("__Secure-", 0) == 0 && !out.secure) {
        outError = "Cookies with the __Secure- prefix must be Secure";
        return false;
      }

      if (out.name.rfind("__Host-", 0) == 0) {
        if (!out.secure) {
          outError = "Cookies with the __Host- prefix must be Secure";
          return false;
        }

        if (sawDomain) {
          outError = "Cookies with the __Host- prefix must not include a Domain attribute";
          return false;
        }

        if (out.path != "/") {
          outError = "Cookies with the __Host- prefix must have Path=/";
          return false;
        }
      }

      if (out.sameSite.has_value() && out.sameSite.value() == COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE && !out.secure) {
        outError = "SameSite=None cookies must be Secure";
        return false;
      }

      if (out.expiresUnixSeconds.has_value() && out.expiresUnixSeconds.value() <= static_cast<int64_t>(nowUnixSeconds)) {
        out.expiresUnixSeconds = static_cast<int64_t>(nowUnixSeconds) - 1;
      }

      return true;
    }
  #endif

    static bool dispatchOrError (
      bridge::Bridge& bridge,
      Function<void()> fn,
      const Function<void(const String&)>& onError
    ) {
      if (!bridge.active()) {
        if (onError != nullptr) {
          onError("Bridge is not active");
        }
        return false;
      }

      bridge.dispatch(std::move(fn));
      return true;
    }
  }

  void get (bridge::Bridge& bridge, const String& url, const CookieCallback callback) {
    if (callback == nullptr) {
      return;
    }

    const auto urlString = trim(url);
    if (urlString.empty()) {
      return callback("", "URL is required");
    }

    dispatchOrError(bridge, [urlString, &bridge, callback]() mutable {
    #if ORO_RUNTIME_PLATFORM_APPLE
      const auto components = URL::Components::parse(urlString);
      const auto host = toLowerCase(trim(components.authority));
      const auto path = components.pathname.size() ? components.pathname : "/";
      const auto schemeValue = toLowerCase(trim(components.scheme));
      const bool isSecureScheme = (
        schemeValue == "https" ||
        scheme::isRuntimeScheme(schemeValue)
      );

      if (host.empty()) {
        return callback("", "");
      }

      if ([NSURL URLWithString: @(urlString.c_str())] == nullptr) {
        return callback("", "Invalid URL");
      }

      WKHTTPCookieStore* cookieStore = WKWebsiteDataStore.defaultDataStore.httpCookieStore;
      if (cookieStore == nullptr) {
        return callback("", "Cookie store is unavailable");
      }

      [cookieStore getAllCookies: ^(NSArray<NSHTTPCookie*>* nsCookies) {
        Vector<Cookie> matches;
        matches.reserve(nsCookies.count);

        const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()
        ).count();

        for (NSHTTPCookie* nsCookie in nsCookies) {
          if (!nsCookie) {
            continue;
          }

          String cookieDomain = "";
          if (nsCookie.domain && nsCookie.domain.UTF8String != nullptr) {
            cookieDomain = nsCookie.domain.UTF8String;
          }

          if (!hostMatchesDomain(host, cookieDomain)) {
            continue;
          }

          String cookiePath = "/";
          if (nsCookie.path && nsCookie.path.UTF8String != nullptr) {
            cookiePath = nsCookie.path.UTF8String;
          }

          if (cookiePath.empty()) {
            cookiePath = "/";
          }

          if (!pathMatchesCookiePath(path, cookiePath)) {
            continue;
          }

          if (nsCookie.isSecure && !isSecureScheme) {
            continue;
          }

          if (nsCookie.expiresDate != nil) {
            const auto expiresSeconds = static_cast<int64_t>(nsCookie.expiresDate.timeIntervalSince1970);
            if (expiresSeconds <= nowSeconds) {
              continue;
            }
          }

          String name = "";
          String value = "";
          if (nsCookie.name && nsCookie.name.UTF8String != nullptr) {
            name = nsCookie.name.UTF8String;
          }
          if (nsCookie.value && nsCookie.value.UTF8String != nullptr) {
            value = nsCookie.value.UTF8String;
          }

          if (name.empty()) {
            continue;
          }

          matches.push_back(Cookie {
            .name = name,
            .value = value,
            .domain = cookieDomain,
            .path = cookiePath,
            .secure = static_cast<bool>(nsCookie.isSecure),
            .httpOnly = static_cast<bool>(nsCookie.isHTTPOnly),
            .expiresMs = nsCookie.expiresDate != nil
              ? static_cast<int64_t>(nsCookie.expiresDate.timeIntervalSince1970) * 1000
              : -1
          });
        }

        callback(cookieHeaderFromList(std::move(matches)), "");
      }];
    #elif ORO_RUNTIME_PLATFORM_LINUX
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback("", "WebView is not ready");
      }

      auto context = webkit_web_view_get_context(webview);
      if (context == nullptr) {
        return callback("", "WebView context is unavailable");
      }

      auto cookieManager = webkit_web_context_get_cookie_manager(context);
      if (cookieManager == nullptr) {
        return callback("", "Cookie manager is unavailable");
      }

      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);

      struct Context {
        CookieCallback callback;
      };

      auto ctx = new Context { callback };
      webkit_cookie_manager_get_cookies(
        cookieManager,
        cookieURL.c_str(),
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
          auto ctx = static_cast<Context*>(userData);
          GError* error = nullptr;
          auto manager = WEBKIT_COOKIE_MANAGER(source);
          auto cookies = webkit_cookie_manager_get_cookies_finish(manager, result, &error);

          if (error != nullptr) {
            const auto message = error->message != nullptr ? String(error->message) : String("Cookie query failed");
            g_error_free(error);
            ctx->callback("", message);
            delete ctx;
            return;
          }

          Vector<Cookie> parsed;
          for (auto item = cookies; item != nullptr; item = item->next) {
            const auto cookie = static_cast<SoupCookie*>(item->data);
            if (cookie == nullptr) {
              continue;
            }

            String name = soup_cookie_get_name(cookie) ? soup_cookie_get_name(cookie) : "";
            if (name.empty()) {
              continue;
            }

            String value = soup_cookie_get_value(cookie) ? soup_cookie_get_value(cookie) : "";
            String domain = soup_cookie_get_domain(cookie) ? soup_cookie_get_domain(cookie) : "";
            String path = soup_cookie_get_path(cookie) ? soup_cookie_get_path(cookie) : "/";

            int64_t expiresMs = -1;
            if (auto expires = soup_cookie_get_expires(cookie)) {
              expiresMs = g_date_time_to_unix(expires) * 1000;
            }

	            parsed.push_back(Cookie {
	              .name = name,
	              .value = value,
	              .domain = domain,
	              .path = path,
	              .secure = soup_cookie_get_secure(cookie) != FALSE,
	              .httpOnly = soup_cookie_get_http_only(cookie) != FALSE,
	              .expiresMs = expiresMs
	            });
	          }

          g_list_free_full(cookies, reinterpret_cast<GDestroyNotify>(soup_cookie_free));
          ctx->callback(cookieHeaderFromList(std::move(parsed)), "");
          delete ctx;
        },
        ctx
      );
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback("", "WebView is not ready");
      }

      Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookieManager;
      auto webview2 = reinterpret_cast<ICoreWebView2_2*>(webview);
      if (webview2 == nullptr || webview2->get_CookieManager(&cookieManager) != S_OK || !cookieManager) {
        return callback("", "Cookie manager is unavailable");
      }

      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      const auto uri = convertStringToWString(cookieURL);
      cookieManager->GetCookies(
        uri.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2GetCookiesCompletedHandler>(
          [callback](HRESULT result, ICoreWebView2CookieList* list) -> HRESULT {
            if (result != S_OK || list == nullptr) {
              callback("", "Cookie query failed");
              return S_OK;
            }

            UINT count = 0;
            list->get_Count(&count);

            Vector<Cookie> cookies;
            cookies.reserve(count);

            for (UINT i = 0; i < count; ++i) {
              Microsoft::WRL::ComPtr<ICoreWebView2Cookie> cookie;
              if (list->GetValueAtIndex(i, &cookie) != S_OK || !cookie) {
                continue;
              }

              LPWSTR nameW = nullptr;
              LPWSTR valueW = nullptr;
              LPWSTR domainW = nullptr;
              LPWSTR pathW = nullptr;
              BOOL secure = FALSE;
              BOOL httpOnly = FALSE;
              double expires = 0;
              BOOL isSession = FALSE;

              cookie->get_Name(&nameW);
              cookie->get_Value(&valueW);
              cookie->get_Domain(&domainW);
              cookie->get_Path(&pathW);
              cookie->get_IsSecure(&secure);
              cookie->get_IsHttpOnly(&httpOnly);
              cookie->get_Expires(&expires);
              cookie->get_IsSession(&isSession);

              String name = nameW ? convertWStringToString(nameW) : "";
              String value = valueW ? convertWStringToString(valueW) : "";
              String domain = domainW ? convertWStringToString(domainW) : "";
              String path = pathW ? convertWStringToString(pathW) : "/";

              if (nameW) CoTaskMemFree(nameW);
              if (valueW) CoTaskMemFree(valueW);
              if (domainW) CoTaskMemFree(domainW);
              if (pathW) CoTaskMemFree(pathW);

              if (name.empty()) {
                continue;
              }

              const int64_t expiresMs = isSession
                ? -1
                : static_cast<int64_t>(expires) * 1000;

              cookies.push_back(Cookie {
                .name = name,
                .value = value,
                .domain = domain,
                .path = path,
                .secure = !!secure,
                .httpOnly = !!httpOnly,
                .expiresMs = expiresMs
              });
            }

            callback(cookieHeaderFromList(std::move(cookies)), "");
            return S_OK;
          }
        ).Get()
      );
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      auto app = app::App::sharedApplication();
      if (app == nullptr) {
        return callback("", "Application is invalid state");
      }

      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);

      const auto cookieManagerClass = attachment.env->FindClass("android/webkit/CookieManager");
      if (cookieManagerClass == nullptr) {
        return callback("", "Cookie manager is unavailable");
      }

      const auto getInstance = attachment.env->GetStaticMethodID(
        cookieManagerClass,
        "getInstance",
        "()Landroid/webkit/CookieManager;"
      );

      if (getInstance == nullptr) {
        attachment.env->DeleteLocalRef(cookieManagerClass);
        return callback("", "Cookie manager is unavailable");
      }

      const auto cookieManager = attachment.env->CallStaticObjectMethod(cookieManagerClass, getInstance);
      attachment.env->DeleteLocalRef(cookieManagerClass);

      if (cookieManager == nullptr) {
        return callback("", "Cookie manager is unavailable");
      }

      const auto jurl = attachment.env->NewStringUTF(cookieURL.c_str());
      const auto value = (jstring) CallClassMethodFromAndroidEnvironment(
        attachment.env,
        Object,
        cookieManager,
        "getCookie",
        "(Ljava/lang/String;)Ljava/lang/String;",
        jurl
      );

      attachment.env->DeleteLocalRef(jurl);

      if (value == nullptr) {
        attachment.env->DeleteLocalRef(cookieManager);
        callback("", "");
        return;
      }

      const auto header = android::StringWrap(attachment.env, value).str();
      attachment.env->DeleteLocalRef(value);
      attachment.env->DeleteLocalRef(cookieManager);
      callback(trim(header), "");
    #else
      callback("", "Cookie APIs are not supported on this platform");
    #endif
    }, [callback](const auto& error) {
      callback("", error);
    });
  }

  void set (
    bridge::Bridge& bridge,
    const String& url,
    const String& setCookie,
    const OkCallback callback
  ) {
    if (callback == nullptr) {
      return;
    }

    const auto urlString = trim(url);
    if (urlString.empty()) {
      return callback(false, "URL is required");
    }

    const auto cookieValue = trim(setCookie);
    if (cookieValue.empty()) {
      return callback(false, "Set-Cookie value is required");
    }

    dispatchOrError(bridge, [urlString, cookieValue, &bridge, callback]() mutable {
    #if ORO_RUNTIME_PLATFORM_APPLE
      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      NSURL* origin = [NSURL URLWithString: @(cookieURL.c_str())];
      if (origin == nullptr) {
        return callback(false, "Invalid URL");
      }

      WKHTTPCookieStore* cookieStore = WKWebsiteDataStore.defaultDataStore.httpCookieStore;
      if (cookieStore == nullptr) {
        return callback(false, "Cookie store is unavailable");
      }

      NSDictionary<NSString*, NSString*>* headers = @{
        @"Set-Cookie": @(cookieValue.c_str())
      };

      const auto cookies = [NSHTTPCookie cookiesWithResponseHeaderFields: headers forURL: origin];
      if (cookies == nullptr || cookies.count == 0) {
        return callback(false, "Failed to parse Set-Cookie");
      }

      auto remaining = std::make_shared<Atomic<int>>(static_cast<int>(cookies.count));
      for (NSHTTPCookie* cookie in cookies) {
        if (!cookie) {
          if (remaining->fetch_sub(1) == 1) {
            callback(true, "");
          }
          continue;
        }

        [cookieStore setCookie: cookie completionHandler: ^() {
          if (remaining->fetch_sub(1) == 1) {
            callback(true, "");
          }
        }];
      }
    #elif ORO_RUNTIME_PLATFORM_LINUX
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback(false, "WebView is not ready");
      }

      auto context = webkit_web_view_get_context(webview);
      if (context == nullptr) {
        return callback(false, "WebView context is unavailable");
      }

      auto cookieManager = webkit_web_context_get_cookie_manager(context);
      if (cookieManager == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      GError* error = nullptr;
      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      GUri* origin = g_uri_parse(trim(cookieURL).c_str(), G_URI_FLAGS_NONE, &error);
      if (origin == nullptr) {
        const auto message = error && error->message ? String(error->message) : String("Invalid URL");
        if (error) g_error_free(error);
        return callback(false, message);
      }

      auto cookie = soup_cookie_parse(cookieValue.c_str(), origin);
      g_uri_unref(origin);

      if (cookie == nullptr) {
        return callback(false, "Failed to parse Set-Cookie");
      }

      struct Context {
        OkCallback callback;
        SoupCookie* cookie;
      };

      auto ctx = new Context { callback, cookie };

      webkit_cookie_manager_add_cookie(
        cookieManager,
        cookie,
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
          auto ctx = static_cast<Context*>(userData);
          GError* error = nullptr;
          const auto ok = webkit_cookie_manager_add_cookie_finish(
            WEBKIT_COOKIE_MANAGER(source),
            result,
            &error
          );

          if (ctx->cookie != nullptr) {
            soup_cookie_free(ctx->cookie);
          }

          if (!ok || error != nullptr) {
            const auto message = error && error->message ? String(error->message) : String("Cookie set failed");
            if (error) g_error_free(error);
            ctx->callback(false, message);
            delete ctx;
            return;
          }

          ctx->callback(true, "");
          delete ctx;
        },
        ctx
      );
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback(false, "WebView is not ready");
      }

      Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookieManager;
      auto webview2 = reinterpret_cast<ICoreWebView2_2*>(webview);
      if (webview2 == nullptr || webview2->get_CookieManager(&cookieManager) != S_OK || !cookieManager) {
        return callback(false, "Cookie manager is unavailable");
      }

      ParsedSetCookie parsed;
      String parseError;
      if (!parseSetCookieHeader(urlString, cookieValue, parsed, parseError)) {
        return callback(false, parseError);
      }

      Microsoft::WRL::ComPtr<ICoreWebView2Cookie> cookie;
      const auto hr = cookieManager->CreateCookie(
        convertStringToWString(parsed.name).c_str(),
        convertStringToWString(parsed.value).c_str(),
        convertStringToWString(parsed.domain).c_str(),
        convertStringToWString(parsed.path).c_str(),
        &cookie
      );

      if (hr != S_OK || !cookie) {
        return callback(false, "Failed to create cookie");
      }

      cookie->put_IsHttpOnly(parsed.httpOnly);
      cookie->put_IsSecure(parsed.secure);

      if (parsed.expiresUnixSeconds.has_value()) {
        cookie->put_Expires(static_cast<double>(parsed.expiresUnixSeconds.value()));
      }

      if (parsed.sameSite.has_value()) {
        cookie->put_SameSite(parsed.sameSite.value());
      }

      cookieManager->AddOrUpdateCookie(cookie.Get());
      callback(true, "");
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      auto app = app::App::sharedApplication();
      if (app == nullptr) {
        return callback(false, "Application is invalid state");
      }

      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);

      const auto cookieManagerClass = attachment.env->FindClass("android/webkit/CookieManager");
      if (cookieManagerClass == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      const auto getInstance = attachment.env->GetStaticMethodID(
        cookieManagerClass,
        "getInstance",
        "()Landroid/webkit/CookieManager;"
      );

      if (getInstance == nullptr) {
        attachment.env->DeleteLocalRef(cookieManagerClass);
        return callback(false, "Cookie manager is unavailable");
      }

      const auto cookieManager = attachment.env->CallStaticObjectMethod(cookieManagerClass, getInstance);
      attachment.env->DeleteLocalRef(cookieManagerClass);

      if (cookieManager == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      const auto jurl = attachment.env->NewStringUTF(cookieURL.c_str());
      const auto jcookie = attachment.env->NewStringUTF(cookieValue.c_str());

      CallVoidClassMethodFromAndroidEnvironment(
        attachment.env,
        cookieManager,
        "setCookie",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        jurl,
        jcookie
      );

      flushAndroidCookieManagerIfAvailable(attachment.env, cookieManager);

      attachment.env->DeleteLocalRef(jurl);
      attachment.env->DeleteLocalRef(jcookie);
      attachment.env->DeleteLocalRef(cookieManager);

      callback(true, "");
    #else
      callback(false, "Cookie APIs are not supported on this platform");
    #endif
    }, [callback](const auto& error) {
      callback(false, error);
    });
  }

  void remove (
    bridge::Bridge& bridge,
    const String& url,
    const String& name,
    const OkCallback callback
  ) {
    if (callback == nullptr) {
      return;
    }

    const auto urlString = trim(url);
    if (urlString.empty()) {
      return callback(false, "URL is required");
    }

    const auto cookieName = trim(name);
    if (cookieName.empty()) {
      return callback(false, "Cookie name is required");
    }

    dispatchOrError(bridge, [urlString, cookieName, &bridge, callback]() mutable {
    #if ORO_RUNTIME_PLATFORM_APPLE
      const auto components = URL::Components::parse(urlString);
      const auto host = toLowerCase(trim(components.authority));
      const auto path = components.pathname.size() ? components.pathname : "/";
      const auto schemeValue = toLowerCase(trim(components.scheme));
      const bool isSecureScheme = (
        schemeValue == "https" ||
        scheme::isRuntimeScheme(schemeValue)
      );
      if (host.empty()) {
        return callback(true, "");
      }

      WKHTTPCookieStore* cookieStore = WKWebsiteDataStore.defaultDataStore.httpCookieStore;
      if (cookieStore == nullptr) {
        return callback(false, "Cookie store is unavailable");
      }

      [cookieStore getAllCookies: ^(NSArray<NSHTTPCookie*>* nsCookies) {
        NSMutableArray<NSHTTPCookie*>* toDelete = [NSMutableArray array];

        for (NSHTTPCookie* nsCookie in nsCookies) {
          if (!nsCookie) continue;

          if (nsCookie.name == nil || nsCookie.name.UTF8String == nullptr) {
            continue;
          }

          const auto nameValue = String(nsCookie.name.UTF8String);
          if (nameValue != cookieName) {
            continue;
          }

          String cookieDomain = "";
          if (nsCookie.domain && nsCookie.domain.UTF8String != nullptr) {
            cookieDomain = nsCookie.domain.UTF8String;
          }

          if (!hostMatchesDomain(host, cookieDomain)) {
            continue;
          }

          String cookiePath = "/";
          if (nsCookie.path && nsCookie.path.UTF8String != nullptr) {
            cookiePath = nsCookie.path.UTF8String;
          }

          if (cookiePath.empty()) {
            cookiePath = "/";
          }

          if (!pathMatchesCookiePath(path, cookiePath)) {
            continue;
          }

          if (nsCookie.isSecure && !isSecureScheme) {
            continue;
          }

          [toDelete addObject: nsCookie];
        }

        if (toDelete.count == 0) {
          return callback(true, "");
        }

        auto remaining = std::make_shared<Atomic<int>>(static_cast<int>(toDelete.count));
        for (NSHTTPCookie* cookie in toDelete) {
          [cookieStore deleteCookie: cookie completionHandler: ^() {
            if (remaining->fetch_sub(1) == 1) {
              callback(true, "");
            }
          }];
        }
      }];
    #elif ORO_RUNTIME_PLATFORM_LINUX
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback(false, "WebView is not ready");
      }

      auto context = webkit_web_view_get_context(webview);
      if (context == nullptr) {
        return callback(false, "WebView context is unavailable");
      }

      auto cookieManager = webkit_web_context_get_cookie_manager(context);
      if (cookieManager == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      struct Context {
        OkCallback callback;
        String name;
      };

      const auto components = URL::Components::parse(urlString);
      const auto host = normaliseTlsPinHost(components.authority);
      if (host.empty()) {
        return callback(true, "");
      }

      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      auto ctx = new Context { callback, cookieName };

      webkit_cookie_manager_get_cookies(
        cookieManager,
        cookieURL.c_str(),
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
          auto ctx = static_cast<Context*>(userData);
          GError* error = nullptr;
          auto manager = WEBKIT_COOKIE_MANAGER(source);
          auto cookies = webkit_cookie_manager_get_cookies_finish(manager, result, &error);

          if (error != nullptr) {
            const auto message = error->message != nullptr ? String(error->message) : String("Cookie query failed");
            g_error_free(error);
            ctx->callback(false, message);
            delete ctx;
            return;
          }

          Vector<SoupCookie*> toDelete;
          for (auto item = cookies; item != nullptr; item = item->next) {
            const auto cookie = static_cast<SoupCookie*>(item->data);
            if (cookie == nullptr) {
              continue;
            }

            const auto name = soup_cookie_get_name(cookie) ? String(soup_cookie_get_name(cookie)) : "";
            if (name == ctx->name) {
              auto copy = soup_cookie_copy(cookie);
              if (copy != nullptr) {
                toDelete.push_back(copy);
              }
            }
          }

          g_list_free_full(cookies, reinterpret_cast<GDestroyNotify>(soup_cookie_free));

          if (toDelete.empty()) {
            ctx->callback(true, "");
            delete ctx;
            return;
          }

          struct DeleteContext {
            OkCallback callback;
            std::shared_ptr<Atomic<int>> remaining;
            Vector<SoupCookie*> cookies;
            AtomicBool hadError = false;
            String error;
          };

          auto remaining = std::make_shared<Atomic<int>>(static_cast<int>(toDelete.size()));
          auto dctx = new DeleteContext { ctx->callback, remaining, std::move(toDelete), false, "" };

          for (auto cookie : dctx->cookies) {
            webkit_cookie_manager_delete_cookie(
              manager,
              cookie,
              nullptr,
              +[](GObject* source, GAsyncResult* result, gpointer userData) {
                auto dctx = static_cast<DeleteContext*>(userData);
                GError* error = nullptr;
                const auto ok = webkit_cookie_manager_delete_cookie_finish(
                  WEBKIT_COOKIE_MANAGER(source),
                  result,
                  &error
                );

                if (!ok || error != nullptr) {
                  if (!dctx->hadError.exchange(true)) {
                    dctx->error = error && error->message
                      ? String(error->message)
                      : String("Cookie delete failed");
                  }
                }

                if (error) g_error_free(error);

                if (dctx->remaining->fetch_sub(1) == 1) {
                  for (auto cookie : dctx->cookies) {
                    if (cookie != nullptr) {
                      soup_cookie_free(cookie);
                    }
                  }

                  const auto okAll = dctx->error.empty();
                  dctx->callback(okAll, okAll ? "" : dctx->error);
                  delete dctx;
                }
              },
              dctx
            );
          }

          delete ctx;
        },
        ctx
      );
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback(false, "WebView is not ready");
      }

      Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookieManager;
      auto webview2 = reinterpret_cast<ICoreWebView2_2*>(webview);
      if (webview2 == nullptr || webview2->get_CookieManager(&cookieManager) != S_OK || !cookieManager) {
        return callback(false, "Cookie manager is unavailable");
      }

      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      const auto uri = convertStringToWString(cookieURL);
      cookieManager->GetCookies(
        uri.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2GetCookiesCompletedHandler>(
          [cookieManager, cookieName, callback](HRESULT result, ICoreWebView2CookieList* list) -> HRESULT {
            if (result != S_OK || list == nullptr) {
              callback(false, "Cookie query failed");
              return S_OK;
            }

            UINT count = 0;
            list->get_Count(&count);

            for (UINT i = 0; i < count; ++i) {
              Microsoft::WRL::ComPtr<ICoreWebView2Cookie> cookie;
              if (list->GetValueAtIndex(i, &cookie) != S_OK || !cookie) {
                continue;
              }

              LPWSTR nameW = nullptr;
              cookie->get_Name(&nameW);
              const auto cookieNameValue = nameW ? convertWStringToString(nameW) : "";
              if (nameW) CoTaskMemFree(nameW);

              if (cookieNameValue == cookieName) {
                cookieManager->DeleteCookie(cookie.Get());
              }
            }

            callback(true, "");
            return S_OK;
          }
        ).Get()
      );
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      auto app = app::App::sharedApplication();
      if (app == nullptr) {
        return callback(false, "Application is invalid state");
      }

      const auto cookieURL = normalizeCookieURLForStore(bridge, urlString);
      const auto components = URL::Components::parse(cookieURL);
      const auto host = normaliseTlsPinHost(components.authority);
      const auto requestPath = components.pathname.size() ? components.pathname : "/";

      Vector<String> paths;
      auto addPath = [&paths](const String& value) {
        if (value.empty()) {
          return;
        }

        for (const auto& existing : paths) {
          if (existing == value) {
            return;
          }
        }

        paths.push_back(value);
      };

      addPath("/");
      if (!requestPath.empty() && requestPath.front() == '/') {
        addPath(requestPath);

        for (size_t i = 1; i < requestPath.size(); ++i) {
          if (requestPath[i] != '/') {
            continue;
          }

          addPath(requestPath.substr(0, i));
          addPath(requestPath.substr(0, i + 1));
        }
      }

      Vector<String> cookies;
      for (const auto& path : paths) {
        if (path.empty() || path.front() != '/') {
          continue;
        }

        cookies.push_back(cookieName + "=; Max-Age=0; Path=" + path);

        if (!host.empty()) {
          cookies.push_back(cookieName + "=; Max-Age=0; Path=" + path + "; Domain=" + host);
          cookies.push_back(cookieName + "=; Max-Age=0; Path=" + path + "; Domain=." + host);
        }
      }

      auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);

      const auto cookieManagerClass = attachment.env->FindClass("android/webkit/CookieManager");
      if (cookieManagerClass == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      const auto getInstance = attachment.env->GetStaticMethodID(
        cookieManagerClass,
        "getInstance",
        "()Landroid/webkit/CookieManager;"
      );

      if (getInstance == nullptr) {
        attachment.env->DeleteLocalRef(cookieManagerClass);
        return callback(false, "Cookie manager is unavailable");
      }

      const auto cookieManager = attachment.env->CallStaticObjectMethod(cookieManagerClass, getInstance);
      attachment.env->DeleteLocalRef(cookieManagerClass);

      if (cookieManager == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      const auto jurl = attachment.env->NewStringUTF(cookieURL.c_str());
      for (const auto& expired : cookies) {
        const auto jcookie = attachment.env->NewStringUTF(expired.c_str());
        CallVoidClassMethodFromAndroidEnvironment(
          attachment.env,
          cookieManager,
          "setCookie",
          "(Ljava/lang/String;Ljava/lang/String;)V",
          jurl,
          jcookie
        );
        attachment.env->DeleteLocalRef(jcookie);
      }

      flushAndroidCookieManagerIfAvailable(attachment.env, cookieManager);

      attachment.env->DeleteLocalRef(jurl);
      attachment.env->DeleteLocalRef(cookieManager);

      callback(true, "");
    #else
      callback(false, "Cookie APIs are not supported on this platform");
    #endif
    }, [callback](const auto& error) {
      callback(false, error);
    });
  }

  void clear (bridge::Bridge& bridge, const OkCallback callback) {
    if (callback == nullptr) {
      return;
    }

    dispatchOrError(bridge, [&bridge, callback]() mutable {
    #if ORO_RUNTIME_PLATFORM_APPLE
      WKHTTPCookieStore* cookieStore = WKWebsiteDataStore.defaultDataStore.httpCookieStore;
      if (cookieStore == nullptr) {
        return callback(false, "Cookie store is unavailable");
      }

      [cookieStore getAllCookies: ^(NSArray<NSHTTPCookie*>* nsCookies) {
        if (nsCookies == nullptr || nsCookies.count == 0) {
          return callback(true, "");
        }

        auto remaining = std::make_shared<Atomic<int>>(static_cast<int>(nsCookies.count));
        for (NSHTTPCookie* cookie in nsCookies) {
          if (!cookie) {
            if (remaining->fetch_sub(1) == 1) {
              callback(true, "");
            }
            continue;
          }

          [cookieStore deleteCookie: cookie completionHandler: ^() {
            if (remaining->fetch_sub(1) == 1) {
              callback(true, "");
            }
          }];
        }
      }];
    #elif ORO_RUNTIME_PLATFORM_LINUX
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback(false, "WebView is not ready");
      }

      auto context = webkit_web_view_get_context(webview);
      if (context == nullptr) {
        return callback(false, "WebView context is unavailable");
      }

      auto cookieManager = webkit_web_context_get_cookie_manager(context);
      if (cookieManager == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      webkit_cookie_manager_replace_cookies(
        cookieManager,
        nullptr,
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
          auto callback = static_cast<OkCallback*>(userData);
          GError* error = nullptr;
          const auto ok = webkit_cookie_manager_replace_cookies_finish(
            WEBKIT_COOKIE_MANAGER(source),
            result,
            &error
          );

          if (!ok || error != nullptr) {
            const auto message = error && error->message ? String(error->message) : String("Cookie clear failed");
            if (error) g_error_free(error);
            (*callback)(false, message);
            delete callback;
            return;
          }

          (*callback)(true, "");
          delete callback;
        },
        new OkCallback(callback)
      );
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      auto webview = getWebViewForBridge(bridge);
      if (webview == nullptr) {
        return callback(false, "WebView is not ready");
      }

      Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookieManager;
      auto webview2 = reinterpret_cast<ICoreWebView2_2*>(webview);
      if (webview2 == nullptr || webview2->get_CookieManager(&cookieManager) != S_OK || !cookieManager) {
        return callback(false, "Cookie manager is unavailable");
      }

      const auto hr = cookieManager->DeleteAllCookies();
      callback(hr == S_OK, hr == S_OK ? "" : "Cookie clear failed");
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      auto app = app::App::sharedApplication();
      if (app == nullptr) {
        return callback(false, "Application is invalid state");
      }

      auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);

      const auto cookieManagerClass = attachment.env->FindClass("android/webkit/CookieManager");
      if (cookieManagerClass == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      const auto getInstance = attachment.env->GetStaticMethodID(
        cookieManagerClass,
        "getInstance",
        "()Landroid/webkit/CookieManager;"
      );

      if (getInstance == nullptr) {
        attachment.env->DeleteLocalRef(cookieManagerClass);
        return callback(false, "Cookie manager is unavailable");
      }

      const auto cookieManager = attachment.env->CallStaticObjectMethod(cookieManagerClass, getInstance);
      attachment.env->DeleteLocalRef(cookieManagerClass);

      if (cookieManager == nullptr) {
        return callback(false, "Cookie manager is unavailable");
      }

      if (!removeAllAndroidCookies(attachment.env, cookieManager)) {
        callback(false, "Cookie manager is unavailable");
        attachment.env->DeleteLocalRef(cookieManager);
        return;
      }

      flushAndroidCookieManagerIfAvailable(attachment.env, cookieManager);

      callback(true, "");
      attachment.env->DeleteLocalRef(cookieManager);
    #else
      callback(false, "Cookie APIs are not supported on this platform");
    #endif
    }, [callback](const auto& error) {
      callback(false, error);
    });
  }
}
