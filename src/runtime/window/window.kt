// vim: set sw=2:
package oro.runtime.window

import java.lang.ref.WeakReference

import kotlin.concurrent.thread

import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.AttributeSet
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.webkit.CookieManager
import android.webkit.WebSettings
import android.webkit.WebView
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Button
import android.webkit.GeolocationPermissions
import android.webkit.PermissionRequest
import android.webkit.JsPromptResult
import android.util.Base64
import kotlin.math.roundToInt

import androidx.appcompat.app.AppCompatActivity
import androidx.core.os.bundleOf
import androidx.fragment.app.Fragment
import androidx.fragment.app.commit
import androidx.webkit.WebViewCompat
import androidx.webkit.WebSettingsCompat
import androidx.webkit.WebViewFeature
import androidx.webkit.WebMessageCompat
import androidx.webkit.ScriptHandler
import org.json.JSONObject

import oro.runtime.app.App
import oro.runtime.bridge.Bridge
import oro.runtime.debug.console
import oro.runtime.ipc.Message
import oro.runtime.webview.WebChromeClient
import oro.runtime.window.Dialog
import oro.runtime.window.WindowManagerActivity

import __BUNDLE_IDENTIFIER__.R

/**
 * Represents the width and height of a frame, such as the webview
 */
data class Size (val width: Int, val height: Int);

/**
 * Represents the actual width of a screen
 */
data class ScreenSize (val width: Int, val height: Int);

/**
 * Represents a position a window may be located at
 */
data class Position (val top: Int, val left: Int);

/**
 * A container for configuring a `Window`
 */
data class WindowOptions (
  var index: Int = 0,
  var shouldExitApplicationOnClose: Boolean = false,
  var headless: Boolean = false
)

/** Absolute WebView zoom has one owner and never compounds earlier scaling. */
internal object SystemTextScaling {
  fun zoom (fontScale: Float, policy: String): Int? {
    if (policy == "manual") return null
    if (policy.isBlank() || policy == "system") {
      val scale = if (fontScale.isFinite() && fontScale > 0) fontScale else 1f
      return (scale.coerceIn(0.01f, 10f) * 100).roundToInt().coerceIn(1, 1000)
    }
    return policy.toIntOrNull()?.takeIf { it in 1..1000 && it.toString() == policy }
      ?: throw IllegalArgumentException("webview.text_zoom must be system, manual, or an integer from 1 to 1000")
  }

  fun apply (webview: WebView, headless: Boolean, policy: String, fontScale: Float) {
    if (headless) return
    val zoom = zoom(fontScale, policy) ?: return
    if (webview.settings.textZoom != zoom) webview.settings.textZoom = zoom
  }
}

/**
 * External JavaScript interface attached to the webview at
 * `window.external`
 */
open class WindowWebViewUserMessageHandler (window: Window) {
  @Volatile var active = true
  val TAG = "WindowWebViewUserMessageHandler"

  val namespace = "external"
  val window = window

  open fun postMessage (value: String): Boolean {
    return this.postMessage(value, null)
  }

  /**
   * Low level external message handler
   */
  open fun postMessage (value: String, bytes: ByteArray? = null): Boolean {
    if (!this.active) return false
    if (value.length > 1_048_576 || !value.startsWith("ipc://")) return false
    val message = Message(value)

    if (message.seq.length > 0 && bytes != null) {
      if (!this.window.bridge.storeBuffer(message.seq, bytes)) return false
    }

    this.window.onMessage(value)
    return true
  }
}

/**
 */
open class WindowWebChromeClient (val window: Window) : WebChromeClient() {
  override fun onShowCustomView (view: View, callback: android.webkit.WebChromeClient.CustomViewCallback) {
    window.activity.presentation.showCustomView(window, view, callback)
  }

  override fun onHideCustomView () {
    window.activity.presentation.hideCustomView(window)
  }

  override fun onJsPrompt (view: WebView, url: String, message: String, value: String?, result: JsPromptResult): Boolean {
    if (!message.startsWith("oro:ipc:")) return super.onJsPrompt(view, url, message, value, result)
    val accepted = this.window.receiveSynchronousMessage(view, url, message, value)
    if (accepted) result.confirm("true") else result.cancel()
    return true
  }

  override fun onConsoleMessage (message: android.webkit.ConsoleMessage): Boolean {
    if (!App.getInstance().isDebugEnabled()) return true
    val text = "WebView: ${message.message()} (${message.sourceId()}:${message.lineNumber()})"
    when (message.messageLevel()) {
      android.webkit.ConsoleMessage.MessageLevel.ERROR -> console.error(text)
      android.webkit.ConsoleMessage.MessageLevel.WARNING -> console.debug(text)
      else -> return super.onConsoleMessage(message)
    }
    return true
  }

