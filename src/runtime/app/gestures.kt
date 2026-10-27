package oro.runtime.app

import android.graphics.Rect
import android.os.Build
import android.os.Looper
import android.view.View
import android.view.ViewTreeObserver
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.window.Window
import kotlin.math.ceil
import kotlin.math.floor

/** A small edge handle, never a whole-screen replacement for system navigation. */
internal object NativeGesturePolicy {
  fun rectangles (width: Int, height: Int, density: Float, regions: JSONArray): List<Rect> {
    require(width > 0 && height > 0 && density.isFinite() && density > 0 && regions.length() <= 4)
    val strip = minOf(floor(24 * density).toInt(), width / 4)
    val budget = minOf(floor(200 * density).toInt(), height / 2)
    require(strip > 0)
    val totals = mutableMapOf("left" to 0, "right" to 0)
    return (0 until regions.length()).map { index ->
      val region = regions.getJSONObject(index)
      require(region.keys().asSequence().toSet() == setOf("edge", "top", "height"))
      val edge = region.getString("edge")
      val top = region.get("top"); val extent = region.get("height")
      require(edge in totals && top is Number && extent is Number)
      val start = top.toDouble(); val length = extent.toDouble()
      require(start.isFinite() && length.isFinite() && start >= 0 && length > 0 && start + length <= 1)
      val first = floor(start * height).toInt()
      val last = ceil((start + length) * height).toInt()
      val used = totals.getValue(edge) + last - first
      require(used <= budget)
      totals[edge] = used
      Rect(if (edge == "left") 0 else width - strip, first, if (edge == "left") strip else width, last)
    }
  }
}

/** One focused document owns a bounded exclusion lease. No lease survives ownership/geometry loss. */
internal object NativeGestures {
  @Volatile private var lease: Lease? = null

  private fun foreground (window: Window): Boolean = window.activity.hasWindowFocus() &&
    window.activity.windowFragmentManager.fragments.lastOrNull {
      it.isVisible && !it.options.headless && it.hasWebView
    }?.window === window

  private class Lease (val window: Window, val view: View) :
    ViewTreeObserver.OnGlobalLayoutListener, ViewTreeObserver.OnWindowFocusChangeListener, View.OnAttachStateChangeListener {
    val document = window.platformDocumentToken
    private val observer = view.viewTreeObserver
    private val geometry = geometry()
    private fun geometry (): List<Int> {
      val location = IntArray(2); view.getLocationInWindow(location)
      return listOf(location[0], location[1], view.width, view.height, view.resources.configuration.orientation,
        view.resources.displayMetrics.densityDpi)
    }
    fun current (): Boolean = window.platformDocumentToken == document && window.fragment.window === window &&
      window.fragment.hasWebView && view.isAttachedToWindow && window.userMessageHandler.active &&
      !window.activity.isFinishing && !window.activity.isDestroyed && foreground(window) && geometry == geometry()
    fun attach () {
      observer.addOnGlobalLayoutListener(this)
      observer.addOnWindowFocusChangeListener(this)
      view.addOnAttachStateChangeListener(this)
    }
    fun close () {
      if (observer.isAlive) {
        observer.removeOnGlobalLayoutListener(this)
        observer.removeOnWindowFocusChangeListener(this)
      }
      view.removeOnAttachStateChangeListener(this)
      if (Build.VERSION.SDK_INT >= 29) view.systemGestureExclusionRects = emptyList()
    }
    override fun onGlobalLayout () { if (!current()) clear(this) }
    override fun onWindowFocusChanged (focused: Boolean) { if (!focused) clear(this) }
    override fun onViewAttachedToWindow (view: View) {}
    override fun onViewDetachedFromWindow (view: View) { clear(this) }
  }

  private fun clear (expected: Lease) {
    if (lease !== expected) return
    lease = null
    expected.close()
  }

  fun invalidate (window: Window) {
    val previous = lease ?: return
    if (previous.window !== window) return
    if (Looper.myLooper() == Looper.getMainLooper()) clear(previous)
    else PlatformOperations.main.post { clear(previous) }
  }

  fun request (call: PlatformCall, action: String, options: JSONObject) {
    val window = call.document()
    lease?.let { if (!it.current()) clear(it) }
    require(action in setOf("get", "set"))
    if (action == "get") require(options.length() == 0)
    else {
      require(options.length() == 1 && options.has("regions"))
      val regions = options.getJSONArray("regions")
      if (regions.length() == 0) lease?.let { if (it.window === window) clear(it) }
      else {
        if (Build.VERSION.SDK_INT < 29) throw PlatformFailure("ENOTSUP")
        call.foreground()
        if (!foreground(window) || !window.fragment.readyForPresentation) throw PlatformFailure("ENOTREADY")
        val view = window.fragment.webview
        val rects = NativeGesturePolicy.rectangles(view.width, view.height, view.resources.displayMetrics.density, regions)
        val insets = ViewCompat.getRootWindowInsets(view) ?: throw PlatformFailure("ENOTREADY")
        val mandatory = insets.getInsets(WindowInsetsCompat.Type.mandatorySystemGestures())
        val location = IntArray(2); view.getLocationInWindow(location)
        val root = window.activity.window.decorView
        val safe = Rect(mandatory.left - location[0], mandatory.top - location[1],
          root.width - mandatory.right - location[0], root.height - mandatory.bottom - location[1])
        val clipped = rects.filter { it.intersect(safe) }
        if (clipped.isEmpty()) throw PlatformFailure("ENOTREADY")
        lease?.let { clear(it) }
        val next = Lease(window, view)
        try {
          view.systemGestureExclusionRects = clipped
          next.attach()
          lease = next
        } catch (error: Exception) { next.close(); throw error }
      }
    }
    val view = window.fragment.webview
    val current = lease?.takeIf { it.window === window }
    val rects = if (current == null || Build.VERSION.SDK_INT < 29) emptyList() else view.systemGestureExclusionRects
    call.success(JSONObject().put("supported", Build.VERSION.SDK_INT >= 29).put("active", current != null)
      .put("viewportWidth", view.width).put("viewportHeight", view.height).put("density", view.resources.displayMetrics.density)
      .put("maximumRegions", 4).put("maximumWidthDp", 24).put("maximumEdgeHeightDp", 200)
      .put("osAcceptanceKnown", false).put("rects", JSONArray(rects.map {
        JSONObject().put("left", it.left).put("top", it.top).put("right", it.right).put("bottom", it.bottom)
      })))
  }
}
