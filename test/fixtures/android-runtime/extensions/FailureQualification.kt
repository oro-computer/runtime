package oro.runtime.qualification.extensions

import android.content.BroadcastReceiver
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.SharedPreferences
import android.os.CancellationSignal
import android.os.SystemClock
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.app.App
import oro.runtime.app.AuthorizedDeferredRead
import oro.runtime.app.DeferredReadExecutor
import oro.runtime.app.DeferredReadOutcome
import oro.runtime.app.DeferredSync
import oro.runtime.app.LauncherDestinations
import oro.runtime.app.NativeExtension
import oro.runtime.app.NativeExtensionCall
import oro.runtime.app.NotificationReceipts
import oro.runtime.app.PlatformCall
import oro.runtime.app.RuntimeStartupTimings
import oro.runtime.securestorage.SecureStorageManager
import java.lang.reflect.Proxy
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** A public foreground surface for native-only scheduling checks. */
class PublicNativeActivity : android.app.Activity() {
  override fun onCreate (savedInstanceState: android.os.Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(android.widget.TextView(this).apply { text = "Public native runtime checks" })
  }
}

/** Public, isolated native failure checks; never include in a production APK. */
class FailureExtension : NativeExtension {
  override val name = "qualification.failures"
  override fun onCreate (app: App) {
    DeferredSync.registerReadExecutor("public.failure", DeferredReadExecutor { context, task, cancellation ->
      val store = context.getSharedPreferences("oro.public.failures", Context.MODE_PRIVATE)
      if (store.getString("mode", "revoked") == "revoked") null else object : AuthorizedDeferredRead {
        override fun reconcile (cancellation: CancellationSignal): DeferredReadOutcome {
          check(store.edit().putString("running", task.id).commit())
          if (store.getString("mode", "revoked") == "complete") {
            cancellation.throwIfCanceled()
            return DeferredReadOutcome.COMPLETED
          }
          val until = SystemClock.elapsedRealtime() + 55000
          while (SystemClock.elapsedRealtime() < until) {
            cancellation.throwIfCanceled()
            Thread.sleep(25)
          }
          return DeferredReadOutcome.UNKNOWN
        }
        override fun close () { check(store.edit().putString("closed", task.id).commit()) }
      }
    })
  }
  override fun request (call: NativeExtensionCall, action: String, options: JSONObject) { call.failure("ENOSYS") }
}

/** Fails commits before applying changes; delegates reads to this fixture's store. */
private class FailedCommitContext (context: Context) : ContextWrapper(context) {
  override fun getSharedPreferences (name: String, mode: Int): SharedPreferences {
    val original = super.getSharedPreferences(name, mode)
    return object : SharedPreferences by original {
      override fun edit (): SharedPreferences.Editor {
        val editor = original.edit()
        return Proxy.newProxyInstance(SharedPreferences.Editor::class.java.classLoader,
          arrayOf(SharedPreferences.Editor::class.java)) { proxy, method, arguments ->
          when (method.name) {
            "commit" -> false
            "apply" -> error("Unexpected asynchronous write")
            else -> {
              val result = method.invoke(editor, *(arguments ?: emptyArray()))
              if (result is SharedPreferences.Editor) proxy else result
            }
          }
        } as SharedPreferences.Editor
      }
    }
  }
}

