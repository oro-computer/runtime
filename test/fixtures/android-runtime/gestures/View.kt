package android.view
import android.graphics.Rect
class Metrics { var density=2f; var densityDpi=320 }
class Configuration { var orientation=1 }
class Resources { val displayMetrics=Metrics(); val configuration=Configuration() }
class ViewTreeObserver {
 fun interface OnGlobalLayoutListener { fun onGlobalLayout() }
 fun interface OnWindowFocusChangeListener { fun onWindowFocusChanged(focused: Boolean) }
 val isAlive=true
 val layouts=mutableSetOf<OnGlobalLayoutListener>();val focus=mutableSetOf<OnWindowFocusChangeListener>()
 fun addOnGlobalLayoutListener(l: OnGlobalLayoutListener) { layouts.add(l) }
 fun removeOnGlobalLayoutListener(l: OnGlobalLayoutListener) { layouts.remove(l) }
 fun addOnWindowFocusChangeListener(l: OnWindowFocusChangeListener) { focus.add(l) }
 fun removeOnWindowFocusChangeListener(l: OnWindowFocusChangeListener) { focus.remove(l) }
}
class View {
 interface OnAttachStateChangeListener { fun onViewAttachedToWindow(view: View); fun onViewDetachedFromWindow(view: View) }
 var width=800;var height=1600;var x=0;var y=0;var isAttachedToWindow=true
 val resources=Resources();val viewTreeObserver=ViewTreeObserver();val attachments=mutableSetOf<OnAttachStateChangeListener>()
 var systemGestureExclusionRects: List<Rect> = emptyList()
 fun getLocationInWindow(out: IntArray) { out[0]=x;out[1]=y }
 fun addOnAttachStateChangeListener(l: OnAttachStateChangeListener) { attachments.add(l) }
 fun removeOnAttachStateChangeListener(l: OnAttachStateChangeListener) { attachments.remove(l) }
 fun layout() { viewTreeObserver.layouts.toList().forEach { it.onGlobalLayout() } }
}
