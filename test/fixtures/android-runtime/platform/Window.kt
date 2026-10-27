package oro.runtime.window
import androidx.lifecycle.Lifecycle
import android.net.Uri
class Options { val headless = false }
class WebView { var isAttachedToWindow = true; val url = "https://public.invalid" }
class Fragment(val index: Int) {
  var window: Window? = null
  val options = Options(); var isHidden = false; var hasWebView = true; val webview = WebView()
  internal fun presentStartup(call: oro.runtime.app.PlatformCall) { call.success() }
}
class Manager { val fragments = mutableListOf<Fragment>() }
internal class ApplicationBack {
  fun invalidate(window: Window) {}
  fun invalidateStale(window: Window) {}
  fun request(call: oro.runtime.app.PlatformCall, action: String, options: org.json.JSONObject) { call.success() }
}
open class WindowManagerActivity : oro.runtime.app.AppActivity() {
  val windowFragmentManager = Manager()
  internal val applicationBack = ApplicationBack()
}
class Messages { var active = true }
class Window(val activity: WindowManagerActivity, val fragment: Fragment) {
  var platformDocumentToken = "a".repeat(32)
  val userMessageHandler = Messages()
  fun isTrustedOrigin(uri: Uri) = uri.value == "https://public.invalid"
}
