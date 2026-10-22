package oro.runtime.qualification

import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import org.json.JSONArray
import org.json.JSONObject

/** Shell-only fixture for this qualification APK's fixed public notifications. */
class NotificationReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    val notifications = manager.activeNotifications.filter {
      it.notification.extras.getString(Notification.EXTRA_TITLE) in
        listOf("Public runtime qualification", "Runtime receipt qualification") && it.tag?.startsWith("oro:") == true
    }.sortedBy { it.postTime }
    val operation = intent.getStringExtra("operation") ?: "inspect"
    val result = JSONObject().put("operation", operation).put("count", notifications.size)
    try {
      if (operation == "inspect") {
        val records = JSONArray()
        notifications.forEach { records.put(JSONObject().put("id", it.id).put("tag", it.tag)
          .put("visibility", it.notification.visibility).put("channel", it.notification.channelId)) }
        result.put("records", records)
      } else {
        val notification = notifications.lastOrNull() ?: throw IllegalStateException("No public fixture notification")
        val pending = when (operation) {
          "tap", "replay" -> notification.notification.contentIntent
          "dismiss" -> notification.notification.deleteIntent
          "action" -> notification.notification.actions?.firstOrNull()?.actionIntent
          else -> throw IllegalArgumentException("Unknown fixture operation")
        } ?: throw IllegalStateException("Public fixture action unavailable")
        pending.send()
        if (operation == "dismiss") manager.cancel(notification.tag, notification.id)
        if (operation == "replay") {
          val refused = try { pending.send(); false } catch (_: PendingIntent.CanceledException) { true }
          result.put("replayRefused", refused)
        }
        result.put("sent", true)
      }
      result.put("ok", true)
    } catch (error: Exception) {
      result.put("ok", false).put("error", error.javaClass.simpleName)
    }
    resultCode = if (result.optBoolean("ok")) -1 else 0
    resultData = result.toString()
  }
}
