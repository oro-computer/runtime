package oro.runtime.app
import oro.runtime.window.*
import android.os.Build
import org.json.*
internal class PlatformFailure(val code: String): Exception(code)
internal object PlatformOperations { val main=android.os.Handler() }
internal class PlatformCall(val window: Window) {
 var value=JSONObject()
 fun document()=window
 fun foreground(): Window { if(!window.fragment.isVisible)throw PlatformFailure("ENOTREADY");return window }
 fun success(value: Any) { this.value=value as JSONObject }
}
private fun expect(code: String, action: () -> Unit) {
 try { action();error("Expected rejection") } catch(e: IllegalArgumentException) { check(code=="EINVAL") }
 catch(e: PlatformFailure) { check(e.code==code) }
}
fun main() {
 val activity=Activity();val window=Window(activity);val view=window.fragment.webview
 fun request(action: String, data: JSONObject=JSONObject()): JSONObject {
  val call=PlatformCall(window);NativeGestures.request(call,action,data);return call.value
 }
 fun set(regions: String="[{edge:left,top:0.2,height:0.1}]")=request("set",JSONObject().put("regions",JSONArray(regions)))
 fun active()=request("get").getBoolean("active")
 check(!active());val first=set();check(active()&&first.getJSONArray("rects").length()==1)
 check(view.systemGestureExclusionRects.single().right==48)
 expect("EINVAL") { set("[{edge:left,top:0,height:0.9}]") };check(active())
 expect("EINVAL") { set("[{edge:left,top:0.2,height:0.2},{edge:left,top:0.5,height:0.2}]") };check(active())
 expect("EINVAL") { set("[{edge:left,top:0.2,height:0.1,password:x}]") };check(active())
 expect("EINVAL") { set("[{edge:left,top:'0.1',height:0.1}]") };check(active())
 set("[{edge:right,top:0.9,height:0.1}]");check(view.systemGestureExclusionRects.single().bottom==1520)
 view.viewTreeObserver.focus.toList().forEach { it.onWindowFocusChanged(false) };check(!active()&&view.systemGestureExclusionRects.isEmpty())
 set();view.height=1400;view.layout();check(!active())
 set();window.platformDocumentToken="replacement";view.layout();check(!active())
 set();val other=Window(activity);view.layout();check(!active())
 expect("ENOTREADY") { set() };other.fragment.isVisible=false
 set();NativeGestures.invalidate(window);check(!active())
 set();view.attachments.toList().forEach { it.onViewDetachedFromWindow(view) };check(!active())
 set();window.fragment.isVisible=false;view.layout();check(!active());window.fragment.isVisible=true
 Build.VERSION.SDK_INT=28;expect("ENOTSUP") { set() };check(!request("get").getBoolean("supported"));set("[]")
 Build.VERSION.SDK_INT=37;set();set("[]");check(!active())
 check(view.viewTreeObserver.layouts.isEmpty()&&view.viewTreeObserver.focus.isEmpty()&&view.attachments.isEmpty())
 println("gesture-ownership-policy-passed")
}
