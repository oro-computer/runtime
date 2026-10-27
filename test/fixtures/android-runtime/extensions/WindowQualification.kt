package oro.runtime.qualification.extensions

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import android.view.FrameMetrics
import android.view.ViewTreeObserver
import android.webkit.WebView
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.app.App
import oro.runtime.app.AppActivity
import oro.runtime.app.NativeExtension
import oro.runtime.app.NativeExtensionCall
import oro.runtime.window.WindowManagerActivity

/** Native observation only, restricted to isolated public qualification packages. */
class WindowQualificationExtension : NativeExtension {
  override val name = "qualification.windows"
  override fun onCreate (app: App) {
    require(app.packageName.startsWith("oro.runtime.qualification."))
  }
  override fun onActivityCreated (activity: AppActivity) = PublicWindowObservations.attach(activity)
  override fun onActivityResumed (activity: AppActivity) = PublicWindowObservations.resume(activity)
  override fun onActivityStopped (activity: AppActivity) = PublicWindowObservations.stop(activity)
  override fun onActivityDestroyed (activity: AppActivity) = PublicWindowObservations.detach(activity)
  override fun request (call: NativeExtensionCall, action: String, options: JSONObject) {
    if (action != "snapshot" || options.length() != 0) {
      call.failure("EINVAL")
      return
    }
    call.foregroundActivity()
    call.success(PublicWindowObservations.snapshot())
  }
}

/** Bounded native draws/FrameMetrics; no input values, page text or URLs are read. */
internal object PublicWindowObservations {
  private val main = Handler(Looper.getMainLooper())
  private val metricsThread = HandlerThread("oro-public-frame-metrics").apply { start() }
  private val metricsHandler = Handler(metricsThread.looper)
  private val frames = java.util.ArrayDeque<JSONObject>()
  private val draws = java.util.ArrayDeque<JSONObject>()
  private val trackers = mutableMapOf<AppActivity, Tracker>()
  private var totalFrames = 0L
  private var droppedReports = 0L

  private class Tracker (val activity: AppActivity) {
    val observers = mutableMapOf<WebView, ViewTreeObserver.OnDrawListener>()
    val documents = java.util.WeakHashMap<WebView, String>()
    var active = false
    val frameListener = android.view.Window.OnFrameMetricsAvailableListener { _, metric, dropped ->
      synchronized(PublicWindowObservations) {
        totalFrames++
        droppedReports += dropped
        if (frames.size >= 2048) frames.removeFirst()
        frames.addLast(JSONObject().put("totalNs", metric.getMetric(FrameMetrics.TOTAL_DURATION))
          .put("vsyncNs", metric.getMetric(FrameMetrics.VSYNC_TIMESTAMP))
          .put("platformFirstDraw", metric.getMetric(FrameMetrics.FIRST_DRAW_FRAME) == 1L))
      }
    }
    val poll = object : Runnable {
      override fun run () {
        if (!active || activity.isFinishing || activity.isDestroyed) return
        val current = activity.windowFragmentManager.fragments.filter { !it.options.headless && it.hasWebView }
        val views = current.map { it.webview }.toSet()
        for (view in observers.keys.toList()) {
          if (view !in views) {
            val listener = observers.remove(view)
            if (listener != null && view.viewTreeObserver.isAlive) view.viewTreeObserver.removeOnDrawListener(listener)
          }
        }
        for (fragment in current) {
          val view = fragment.webview
          if (view in observers || observers.size >= 16) continue
          val listener = ViewTreeObserver.OnDrawListener {
            val owner = fragment.window
            val document = owner?.platformDocumentToken ?: ""
            if (document.isNotEmpty() && documents[view] != document) {
              documents[view] = document
              val value = JSONObject().put("index", fragment.index).put("elapsedMs", SystemClock.elapsedRealtime())
                .put("textZoom", view.settings.textZoom).put("fontScale", view.resources.configuration.fontScale)
                .put("width", view.width).put("height", view.height).put("hidden", fragment.isHidden)
                .put("phase", "first-observed-draw-for-document")
              synchronized(PublicWindowObservations) {
                if (draws.size >= 128) draws.removeFirst()
                draws.addLast(value)
              }
            }
          }
          observers[view] = listener
          view.viewTreeObserver.addOnDrawListener(listener)
        }
        main.postDelayed(this, 100)
      }
    }
    fun stop () { active = false; main.removeCallbacks(poll) }
    fun detach () {
      stop()
      for ((view, observer) in observers) {
        if (view.viewTreeObserver.isAlive) view.viewTreeObserver.removeOnDrawListener(observer)
      }
      observers.clear()
      activity.window.removeOnFrameMetricsAvailableListener(frameListener)
    }
  }