  override fun onGeolocationPermissionsShowPrompt (
    origin: String,
    callback: GeolocationPermissions.Callback
  ) {
    val app = App.getInstance()
    val allowed = this.window.userMessageHandler.active && this.window.fragment.window === this.window &&
      this.window.fragment.isVisible && this.window.isTrustedOrigin(Uri.parse(origin)) &&
      app.getUserConfigValue("permissions_allow_geolocation") == "true" &&
      (androidx.core.content.ContextCompat.checkSelfPermission(this.window.activity, android.Manifest.permission.ACCESS_FINE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED ||
       androidx.core.content.ContextCompat.checkSelfPermission(this.window.activity, android.Manifest.permission.ACCESS_COARSE_LOCATION) == android.content.pm.PackageManager.PERMISSION_GRANTED)
    callback(origin, allowed, allowed)
  }

  override fun onPermissionRequest (request: PermissionRequest) {
    if (!this.window.userMessageHandler.active || this.window.fragment.window !== this.window ||
        !this.window.fragment.hasWebView || !this.window.fragment.isVisible || this.window.fragment.options.headless ||
        !this.window.isTrustedOrigin(request.origin)) {
      request.deny()
      return
    }
    val resources = request.resources
    var grants = mutableListOf<String>()
    val app = App.getInstance()

    for (resource in resources) {
      when (resource) {
        PermissionRequest.RESOURCE_AUDIO_CAPTURE -> {
          if ((app.getUserConfigValue("permissions_allow_microphone") == "true" || app.getUserConfigValue("permissions_allow_user_media") == "true") &&
              androidx.core.content.ContextCompat.checkSelfPermission(this.window.activity, android.Manifest.permission.RECORD_AUDIO) == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            grants.add(PermissionRequest.RESOURCE_AUDIO_CAPTURE)
          }
        }

        PermissionRequest.RESOURCE_VIDEO_CAPTURE -> {
          if ((app.getUserConfigValue("permissions_allow_camera") == "true" || app.getUserConfigValue("permissions_allow_user_media") == "true") &&
              androidx.core.content.ContextCompat.checkSelfPermission(this.window.activity, android.Manifest.permission.CAMERA) == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            grants.add(PermissionRequest.RESOURCE_VIDEO_CAPTURE)
          }
        }

        PermissionRequest.RESOURCE_PROTECTED_MEDIA_ID -> {
          if (app.getUserConfigValue("webview_allow_protected_media") == "true") grants.add(resource)
        }
      }
    }

    if (grants.size > 0) {
      request.grant(grants.toTypedArray())
    } else {
      request.deny()
    }
  }

  override fun onShowFileChooser (
    webview: android.webkit.WebView,
    callback: android.webkit.ValueCallback<Array<android.net.Uri>>,
    params: android.webkit.WebChromeClient.FileChooserParams
  ): Boolean {
    val fragment = this.window.fragment
    val activity = fragment.requireActivity() as WindowManagerActivity
    val options = Dialog.FileSystemPickerOptions(
      params,
      multiple = params.mode == android.webkit.WebChromeClient.FileChooserParams.MODE_OPEN_MULTIPLE,
      owner = this.window
    )

    activity.dialog.showFileSystemPicker(options, fun (uris: Array<Uri>) {
      callback.onReceiveValue(uris)
    })

    return true
  }
}

/**
 */
open class Window (val fragment: WindowFragment) {
  val userMessageHandler = WindowWebViewUserMessageHandler(this)
  val activity = fragment.requireActivity() as WindowManagerActivity
  val bridge = Bridge(fragment.index, activity, this)
  val client = WindowWebChromeClient(this)
  val index = fragment.index
  private val bridgeNonce = java.util.UUID.randomUUID().toString()
  private var bridgeScript: ScriptHandler? = null
  private val trustedOrigins: Set<String> = buildSet {
    val app = App.getInstance()
    add("https://${app.getUserConfigValue("meta_bundle_identifier").lowercase()}")
    if (app.isDebugEnabled()) {
      val developmentOrigin = try { app.getDevelopmentOrigin() } catch (_: UnsatisfiedLinkError) { "" }
      originOf(Uri.parse(developmentOrigin))?.let { add(it) }
    }
    for (origin in app.getUserConfigValue("webview_bridge_origins").split(Regex("\\s+"))) {
      val uri = Uri.parse(origin)
      if (uri.scheme == "https" && (uri.path.isNullOrEmpty() || uri.path == "/") &&
          uri.query == null && uri.fragment == null) {
        originOf(uri)?.let { add(it) }
      }
    }
  }
  fun setPresentation (action: String, enabled: Boolean): String = activity.presentation.request(this, action, enabled)

  var title = ""

  var position = Position(0, 0)
  var frame = Size(0, 0)
  @Volatile var platformDocumentToken = ""

