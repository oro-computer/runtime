package oro.runtime.qualification.callbacks

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.CancellationSignal
import org.json.JSONObject
import oro.runtime.app.*
import java.util.concurrent.Executors
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

object PublicCallbackFixture {
  external fun runPublicNativeCase (operation: String): String
}

/** Public data only; DUMP permission prevents ordinary applications from driving this receiver. */
class PublicCallbackReceiver : BroadcastReceiver() {
  private fun lifecycle (recreate: Boolean): JSONObject {
    val result = CompletableFuture<JSONObject>()
    android.os.Handler(android.os.Looper.getMainLooper()).post {
      try {
        val activity = oro.runtime.app.App.getInstance().activity
        val snapshot = JSONObject().put("activityAvailable", activity != null)
        if (activity != null) {
          val fragments = activity.windowFragmentManager.fragments
          snapshot.put("activityInstance", System.identityHashCode(activity))
            .put("orientation", activity.resources.configuration.orientation)
            .put("windows", fragments.count { !it.options.headless })
            .put("visibleWindows", fragments.count { !it.options.headless && it.isVisible && it.hasWebView })
        }
        result.complete(snapshot)
        if (recreate) activity?.recreate()
      } catch (error: Exception) { result.completeExceptionally(error) }
    }
    return result.get(10, TimeUnit.SECONDS)
  }

  override fun onReceive (context: Context, intent: Intent) {
    val pending = goAsync()
    val executor = Executors.newSingleThreadExecutor()
    executor.execute {
      val result = try {
        when (intent.getStringExtra("mode")) {
          "read" -> JSONObject(PublicCallbackFixture.runPublicNativeCase("read"))
          "lifecycle" -> lifecycle(false)
          "recreate" -> lifecycle(true)
          "stop" -> {
            val status = ContinuousTasks.status(context, "public-monitor")
            ContinuousTasks.stop(context, "public-monitor", status.getString("generation"))
          }
          "status" -> ContinuousTasks.status(context, "public-monitor")
          "key-lifetime" -> JSONObject(PublicCallbackFixture.runPublicNativeCase("key-lifetime"))
          "interop-outside" -> JSONObject(PublicCallbackFixture.runPublicNativeCase("interop-outside"))
          "recovery-seed", "push-drain-seed" -> {
            val draining = intent.getStringExtra("mode") == "push-drain-seed"
            val id = if (draining) "public-cancel" else "public-recovery"
            val record = JSONObject().put("id", id).put("kind", if (draining) "public.waiting-alert" else "public.alert").put("authorityRef", "authority")
              .put("accountRef", "account").put("targetRef", "target").put("phase", "registered")
              .put("enrollmentPending", draining).put("enrollmentAttempts", 0)
              .put("token", java.util.UUID.randomUUID().toString()).put("generation", java.util.UUID.randomUUID().toString())
              .put("distributor", context.packageName).put("keys", PushCryptography.keys()).put("endpoint", "https://public.invalid/push")
            val save = NativePush.javaClass.getDeclaredMethod("save", Context::class.java, String::class.java, JSONObject::class.java)
              .apply { isAccessible = true }
            save.invoke(NativePush, context, "registration.$id", record)
            if (draining) NativePush.start(context)
            JSONObject().put("seeded", true)
          }
          "recovery-discover", "recovery-stale", "recovery-remove", "push-drain-stop" ->
            JSONObject(PublicCallbackFixture.runPublicNativeCase(intent.getStringExtra("mode")!!))
          "push" -> {
            val type = RuntimeNativeCallbacks.javaClass
            val native = type.getDeclaredMethod("nativeCall", String::class.java, java.lang.Long.TYPE,
              Integer.TYPE, String::class.java, ByteArray::class.java).apply { isAccessible = true }
            fun invoke(command: String, handle: Long, category: Int = 0, source: String = "", bytes: ByteArray = byteArrayOf()): Any {
              val result = JSONObject(native.invoke(RuntimeNativeCallbacks, command, handle, category, source, bytes) as String)
              check(!result.has("err"))
              return result.get("data")
            }
            val task = JSONObject().put("id", "public-push").put("kind", "public.alert").put("authorityRef", "authority")
              .put("accountRef", "account").put("targetRef", "target").put("operationRef", "")
            val handle = (invoke("begin", 0, 4, task.toString()) as Number).toLong()
            try { JSONObject().put("pushPassed", invoke("push-message", handle, bytes = "public-message".toByteArray()) == true) }
            finally { invoke("close", handle) }
          }
          else -> throw IllegalArgumentException()
        }
      } catch (_: Exception) { JSONObject().put("failed", true) }
      result.put("requestId", intent.getStringExtra("requestId"))
      try { check(context.getSharedPreferences("oro.public.callbacks", Context.MODE_PRIVATE).edit().putString("receipt", result.toString()).commit()) }
      finally { executor.shutdown(); pending.finish() }
    }
  }
}
