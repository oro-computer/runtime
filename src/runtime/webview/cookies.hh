#ifndef ORO_RUNTIME_WEBVIEW_COOKIES_H
#define ORO_RUNTIME_WEBVIEW_COOKIES_H

#include "../platform.hh"

namespace oro::runtime::bridge {
  class Bridge;
}

namespace oro::runtime::webview::cookies {
  using CookieCallback = Function<void(const String& cookieHeader, const String& error)>;
  using OkCallback = Function<void(bool ok, const String& error)>;

  /**
   * Get cookies for `url` as a `Cookie` header value ("a=b; c=d").
   */
  void get (bridge::Bridge& bridge, const String& url, const CookieCallback callback);

  /**
   * Set a cookie for `url` from a `Set-Cookie` header value.
   */
  void set (bridge::Bridge& bridge, const String& url, const String& setCookie, const OkCallback callback);

  /**
   * Delete cookies matching `name` for `url`.
   */
  void remove (bridge::Bridge& bridge, const String& url, const String& name, const OkCallback callback);

  /**
   * Clear all cookies for the current WebView data store.
   */
  void clear (bridge::Bridge& bridge, const OkCallback callback);
}

#endif
