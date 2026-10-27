package android.content

import java.io.File
import android.app.*
import android.content.pm.PackageManager
import oro.runtime.app.RuntimeForegroundService
import oro.runtime.app.RuntimeContinuousService

object PublicEnvironment {
  lateinit var root: File
  var notificationsAllowed = true
  var denyStart = false
  var delayDestroy = false
  var service: RuntimeForegroundService? = null
  var continuousService: RuntimeForegroundService? = null
  var continuousPermission = true
  var continuousProcess: String? = null
  var continuousSubtype: String? = "Public monitoring"
  var continuousType = android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
  var starts = 0
  val notificationManager = NotificationManager()
}
open class Context(val root: File = PublicEnvironment.root) {
  val applicationContext get() = this
  val packageName = "oro.runtime.public.state"
  val noBackupFilesDir get() = File(root, "no-backup")
  val packageManager = PackageManager()
  val resources = Resources()
  companion object { const val ACTIVITY_SERVICE = "activity" }
  fun checkSelfPermission(permission: String) = if (permission.endsWith("FOREGROUND_SERVICE_SPECIAL_USE") && !PublicEnvironment.continuousPermission) -1 else if (permission.endsWith("FOREGROUND_SERVICE") && android.os.Build.VERSION.SDK_INT < 28) -1 else if (permission.endsWith("POST_NOTIFICATIONS") && !PublicEnvironment.notificationsAllowed) -1 else 0
  fun <T> getSystemService(type: Class<T>): T = type.cast(PublicEnvironment.notificationManager)
  fun getSystemService(name: String) = ActivityManager()
  fun startForegroundService(intent: Intent): ComponentName {
    if (PublicEnvironment.denyStart) throw SecurityException("public-denied")
    PublicEnvironment.starts++
    val continuous = intent.type == RuntimeContinuousService::class.java
    val service = if (continuous) PublicEnvironment.continuousService ?: RuntimeContinuousService().also { PublicEnvironment.continuousService = it }
      else PublicEnvironment.service ?: RuntimeForegroundService().also { PublicEnvironment.service = it }
    service.onStartCommand(intent, 0, PublicEnvironment.starts)
    return ComponentName(this, RuntimeForegroundService::class.java)
  }
}
class ComponentName(context: Context, val type: Class<*>)
class Intent {
  var action: String? = null
  var type: Class<*>? = null
  private val extras = mutableMapOf<String,String>()
  constructor()
  constructor(context: Context, type: Class<*>) { this.type = type }
  fun setAction(value: String) = apply { action = value }
  fun setData(value: android.net.Uri) = this
  fun putExtra(key: String, value: String) = apply { extras[key] = value }
  fun getStringExtra(key: String) = extras[key]
}

class Resources {
  fun getIdentifier(name: String, type: String, owner: String) = if (name == "public_status" && type == "drawable") 73 else 0
}
