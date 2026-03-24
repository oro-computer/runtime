// vim: set sw=2:
package oro.runtime.bridge

import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.net.http.SslCertificate
import android.net.http.SslError
import android.util.Base64
import android.webkit.SslErrorHandler
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView

import java.io.ByteArrayInputStream
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate

import oro.runtime.app.App
import oro.runtime.debug.console
import oro.runtime.webview.Navigator
import oro.runtime.webview.SchemeHandlers
import oro.runtime.webview.WebViewClient
import oro.runtime.window.Window
import oro.runtime.window.WindowManagerActivity

private fun isAndroidAssetsUri (uri: Uri): Boolean {
  if (uri.pathSegments.size == 0) {
    return false
  }

  val scheme = uri.scheme
  val host = uri.host
  // handle no path segments, not currently required but future proofing
  val path = uri.pathSegments?.get(0)

  if (host == "appassets.androidplatform.net") {
    return true
  }

  if (scheme == "file" && host == "" && path == "android_asset") {
    return true
  }

  return false
}

open class Bridge (
  val index: Int,
  val activity: WindowManagerActivity,
  val window: Window
): WebViewClient() {
  open val schemeHandlers = SchemeHandlers(this)
  open val navigator = Navigator(this)
  open val buffers = mutableMapOf<String, ByteArray>()

  private fun normaliseTlsPinHost (rawHost: String?): String {
    if (rawHost == null) {
      return ""
    }

    var host = rawHost.trim().lowercase()
    if (host.isEmpty()) {
      return ""
    }

    val schemeIndex = host.indexOf("://")
    host = when {
      schemeIndex >= 0 -> host.substring(schemeIndex + 3)
      host.startsWith("//") -> host.substring(2)
      else -> host
    }

    val cut = host.indexOfAny(charArrayOf('/', '?', '#'))
    if (cut >= 0) {
      host = host.substring(0, cut)
    }

    val at = host.lastIndexOf('@')
    if (at >= 0) {
      host = host.substring(at + 1)
    }

    host = host.trim()
    if (host.isEmpty()) {
      return ""
    }

    while (host.endsWith(".")) {
      host = host.dropLast(1)
    }

    if (host.isEmpty()) {
      return ""
    }

    if (host.startsWith("[")) {
      val end = host.indexOf(']')
      if (end <= 1) {
        return ""
      }

      val inner = host.substring(1, end)
      val suffix = host.substring(end + 1)

      if (suffix.isEmpty()) {
        return inner
      }

      if (!suffix.startsWith(":")) {
        return ""
      }

      val port = suffix.substring(1)
      if (port.isEmpty() || !port.all { it in '0'..'9' }) {
        return ""
      }

      return inner
    }

    val colon = host.lastIndexOf(':')
    if (colon >= 0) {
      val prefix = host.substring(0, colon)
      val suffix = if (colon + 1 < host.length) host.substring(colon + 1) else ""
      if (!prefix.contains(':')) {
        if (prefix.isEmpty() || suffix.isEmpty()) {
          return ""
        }

        if (!suffix.all { it in '0'..'9' }) {
          return ""
        }

        host = prefix
        while (host.endsWith(".")) {
          host = host.dropLast(1)
        }

        if (host.isEmpty()) {
          return ""
        }
      }
    }

    return host
  }

  private fun normaliseTlsPinEndpoint (rawHost: String?): String {
    if (rawHost == null) {
      return ""
    }

    var host = rawHost.trim().lowercase()
    if (host.isEmpty()) {
      return ""
    }

    val schemeIndex = host.indexOf("://")
    host = when {
      schemeIndex >= 0 -> host.substring(schemeIndex + 3)
      host.startsWith("//") -> host.substring(2)
      else -> host
    }

    val cut = host.indexOfAny(charArrayOf('/', '?', '#'))
    if (cut >= 0) {
      host = host.substring(0, cut)
    }

    val at = host.lastIndexOf('@')
    if (at >= 0) {
      host = host.substring(at + 1)
    }

    host = host.trim()
    if (host.isEmpty()) {
      return ""
    }

    while (host.endsWith(".")) {
      host = host.dropLast(1)
    }

    if (host.isEmpty()) {
      return ""
    }

    if (host.startsWith("[")) {
      val end = host.indexOf(']')
      if (end <= 1) {
        return ""
      }

      val inner = host.substring(1, end)
      val suffix = host.substring(end + 1)

      if (suffix.isEmpty()) {
        return inner
      }

      if (!suffix.startsWith(":")) {
        return ""
      }

      val port = suffix.substring(1)
      if (port.isEmpty() || !port.all { it in '0'..'9' }) {
        return ""
      }

      return "[$inner]:$port"
    }

    val colon = host.lastIndexOf(':')
    if (colon >= 0) {
      val prefix = host.substring(0, colon)
      val suffix = if (colon + 1 < host.length) host.substring(colon + 1) else ""
      if (!prefix.contains(':')) {
        if (prefix.isEmpty() || suffix.isEmpty()) {
          return ""
        }

        if (!suffix.all { it in '0'..'9' }) {
          return ""
        }

        var base = prefix
        while (base.endsWith(".")) {
          base = base.dropLast(1)
        }

        if (base.isEmpty()) {
          return ""
        }

        return "$base:$suffix"
      }
    }

    return host
  }

  private fun normaliseTlsPinToken (rawToken: String?): String? {
    if (rawToken == null) {
      return null
    }

    var token = rawToken.trim()
    if (token.isEmpty()) {
      return null
    }

    if (token.startsWith("sha256/", ignoreCase = true)) {
      token = token.substring("sha256/".length)
    }

    token = token.replace('-', '+').replace('_', '/')

    val remainder = token.length % 4
    if (remainder != 0) {
      token += "=".repeat(4 - remainder)
    }

    val decoded = try {
      Base64.decode(token, Base64.DEFAULT)
    } catch (_: IllegalArgumentException) {
      null
    } ?: return null

    if (decoded.size != 32) {
      return null
    }

    return Base64.encodeToString(decoded, Base64.NO_WRAP)
  }

  private fun parseTlsPins (raw: String): Map<String, List<String>> {
    val result = mutableMapOf<String, MutableList<String>>()

    raw.lineSequence().forEach { original ->
      var line = original.trim()
      if (line.isEmpty()) return@forEach

      val hashIndex = line.indexOf('#')
      val semiIndex = line.indexOf(';')
      val commentIndex = when {
        hashIndex < 0 -> semiIndex
        semiIndex < 0 -> hashIndex
        else -> minOf(hashIndex, semiIndex)
      }

      if (commentIndex >= 0) {
        line = line.substring(0, commentIndex).trim()
        if (line.isEmpty()) return@forEach
      }

      val parts = line.split(Regex("\\s+")).filter { it.isNotEmpty() }
      if (parts.isEmpty()) return@forEach

      val firstToken = parts[0].trim()
      // Guard against accidentally providing pin tokens where a host is expected.
      if (
        firstToken.startsWith("sha256/", ignoreCase = true) ||
        normaliseTlsPinToken(firstToken) != null
      ) {
        return@forEach
      }

      val host = normaliseTlsPinEndpoint(parts[0])
      if (host.isEmpty()) return@forEach

      val pins = result.getOrPut(host) { mutableListOf() }

      // Fail-closed semantics: a configured host with no valid pins should
      // reject all certificates when evaluated.
      if (parts.size < 2) return@forEach

      parts.drop(1).forEach { rawPin ->
        val pin = normaliseTlsPinToken(rawPin) ?: return@forEach
        if (!pins.contains(pin)) {
          pins.add(pin)
        }
      }
    }

    return result
  }

  private fun isCertificateAllowedForHost (
    pins: Map<String, List<String>>,
    host: String?,
    certSha256: ByteArray
  ): Boolean {
    if (host == null || host.isEmpty()) {
      return true
    }

    val endpointKey = normaliseTlsPinEndpoint(host)
    if (endpointKey.isEmpty()) {
      return true
    }

    val hostKey = normaliseTlsPinHost(host)
    if (hostKey.isEmpty()) {
      return true
    }

    val hostPins = pins[endpointKey] ?: pins[hostKey] ?: return true

    if (certSha256.isEmpty()) {
      return false
    }

    if (hostPins.isEmpty()) {
      return false
    }

    val digestB64 = Base64.encodeToString(certSha256, Base64.NO_WRAP)
    return hostPins.any { it == digestB64 }
  }

  private fun hasTlsPinsForHost (
    pins: Map<String, List<String>>,
    host: String?
  ): Boolean {
    if (host == null || host.isEmpty()) {
      return false
    }

    val endpointKey = normaliseTlsPinEndpoint(host)
    if (endpointKey.isEmpty()) {
      return false
    }

    val hostKey = normaliseTlsPinHost(host)
    if (hostKey.isEmpty()) {
      return false
    }

    return pins.containsKey(endpointKey) || pins.containsKey(hostKey)
  }

  override fun shouldOverrideUrlLoading (
    view: WebView,
    request: WebResourceRequest
  ): Boolean {
    if (isAndroidAssetsUri(request.url)) {
      return false
    }

    val app = App.getInstance()
    val bundleIdentifier = app.getUserConfigValue("meta_bundle_identifier")

    if (request.url.host == bundleIdentifier) {
      return false
    }

    if (
      request.url.scheme == "ipc" ||
      request.url.scheme == "node" ||
      request.url.scheme == "npm" ||
      request.url.scheme == "oro"
    ) {
      return false
    }

    val scheme = request.url.scheme
    if (scheme != null && schemeHandlers.hasHandlerForScheme(scheme)) {
      return true
    }

    val allowed = this.navigator.isNavigationRequestAllowed(
      view.url ?: "",
      request.url.toString()
    )

    if (allowed) {
      return true
    }

    val intent = Intent(Intent.ACTION_VIEW, request.url)

    try {
      this.activity.startActivity(intent)
    } catch (err: Exception) {
      // TODO(jwerle): handle this error gracefully
      console.error(err.toString())
      return false
    }

    return true
  }

  override fun shouldInterceptRequest (
    view: WebView,
    request: WebResourceRequest
  ): WebResourceResponse? {
    return this.schemeHandlers.handleRequest(request)
  }

  override fun onReceivedSslError (
    view: WebView?,
    handler: SslErrorHandler,
    error: SslError
  ) {
    val pinsRaw = try {
      App.getInstance().getUserConfigValue("webview_tls_pins")
    } catch (_: Exception) {
      ""
    }.ifBlank {
      try {
        App.getInstance().getUserConfigValue("webview.tls_pins")
      } catch (_: Exception) {
        ""
      }
    }

    if (pinsRaw.isBlank()) {
      super.onReceivedSslError(view, handler, error)
      return
    }

    val pins = parseTlsPins(pinsRaw)

    val pinHost = try {
      val url = error.url
      if (url == null) {
        null
      } else {
        val uri = Uri.parse(url)
        val host = uri.host
        if (host == null || host.isEmpty()) {
          null
        } else {
          val explicitPort = uri.port
          val scheme = uri.scheme?.lowercase()
          val port = when {
            explicitPort > 0 -> explicitPort
            scheme == "https" -> 443
            scheme == "http" -> 80
            else -> -1
          }

          if (port > 0) {
            if (host.contains(':')) "[${host}]:${port}" else "${host}:${port}"
          } else {
            host
          }
        }
      }
    } catch (_: Exception) {
      null
    }

    // Preserve the platform default behaviour when no pins apply to this host.
    if (!hasTlsPinsForHost(pins, pinHost)) {
      super.onReceivedSslError(view, handler, error)
      return
    }

    val certificate = error.certificate
    val x509: X509Certificate? = try {
      if (certificate == null) {
        null
      } else {
        val bundle = SslCertificate.saveState(certificate)
        val bytes = bundle?.getByteArray("x509-certificate")

        if (bytes == null) {
          null
        } else {
          val factory = CertificateFactory.getInstance("X.509")
          factory.generateCertificate(ByteArrayInputStream(bytes)) as X509Certificate
        }
      }
    } catch (_: Exception) {
      null
    }

    if (x509 == null) {
      handler.cancel()
      return
    }

    val digest: ByteArray = try {
      val md = MessageDigest.getInstance("SHA-256")
      md.digest(x509.encoded)
    } catch (_: Exception) {
      handler.cancel()
      return
    }

    if (isCertificateAllowedForHost(pins, pinHost, digest)) {
      handler.proceed()
    } else {
      handler.cancel()
    }
  }

  override fun onPageStarted (
    view: WebView,
    url: String,
    favicon: Bitmap?
  ) {
    val uri = Uri.parse(url)
    if (uri.scheme != "about" && uri.authority != "__BUNDLE_IDENTIFIER__") {
      val preloadUserScript =
        try { this.window.getPreloadUserScript() } catch (_: Exception) { null }

      if (preloadUserScript != null) {
        view.evaluateJavascript(preloadUserScript, { _ -> })
      }
    }

    super.onPageStarted(view, url, favicon)
  }

  fun emit (event: String, data: String): Boolean {
    return this.emit(this.index, event, data)
  }

  @Throws(Exception::class)
  external fun emit (index: Int, event: String, data: String): Boolean
}
