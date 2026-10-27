package oro.runtime.app

import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.lifecycle.Lifecycle
import org.json.JSONObject
import oro.runtime.window.Window
import oro.runtime.window.WindowManagerActivity
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

internal class PlatformFailure (val code: String) : Exception()

/** One bounded, document-owned native operation. No exception text crosses IPC. */
internal class PlatformCall (
  val app: App,
  val token: Long,
  val index: Int,
  val id: String,
  val window: Window?,
  val documentToken: String
) {
  val done = AtomicBoolean(false)
  @Volatile var cancel: (() -> Unit)? = null
  var deadline: java.util.concurrent.ScheduledFuture<*>? = null
  private var cancellationRegistered = false

  fun setCancellation (callback: () -> Unit) {
    val completed = synchronized(this) {
      check(!cancellationRegistered && cancel == null)
      cancellationRegistered = true
      if (done.get()) true else { cancel = callback; false }
    }
    if (completed) dispatchCancellation(callback)
  }

  private fun dispatchCancellation (callback: () -> Unit) {
    PlatformOperations.main.post {
      try { callback()
      } catch (_: Exception) { oro.runtime.debug.console.error("Native operation cancellation callback failed") }
    }
  }

  /** A live trusted document can release its resources while hidden or paused. */
  fun document (): Window {
    val owner = window ?: throw PlatformFailure("ENOTREADY")
    if (owner.activity.isFinishing || owner.activity.isDestroyed ||
        owner.fragment.options.headless ||
        !owner.fragment.hasWebView || owner.fragment.window !== owner ||
        !owner.fragment.webview.isAttachedToWindow || !owner.userMessageHandler.active ||
        owner.platformDocumentToken != documentToken ||
        !owner.isTrustedOrigin(Uri.parse(owner.fragment.webview.url ?: ""))) throw PlatformFailure("ENOTREADY")
    return owner
  }

  fun foreground (): Window {
    val owner = document()
    if (!owner.activity.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED) || owner.fragment.isHidden) {
      throw PlatformFailure("ENOTREADY")
    }
    return owner
  }

  fun success (value: Any = JSONObject.NULL) {
    if (window == null || window.platformDocumentToken != documentToken || window.fragment.window !== window) {
      failure("ABORT_ERR")
    } else {
      finish(JSONObject().put("data", value))
    }
  }

  fun failure (code: String) = finish(JSONObject().put("err", JSONObject()
    .put("code", code).put("message", "Native platform operation did not complete ($code)")))

  private fun finish (result: JSONObject) {
    if (!done.compareAndSet(false, true)) return
    deadline?.cancel(false)
    PlatformOperations.remove(this)
    val cancellation = synchronized(this) { val value = cancel; cancel = null; value }
    if (cancellation != null) dispatchCancellation(cancellation)
    val source = result.toString()
    app.onPlatformOperationResult(token, if (source.toByteArray(Charsets.UTF_8).size <= 524288) source
      else "{\"err\":{\"code\":\"E2BIG\",\"message\":\"Native result exceeds the limit\"}}")
  }
}

/** Shared IPC lifecycle for prompts and bounded native state operations. */
internal object PlatformOperations {
  val main = Handler(Looper.getMainLooper())
  private val deadlines = ScheduledThreadPoolExecutor(1) { task ->
    Thread(task, "oro-platform-deadlines").apply { isDaemon = true }
  }.apply { removeOnCancelPolicy = true }
  private val pending = mutableMapOf<String, PlatformCall>()
  private val cancelled = linkedMapOf<String, Long>()

  @Synchronized fun remove (call: PlatformCall) {
    val key = "${call.index}:${call.documentToken}:${call.id}"
    if (pending[key] === call) pending.remove(key)
  }

  fun cancelWindow (window: Window) {
    NativeGestures.invalidate(window)
    if (Looper.myLooper() == Looper.getMainLooper()) window.activity.applicationBack.invalidate(window)
    else main.post { window.activity.applicationBack.invalidateStale(window) }
    val calls = synchronized(this) { pending.values.filter { it.window === window } }
    calls.forEach { it.failure("ABORT_ERR") }
  }

  fun cancelActivity (activity: AppActivity) {
    val calls = synchronized(this) { pending.values.filter { it.window?.activity === activity } }
    calls.forEach { it.failure("ABORT_ERR") }
  }

  fun start (app: App, token: Long, index: Int, feature: String, action: String, source: String, id: String, documentToken: String) {
    main.post {
      val activity = app.activity as? WindowManagerActivity
      val owner = activity?.windowFragmentManager?.fragments?.find { it.index == index }?.window
      val call = PlatformCall(app, token, index, id, owner, documentToken)
      val key = "$index:$documentToken:$id"
      try {
        if (!Regex("[a-zA-Z0-9-]{1,80}").matches(id) || source.length > 196608) throw PlatformFailure("EINVAL")
        val options = JSONObject(source)
        if (!Regex("[a-f0-9]{32}").matches(documentToken) || owner == null ||
            owner.platformDocumentToken != documentToken) throw PlatformFailure("ABORT_ERR")
        if (feature == "cancel") {
          val cancelledId = options.getString("id")
          require(Regex("[a-zA-Z0-9-]{1,80}").matches(cancelledId))
          val target = synchronized(this) {
            val cancelledKey = "$index:$documentToken:$cancelledId"
            val target = pending[cancelledKey]
            if (target == null) {
              if (cancelled.size >= 128) cancelled.remove(cancelled.keys.first())
              cancelled[cancelledKey] = android.os.SystemClock.elapsedRealtime()
            }
            target
          }
          target?.failure("ABORT_ERR")
          call.success()
          return@post
        }
        synchronized(this) {
          val now = android.os.SystemClock.elapsedRealtime()
          cancelled.entries.removeAll { now - it.value > 120000 }
          if (cancelled.remove(key) != null) throw PlatformFailure("ABORT_ERR")
          if (pending.size >= 32 || pending.containsKey(key)) throw PlatformFailure("EBUSY")
          pending[key] = call
        }
        call.deadline = deadlines.schedule({ call.failure("ETIMEDOUT") }, 90, TimeUnit.SECONDS)
        when (feature) {
          "authentication" -> ProtectedAuthentication.request(call, action, options)
          "launcher" -> LauncherDestinations.request(call, action, options)
          "deferred" -> DeferredSync.request(call, action, options)
          "foreground" -> ForegroundTasks.request(call, action, options)
          "gestures" -> NativeGestures.request(call, action, options)
          "credentials" -> PasswordCredentials.request(call, action, options)
          "extension" -> NativeExtensions.request(call, action, options)
          "notifications" -> NativeNotifications.request(call, action, options)
          "back" -> owner.activity.applicationBack.request(call, action, options)
          "push" -> NativePush.request(call, action, options)
          "workflows" -> NativeWorkflows.request(call, action, options)
          "startup" -> {
            require(action == "present" && options.length() == 0)
            call.document().fragment.presentStartup(call)
          }
          else -> call.failure("ENOSYS")
        }
      } catch (error: PlatformFailure) { call.failure(error.code)
      } catch (error: NativeStateException) { call.failure(error.code)
      } catch (_: SecurityException) { call.failure("EACCES")
      } catch (_: IllegalArgumentException) { call.failure("EINVAL")
      } catch (_: Exception) { call.failure("EIO") }
    }
  }
}
