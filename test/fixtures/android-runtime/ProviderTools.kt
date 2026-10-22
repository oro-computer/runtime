package oro.runtime.qualification

import android.app.Activity
import android.app.Instrumentation
import android.app.UiAutomation
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.graphics.Rect
import android.net.Uri
import android.os.Bundle
import android.view.accessibility.AccessibilityNodeInfo
import org.json.JSONArray
import org.json.JSONObject

/** Manually invoked native inspector: reports only fixed public picker controls. */
class PickerInspector : Instrumentation() {
  private var click: String? = null
  override fun onCreate (arguments: Bundle?) {
    super.onCreate(arguments)
    click = arguments?.getString("click")
    start()
  }
  override fun onStart () {
    val result = JSONObject().put("available", false)
    try {
      val automation = getUiAutomation(UiAutomation.FLAG_DONT_SUPPRESS_ACCESSIBILITY_SERVICES)
      var root = automation.rootInActiveWindow
      var attempts = 0
      while (root == null && attempts++ < 20) {
        Thread.sleep(50)
        root = automation.rootInActiveWindow
      }
      val owner = root?.packageName?.toString()
      if (root != null && owner in listOf("com.google.android.documentsui", "com.android.documentsui", "com.google.android.permissioncontroller")) {
        val nodes = JSONArray()
        val publicLabels = setOf("Public cross-UID fixtures", "Public runtime fixtures", "Use this folder", "USE THIS FOLDER", "Allow", "ALLOW")
        val navigation = setOf("Show roots", "Open navigation drawer", "Open drawer", "Show document roots")
        var visited = 0
        var target: AccessibilityNodeInfo? = null
        fun collect (node: AccessibilityNodeInfo) {
          if (++visited > 4096) return
          val text = node.text?.toString() ?: ""
          val description = node.contentDescription?.toString() ?: ""
          if (text in publicLabels || description in navigation || node.className?.toString() == "android.widget.ImageButton") {
            val bounds = Rect()
            node.getBoundsInScreen(bounds)
            nodes.put(JSONObject().put("label", if (text in publicLabels) text else if (description in navigation) description else "navigation control")
              .put("id", node.viewIdResourceName ?: JSONObject.NULL).put("bounds", JSONArray(listOf(bounds.left, bounds.top, bounds.right, bounds.bottom)))
              .put("enabled", node.isEnabled).put("clickable", node.isClickable))
            if (click != null && (text == click || description == click) && node.isEnabled &&
                (click in publicLabels || click in navigation)) target = node
          }
          for (index in 0 until node.childCount) node.getChild(index)?.let { collect(it) }
        }
        collect(root)
        result.put("available", true).put("owner", owner).put("controls", nodes)
        if (click != null && owner in listOf("com.google.android.documentsui", "com.android.documentsui")) {
          var control = target
          var parents = 0
          while (control != null && !control.isClickable && parents++ < 8) control = control.parent
          result.put("clicked", control?.performAction(AccessibilityNodeInfo.ACTION_CLICK) ?: false)
        }
      }
    } catch (error: Exception) { result.put("error", error.javaClass.simpleName) }
    finish(Activity.RESULT_OK, Bundle().apply { putString("publicPicker", result.toString()) })
  }
}

/** Shell-only provider-side revocation for the fixed public root and qualification caller. */
class GrantReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    val uri = Uri.parse("content://${context.packageName}.documents/tree/root")
    context.revokeUriPermission("oro.runtime.qualification.client", uri,
      Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
    resultCode = Activity.RESULT_OK
    resultData = "public-root-revoked"
  }
}