  init {
    val userMessageHandler = this.userMessageHandler
    val bridge = this.bridge
    val client = this.client

    fragment.webview.apply {
      // clients
      webViewClient = bridge
      webChromeClient = client

      if (!WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER) ||
          !WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT)) {
        console.error("Runtime startup requires WebView message-listener and document-start support")
        fragment.failStartup(R.string.runtime_webview_update_required)
        return@apply
      }
      WebViewCompat.addWebMessageListener(this, "__oroNativeBridge", trustedOrigins) { view, message, origin, mainFrame, _ ->
        if (mainFrame && isTrustedOrigin(origin) && userMessageHandler.active &&
            fragment.hasWebView && fragment.webview === view && message.type == WebMessageCompat.TYPE_STRING) {
          message.data?.let {
            if (it.startsWith("oro:document:")) {
              val token = it.removePrefix("oro:document:")
              if (Regex("[a-f0-9]{32}").matches(token)) platformDocumentToken = token
            } else {
              userMessageHandler.postMessage(it)
            }
          }
        }
      }
      // Synchronous bodies must be mapped before a synchronous IPC XHR reads
      // them. The prompt path validates the caller origin and a per-window nonce.
      bridgeScript = WebViewCompat.addDocumentStartJavaScript(this, """
        (() => {
          if (globalThis.top !== globalThis) return
          const prompt = globalThis.prompt.bind(globalThis)
          const bridge = globalThis.__oroNativeBridge
          const tokenBytes = new Uint8Array(16)
          globalThis.crypto.getRandomValues(tokenBytes)
          const documentToken = Array.from(tokenBytes, value => value.toString(16).padStart(2, '0')).join('')
          Object.defineProperty(globalThis, '__oroNativeDocumentToken', { value: documentToken })
          const announceDocument = () => bridge.postMessage('oro:document:' + documentToken)
          announceDocument()
          globalThis.addEventListener('pageshow', announceDocument)
          const open = XMLHttpRequest.prototype.open
          const setHeader = XMLHttpRequest.prototype.setRequestHeader
          const URLConstructor = URL
          XMLHttpRequest.prototype.open = function(method, url, ...args) {
            const destination = new URLConstructor(String(url), location.href)
            const result = open.call(this, method, destination.href, ...args)
            if (destination.protocol === 'ipc:') {
              setHeader.call(this, 'X-Oro-Bridge-Token', '$bridgeNonce')
            }
            return result
          }
          Object.defineProperty(globalThis, 'external', {
            configurable: false,
            value: Object.freeze({ postMessage(message, bytes) {
              if (typeof message !== 'string' || message.length > 1048576) {
                throw new RangeError('IPC message exceeds the supported size')
              }
              if (bytes !== undefined && bytes !== null) {
                const buffer = typeof bytes === 'string' ? new TextEncoder().encode(bytes)
                  : ArrayBuffer.isView(bytes) ? new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength)
                  : new Uint8Array(bytes)
                if (buffer.byteLength > 8388608) throw new RangeError('IPC body exceeds 8 MiB; split the operation into chunks')
                const parts = []
                for (let offset = 0; offset < buffer.length; offset += 8192) {
                  parts.push(String.fromCharCode(...buffer.subarray(offset, offset + 8192)))
                }
                const payload = JSON.stringify({ message, bytes: btoa(parts.join('')) })
                if (prompt('oro:ipc:$bridgeNonce', payload) !== 'true') {
                  throw new Error('Native IPC body mapping was rejected')
                }
              } else {
                bridge.postMessage(message)
              }
              return true
            }})
          })
        })()
      """.trimIndent(), trustedOrigins)
    }
  }

  private fun originOf (uri: Uri): String? {
    val scheme = uri.scheme?.lowercase() ?: return null
    val host = uri.host?.lowercase()?.removeSurrounding("[", "]") ?: return null
    if (scheme !in setOf("http", "https") || uri.userInfo != null || host.isEmpty() ||
        host.contains('*') || host.any { it.isWhitespace() || it.code > 127 }) return null
    val authority = if (host.contains(':')) "[$host]" else host
    val port = uri.port
    if (port !in -1..65535 || port == 0 ||
        !uri.encodedAuthority.equals(authority + if (port == -1) "" else ":$port", ignoreCase = true)) return null
    val suffix = if (port == -1 || (scheme == "https" && port == 443) || (scheme == "http" && port == 80)) "" else ":$port"
    return "$scheme://$authority$suffix"
  }

  fun isTrustedOrigin (uri: Uri): Boolean = originOf(uri) in this.trustedOrigins

  fun receiveSynchronousMessage (view: WebView, url: String, message: String, value: String?): Boolean {
    if (!this.userMessageHandler.active || !this.fragment.hasWebView || this.fragment.webview !== view ||
        message != "oro:ipc:${this.bridgeNonce}" || !this.isTrustedOrigin(Uri.parse(url)) ||
        value == null || value.length > 12_582_912) return false
    return try {
      val payload = JSONObject(value)
      val bytes = Base64.decode(payload.getString("bytes"), Base64.NO_WRAP)
      if (bytes.size > 8_388_608) return false
      this.userMessageHandler.postMessage(payload.getString("message"), bytes)
    } catch (_: IllegalArgumentException) {
      false
    } catch (_: org.json.JSONException) {
      false
    }
  }

  fun detachBridge (view: WebView) {
    this.userMessageHandler.active = false
    this.bridgeScript?.remove()
    this.bridgeScript = null
    if (WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
      WebViewCompat.removeWebMessageListener(view, "__oroNativeBridge")
    }
  }

  fun acceptsIpcRequest (request: android.webkit.WebResourceRequest): Boolean {
    if (!userMessageHandler.active || request.isForMainFrame || request.url.toString().length > 1_048_576) return false
    fun header (name: String) = request.requestHeaders.entries.firstOrNull { it.key.equals(name, true) }?.value
    val origin = header("Origin")
    // IPC is cross-scheme even for a legitimate application caller. Its
    // document-start token authenticates requests independently of fetch-site.
    if (header("X-Oro-Bridge-Token") == bridgeNonce) {
      return origin == null || origin == "null" || isTrustedOrigin(Uri.parse(origin))
    }
    // Worker XHR does not inherit the document's nonce closure. Its browser-set
    // Origin must match an explicitly trusted application origin.
    return header("Runtime-Frame-Type") == "worker" && origin != null && isTrustedOrigin(Uri.parse(origin))
  }

  fun startNavigation () {
    thread {
      val pendingNavigationLocation = this.getPendingNavigationLocation(this.index)
      if (pendingNavigationLocation.length > 0) {
        this.navigate(pendingNavigationLocation)
      }
    }
  }

  fun evaluateJavaScript (source: String, token: String): Boolean {
    if (!this.userMessageHandler.active) return false
    this.activity.runOnUiThread {
      if (!this.userMessageHandler.active || this.fragment.window !== this || !this.fragment.hasWebView) return@runOnUiThread
      this.fragment.webview.evaluateJavascript(source) { result ->
        if (this.userMessageHandler.active && this.fragment.window === this) {
          this.onEvaluateJavascriptResult(this.index, token, result)
        }
      }
    }
    return true
  }

  fun show () {
    val fragment = this.fragment
    val activity = fragment.activity
    val manager = activity?.supportFragmentManager
    activity?.runOnUiThread {
      manager?.commit {
        show(fragment)
      }
    }
  }

  fun hide () {
    val fragment = this.fragment
    val activity = fragment.activity
    val manager = activity?.supportFragmentManager
    activity?.runOnUiThread {
      manager?.commit {
        hide(fragment)
      }
    }
  }

  fun close () {
    this.bridge.activity.runOnUiThread {
      this.onClose(this.index)
      // A lost renderer has already detached the native window's Java reference.
      this.activity.closeWindow(this.index)
    }
  }

  fun navigate (url: String) {
    val fragment = this.fragment
    val activity = fragment.activity
    val bundleIdentifier = this.getBundleIdentifier()
    thread(name = "oro-package-navigation") {
      if (!App.getInstance().verifyPackagedAssets()) {
        activity?.runOnUiThread { if (fragment.window === this) fragment.failStartup(R.string.runtime_package_mismatch) }
        return@thread
      }
      activity?.runOnUiThread {
      if (!fragment.hasWebView || !this.userMessageHandler.active) return@runOnUiThread
      val webview = fragment.webview
      fragment.rememberStartupLocation(url)
      val uri = Uri.parse(url)
      val isRuntimeScheme = uri.scheme == "oro" && uri.host == bundleIdentifier

      if (isRuntimeScheme) {
        val httpsUrl = url.replace("oro:", "https:")
        webview.loadUrl(httpsUrl)
      } else {
        webview.loadUrl(url)
      }
      }
    }
  }

  fun getBundleIdentifier (): String {
    return this.getBundleIdentifier(this.index)
  }

  fun getPreloadUserScript (): String {
    return this.getPreloadUserScript(this.index)
  }

  fun getScreen (): ScreenSize {
    return ScreenSize(
      this.bridge.activity.getScreenSizeWidth(),
      this.bridge.activity.getScreenSizeHeight()
    )
  }

  fun getSize (): Size {
    return this.frame
  }

  fun setSize (width: Int, height: Int) {
    return this.setSize(Size(width, height))
  }

  fun setSize (size: Size) {
    this.activity.runOnUiThread {
      if (!this.userMessageHandler.active || !this.fragment.hasWebView) return@runOnUiThread
      val viewport = this.fragment.view?.parent as? View
      val screen = this.getScreen()
      val containerWidth = viewport?.width?.takeIf { it > 0 } ?: screen.width
      val containerHeight = viewport?.height?.takeIf { it > 0 } ?: screen.height
      // Screen-sized requests exclude system bars even when the container draws
      // behind them. Both dimensions must keep following the container.
      val availableWidth = if (screen.width > 0) minOf(containerWidth, screen.width) else containerWidth
      val availableHeight = if (screen.height > 0) minOf(containerHeight, screen.height) else containerHeight
      // Keep full-view dimensions attached to the container through rotation,
      // window resizing, system insets, and keyboard visibility changes.
      val width = if (availableWidth > 0 && size.width >= availableWidth) {
        ViewGroup.LayoutParams.MATCH_PARENT
      } else {
        size.width
      }
      val height = if (availableHeight > 0 && size.height >= availableHeight) {
        ViewGroup.LayoutParams.MATCH_PARENT
      } else {
        size.height
      }
      val requested = FrameLayout.LayoutParams(width, height)
      if (!this.activity.presentation.resize(this, requested)) this.fragment.webview.layoutParams = requested
    }
  }

  fun handleApplicationURL (url: String) {
    return this.handleApplicationURL(this.index, url)
  }

  fun onReady () {
    this.bridge.activity.runOnUiThread {
      this.onReady(this.index)
    }
  }

  fun onMessage (value: String, bytes: ByteArray? = null) {
    this.onMessage(this.index, value, bytes)
  }

  @Throws(Exception::class)
  external fun onReady (index: Int): Unit

  @Throws(Exception::class)
  external fun onViewDestroyed (index: Int): Unit

  @Throws(Exception::class)
  external fun onClose (index: Int): Unit

  @Throws(Exception::class)
  external fun onMessage (index: Int, value: String, bytes: ByteArray? = null): Unit

  @Throws(Exception::class)
  external fun onEvaluateJavascriptResult (index: Int, token: String, result: String): Unit

  @Throws(Exception::class)
  external fun getPendingNavigationLocation (index: Int): String

  @Throws(Exception::class)
  external fun getPreloadUserScript (index: Int): String

  @Throws(Exception::class)
  external fun handleApplicationURL (index: Int, url: String): Unit

  @Throws(Exception::class)
  external fun getBundleIdentifier (index: Int): String
}

