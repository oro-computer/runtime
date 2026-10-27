package oro.runtime.app
import androidx.lifecycle.Lifecycle
import org.json.JSONObject
import oro.runtime.window.*
open class AppActivity {
  var isFinishing = false; var isDestroyed = false; val lifecycle = Lifecycle()
}
class App {
  val activity = WindowManagerActivity()
  val results = mutableMapOf<Long, JSONObject>()
  fun onPlatformOperationResult(token: Long, value: String) {
    check(!results.containsKey(token)) { "Operation completed more than once" }
    results[token] = JSONObject(value)
  }
}
internal object ProtectedAuthentication {
  var calls = 0; var cancelled = 0
  val pending = mutableListOf<PlatformCall>()
  fun request(call: PlatformCall, action: String, options: JSONObject) {
    call.foreground(); calls++; call.cancel = { cancelled++ }; pending.add(call)
  }
}
internal object LauncherDestinations { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
internal object DeferredSync { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
internal object ForegroundTasks { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
internal object NativeGestures {
  fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() }
  fun invalidate(window: oro.runtime.window.Window) {}
}
class NativeStateException(val code: String) : Exception(code)
internal object PasswordCredentials { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
internal object NativeExtensions { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
internal object NativeNotifications { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
internal object NativePush { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
fun main() {
  val app = App(); val fragment = Fragment(0); val owner = Window(app.activity, fragment)
  fragment.window = owner; app.activity.windowFragmentManager.fragments.add(fragment)
  val document = owner.platformDocumentToken
  fun start(token: Long, id: String, feature: String = "authentication", source: String = "{}", doc: String = document) =
    PlatformOperations.start(app, token, 0, feature, "status", source, id, doc)
  fun code(token: Long) = app.results[token]?.optJSONObject("err")?.optString("code")
  start(1, "early-cancel", "cancel", "{\"id\":\"cancelled-before-start\"}")
  check(app.results[1]!!.has("data"))
  start(2, "cancelled-before-start")
  check(code(2) == "ABORT_ERR" && ProtectedAuthentication.calls == 0)
  start(3, "request-a")
  check(ProtectedAuthentication.calls == 1)
  start(4, "cancel-a", "cancel", "{\"id\":\"request-a\"}")
  check(code(3) == "ABORT_ERR" && ProtectedAuthentication.cancelled == 1)
  ProtectedAuthentication.pending.last().success(JSONObject().put("secret", "public-fixture"))
  check(code(3) == "ABORT_ERR")
  start(5, "stale-request")
  owner.platformDocumentToken = "b".repeat(32)
  ProtectedAuthentication.pending.last().success(JSONObject().put("secret", "public-fixture"))
  check(code(5) == "ABORT_ERR")
  start(6, "old-document")
  check(code(6) == "ABORT_ERR")
  start(7, "active-request", doc = owner.platformDocumentToken)
  start(8, "old-cancel", "cancel", "{\"id\":\"active-request\"}")
  check(code(8) == "ABORT_ERR" && !app.results.containsKey(7))
  PlatformOperations.cancelActivity(app.activity)
  check(code(7) == "ABORT_ERR")
  owner.platformDocumentToken = document
  app.activity.lifecycle.currentState = Lifecycle.State.DESTROYED
  start(9, "background-request")
  check(code(9) == "ENOTREADY")
  app.activity.lifecycle.currentState = Lifecycle.State.RESUMED
  repeat(32) { start(100L + it, "bounded-$it") }
  start(200, "overflow")
  check(code(200) == "EBUSY")
  PlatformOperations.cancelWindow(owner)
  repeat(32) { check(code(100L + it) == "ABORT_ERR") }
  check(app.results.values.none { it.optJSONObject("data")?.has("secret") == true })
  var callbacks = 0
  val completed = PlatformCall(app, 300, 0, "completed", owner, document)
  completed.failure("ABORT_ERR")
  completed.setCancellation { callbacks++ }
  check(callbacks == 1)
  check(runCatching { completed.setCancellation { callbacks++ } }.isFailure)
  val throwing = PlatformCall(app, 301, 0, "throwing", owner, document)
  throwing.setCancellation { throw IllegalStateException("public fixture") }
  throwing.failure("ABORT_ERR")
  check(code(301) == "ABORT_ERR")
  start(302, "old-tombstone", "cancel", "{\"id\":\"reused-id\"}")
  owner.platformDocumentToken = "c".repeat(32)
  start(303, "reused-id", doc = owner.platformDocumentToken)
  check(!app.results.containsKey(303))
  PlatformOperations.cancelWindow(owner)
  check(code(303) == "ABORT_ERR")
  for ((token, paused) in listOf(350L to true, 351L to false)) {
    if (paused) app.activity.lifecycle.currentState = Lifecycle.State.STARTED
    else fragment.isHidden = true
    PlatformOperations.start(app, token, 0, "back", "disable", "{\"registration\":\"public\"}",
      "back-cleanup-$token", owner.platformDocumentToken)
    check(app.results[token]?.has("data") == true) { "Background cleanup failed: ${code(token)}" }
    PlatformOperations.start(app, token + 10, 0, "authentication", "status", "{}",
      "private-background-$token", owner.platformDocumentToken)
    check(code(token + 10) == "ENOTREADY")
    app.activity.lifecycle.currentState = Lifecycle.State.RESUMED
    fragment.isHidden = false
  }
  val field = PlatformOperations::class.java.getDeclaredField("deadlines")
  field.isAccessible = true
  val deadlines = field.get(PlatformOperations) as java.util.concurrent.ScheduledThreadPoolExecutor
  check(deadlines.queue.isEmpty())
}

internal object NativeWorkflows { fun request(call: PlatformCall, action: String, options: JSONObject) { call.success() } }
