package oro.runtime.window

import androidx.lifecycle.*
import android.credentials.CredentialManager
import java.util.concurrent.Executor
import oro.runtime.app.AppActivity

class Options { var headless = false }
class WebView { var isAttachedToWindow = true; var url = "https://public.example" }
class Fragment (val index: Int) {
  val options = Options()
  var isHidden = false
  val isVisible get() = !isHidden
  var hasWebView = true
  val webview = WebView()
  var window: Window? = null
  internal fun presentStartup(call: oro.runtime.app.PlatformCall) { call.success() }
}
class Manager { val fragments = mutableListOf<Fragment>() }
internal class ApplicationBack {
  fun invalidate(window: Window) {}
  fun invalidateStale(window: Window) {}
  fun request(call: oro.runtime.app.PlatformCall, action: String, options: org.json.JSONObject) { call.success() }
}
class WindowManagerActivity : AppActivity(), LifecycleOwner {
  override val lifecycle = Lifecycle(this)
  val windowFragmentManager = Manager()
  internal val applicationBack = ApplicationBack()
  val credentials = CredentialManager()
  val mainExecutor = Executor { it.run() }
  var focused = true
  fun hasWindowFocus () = focused
  @Suppress("UNCHECKED_CAST")
  fun <T> getSystemService (type: Class<T>): T = credentials as T
}
class MessageHandler { var active = true }
class Window (val activity: WindowManagerActivity, val fragment: Fragment) {
  var platformDocumentToken = "a".repeat(32)
  val userMessageHandler = MessageHandler()
  fun isTrustedOrigin (uri: android.net.Uri) = uri.source == "https://public.example"
}