  fun attach (activity: AppActivity) {
    if (activity in trackers) return
    val tracker = Tracker(activity)
    trackers[activity] = tracker
    activity.window.addOnFrameMetricsAvailableListener(tracker.frameListener, metricsHandler)
  }
  fun resume (activity: AppActivity) {
    attach(activity)
    trackers[activity]?.let { it.stop(); it.active = true; it.poll.run() }
  }
  fun stop (activity: AppActivity) { trackers[activity]?.stop() }
  fun detach (activity: AppActivity) { trackers.remove(activity)?.detach() }
  @Synchronized fun resetMetrics () { frames.clear(); totalFrames = 0; droppedReports = 0 }

  @Synchronized fun snapshot (): JSONObject {
    val app = App.getInstance()
    val result = JSONObject().put("identity", JSONObject(app.getBuildIdentity()))
      .put("frames", JSONArray(frames.map { JSONObject(it.toString()) })).put("frameCount", totalFrames)
      .put("droppedMetricReports", droppedReports).put("firstObservedDraws", JSONArray(draws.map { JSONObject(it.toString()) }))
    val windows = JSONArray()
    val activity = app.activity as? WindowManagerActivity
    activity?.windowFragmentManager?.fragments?.forEach { fragment ->
      val entry = JSONObject().put("index", fragment.index).put("headless", fragment.options.headless)
        .put("hidden", fragment.isHidden).put("visible", fragment.isVisible).put("ready", fragment.readyForPresentation)
        .put("hasWebView", fragment.hasWebView)
      if (fragment.hasWebView) {
        val view = fragment.webview
        val location = IntArray(2)
        view.getLocationInWindow(location)
        entry.put("textZoom", view.settings.textZoom).put("fontScale", view.resources.configuration.fontScale)
          .put("density", view.resources.displayMetrics.density).put("width", view.width).put("height", view.height)
          .put("leftInWindow", location[0]).put("topInWindow", location[1])
          .put("focused", view.hasFocus()).put("attached", view.isAttachedToWindow)
        if (android.os.Build.VERSION.SDK_INT >= 29) entry.put("gestureRects", JSONArray(view.systemGestureExclusionRects.map {
          JSONObject().put("left", it.left).put("top", it.top).put("right", it.right).put("bottom", it.bottom)
        }))
      }
      windows.put(entry)
    }
    return result.put("windows", windows).put("activityState", activity?.lifecycle?.currentState?.name ?: "absent")
      .put("windowFocused", activity?.hasWindowFocus() ?: false)
      .put("refreshRate", if (android.os.Build.VERSION.SDK_INT >= 30) activity?.display?.mode?.refreshRate ?: 0f
        else activity?.windowManager?.defaultDisplay?.mode?.refreshRate ?: 0f)
  }
}

/** Fixed public operations only; protect this fixture receiver with android.permission.DUMP. */
class WindowQualificationReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    if (!context.packageName.startsWith("oro.runtime.qualification.")) return
    when (intent.action) {
      "oro.runtime.qualification.WINDOWS.SNAPSHOT" -> {
        resultCode = 0
        resultData = PublicWindowObservations.snapshot().toString()
      }
      "oro.runtime.qualification.WINDOWS.RESET_METRICS" -> {
        PublicWindowObservations.resetMetrics()
        resultCode = 0
        resultData = "public-native-metrics-reset"
      }
      "oro.runtime.qualification.WINDOWS.RECREATE" -> {
        App.getInstance().activity?.recreate()
        resultCode = 0
        resultData = "public-activity-recreation-requested"
      }
      "oro.runtime.qualification.WINDOWS.PORTRAIT",
      "oro.runtime.qualification.WINDOWS.LANDSCAPE",
      "oro.runtime.qualification.WINDOWS.RELEASE_ORIENTATION" -> {
        App.getInstance().activity?.requestedOrientation = when (intent.action) {
          "oro.runtime.qualification.WINDOWS.PORTRAIT" -> android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
          "oro.runtime.qualification.WINDOWS.LANDSCAPE" -> android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
          else -> android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }
        resultCode = 0
        resultData = "public-activity-orientation-requested"
      }
    }
  }
}
