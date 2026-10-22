package oro.runtime.window
import androidx.lifecycle.Lifecycle
import android.net.Uri
class Options { val headless = false }
class WebView { var isAttachedToWindow = true; val url = "https://public.invalid" }
class Fragment(val index: Int) {
  var window: Window? = null
  val options = Options(); var isHidden = false; var hasWebView = true; val webview = WebView()
}
class Manager { val fragments = mutableListOf<Fragment>() }
open class WindowManagerActivity : oro.runtime.app.AppActivity() { val windowFragmentManager = Manager() }
class Messages { var active = true }
class Window(val activity: WindowManagerActivity, val fragment: Fragment) {
  var platformDocumentToken = "a".repeat(32)
  val userMessageHandler = Messages()
  fun isTrustedOrigin(uri: Uri) = uri.value == "https://public.invalid"
}
