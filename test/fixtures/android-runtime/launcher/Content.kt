package android.content
import android.content.pm.ShortcutManager
class Intent {
  companion object { const val FLAG_ACTIVITY_NEW_TASK = 1; const val FLAG_ACTIVITY_CLEAR_TOP = 2; const val FLAG_ACTIVITY_SINGLE_TOP = 4 }
  var action: String? = null; var data: Any? = null; var flags = 0
  private val extras = mutableMapOf<String, String>()
  fun putExtra(key: String, value: String) { extras[key] = value }
  fun getStringExtra(key: String) = extras[key]
  fun removeExtra(key: String) { extras.remove(key) }
}
class ApplicationInfo { val icon = 0 }
class PackageManager { fun getLaunchIntentForPackage(name: String) = Intent() }
class SharedPreferences {
  private var state = "{}"
  fun getString(key: String, fallback: String?) = state
  fun edit() = Editor()
  inner class Editor {
    private var next = "{}"
    fun putString(key: String, value: String): Editor { next = value; return this }
    fun commit(): Boolean { state = next; return true }
  }
}
open class Context {
  companion object { const val MODE_PRIVATE = 0 }
  val packageName = "public.fixture"
  val packageManager = PackageManager(); val applicationInfo = ApplicationInfo(); val preferences = SharedPreferences()
  fun getSharedPreferences(name: String, mode: Int) = preferences
  fun <T> getSystemService(type: Class<T>): T = type.cast(ShortcutManager.instance)
}
