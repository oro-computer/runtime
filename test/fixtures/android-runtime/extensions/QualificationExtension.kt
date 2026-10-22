package oro.runtime.qualification.extensions

import android.app.NotificationManager
import android.app.NotificationChannel
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.app.App
import oro.runtime.app.AppActivity
import oro.runtime.app.NativeExtension
import oro.runtime.app.NativeExtensionCall
import oro.runtime.app.NativeNotifications
import oro.runtime.app.PlatformFailure

/** Fixed public data only; compile into an isolated qualification APK. */
class QualificationExtension : NativeExtension {
  override val name = "qualification"
  companion object {
    var startups = 0
    var resumed = 0
    var cancellations = 0
    var intents = 0
  }
  override fun onCreate (app: App) { startups++ }
  override fun onActivityResumed (activity: AppActivity) { resumed++ }
  override fun onNewIntent (activity: AppActivity, intent: Intent) { intents++ }
  override fun request (call: NativeExtensionCall, action: String, options: JSONObject) {
    when (action) {
      "state" -> call.success(JSONObject().put("startups", startups).put("resumed", resumed)
        .put("cancellations", cancellations).put("intents", intents))
      "echo" -> call.success(JSONObject().put("fixture", "public-native-extension"))
      "wait" -> {
        call.onCancel { cancellations++ }
        Handler(Looper.getMainLooper()).postDelayed({ call.success(JSONObject().put("late", true)) }, 1500)
      }
      "notifications" -> {
        val manager = call.context.getSystemService(NotificationManager::class.java)
        val values = JSONArray()
        for (entry in manager.activeNotifications) {
          val item = entry.notification
          values.put(JSONObject().put("channel", item.channelId).put("group", item.group)
            .put("groupAlertBehavior", item.groupAlertBehavior)
            .put("summary", item.flags and android.app.Notification.FLAG_GROUP_SUMMARY != 0)
            .put("private", item.visibility == android.app.Notification.VISIBILITY_PRIVATE)
            .put("publicVersion", item.publicVersion != null))
        }
        call.success(values)
      }
      else -> call.failure("ENOSYS")
    }
  }
}

class SecondExtension : NativeExtension {
  override val name = "qualification.second"
  override fun request (call: NativeExtensionCall, action: String, options: JSONObject) {
    call.success(JSONObject().put("fixture", "second-native-extension"))
  }
}

