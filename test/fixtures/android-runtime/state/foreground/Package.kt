package android.content.pm

class ServiceInfo {
  var processName: String? = null
  var enabled = true; var exported = false; var foregroundServiceType = FOREGROUND_SERVICE_TYPE_DATA_SYNC
  companion object { const val FOREGROUND_SERVICE_TYPE_DATA_SYNC = 1; const val FOREGROUND_SERVICE_TYPE_SPECIAL_USE = 0x40000000 }
}
class PackageManager {
  companion object { const val PERMISSION_GRANTED = 0 }
  fun getServiceInfo(component: android.content.ComponentName, flags: Int) = ServiceInfo().apply {
    if (component.type == oro.runtime.app.RuntimeContinuousService::class.java) {
      foregroundServiceType = android.content.PublicEnvironment.continuousType
      processName = android.content.PublicEnvironment.continuousProcess
    }
  }
  class Property(val string: String?)
  fun getProperty(name: String, component: android.content.ComponentName) = Property(android.content.PublicEnvironment.continuousSubtype)
  fun getLaunchIntentForPackage(name: String): android.content.Intent? = android.content.Intent()
}
