package oro.runtime.app

import android.content.Context
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.drawable.Icon
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** Shortcuts carry only an opaque routing token. Apps resolve targets after sign-in. */
internal object LauncherDestinations {
  private const val STORE = "oro.launcher.v1"
  private const val TTL = 7 * 24 * 60 * 60 * 1000L
  private const val TOKEN = "oro.launcher.token"
  private const val STATIC = "oro.launcher.static"
  private fun action (context: Context) = context.packageName + ".oro.LAUNCH_DESTINATION"
  private fun enabled () = App.getInstance().getUserConfigValue("launcher_enabled") == "true"
  private fun preferences (context: Context) = context.getSharedPreferences(STORE, Context.MODE_PRIVATE)
  private fun read (context: Context) = JSONObject(preferences(context).getString("state", "{}") ?: "{}")
  private fun save (context: Context, state: JSONObject) {
    check(preferences(context).edit().putString("state", state.toString()).commit())
  }
  private fun manager (context: Context) = context.getSystemService(ShortcutManager::class.java)
  private fun id (value: String): String {
    require(Regex("[a-zA-Z0-9_-]{1,128}").matches(value))
    return value
  }
  private fun references (value: JSONObject): JSONObject {
    require(value.keys().asSequence().all { it in listOf("id", "label", "accountRef", "targetRef") })
    val label = value.getString("label")
    require(label.length in 1..80 && label.none { it.code < 32 || it.code == 127 })
    return JSONObject().put("id", id(value.getString("id"))).put("label", label)
      .put("accountRef", id(value.getString("accountRef"))).put("targetRef", id(value.getString("targetRef")))
  }
  private fun shortcut (context: Context, record: JSONObject): ShortcutInfo {
    val intent = context.packageManager.getLaunchIntentForPackage(context.packageName) ?: throw PlatformFailure("ENOTREADY")
    intent.action = action(context)
    intent.data = null
    intent.putExtra(TOKEN, record.getString("token"))
    intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
    val builder = ShortcutInfo.Builder(context, "oro.destination.${record.getString("id")}")
      .setShortLabel(record.getString("label")).setIntent(intent)
    if (context.applicationInfo.icon != 0) builder.setIcon(Icon.createWithResource(context, context.applicationInfo.icon))
    return builder.build()
  }
  private fun receipts (state: JSONObject): JSONArray {
    val source = state.optJSONArray("receipts") ?: JSONArray()
    val result = JSONArray()
    for (index in 0 until source.length()) {
      val receipt = source.getJSONObject(index)
      if (System.currentTimeMillis() - receipt.optLong("receivedAt") in 0..TTL) result.put(receipt)
    }
    return result
  }

  @Synchronized
  fun accept (context: Context, intent: Intent) {
    if (!enabled() || intent.action != action(context)) return
    try {
      val token = intent.getStringExtra(TOKEN)
      val staticId = intent.getStringExtra(STATIC)
      intent.removeExtra(TOKEN)
      intent.removeExtra(STATIC)
      val state = read(context)
      val entry = if (token != null && token.length <= 80) {
        val entries = state.optJSONObject("entries") ?: JSONObject()
        entries.keys().asSequence().map { entries.getJSONObject(it) }.firstOrNull { it.optString("token") == token }
      } else if (staticId != null && Regex("[a-zA-Z0-9_-]{1,64}").matches(staticId) &&
          staticId in App.getInstance().getUserConfigValue("launcher_static_destinations").split(Regex("\\s+"))) {
        JSONObject().put("id", staticId).put("targetRef", staticId).put("accountRef", JSONObject.NULL).put("static", true)
      } else null
      if (entry == null) return
      val retained = receipts(state)
      while (retained.length() >= 32) retained.remove(0)
      retained.put(JSONObject().put("receiptId", UUID.randomUUID().toString()).put("id", entry.getString("id"))
        .put("targetRef", entry.getString("targetRef")).put("accountRef", entry.opt("accountRef") ?: JSONObject.NULL)
        .put("static", entry.optBoolean("static", false)).put("receivedAt", System.currentTimeMillis()))
      save(context, state.put("receipts", retained))
    } catch (_: Exception) {
      // Invalid routing identities are ignored; no destination is opened here.
    }
  }

  @Synchronized
  fun request (call: PlatformCall, operation: String, options: JSONObject) {
    val context = call.app
    val service = manager(context)
    if (operation == "status") {
      call.success(JSONObject().put("available", enabled()).put("pinSupported", service.isRequestPinShortcutSupported)
        .put("maximumPerActivity", service.maxShortcutCountPerActivity).put("rateLimited", service.isRateLimitingActive)
        .put("authorizationBoundary", "resolve-after-authentication"))
      return
    }
    if (!enabled()) throw PlatformFailure("ENOSYS")
    val state = read(context)
    when (operation) {
      "responses" -> call.success(receipts(state))
      "acknowledge" -> {
        val receiptId = options.getString("receiptId")
        require(receiptId.length <= 80)
        val retained = receipts(state)
        val result = JSONArray()
        for (index in 0 until retained.length()) {
          if (retained.getJSONObject(index).optString("receiptId") != receiptId) result.put(retained.getJSONObject(index))
        }
        save(context, state.put("receipts", result))
        call.success()
      }
      "replace" -> {
        call.foreground()
        val source = options.getJSONArray("destinations")
        require(source.length() <= minOf(16, service.maxShortcutCountPerActivity))
        val previous = state.optJSONObject("entries") ?: JSONObject()
        val entries = JSONObject()
        val shortcuts = mutableListOf<ShortcutInfo>()
        for (index in 0 until source.length()) {
          val record = references(source.getJSONObject(index))
          val identifier = record.getString("id")
          require(!entries.has(identifier))
          val old = previous.optJSONObject(identifier)
          val same = old?.optString("accountRef") == record.getString("accountRef") &&
            old?.optString("targetRef") == record.getString("targetRef")
          record.put("token", if (same) old!!.getString("token") else UUID.randomUUID().toString())
          entries.put(identifier, record)
          shortcuts.add(shortcut(context, record))
        }
        save(context, state.put("entries", entries))
        try {
          if (shortcuts.isNotEmpty() && !service.addDynamicShortcuts(shortcuts)) throw PlatformFailure("EAGAIN")
        } catch (error: Exception) {
          save(context, state.put("entries", previous))
          throw error
        }
        val obsolete = previous.keys().asSequence().filter { !entries.has(it) }.map { "oro.destination.$it" }.toList()
        if (obsolete.isNotEmpty()) {
          service.disableShortcuts(obsolete, "Destination is no longer available")
          service.removeDynamicShortcuts(obsolete)
        }
        if (shortcuts.isNotEmpty()) service.enableShortcuts(entries.keys().asSequence().map { "oro.destination.$it" }.toList())
        call.success(JSONObject().put("published", source.length()).put("authorizationEstablished", false))
      }
      "pin" -> {
        call.foreground()
        if (!service.isRequestPinShortcutSupported) throw PlatformFailure("ENOSYS")
        val entry = state.optJSONObject("entries")?.optJSONObject(id(options.getString("id"))) ?: throw PlatformFailure("ENOENT")
        val requested = service.requestPinShortcut(shortcut(context, entry), null)
        call.success(JSONObject().put("requested", requested).put("pinConfirmed", false))
      }
      else -> throw PlatformFailure("EINVAL")
    }
  }
}