/** Declare exported with android.permission.DUMP only in the isolated test manifest. */
class QualificationReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    if (intent.action == "oro.runtime.qualification.PRESENTATION_RECEIPT") {
      try {
        val saved = context.getSharedPreferences("oro.public.qualification", Context.MODE_PRIVATE)
          .getString("presentation", null) ?: error("No public presentation run")
        val state = JSONObject(saved)
        val rows = JSONArray(App.getInstance().getNotificationResponses())
        val receipt = (0 until rows.length()).map { rows.getJSONObject(it) }
          .firstOrNull { it.optString("id") == state.getString("childId") && it.optString("action") == "dismiss" }
        val manager = context.getSystemService(NotificationManager::class.java)
        val remaining = manager.activeNotifications.any { it.notification.group == state.getString("group") }
        if (receipt == null || remaining) {
          resultCode = 0
          resultData = "public-receipt-pending"
          return
        }
        check(state.getBoolean("cancelled"))
        check(App.getInstance().acknowledgeNotificationResponse(receipt.getString("receiptId")))
        val checks = state.getJSONArray("checks").put("shared-durable-receipt")
          .put("receipt-acknowledged").put("native-cancellation")
        check(context.getSharedPreferences("oro.public.qualification", Context.MODE_PRIVATE)
          .edit().remove("presentation").commit())
        resultCode = -1
        resultData = JSONObject().put("checks", checks).put("startupHooks", QualificationExtension.startups).toString()
      } catch (_: Exception) {
        resultCode = 0
        resultData = "public-receipt-failed"
      }
      return
    }
    if (intent.action == "oro.runtime.qualification.PRESENTATION") {
      val pending = goAsync()
      Thread {
        var phase = "configure-channels"
        try {
          val state = presentation(context) { phase = it }
          check(context.getSharedPreferences("oro.public.qualification", Context.MODE_PRIVATE)
            .edit().putString("presentation", state.toString()).commit())
          pending.resultData = JSONObject().put("checks", state.getJSONArray("checks"))
            .put("receiptVerificationPending", true).toString()
          pending.resultCode = -1
        } catch (error: Exception) {
          pending.resultData = JSONObject().put("phase", phase).put("error", error.javaClass.simpleName)
            .put("code", if (error is PlatformFailure) error.code else "public-presentation-failed").toString()
          pending.resultCode = 0
        } finally { pending.finish() }
      }.start()
      return
    }
    NativeNotifications.configureChannels(context, JSONArray().put(JSONObject()
      .put("id", "public.native.v1").put("name", "Public native fixture").put("importance", "low").put("sound", "none")))
    NativeNotifications.submit(context, JSONObject().put("id", "public-cold-native").put("title", "Public native fixture")
      .put("channel", "public.native.v1").put("publicVersion", JSONObject().put("title", "Public fixture")))
    resultCode = -1
    resultData = "public-native-submitted"
  }

  private fun presentation (context: Context, progress: (String) -> Unit): JSONObject {
    val checks = JSONArray()
    fun verify (name: String, value: Boolean) { progress(name); check(value); checks.put(name) }
    fun rejects (name: String, code: String, action: () -> Unit) {
      val error = runCatching { action() }.exceptionOrNull()
      verify(name, if (code == "EINVAL") error is IllegalArgumentException else error is PlatformFailure && error.code == code)
    }
    val channel = JSONObject().put("id", "public.presentation.v1").put("name", "Public presentation fixture")
      .put("importance", "high").put("sound", "none")
    NativeNotifications.configureChannels(context, JSONArray().put(channel))
    verify("immutable-channel-repeat", NativeNotifications.configureChannels(context, JSONArray().put(channel)).length() == 1)
    rejects("immutable-channel-change", "EEXIST") {
      NativeNotifications.configureChannels(context, JSONArray().put(JSONObject(channel.toString()).put("importance", "low")))
    }
    val run = java.util.UUID.randomUUID().toString()
    val childId = "public-presentation-child-$run"
    val summaryId = "public-presentation-summary-$run"
    val group = "public-presentation-group-$run"
    val options = JSONObject().put("id", childId).put("title", "Public presentation fixture")
      .put("channel", "public.presentation.v1").put("group", group)
      .put("groupAlertBehavior", "children").put("smallIcon", "qualification_notification")
      .put("publicVersion", JSONObject().put("title", "Public fixture").put("body", "Open the public fixture"))
    val state = JSONObject().put("childId", childId).put("summaryId", summaryId).put("group", group)
    try {
      val submitted = NativeNotifications.submit(context, options)
      verify("submission-boundary", submitted.optBoolean("submitted") && !submitted.optBoolean("presentationObserved"))
      NativeNotifications.submit(context, JSONObject(options.toString()).put("id", summaryId).put("groupSummary", true))
      val manager = context.getSystemService(NotificationManager::class.java)
      val postedDeadline = android.os.SystemClock.elapsedRealtime() + 5000
      var active = manager.activeNotifications.filter { it.notification.group == group }
      while (active.size != 2 && android.os.SystemClock.elapsedRealtime() < postedDeadline) {
        Thread.sleep(50)
        active = manager.activeNotifications.filter { it.notification.group == group }
      }
      verify("group-members", active.size == 2)
      verify("private-public-version", active.all { it.notification.visibility == android.app.Notification.VISIBILITY_PRIVATE && it.notification.publicVersion != null })
      verify("group-alert-policy", active.all { it.notification.groupAlertBehavior == 2 })
      verify("explicit-summary", active.any { it.notification.flags and android.app.Notification.FLAG_GROUP_SUMMARY != 0 })
      rejects("unknown-channel", "ENOENT") {
        NativeNotifications.submit(context, JSONObject(options.toString()).put("channel", "public.missing.v1"))
      }
      rejects("invalid-small-icon", "EINVAL") {
        NativeNotifications.submit(context, JSONObject(options.toString()).put("smallIcon", "missing_fixture_icon"))
      }
      rejects("summary-requires-group", "EINVAL") {
        NativeNotifications.submit(context, JSONObject(options.toString()).put("group", "").put("groupSummary", true))
      }
      manager.createNotificationChannel(NotificationChannel("public.blocked.v1", "Public blocked fixture", NotificationManager.IMPORTANCE_NONE))
      rejects("blocked-channel", "EACCES") {
        NativeNotifications.submit(context, JSONObject(options.toString()).put("channel", "public.blocked.v1"))
      }
      val child = active.first { it.notification.flags and android.app.Notification.FLAG_GROUP_SUMMARY == 0 }
      val dismiss = child.notification.deleteIntent
      dismiss.send()
      verify("one-shot-dismiss", try { dismiss.send(); false } catch (_: PendingIntent.CanceledException) { true })
      return state.put("checks", checks)
    } finally {
      val childCancelled = NativeNotifications.cancel(context, childId)
      val summaryCancelled = NativeNotifications.cancel(context, summaryId)
      state.put("cancelled", childCancelled && summaryCancelled)
    }
  }
}