interface IWindowFragment {
  fun onBackPressed (): Boolean;
}

/**
 * A fragment that contains a single `WebView`. A `WindowFragment` is managed
 * by a `WindowFragmentManager` and holds a reference to a `oro::ManagedWindow`
 * instance managed by `oro::WindowManager` in the native runtime.
 */
open class WindowFragment : Fragment(R.layout.web_view), IWindowFragment {
  companion object {
    /**
     * Factory for creating new `WindowFragment` instances from a `Bundle`
     */
    fun newInstance (bundle: Bundle): WindowFragment {
      return WindowFragment().apply {
        arguments = bundle
      }
    }

    /**
     * Factory for creating new `WindowFragment` instances from a `WindowOptions`
     */
    fun newInstance (options: WindowOptions): WindowFragment {
      return WindowFragment().apply {
        arguments = bundleOf(
          "index" to options.index,
          "shouldExitApplicationOnClose" to options.shouldExitApplicationOnClose,
          "headless" to options.headless
        )
      }
    }
  }

  /**
   * The options used to create this `WindowFragment`.
   */
  open val options = WindowOptions()

  /**
   * XXX(@jwerle)
   */
  open var window: Window? = null

  /**
   * XXX(@jwerle)
   */
  open var index: Int
    get () = this.options.index
    set (index) { this.options.index = index }

