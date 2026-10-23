package oro.runtime.qualification.extensions

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import org.json.JSONObject
import oro.runtime.window.SystemTextScaling

/** Public policy checks without a WebView; not first-frame or accessibility acceptance. */
class TextScaleReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    if (intent.action != "oro.runtime.qualification.TEXT_SCALE") return
    val scale = context.resources.configuration.fontScale
    val checks = linkedMapOf(
      "system100" to (SystemTextScaling.zoom(1f, "system") == 100),
      "system130" to (SystemTextScaling.zoom(1.3f, "system") == 130),
      "system200" to (SystemTextScaling.zoom(2f, "system") == 200),
      "fixed" to (SystemTextScaling.zoom(2f, "125") == 125),
      "manual" to (SystemTextScaling.zoom(2f, "manual") == null),
      "invalid" to runCatching { SystemTextScaling.zoom(1f, "invalid") }.isFailure
    )
    val result = JSONObject().put("checks", JSONObject(checks)).put("passed", checks.values.all { it })
      .put("fontScale", scale).put("systemZoom", SystemTextScaling.zoom(scale, "system"))
      .put("webviewCreated", false).put("firstFrameQualified", false)
    resultCode = if (checks.values.all { it }) 0 else 1
    resultData = result.toString()
  }
}
