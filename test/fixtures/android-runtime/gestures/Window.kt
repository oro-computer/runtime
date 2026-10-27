package oro.runtime.window
import android.view.View
class Options { val headless=false }
class Fragment { val options=Options();var isVisible=true;var hasWebView=true;var readyForPresentation=true;val webview=View();var window: Window?=null }
class Messages { var active=true }
class Manager { val fragments=mutableListOf<Fragment>() }
class NativeWindow { val decorView=View() }
class Activity { var isFinishing=false;var isDestroyed=false;var focused=true;val windowFragmentManager=Manager();val window=NativeWindow();fun hasWindowFocus()=focused }
class Window(val activity: Activity) {
 val fragment=Fragment().also { it.window=this;activity.windowFragmentManager.fragments.add(it) }
 var platformDocumentToken="public-document";val userMessageHandler=Messages()
}