  /**
   * A reference to the child `WebView` owned by this `WindowFragment`
   */
  private var currentWebView: WebView? = null
  open val webview: WebView
    get () = checkNotNull(this.currentWebView) { "Window has no active WebView" }
  val hasWebView: Boolean
    get () = this.currentWebView != null

  private fun applySystemTextScale (configuration: Configuration) {
    val webview = this.currentWebView ?: return
    SystemTextScaling.apply(webview, options.headless,
      App.getInstance().getUserConfigValue("webview_text_zoom"), configuration.fontScale)
  }

  override fun onConfigurationChanged (configuration: Configuration) {
    super.onConfigurationChanged(configuration)
    applySystemTextScale(configuration)
  }

  private var content: FrameLayout? = null
  private var recovery: LinearLayout? = null
  private var startupLocation: String? = null
  private var loading = false
  private var failed = false
  private var startupPresented = false
  internal val readyForPresentation: Boolean get() = hasWebView && !loading && !failed
  private var startupStartedAt = 0L
  private val startupHandler = Handler(Looper.getMainLooper())
  private val startupWatchdog = Runnable { this.checkStartupDeadline() }

  fun rememberStartupLocation (url: String) {
    if (this.startupLocation == null) this.startupLocation = url
  }

  fun beginStartup () {
    if (this.failed) {
      this.currentWebView?.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
      this.showRecovery(true)
      return
    }
    if (this.loading) {
      if (this.startupPresented) {
        this.startupPresented = false
        this.currentWebView?.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        this.showRecovery(false)
      }
      return
    }
    this.loading = true
    this.startupPresented = false
    this.startupStartedAt = SystemClock.elapsedRealtime()
    oro.runtime.app.RuntimeLifecycleEvents.record(index, "loading")
    this.currentWebView?.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
    this.showRecovery(false)
    this.startupHandler.postDelayed(this.startupWatchdog, 30_000)
  }