/** Export only with android.permission.DUMP; accepts fixed operations and public data. */
class FailureReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    val operation = intent.action?.removePrefix("oro.runtime.qualification.FAILURE.") ?: return
    val pending = goAsync()
    Thread {
      try {
        val result = run(context.applicationContext, operation)
        pending.resultCode = -1
        pending.resultData = result.toString()
      } catch (_: Exception) {
        pending.resultCode = 0
        pending.resultData = "public-native-failure-check-failed"
      } finally { pending.finish() }
    }.start()
  }

  private fun run (context: Context, operation: String): JSONObject {
    val store = context.getSharedPreferences("oro.public.failures", Context.MODE_PRIVATE)
    val app = App.getInstance()
    val result = JSONObject().put("operation", operation).put("startupTimings", RuntimeStartupTimings.list())
    fun call () = PlatformCall(app, 0, 0, "public-native-check", null, "a".repeat(32))
    fun rows () = JSONArray(context.getSharedPreferences("oro.deferred.reads.v1", Context.MODE_PRIVATE)
      .getString("records", "[]"))
    fun record () = rows().getJSONObject(0)
    when (operation) {
      "SEED" -> {
        val target = NotificationReceipts.createIntent(context, Intent(), "public-process-restart", "dismiss")!!
        check(store.edit().putString("receipt", target.data.toString()).commit())
        result.put("receiptSeeded", true)
      }
      "RECEIPTS" -> {
        val target = Intent().setData(android.net.Uri.parse(store.getString("receipt", "")))
        val failure = FailedCommitContext(context)
        check(NotificationReceipts.consume(failure, target) == null)
        val received = NotificationReceipts.consume(context, target)!!
        check(received.getString("id") == "public-process-restart")
        check(NotificationReceipts.consume(context, target) == null)
        check(!NotificationReceipts.acknowledge(failure, received.getString("receiptId")))
        check(NotificationReceipts.acknowledge(context, received.getString("receiptId")))
        check(NotificationReceipts.createIntent(failure, Intent(), "public", "dismiss") == null)
        val preferences = context.getSharedPreferences("oro.notification.receipts", Context.MODE_PRIVATE)
        for (malformed in listOf(false, true)) {
          val test = NotificationReceipts.createIntent(context, Intent(), "public-expiry", "dismiss")!!
          val state = JSONObject(preferences.getString("state", "{}"))
          val row = state.getJSONObject("pending").getJSONObject(test.data!!.lastPathSegment!!)
          if (malformed) row.put("id", JSONObject()) else row.put("createdAt", 0)
          check(preferences.edit().putString("state", state.toString()).commit())
          check(NotificationReceipts.consume(context, test) == null)
          val foreign = Intent(test).setData(test.data!!.buildUpon().authority("oro.public.other").build())
          check(NotificationReceipts.consume(context, foreign) == null)
        }
        result.put("checks", JSONArray(listOf("process-restart-receipt", "duplicate-refused", "expired-refused",
          "malformed-refused", "foreign-refused", "failed-create-commit", "failed-consume-commit", "failed-ack-commit")))
      }
      "CANCELLATION" -> {
        val early = call()
        early.failure("ETIMEDOUT")
        val cleanup = CountDownLatch(1)
        early.setCancellation { cleanup.countDown() }
        check(cleanup.await(5, TimeUnit.SECONDS))
        check(runCatching { early.setCancellation {} }.isFailure)
        early.success(JSONObject().put("public", "late"))
        early.failure("EIO")
        val concurrent = call()
        val count = java.util.concurrent.atomic.AtomicInteger()
        val settled = CountDownLatch(1)
        concurrent.setCancellation { count.incrementAndGet(); settled.countDown() }
        val workers = (0 until 8).map { Thread { concurrent.failure("ABORT_ERR") }.apply { start() } }
        workers.forEach { it.join(2000); check(!it.isAlive) }
        check(settled.await(5, TimeUnit.SECONDS) && count.get() == 1 && early.done.get())
        result.put("checks", JSONArray(listOf("late-cleanup", "duplicate-cleanup-rejected", "late-result-ignored", "concurrent-settle-once")))
      }
      "LAUNCHER" -> {
        val preferences = context.getSharedPreferences("oro.launcher.v1", Context.MODE_PRIVATE)
        val token = java.util.UUID.randomUUID().toString()
        val state = JSONObject().put("entries", JSONObject().put("public", JSONObject().put("id", "public")
          .put("label", "Public fixture").put("accountRef", "public-account").put("targetRef", "public-target").put("token", token)))
        check(preferences.edit().putString("state", state.toString()).commit())
        fun route () = Intent(context.packageName + ".oro.LAUNCH_DESTINATION").putExtra("oro.launcher.token", token)
        LauncherDestinations.accept(context, route())
        var saved = JSONObject(preferences.getString("state", "{}"))
        check(saved.getJSONArray("receipts").length() == 1)
        saved.put("entries", JSONObject())
        check(preferences.edit().putString("state", saved.toString()).commit())
        LauncherDestinations.accept(context, route())
        saved = JSONObject(preferences.getString("state", "{}"))
        check(saved.getJSONArray("receipts").length() == 1)
        result.put("checks", JSONArray(listOf("opaque-route", "revoked-route-refused")))
      }
      "STORAGE" -> {
        val scope = "public-failure-fixture"
        val bytes = "public fixture only".toByteArray()
        check(SecureStorageManager.set(context, scope, "value", bytes))
        check(SecureStorageManager.get(context, scope, "value")!!.contentEquals(bytes))
        val failure = FailedCommitContext(context)
        check(!SecureStorageManager.set(failure, scope, "value", "changed".toByteArray()))
        check(!SecureStorageManager.remove(failure, scope, "value"))
        check(!SecureStorageManager.clear(failure, scope))
        check(SecureStorageManager.get(context, scope, "value")!!.contentEquals(bytes))
        val preferences = context.getSharedPreferences("oro_secure_storage", Context.MODE_PRIVATE)
        val key = "oro:" + android.util.Base64.encodeToString(scope.toByteArray(), 2) + ":" +
          android.util.Base64.encodeToString("value".toByteArray(), 2)
        val encrypted = preferences.getString(key, null)!!
        check(preferences.edit().putString(key, "invalid-envelope").commit())
        try {
          check(runCatching { SecureStorageManager.get(context, scope, "value") }.isFailure)
          check(preferences.getString(key, "") == "invalid-envelope")
        } finally { check(preferences.edit().putString(key, encrypted).commit()) }
        check(SecureStorageManager.get(context, scope, "value")!!.contentEquals(bytes))
        result.put("checks", JSONArray(listOf("encrypted-roundtrip", "failed-write-preserves", "failed-remove-preserves",
          "failed-clear-preserves", "corrupt-envelope-errors", "corrupt-envelope-preserved", "restored-envelope-readable")))
      }
      "ENQUEUE", "ENQUEUE_AUTONOMOUS" -> {
        val autonomous = operation == "ENQUEUE_AUTONOMOUS"
        check(store.edit().putString("mode", if (autonomous) "complete" else "wait")
          .remove("running").remove("closed").commit())
        DeferredSync.request(context, "enqueue", JSONObject().put("id", "public-interruption").put("kind", "public.failure")
          .put("authorityRef", "public-authority").put("accountRef", "public-account").put("targetRef", "public-target")
          .put("operationRef", "public-read").put("network", "any").put("requiresBatteryNotLow", false)
          .put("delayMs", if (autonomous) 30000 else 604800000))
        result.put("record", record())
      }
      "STATUS" -> result.put("record", record()).put("running", store.getString("running", ""))
        .put("closed", store.getString("closed", ""))
      "REVOKE" -> { check(store.edit().putString("mode", "revoked").commit()); result.put("revoked", true) }
      "RESUME" -> { DeferredSync.request(context, "resume", JSONObject().put("id", "public-interruption")); result.put("record", record()) }
      "CANCEL" -> { DeferredSync.request(context, "cancel", JSONObject().put("id", "public-interruption")); result.put("record", record()) }
      "RELEASE" -> { DeferredSync.request(context, "release", JSONObject().put("id", "public-interruption")); result.put("remaining", rows().length()) }
      "PROFILE" -> result.put("pid", android.os.Process.myPid())
        .put("buildIdentity", JSONObject(app.getBuildIdentity()))
      else -> error("Unknown public fixture operation")
    }
    return result
  }
}
