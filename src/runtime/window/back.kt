package oro.runtime.window

import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.lifecycle.Lifecycle
import oro.runtime.app.PlatformCall
import oro.runtime.app.PlatformFailure
import org.json.JSONObject
import java.util.UUID

/** Document-owned application Back handling with a bounded native fallback. */
internal class ApplicationBackNavigation (private val activity: WindowManagerActivity) {
  private data class Registration (val document: String, val id: String)
  private class Pending (val window: Window, val registration: Registration,
    val id: String, val fallback: () -> Unit) {
    lateinit var timeout: Runnable
  }
  private val main = Handler(Looper.getMainLooper())
  private val registrations = mutableMapOf<Window, Registration>()
  private var pending: Pending? = null

  private fun current (window: Window, registration: Registration): Boolean =
    !activity.isFinishing && !activity.isDestroyed &&
      activity.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED) &&
      window.platformDocumentToken == registration.document &&
      window.userMessageHandler.active && window.fragment.window === window &&
      window.fragment.hasWebView && !window.fragment.isHidden &&
      window.fragment.webview.isAttachedToWindow &&
      window.isTrustedOrigin(Uri.parse(window.fragment.webview.url ?: "")) &&
      activity.windowFragmentManager.fragments.lastOrNull {
        it.isVisible && !it.options.headless && it.hasWebView
      }?.window === window

  fun request (call: PlatformCall, action: String, options: JSONObject) {
    val window = if (action == "disable") call.document() else call.foreground()
    when (action) {
      "enable" -> {
        require(options.length() == 0)
        invalidate(window)
        if (registrations.size >= 32) throw PlatformFailure("ENOSPC")
        val registration = Registration(call.documentToken, UUID.randomUUID().toString())
        registrations[window] = registration
        call.success(JSONObject().put("registration", registration.id).put("deadlineMs", 750))
      }
      "disable" -> {
        require(options.keys().asSequence().all { it == "registration" })
        if (registrations[window]?.id == options.getString("registration")) invalidate(window)
        call.success()
      }
      "respond" -> {
        require(options.keys().asSequence().all { it in listOf("registration", "id", "handled") })
        require(options.get("handled") is Boolean)
        val run = pending
        if (run == null || run.window !== window || run.id != options.getString("id") ||
            run.registration.id != options.getString("registration") || !current(window, run.registration)) {
          throw PlatformFailure("ABORT_ERR")
        }
        finish(run, !options.getBoolean("handled"))
        call.success()
      }
      else -> throw PlatformFailure("EINVAL")
    }
  }

  /** Returns true while application handling or its native fallback owns Back. */
  fun dispatch (fallback: () -> Unit): Boolean {
    val window = activity.windowFragmentManager.fragments.lastOrNull {
      it.isVisible && !it.options.headless && it.hasWebView
    }?.window ?: return false
    val registration = registrations[window] ?: return false
    if (!current(window, registration)) {
      invalidate(window)
      return false
    }
    pending?.let {
      if (it.window === window && it.registration == registration) return true
      finish(it, false)
    }
    val run = Pending(window, registration, UUID.randomUUID().toString(), fallback)
    run.timeout = Runnable { finish(run, true) }
    pending = run
    main.postDelayed(run.timeout, 750)
    window.bridge.emit("backrequest", JSONObject().put("registration", registration.id)
      .put("id", run.id).put("deadlineMs", 750).toString())
    return true
  }

  private fun finish (run: Pending, useFallback: Boolean) {
    if (pending !== run) return
    pending = null
    main.removeCallbacks(run.timeout)
    if (useFallback && registrations[run.window] == run.registration && current(run.window, run.registration)) {
      run.fallback()
    }
  }

  fun invalidate (window: Window) {
    pending?.takeIf { it.window === window }?.let { finish(it, false) }
    registrations.remove(window)
  }

  fun invalidateStale (window: Window) {
    val registration = registrations[window] ?: return
    if (window.platformDocumentToken != registration.document || !window.userMessageHandler.active ||
        !window.fragment.hasWebView || window.fragment.window !== window) invalidate(window)
  }

  fun pause () { pending?.let { finish(it, false) } }
  fun destroy () {
    pause()
    registrations.clear()
  }
}