  fun checkStartupDeadline () {
    if (this.loading && SystemClock.elapsedRealtime() - this.startupStartedAt >= 30_000) {
      this.failStartup()
    }
  }

  /** Presentation releases the loading cover, never the readiness watchdog. */
  internal fun presentStartup (call: oro.runtime.app.PlatformCall) {
    val owner = call.document()
    val webview = this.currentWebView ?: throw oro.runtime.app.PlatformFailure("ENOTREADY")
    fun validate () {
      val current = call.document()
      val uri = Uri.parse(webview.url ?: "")
      if (current !== owner || current.fragment !== this || this.currentWebView !== webview || this.failed) {
        throw oro.runtime.app.PlatformFailure("ABORT_ERR")
      }
      if (!call.app.packagedAssetsVerified || uri.scheme != "https" || uri.authority != owner.getBundleIdentifier()) {
        throw oro.runtime.app.PlatformFailure("EACCES")
      }
    }
    validate()
    if (!this.loading || this.startupPresented) {
      call.success()
      return
    }
    webview.postVisualStateCallback(0, object : WebView.VisualStateCallback() {
      override fun onComplete (requestId: Long) {
        if (call.done.get()) return
        try {
          validate()
          if (this@WindowFragment.loading && !this@WindowFragment.startupPresented) {
            oro.runtime.app.RuntimeLifecycleEvents.record(index, "presented", SystemClock.elapsedRealtime() - startupStartedAt)
            this@WindowFragment.recovery?.let { this@WindowFragment.content?.removeView(it) }
            this@WindowFragment.recovery = null
            this@WindowFragment.startupPresented = true
            webview.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_AUTO
          }
          call.success()
        } catch (error: oro.runtime.app.PlatformFailure) { call.failure(error.code)
        } catch (_: Exception) {
          call.failure("EIO")
          this@WindowFragment.failStartup()
        }
      }
    })
  }

  fun markReady (explicit: Boolean = false) {
    if (this.failed || !this.hasWebView) return
    if (!explicit && !this.options.headless && App.getInstance().getUserConfigValue("webview_require_ready") == "true") return
    if (this.loading) oro.runtime.app.RuntimeLifecycleEvents.record(index, "ready", SystemClock.elapsedRealtime() - startupStartedAt)
    this.loading = false
    this.startupPresented = false
    this.startupHandler.removeCallbacks(this.startupWatchdog)
    this.recovery?.let { this.content?.removeView(it) }
    this.recovery = null
    this.currentWebView?.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_AUTO
  }

  private var failureMessage = R.string.runtime_startup_failed

  fun failStartup (message: Int = R.string.runtime_startup_failed) {
    if (this.failed) return
    this.window?.let {
      oro.runtime.app.PlatformOperations.cancelWindow(it)
      it.activity.presentation.navigation(it)
    }
    this.failureMessage = message
    console.error("Runtime window $index entered native startup recovery")
    oro.runtime.app.RuntimeLifecycleEvents.record(index, "failed", if (loading) SystemClock.elapsedRealtime() - startupStartedAt else 0)
    this.loading = false
    this.failed = true
    this.startupPresented = false
    this.startupHandler.removeCallbacks(this.startupWatchdog)
    this.currentWebView?.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
    this.showRecovery(true)
  }

