package oro.runtime.app

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Looper
import android.os.CancellationSignal
import android.os.OperationCanceledException
import org.json.JSONObject
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.FutureTask
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.Semaphore
import java.util.concurrent.atomic.AtomicBoolean

/** Four native workers and sixteen queued calls. Uncooperative calls retain their worker until they exit. */
internal object NativeExtensionWorkers {
  val capacity = Semaphore(20)
  val cancellations = ThreadPoolExecutor(4, 4, 30, TimeUnit.SECONDS, ArrayBlockingQueue<Runnable>(32),
    { task -> Thread(task, "oro-native-extension-cancel").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  val executor = ThreadPoolExecutor(4, 4, 30, TimeUnit.SECONDS, ArrayBlockingQueue<Runnable>(16),
    { task -> Thread(task, "oro-native-extension").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
}

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

/** Internal continuation marker; a shared native controller completes the existing document call. */
internal object NativeDeferredReply

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

  /**
   * Runs compiled blocking code with bounded admission and document cancellation.
   * Invoke once on the main thread instead of installing a separate onCancel callback.
   * The worker must observe cancellation and close its own resources. A result is
   * delivered only after foreground/document revalidation; late values are discarded.
   */
  fun runOnWorker (operation: (CancellationSignal) -> Any?) {
    foregroundActivity()
    if (completed) return
    if (!NativeExtensionWorkers.capacity.tryAcquire()) { failure("EBUSY"); return }
    val cancellation = CancellationSignal()
    val workerFinished = AtomicBoolean(false)
    val cancelFinished = AtomicBoolean(false)
    val released = AtomicBoolean(false)
    val admission = Any()
    var submitted = false
    fun release () {
      if (workerFinished.get() && cancelFinished.get() && released.compareAndSet(false, true)) NativeExtensionWorkers.capacity.release()
    }
    val task = FutureTask<Unit> {
      try {
        if (completed) return@FutureTask
        cancellation.throwIfCanceled()
        val value = operation(cancellation)
        cancellation.throwIfCanceled()
        if (!completed && value !== NativeDeferredReply) success(value ?: JSONObject.NULL)
      } catch (_: OperationCanceledException) { failure("ABORT_ERR")
      } catch (_: InterruptedException) { Thread.currentThread().interrupt(); failure("ABORT_ERR")
      } catch (error: NativeStateException) { failure(error.code)
      } catch (error: PlatformFailure) { failure(error.code)
      } catch (_: IllegalArgumentException) { failure("EINVAL")
      } catch (_: org.json.JSONException) { failure("EINVAL")
      } catch (_: Exception) { failure("EIO") }
    }
    val worker = Runnable {
      try { task.run() } finally { workerFinished.set(true); release() }
    }
    try {
      onCancel {
        synchronized(admission) {
          task.cancel(true)
          if (!submitted || NativeExtensionWorkers.executor.remove(worker)) workerFinished.set(true)
        }
        NativeExtensionWorkers.cancellations.execute {
          try { cancellation.cancel() } finally { cancelFinished.set(true); release() }
        }
      }
    } catch (error: Exception) {
      workerFinished.set(true); cancelFinished.set(true); release()
      throw error
    }
    synchronized(admission) {
      if (completed) { task.cancel(true); workerFinished.set(true); release(); return }
      try { NativeExtensionWorkers.executor.execute(worker); submitted = true
      } catch (_: RejectedExecutionException) { workerFinished.set(true); failure("EBUSY") }
    }
  }

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
    val configured = ConfiguredNativeExtensions.create() + RuntimeNativeCallbacks.initialize(app)
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
