package android.content

import android.app.job.JobScheduler

class ComponentName(context: Context, type: Class<*>)
class ServiceInfo { val enabled = true; val permission = "android.permission.BIND_JOB_SERVICE" }
class PackageManager { fun getServiceInfo(component: ComponentName, flags: Int) = ServiceInfo() }
class SharedPreferences {
  companion object {
    val values = java.util.concurrent.ConcurrentHashMap<String, String>()
    @Volatile var afterRead: (() -> Unit)? = null
    @Volatile var afterCommit: (() -> Unit)? = null
  }
  fun getString(key: String, fallback: String?): String? {
    val snapshot = values[key] ?: fallback
    afterRead?.invoke()
    return snapshot
  }
  fun edit() = Editor()
  class Editor {
    private val changes = mutableMapOf<String, String>()
    fun putString(key: String, value: String): Editor { changes[key] = value; return this }
    fun commit(): Boolean { values.putAll(changes); afterCommit?.invoke(); return true }
  }
}
open class Context {
  companion object { const val MODE_PRIVATE = 0 }
  val applicationContext: Context get() = this
  val packageManager = PackageManager()
  fun checkSelfPermission(permission: String) = 0
  fun getSharedPreferences(name: String, mode: Int) = SharedPreferences()
  fun <T> getSystemService(type: Class<T>): T = type.cast(JobScheduler.instance)
}