  private fun showRecovery (error: Boolean) {
    val container = this.content ?: return
    this.recovery?.let { container.removeView(it) }
    val surface = LinearLayout(requireContext()).apply {
      orientation = LinearLayout.VERTICAL
      gravity = android.view.Gravity.CENTER
      val padding = (24 * resources.displayMetrics.density).toInt()
      setPadding(padding, padding, padding, padding)
      isClickable = true
      isFocusableInTouchMode = true
      val background = android.util.TypedValue()
      requireContext().theme.resolveAttribute(android.R.attr.colorBackground, background, true)
      setBackgroundColor(background.data)
    }
    surface.addView(TextView(requireContext()).apply {
      setText(if (error) failureMessage else R.string.runtime_startup_loading)
      accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
    })
    if (error) {
      surface.addView(Button(requireContext()).apply {
        setText(R.string.runtime_reload)
        setOnClickListener { restartWebView() }
      })
    }
    container.addView(surface, FrameLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      ViewGroup.LayoutParams.MATCH_PARENT
    ))
    this.recovery = surface
    surface.requestFocus()
  }

  private fun releaseWebView () {
    val webview = this.currentWebView ?: return
    this.currentWebView = null
    this.window?.let {
      oro.runtime.app.PlatformOperations.cancelWindow(it)
      it.activity.presentation.detach(it)
      it.userMessageHandler.active = false
      it.detachBridge(webview)
      it.activity.dialog.cancelFileSystemPicker(it)
      it.bridge.clearBuffers()
      try {
        it.onViewDestroyed(this.index)
      } catch (_: UnsatisfiedLinkError) {
        // An incompatible native library is already blocked by the package gate.
        console.error("Native runtime does not support view detachment")
      }
    }
    (webview.parent as? ViewGroup)?.removeView(webview)
    webview.removeJavascriptInterface("external")
    webview.webChromeClient = null
    webview.destroy()
  }

  fun onRendererGone (webview: WebView) {
    if (webview !== this.currentWebView) return
    oro.runtime.app.RuntimeLifecycleEvents.record(index, "renderer-lost")
    this.releaseWebView()
    this.failStartup()
  }

  private fun restartWebView () {
    val container = this.content ?: return
    val location = this.startupLocation ?: return
    // The explicit Reload action also restores failed hidden runtime workers.
    // Their recovery controls cannot be reached while their fragments are hidden.
    if (!this.options.headless) {
      (this.activity as? WindowManagerActivity)?.windowFragmentManager?.fragments?.toList()
        ?.filter { it.options.headless && it.failed }
        ?.forEach { it.restartWebView() }
    }
    oro.runtime.app.RuntimeLifecycleEvents.record(index, "reload")
    this.releaseWebView()
    this.failed = false
    this.loading = false
    val webview = WebView(requireContext())
    container.addView(webview, 0, FrameLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      ViewGroup.LayoutParams.MATCH_PARENT
    ))
    this.attachWebView(webview)
    this.window?.navigate(location)
  }

  override fun onAttach (context: Context) {
    super.onAttach(context)
  }

  override fun onCreate (savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
  }

  override fun onDestroyView () {
    (this.activity as? WindowManagerActivity)?.systemAppearance?.detach(this)
    this.startupHandler.removeCallbacks(this.startupWatchdog)
    this.releaseWebView()
    (this.activity as? WindowManagerActivity)?.systemAppearance?.refresh()
    (this.activity as? WindowManagerActivity)?.presentation?.refresh()
    this.window = null
    this.recovery = null
    this.content = null
    this.loading = false
    super.onDestroyView()
  }

  override fun onHiddenChanged (hidden: Boolean) {
    super.onHiddenChanged(hidden)
    if (hidden) this.window?.let { oro.runtime.app.NativeGestures.invalidate(it) }
    (this.activity as? WindowManagerActivity)?.systemAppearance?.refresh()
    (this.activity as? WindowManagerActivity)?.presentation?.refresh()
  }

  override fun onCreateView (
    inflater: LayoutInflater,
    container: ViewGroup?,
    savedInstanceState: Bundle?
  ): View? {
    val webview = super.onCreateView(inflater, container, savedInstanceState) ?: return null
    return FrameLayout(requireContext()).also {
      it.addView(webview)
      this.content = it
    }
  }

  override fun onStart () {
    super.onStart()
  }

  override fun onStop () {
    super.onStop()
  }

  override fun onResume () {
    super.onResume()
    (this.activity as? WindowManagerActivity)?.systemAppearance?.refresh()
    (this.activity as? WindowManagerActivity)?.presentation?.refresh()
    this.checkStartupDeadline()
  }

  override fun onPause () {
    this.window?.let { oro.runtime.app.NativeGestures.invalidate(it) }
    super.onPause()
  }

  override fun onBackPressed (): Boolean {
    val window = this.window

    if (this.hasWebView && !this.failed && this.webview.canGoBack()) {
      this.webview.goBack()
      return true
    }

    // Android root Back backgrounds the task, preserving its primary window.
    if (this.index == 0) {
      this.activity?.moveTaskToBack(true)
      return true
    }

    if (window != null) {
      window.close()
      return true
    }
    return false
  }

  /**
   * Called when the view was created and when the `WindowFragment` should
   * assign the `WebView` instance.
   */
  override fun onViewCreated (view: View, savedInstanceState: Bundle?) {
    super.onViewCreated(view, savedInstanceState)

    val arguments = this.arguments
    if (arguments != null) {
      this.options.index = arguments.getInt("index", this.options.index)
      this.options.headless = arguments.getBoolean("headless", this.options.headless)
      this.options.shouldExitApplicationOnClose = arguments.getBoolean(
        "shouldExitApplicationOnClose",
        this.options.shouldExitApplicationOnClose
      )
    }

    this.attachWebView(view.findViewById<WebView>(R.id.webview))
    if (!this.failed) this.window?.startNavigation()
  }

  private fun attachWebView (webview: WebView) {
    val fragment = this
    val app = App.getInstance()
    this.currentWebView = webview
    this.applySystemTextScale(webview.resources.configuration)
    this.beginStartup()
    this.webview.apply {
      // features
      settings.setGeolocationEnabled(app.getUserConfigValue("permissions_allow_geolocation") == "true")
      settings.javaScriptCanOpenWindowsAutomatically = app.getUserConfigValue("webview_allow_automatic_windows") == "true"
      settings.javaScriptEnabled = true
      importantForAutofill = if (fragment.options.headless || app.getUserConfigValue("webview_autofill_enabled") == "false") {
        View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS
      } else {
        View.IMPORTANT_FOR_AUTOFILL_YES
      }
      if (WebViewFeature.isFeatureSupported(WebViewFeature.WEB_AUTHENTICATION)) {
        WebSettingsCompat.setWebAuthenticationSupport(settings,
          if (!fragment.options.headless && app.getUserConfigValue("webview_webauthn_enabled") == "true") WebSettingsCompat.WEB_AUTHENTICATION_SUPPORT_FOR_APP
          else WebSettingsCompat.WEB_AUTHENTICATION_SUPPORT_NONE)
      }
      settings.domStorageEnabled = app.hasRuntimePermission("data_access")
      settings.databaseEnabled = app.hasRuntimePermission("data_access")

      val cookiesAllowed = app.hasRuntimePermission("cookies")
      val cookieManager = CookieManager.getInstance()
      cookieManager.setAcceptCookie(cookiesAllowed)
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
        cookieManager.setAcceptThirdPartyCookies(this,
          cookiesAllowed && app.getUserConfigValue("webview_allow_third_party_cookies") == "true")
      }

      settings.allowContentAccess = app.getUserConfigValue("webview_allow_content_access") == "true"
      settings.allowFileAccess = app.getUserConfigValue("webview_allow_file_access") == "true"
      settings.allowFileAccessFromFileURLs = false
      settings.allowUniversalAccessFromFileURLs = false
      settings.safeBrowsingEnabled = true

      settings.mixedContentMode = if (app.getUserConfigValue("webview_allow_mixed_content") == "true") {
        WebSettings.MIXED_CONTENT_ALWAYS_ALLOW
      } else {
        WebSettings.MIXED_CONTENT_NEVER_ALLOW
      }

      val runtimeVersion = runCatching {
        app.getUserConfigValue("meta_version")
      }.getOrNull()?.takeIf { it.isNotBlank() }

      val currentUserAgent = settings.userAgentString ?: ""
      val cleanedUserAgent = currentUserAgent
        .replace(Regex("OroRuntime/\\S+"), "")
        .replace(Regex("\\s+"), " ")
        .trim()

      val updatedUserAgent = when {
        runtimeVersion?.let { cleanedUserAgent.contains("OroRuntime/") } == true -> cleanedUserAgent
        runtimeVersion != null && runtimeVersion.isNotBlank() -> listOf(cleanedUserAgent, "OroRuntime/$runtimeVersion")
          .filter { it.isNotBlank() }
          .joinToString(" ")
        else -> cleanedUserAgent
      }

      if (updatedUserAgent.isNotEmpty() && updatedUserAgent != currentUserAgent) {
        settings.userAgentString = updatedUserAgent
      }

      addOnLayoutChangeListener { v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom ->
        val activity = fragment.requireActivity() as WindowManagerActivity
        val metrics = activity.getScreenDisplayMetrics()

        fragment.window?.frame = Size(
          ((right - left).toFloat() * metrics.density).toInt(),
          ((bottom - top).toFloat() * metrics.density).toInt()
        )

        fragment.window?.position = Position(
          (top.toFloat() * metrics.density).toInt(),
          (left.toFloat() * metrics.density).toInt()
        )
      }
    }

    this.window = Window(this).apply {
      val activity = fragment.requireActivity() as WindowManagerActivity
      val metrics = activity.getScreenDisplayMetrics()

      frame = Size(
        (webview.width.toFloat() * metrics.density).toInt(),
        (webview.height.toFloat() * metrics.density).toInt()
      )

      onReady()
    }
  }
}
