package oro.runtime.app
import android.content.Context
import android.content.Intent
import android.content.pm.ShortcutManager
import org.json.JSONObject
import org.json.JSONArray
class App : Context() {
  companion object { private val instance = App(); fun getInstance() = instance }
  fun getUserConfigValue(key: String) = when (key) { "launcher_enabled" -> "true"; "launcher_static_destinations" -> "public"; else -> "" }
}
internal class PlatformFailure(val code: String) : Exception(code)
internal class PlatformCall(val app: App = App.getInstance()) {
  var result: Any? = null
  fun foreground() {}
  fun success(value: Any = JSONObject.NULL) { result = value }
}
private fun request(action: String, options: JSONObject = JSONObject()): Any {
  val call = PlatformCall(); LauncherDestinations.request(call, action, options); return checkNotNull(call.result)
}
private fun publish(account: String = "public-account") = request("replace", JSONObject().put("destinations", JSONArray()
  .put(JSONObject().put("id", "public").put("label", "Public target").put("accountRef", account).put("targetRef", "public-target"))))
private fun copy(source: Intent): Intent = Intent().also {
  it.action = source.action; source.getStringExtra("oro.launcher.token")?.let { token -> it.putExtra("oro.launcher.token", token) }
}
fun main() {
  val app = App.getInstance(); val manager = ShortcutManager.instance
  val unrelated = android.content.pm.ShortcutInfo("unrelated", Intent()); manager.shortcuts[unrelated.id] = unrelated
  publish()
  val original = copy(manager.shortcuts["oro.destination.public"]!!.intent)
  val token = original.getStringExtra("oro.launcher.token")
  publish()
  check(manager.shortcuts["oro.destination.public"]!!.intent.getStringExtra("oro.launcher.token") == token)
  LauncherDestinations.accept(app, original)
  check(original.getStringExtra("oro.launcher.token") == null)
  LauncherDestinations.accept(app, original)
  var rows = request("responses") as JSONArray
  check(rows.length() == 1 && rows.getJSONObject(0).getString("accountRef") == "public-account")
  request("acknowledge", JSONObject().put("receiptId", rows.getJSONObject(0).getString("receiptId")))
  check((request("responses") as JSONArray).length() == 0)
  val revoked = copy(manager.shortcuts["oro.destination.public"]!!.intent)
  publish("another-public-account")
  LauncherDestinations.accept(app, revoked)
  check((request("responses") as JSONArray).length() == 0)
  manager.accept = false
  try { publish("failed-public-account"); error("Rejected publication accepted") } catch (error: PlatformFailure) { check(error.code == "EAGAIN") }
  manager.accept = true
  LauncherDestinations.accept(app, copy(manager.shortcuts["oro.destination.public"]!!.intent))
  rows = request("responses") as JSONArray
  check(rows.getJSONObject(0).getString("accountRef") == "another-public-account")
  request("replace", JSONObject().put("destinations", JSONArray()))
  check(manager.shortcuts.keys == setOf("unrelated") && "oro.destination.public" in manager.disabled)
  publish()
  check("oro.destination.public" !in manager.disabled)
  repeat(40) {
    val intent = Intent(); intent.action = "public.fixture.oro.LAUNCH_DESTINATION"; intent.putExtra("oro.launcher.static", "public")
    LauncherDestinations.accept(app, intent)
  }
  check((request("responses") as JSONArray).length() == 32)
  val unknown = Intent(); unknown.action = "public.fixture.oro.LAUNCH_DESTINATION"; unknown.putExtra("oro.launcher.static", "unknown")
  LauncherDestinations.accept(app, unknown)
  check((request("responses") as JSONArray).length() == 32)
}
