package android.content.pm
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
class ShortcutInfo(val id: String, val intent: Intent) {
  class Builder(context: Context, val id: String) {
    var intent = Intent()
    fun setShortLabel(label: String) = this
    fun setIntent(value: Intent): Builder { intent = value; return this }
    fun setIcon(icon: Icon) = this
    fun build() = ShortcutInfo(id, intent)
  }
}
class ShortcutManager {
  companion object { val instance = ShortcutManager() }
  val isRequestPinShortcutSupported = true; val maxShortcutCountPerActivity = 4; val isRateLimitingActive = false
  var accept = true
  val shortcuts = mutableMapOf<String, ShortcutInfo>(); val disabled = mutableSetOf<String>()
  fun addDynamicShortcuts(values: List<ShortcutInfo>): Boolean {
    check(values.isNotEmpty())
    if (!accept) return false
    values.forEach { shortcuts[it.id] = it }; return true
  }
  fun disableShortcuts(ids: List<String>, message: String) { disabled.addAll(ids) }
  fun enableShortcuts(ids: List<String>) { disabled.removeAll(ids.toSet()) }
  fun removeDynamicShortcuts(ids: List<String>) { ids.forEach { shortcuts.remove(it) } }
  fun requestPinShortcut(info: ShortcutInfo, callback: Any?) = true
}
