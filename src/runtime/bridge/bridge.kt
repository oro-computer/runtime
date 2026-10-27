// vim: set sw=2:
package oro.runtime.bridge

import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.net.http.SslCertificate
import android.net.http.SslError
import android.webkit.SslErrorHandler
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.RenderProcessGoneDetail
import android.webkit.WebResourceError

import java.io.ByteArrayInputStream
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate

import oro.runtime.app.App
import oro.runtime.app.NativeTlsPolicy
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
  private val bufferTimes = mutableMapOf<String, Long>()

  fun storeBuffer (sequence: String, bytes: ByteArray): Boolean = synchronized(buffers) {
    val now = android.os.SystemClock.elapsedRealtime()
    for (key in bufferTimes.filterValues { now - it >= 60_000 }.keys.toList()) {
      buffers.remove(key)
      bufferTimes.remove(key)
    }
    if (sequence.length > 512 || bytes.size > 8_388_608 ||
        (!buffers.containsKey(sequence) && buffers.size >= 32) ||
        buffers.values.sumOf { it.size.toLong() } - (buffers[sequence]?.size ?: 0) + bytes.size > 33_554_432) {
      false
    } else {
      buffers[sequence] = bytes
      bufferTimes[sequence] = now
      true
    }
  }

  fun takeBuffer (sequence: String): ByteArray? = synchronized(buffers) {
    val storedAt = bufferTimes.remove(sequence) ?: return@synchronized null
    val bytes = buffers.remove(sequence)
    if (android.os.SystemClock.elapsedRealtime() - storedAt >= 60_000) null else bytes
  }

  fun clearBuffers () = synchronized(buffers) {
    buffers.clear()
    bufferTimes.clear()
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
      console.error("External navigation could not be opened")
      return true
    }

    return true
  }

  override fun shouldInterceptRequest (
    view: WebView,
    resourceRequest: WebResourceRequest
  ): WebResourceResponse? {
    val bundle = App.getInstance().getUserConfigValue("meta_bundle_identifier")
    val resourceUri = resourceRequest.url
    // Workers use the application's HTTPS origin to avoid mixed-content blocking.
    // Normalize before authentication so the endpoint uses the ordinary IPC policy.
    val request = if (resourceUri.scheme == "https" && resourceUri.host.equals(bundle, ignoreCase = true) &&
        resourceUri.port in setOf(-1, 443) && resourceUri.userInfo == null &&
        resourceUri.path?.startsWith("/__oro_ipc__/") == true) {
      val command = resourceUri.path!!.removePrefix("/__oro_ipc__/")
      if (!Regex("[A-Za-z0-9_.]+").matches(command)) {
        return WebResourceResponse("text/plain", "UTF-8", 400, "Bad Request", emptyMap(), ByteArrayInputStream(byteArrayOf()))
      }
      val ipcUri = Uri.parse("ipc://$command" + (resourceUri.encodedQuery?.let { "?$it" } ?: ""))
      object : WebResourceRequest by resourceRequest {
        override fun getUrl (): Uri = ipcUri
      }
    } else {
      resourceRequest
    }
    val uri = request.url
    val internalResource = uri.scheme in setOf("oro", "node", "npm") ||
      (uri.scheme in setOf("http", "https") && uri.host.equals(bundle, ignoreCase = true))
    val resourceOrigin = request.requestHeaders.entries.firstOrNull { it.key.equals("Origin", true) }?.value
    // HTML assets include per-process runtime configuration. A permissive CORS
    // response must not disclose that preload to a foreign frame's fetch.
    if (internalResource && resourceOrigin != null && !this.window.isTrustedOrigin(Uri.parse(resourceOrigin))) {
      return WebResourceResponse("text/plain", "UTF-8", 403, "Forbidden", emptyMap(), ByteArrayInputStream(byteArrayOf()))
    }
    if (request.url.scheme == "ipc") {
      // Preflight carries header names, never the document token. Answer it
      // without routing a native command; the following request must authenticate.
      if (request.method == "OPTIONS") {
        val origin = request.requestHeaders.entries.firstOrNull { it.key.equals("Origin", true) }?.value
        val headers = request.requestHeaders.entries.firstOrNull { it.key.equals("Access-Control-Request-Headers", true) }?.value ?: ""
        if (origin != null && this.window.isTrustedOrigin(Uri.parse(origin)) && headers.length <= 4096 &&
            (headers.isEmpty() || headers.split(',').all { Regex("[!#$%&'*+.^_`|~0-9A-Za-z-]+").matches(it.trim()) })) {
          return WebResourceResponse("text/plain", "UTF-8", 204, "No Content", mapOf(
            "Access-Control-Allow-Origin" to origin,
            "Access-Control-Allow-Methods" to "GET, POST, PUT, PATCH, DELETE, OPTIONS",
            "Access-Control-Allow-Headers" to headers,
            "Access-Control-Max-Age" to "600",
            "Vary" to "Origin"
          ), ByteArrayInputStream(byteArrayOf()))
        }
        return WebResourceResponse("text/plain", "UTF-8", 403, "Forbidden", emptyMap(), ByteArrayInputStream(byteArrayOf()))
      }
      if (!this.window.acceptsIpcRequest(request)) {
        return WebResourceResponse("text/plain", "UTF-8", 403, "Forbidden", emptyMap(),
          ByteArrayInputStream("IPC origin is not authorized".toByteArray()))
      }
    }
    return this.schemeHandlers.handleRequest(request)
  }

  override fun onReceivedSslError (
    view: WebView?,
    handler: SslErrorHandler,
    error: SslError
  ) {
    val pins = try {
      NativeTlsPolicy.pins(App.getInstance())
    } catch (_: Exception) {
      handler.cancel()
      return
    }

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
    if (!NativeTlsPolicy.hasTlsPinsForHost(pins, pinHost)) {
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

    if (NativeTlsPolicy.isCertificateAllowedForHost(pins, pinHost, digest)) {
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
    this.window.platformDocumentToken = ""
    oro.runtime.app.PlatformOperations.cancelWindow(this.window)
    this.window.activity.presentation.navigation(this.window)
    this.window.fragment.beginStartup()
    val uri = Uri.parse(url)
    if (this.window.isTrustedOrigin(uri) && uri.scheme != "about" && uri.authority != "__BUNDLE_IDENTIFIER__") {
      val preloadUserScript =
        try { this.window.getPreloadUserScript() } catch (_: Exception) { null }

      if (preloadUserScript != null) {
        view.evaluateJavascript(preloadUserScript, { _ -> })
      }
    }

    super.onPageStarted(view, url, favicon)
  }

  override fun onPageFinished (view: WebView, url: String) {
    super.onPageFinished(view, url)
    // A document load alone does not establish that the runtime bridge initialized.
    view.evaluateJavascript("typeof globalThis.__RUNTIME_INIT_NOW__ === 'number'") { result ->
      if (result == "true" && this.window.userMessageHandler.active && this.window.fragment.hasWebView) {
        this.window.fragment.markReady()
      }
    }
  }

  override fun onReceivedError (view: WebView, request: WebResourceRequest, error: WebResourceError) {
    if (request.isForMainFrame && this.window.userMessageHandler.active) this.window.fragment.failStartup()
  }

  override fun onReceivedHttpError (view: WebView, request: WebResourceRequest, response: WebResourceResponse) {
    if (request.isForMainFrame && response.statusCode >= 400 && this.window.userMessageHandler.active) {
      this.window.fragment.failStartup()
    }
  }

  override fun onRenderProcessGone (view: WebView, detail: RenderProcessGoneDetail): Boolean {
    this.window.fragment.onRendererGone(view)
    return true
  }

  fun emit (event: String, data: String): Boolean {
    return this.emit(this.index, event, data)
  }

  @Throws(Exception::class)
  external fun emit (index: Int, event: String, data: String): Boolean
}
