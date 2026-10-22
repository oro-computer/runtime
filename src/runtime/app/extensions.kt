package oro.runtime.app

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Looper
import org.json.JSONObject

/** Compiled, explicitly configured host code; never dynamically supplied by a page. */
interface NativeExtension {
  val name: String
  fun onCreate (app: App) {}
  fun onActivityCreated (activity: AppActivity) {}
  fun onActivityResumed (activity: AppActivity) {}
  fun onActivityStopped (activity: AppActivity) {}
  fun onActivityDestroyed (activity: AppActivity) {}
  fun onNewIntent (activity: AppActivity, intent: Intent) {}
  fun onActivityResult (activity: AppActivity, requestCode: Int, resultCode: Int, data: Intent?) {}
  fun request (call: NativeExtensionCall, action: String, options: JSONObject)
}

/** A request retains the common document ownership, capacity and 90-second deadline. */
class NativeExtensionCall internal constructor (private val call: PlatformCall) {
  val context: Context get () = call.app
  val completed: Boolean get () = call.done.get()

  /** Access only on the main thread; stale, hidden and background owners fail closed. */
  fun foregroundActivity (): AppActivity {
    check(Looper.myLooper() == Looper.getMainLooper())
    return call.foreground().activity as? AppActivity ?: throw PlatformFailure("ENOTREADY")
  }

  /** Installs one cancellation/cleanup callback, also run after successful completion. */
  fun onCancel (callback: () -> Unit) = call.setCancellation(callback)

  /** Results are bounded to 512 KiB and returned only to the original document. */
  fun success (result: Any = JSONObject.NULL) {
    PlatformOperations.main.post {
      try {
        call.foreground()
        call.success(result)
      } catch (error: PlatformFailure) { call.failure(error.code)
      } catch (_: Exception) { call.failure("EIO") }
    }
  }

  /** Only a fixed code crosses IPC; exception messages and stacks are never exposed. */
  fun failure (code: String) {
    require(Regex("[A-Z][A-Z0-9_]{0,63}").matches(code))
    call.failure(code)
  }
}

/** Startup and activity hooks for at most 32 explicitly configured native extensions. */
object NativeExtensions {
  private val extensions = linkedMapOf<String, NativeExtension>()

  internal fun initialize (app: App) {
    try {
      install(app)
    } catch (_: Exception) {
      throw IllegalStateException("Native extension startup failed")
    }
  }

  private fun install (app: App) {
    val configured = ConfiguredNativeExtensions.create()
    require(configured.size <= 32)
    if (configured.isEmpty()) return
    for (extension in configured) {
      require(Regex("[a-z][a-z0-9_.-]{0,63}").matches(extension.name) && !extensions.containsKey(extension.name))
      extensions[extension.name] = extension
      extension.onCreate(app)
    }
    app.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
      override fun onActivityCreated (activity: Activity, state: Bundle?) {
        if (activity is AppActivity) dispatch { it.onActivityCreated(activity) }
      }
      override fun onActivityResumed (activity: Activity) {
        if (activity is AppActivity) dispatch { it.onActivityResumed(activity) }
      }
      override fun onActivityStopped (activity: Activity) {
        if (activity is AppActivity) dispatch { it.onActivityStopped(activity) }
      }
      override fun onActivityDestroyed (activity: Activity) {
        if (activity is AppActivity) dispatch { it.onActivityDestroyed(activity) }
      }
      override fun onActivityStarted (activity: Activity) {}
      override fun onActivityPaused (activity: Activity) {}
      override fun onActivitySaveInstanceState (activity: Activity, state: Bundle) {}
    })
  }

  internal fun dispatch (callback: (NativeExtension) -> Unit) {
    for (extension in extensions.values) {
      try { callback(extension)
      } catch (_: Exception) { oro.runtime.debug.console.error("Native extension lifecycle callback failed") }
    }
  }

  internal fun request (call: PlatformCall, action: String, options: JSONObject) {
    call.foreground()
    require(options.keys().asSequence().all { it in listOf("extension", "payload") })
    require(Regex("[a-zA-Z][a-zA-Z0-9_.-]{0,63}").matches(action))
    val extension = extensions[options.getString("extension")] ?: throw PlatformFailure("ENOSYS")
    extension.request(NativeExtensionCall(call), action, options.getJSONObject("payload"))
  }
}
