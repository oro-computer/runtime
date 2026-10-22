package oro.runtime.app

import android.credentials.CredentialManager
import android.credentials.CredentialOption
import android.credentials.GetCredentialException
import android.credentials.GetCredentialRequest
import android.credentials.GetCredentialResponse
import android.os.Build
import android.os.Bundle
import android.os.CancellationSignal
import android.os.OutcomeReceiver
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import org.json.JSONObject
import oro.runtime.window.Window
import java.util.concurrent.atomic.AtomicBoolean

/** Explicit password selection, separate from WebAuthn and WebView Autofill. */
internal object PasswordCredentials {
  private val active = AtomicBoolean(false)

  /** Provider callbacks can precede the requesting Activity's resume event. */
  private class PendingResult (
    private val call: PlatformCall,
    private val owner: Window,
    private val username: String,
    result: GetCredentialResponse
  ) : DefaultLifecycleObserver {
    private var result: GetCredentialResponse? = result
    private var observing = false
    private val resume = Runnable { deliver() }
    private val timeout = Runnable { fail("ENOTREADY") }

    fun start () {
      if (call.done.get()) {
        close()
      } else if (owner.activity.isFinishing || owner.activity.isDestroyed ||
          owner.activity.lifecycle.currentState == Lifecycle.State.DESTROYED) {
        fail("ABORT_ERR")
      } else if (owner.activity.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
        deliver()
      } else {
        // Keep private data in native memory for at most five seconds. The
        // operation's original deadline and cancellation remain in force.
        observing = true
        PlatformOperations.main.postDelayed(timeout, 5000)
        owner.activity.lifecycle.addObserver(this)
      }
    }

    override fun onResume (owner: LifecycleOwner) {
      PlatformOperations.main.post(resume)
    }

    override fun onStop (owner: LifecycleOwner) { fail("ABORT_ERR") }
    override fun onDestroy (owner: LifecycleOwner) { fail("ABORT_ERR") }

    fun close () {
      result = null
      PlatformOperations.main.removeCallbacks(timeout)
      PlatformOperations.main.removeCallbacks(resume)
      if (observing) {
        observing = false
        owner.activity.lifecycle.removeObserver(this)
      }
    }

    private fun fail (code: String) {
      close()
      call.failure(code)
    }

    private fun deliver () {
      val response = result ?: return
      close()
      if (call.done.get()) return
      try {
        if (call.foreground() !== owner || owner.activity.windowFragmentManager.fragments.lastOrNull {
            it.isVisible && !it.options.headless && it.hasWebView
          }?.window !== owner) throw PlatformFailure("ENOTREADY")
        val credential = response.credential
        val id = credential.data.getString("androidx.credentials.BUNDLE_KEY_ID")
        val password = credential.data.getString("androidx.credentials.BUNDLE_KEY_PASSWORD")
        if (credential.type != "android.credentials.TYPE_PASSWORD_CREDENTIAL" || id.isNullOrEmpty() ||
            id.length > 320 || password.isNullOrEmpty() || password.length > 4096 ||
            (username.isNotEmpty() && username != id)) throw PlatformFailure("EINVAL")
        call.success(JSONObject().put("username", id).put("password", password))
      } catch (error: PlatformFailure) { call.failure(error.code)
      } catch (_: Exception) { call.failure("EIO") }
    }
  }

  fun available (app: App): Boolean = Build.VERSION.SDK_INT >= 34 &&
    app.getUserConfigValue("credentials_enabled") == "true"

  fun request (call: PlatformCall, action: String, options: JSONObject) {
    if (action == "status") {
      call.success(JSONObject().put("supported", Build.VERSION.SDK_INT >= 34)
        .put("enabled", available(call.app)).put("busy", active.get()))
      return
    }
    if (!available(call.app)) throw PlatformFailure("ENOSYS")
    require(action == "selectPassword")
    require(options.keys().asSequence().all { it == "username" })
    val username = options.optString("username")
    require(username.length <= 320 && username.none { it.code < 32 || it.code == 127 })
    val owner = call.foreground()
    if (!owner.activity.hasWindowFocus()) throw PlatformFailure("ENOTREADY")
    if (!active.compareAndSet(false, true)) throw PlatformFailure("EBUSY")
    val signal = CancellationSignal()
    var pendingResult: PendingResult? = null
    var received = false
    call.setCancellation {
      try {
        pendingResult?.close()
        pendingResult = null
        signal.cancel()
      } finally { active.set(false) }
    }
    val data = Bundle().apply {
      putBoolean("androidx.credentials.BUNDLE_KEY_IS_AUTO_SELECT_ALLOWED", false)
      if (username.isNotEmpty()) putStringArrayList("androidx.credentials.BUNDLE_KEY_ALLOWED_USER_IDS", arrayListOf(username))
    }
    val option = CredentialOption.Builder("android.credentials.TYPE_PASSWORD_CREDENTIAL", data, Bundle(data))
      .setIsSystemProviderRequired(false).build()
    val request = GetCredentialRequest.Builder(Bundle()).addCredentialOption(option).build()
    owner.activity.getSystemService(CredentialManager::class.java).getCredential(
      owner.activity, request, signal, owner.activity.mainExecutor,
      object : OutcomeReceiver<GetCredentialResponse, GetCredentialException> {
        override fun onResult (result: GetCredentialResponse) {
          if (call.done.get() || received) return
          received = true
          pendingResult = PendingResult(call, owner, username, result).also { it.start() }
        }

        override fun onError (error: GetCredentialException) {
          if (call.done.get() || received) return
          call.failure(when (error.type) {
            GetCredentialException.TYPE_USER_CANCELED -> "ABORT_ERR"
            GetCredentialException.TYPE_NO_CREDENTIAL -> "ENOENT"
            GetCredentialException.TYPE_INTERRUPTED -> "EINTR"
            else -> "EIO"
          })
        }
      }
    )
  }
}
