package oro.runtime.app

import android.credentials.*
import android.os.Bundle
import androidx.lifecycle.Lifecycle
import org.json.JSONObject
import oro.runtime.window.*

open class AppActivity { var isFinishing = false; var isDestroyed = false }
class App {
  val activity = WindowManagerActivity()
  val results = mutableMapOf<Long, JSONObject>()
  fun getUserConfigValue (key: String) = "true"
  fun onPlatformOperationResult (token: Long, value: String) {
    check(!results.containsKey(token)) { "A provider operation completed twice" }
    results[token] = JSONObject(value)
  }
}
internal object ProtectedAuthentication { fun request(c: PlatformCall, a: String, o: JSONObject) { c.success() } }
internal object LauncherDestinations { fun request(c: PlatformCall, a: String, o: JSONObject) { c.success() } }
internal object DeferredSync { fun request(c: PlatformCall, a: String, o: JSONObject) { c.success() } }
internal object NativeExtensions { fun request(c: PlatformCall, a: String, o: JSONObject) { c.success() } }
internal object NativeNotifications { fun request(c: PlatformCall, a: String, o: JSONObject) { c.success() } }

private class Fixture {
  val app = App()
  val fragment = Fragment(0)
  val window = Window(app.activity, fragment)
  val manager get() = app.activity.credentials
  val lifecycle get() = app.activity.lifecycle
  init { fragment.window = window; app.activity.windowFragmentManager.fragments.add(fragment) }
  fun start (username: String = "") {
    PlatformOperations.start(app, 1, 0, "credentials", "selectPassword",
      JSONObject().put("username", username).toString(), "public-provider", window.platformDocumentToken)
    PlatformOperations.main.flush()
  }
  fun response (username: String = "public", password: String = "public-value", type: String = "android.credentials.TYPE_PASSWORD_CREDENTIAL") =
    GetCredentialResponse(Credential(type, Bundle().apply {
      putString("androidx.credentials.BUNDLE_KEY_ID", username)
      putString("androidx.credentials.BUNDLE_KEY_PASSWORD", password)
    }))
  fun result () { manager.receiver.onResult(response()) }
  fun code () = app.results[1]?.optJSONObject("err")?.optString("code")
  fun waitForResume () {
    lifecycle.move(Lifecycle.State.STARTED)
    result()
    PlatformOperations.main.flush()
    check(!app.results.containsKey(1)) { "Provider response must wait for the same Activity to resume" }
  }
  fun resume () { lifecycle.move(Lifecycle.State.RESUMED); PlatformOperations.main.flush() }
  fun clean () {
    PlatformOperations.main.flush()
    check(lifecycle.observers.isEmpty())
    check(PlatformOperations.main.size() == 0)
    val statusFragment = Fragment(99)
    val statusWindow = Window(app.activity, statusFragment)
    statusFragment.window = statusWindow
    val call = PlatformCall(app, 99, 99, "public-status", statusWindow, statusWindow.platformDocumentToken)
    PasswordCredentials.request(call, "status", JSONObject())
    check(!app.results.getValue(99).getJSONObject("data").getBoolean("busy"))
    check(manager.signal.cancelled)
  }
}

fun main () {
  Fixture().apply {
    start(); waitForResume(); resume()
    check(app.results.getValue(1).getJSONObject("data").getString("password") == "public-value")
    clean()
  }
  Fixture().apply {
    start(); result(); PlatformOperations.main.flush()
    check(app.results.getValue(1).has("data")); clean()
  }
  Fixture().apply {
    start(); waitForResume(); manager.receiver.onResult(response("other"))
    manager.receiver.onError(GetCredentialException(GetCredentialException.TYPE_NO_CREDENTIAL))
    resume(); check(app.results.getValue(1).getJSONObject("data").getString("username") == "public"); clean()
  }
  Fixture().apply {
    start(); waitForResume(); PlatformOperations.cancelWindow(window); resume()
    check(code() == "ABORT_ERR"); clean()
  }
  Fixture().apply {
    start(); waitForResume(); lifecycle.move(Lifecycle.State.RESUMED)
    lifecycle.move(Lifecycle.State.STARTED); PlatformOperations.main.flush()
    check(code() == "ENOTREADY"); clean()
  }
  for (mutation in listOf("document", "replacement", "hidden", "detached", "foreign", "inactive", "another-window")) {
    Fixture().apply {
      start(); waitForResume()
      when (mutation) {
        "document" -> window.platformDocumentToken = "b".repeat(32)
        "replacement" -> fragment.window = Window(app.activity, fragment)
        "hidden" -> fragment.isHidden = true
        "detached" -> fragment.webview.isAttachedToWindow = false
        "foreign" -> fragment.webview.url = "https://foreign.example"
        "inactive" -> window.userMessageHandler.active = false
        "another-window" -> app.activity.windowFragmentManager.fragments.add(Fragment(1).also { it.window = Window(app.activity, it) })
      }
      resume(); check(code() == "ENOTREADY") { mutation }; clean()
    }
  }
  Fixture().apply {
    start(); waitForResume(); PlatformOperations.main.advance(5000)
    check(code() == "ENOTREADY"); resume(); clean()
  }
  for (state in listOf(Lifecycle.State.CREATED, Lifecycle.State.DESTROYED)) {
    Fixture().apply { start(); waitForResume(); lifecycle.move(state); PlatformOperations.main.flush(); check(code() == "ABORT_ERR"); clean() }
  }
  Fixture().apply {
    start(); waitForResume()
    val field = PlatformOperations::class.java.getDeclaredField("pending").apply { isAccessible = true }
    @Suppress("UNCHECKED_CAST") val calls = field.get(PlatformOperations) as Map<String, PlatformCall>
    calls.values.single().failure("ETIMEDOUT"); PlatformOperations.main.flush(); resume()
    check(code() == "ETIMEDOUT"); clean()
  }
  for ((type, expected) in listOf(GetCredentialException.TYPE_USER_CANCELED to "ABORT_ERR",
      GetCredentialException.TYPE_NO_CREDENTIAL to "ENOENT", GetCredentialException.TYPE_INTERRUPTED to "EINTR", "other" to "EIO")) {
    Fixture().apply { start(); manager.receiver.onError(GetCredentialException(type)); PlatformOperations.main.flush(); check(code() == expected); clean() }
  }
  for ((id, password, type, filter) in listOf(
      listOf("", "public", "android.credentials.TYPE_PASSWORD_CREDENTIAL", ""),
      listOf("public", "", "android.credentials.TYPE_PASSWORD_CREDENTIAL", ""),
      listOf("public", "public", "other", ""),
      listOf("public", "public", "android.credentials.TYPE_PASSWORD_CREDENTIAL", "different"))) {
    Fixture().apply { start(filter); manager.receiver.onResult(response(id, password, type)); PlatformOperations.main.flush(); check(code() == "EINVAL"); clean() }
  }
  val field = PlatformOperations::class.java.getDeclaredField("deadlines").apply { isAccessible = true }
  check((field.get(PlatformOperations) as java.util.concurrent.ScheduledThreadPoolExecutor).queue.isEmpty())
  println("credential return ordering, cancellation, ownership, timeout and validation checks passed")
}
