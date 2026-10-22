package oro.runtime.usb

import android.content.Context
import android.content.BroadcastReceiver
import android.content.Intent
import android.content.IntentFilter
import android.app.PendingIntent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import org.json.JSONArray
import org.json.JSONObject

object USBPlatform {
  private val lock = Any()
  private val handler = Handler(Looper.getMainLooper())
  private val pendingRequests = mutableMapOf<Long, MutableSet<String>>()

  @JvmStatic
  fun enumerate (context: Context): String {
    val appContext = context.applicationContext
    val manager = appContext.getSystemService(Context.USB_SERVICE) as UsbManager
    val array = JSONArray()
    for (device in manager.deviceList.values) {
      array.put(deviceToJson(device, manager.hasPermission(device)))
    }
    return array.toString()
  }

  @JvmStatic
  fun requestPermission (context: Context, requestId: Long, deviceIds: Array<String>) {
    val ids = deviceIds
      .mapNotNull { it?.trim() }
      .filter { it.isNotEmpty() }
      .toMutableSet()

    if (ids.isEmpty() || ids.size > 16) {
      synchronized(lock) {
        pendingRequests.remove(requestId)
      }
      onPermissionResult(requestId, "", false)
      return
    }

    val appContext = context.applicationContext
    val accepted = synchronized(lock) {
      if (pendingRequests.size >= 32 || pendingRequests.containsKey(requestId)) {
        false
      } else {
        pendingRequests[requestId] = ids
        true
      }
    }
    if (!accepted) {
      for (id in ids) onPermissionResult(requestId, id, false)
      return
    }

    // USB permission is a user interaction, not a foreground-service workflow.
    // Enumeration and permission requests never start a service or request alerts.
    handler.post {
      val manager = appContext.getSystemService(Context.USB_SERVICE) as UsbManager
      for (id in ids.toList()) {
        val device = manager.deviceList.values.firstOrNull { it.deviceName == id }
        if (device == null) {
          notifyPermissionResult(id, false, requestId)
        } else if (manager.hasPermission(device)) {
          notifyPermissionResult(id, true, requestId)
        } else {
          requestDevicePermission(appContext, manager, device, requestId)
        }
      }
    }
  }

  private fun requestDevicePermission (context: Context, manager: UsbManager, device: UsbDevice, requestId: Long) {
    val action = "${context.packageName}.oro.USB_PERMISSION.${java.util.UUID.randomUUID()}"
    val intent = PendingIntent.getBroadcast(context, 0, Intent(action).setPackage(context.packageName),
      PendingIntent.FLAG_UPDATE_CURRENT or if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
    var completed = false
    var registered = false
    lateinit var receiver: BroadcastReceiver
    lateinit var timeout: Runnable
    fun finish (granted: Boolean) {
      if (completed) return
      completed = true
      handler.removeCallbacks(timeout)
      if (registered) context.unregisterReceiver(receiver)
      intent.cancel()
      notifyPermissionResult(device.deviceName, granted, requestId)
    }
    receiver = object : BroadcastReceiver() {
      override fun onReceive (context: Context, intent: Intent) {
        if (intent.action == action) finish(manager.hasPermission(device))
      }
    }
    timeout = Runnable { finish(false) }
    try {
      if (Build.VERSION.SDK_INT >= 33) {
        context.registerReceiver(receiver, IntentFilter(action), Context.RECEIVER_NOT_EXPORTED)
      } else {
        @Suppress("DEPRECATION")
        context.registerReceiver(receiver, IntentFilter(action))
      }
      registered = true
      handler.postDelayed(timeout, 60_000)
      manager.requestPermission(device, intent)
    } catch (_: RuntimeException) {
      finish(false)
    }
  }

  internal fun notifyPermissionResult (deviceId: String, granted: Boolean, requestIdOverride: Long? = null) {
    val normalized = deviceId.trim()
    if (normalized.isEmpty()) {
      return
    }

    val affected = mutableListOf<Pair<Long, Boolean>>()
    synchronized(lock) {
      val iterator = pendingRequests.iterator()
      while (iterator.hasNext()) {
        val entry = iterator.next()
        val requestId = entry.key
        if (requestIdOverride != null && requestId != requestIdOverride) {
          continue
        }
        val pending = entry.value
        if (pending.remove(normalized)) {
          affected.add(Pair(requestId, granted))
          if (pending.isEmpty()) {
            iterator.remove()
          }
        }
      }
    }

    for ((requestId, ok) in affected) {
      onPermissionResult(requestId, normalized, ok)
    }
  }

  @JvmStatic
  external fun onPermissionResult (requestId: Long, deviceId: String, granted: Boolean)

  private fun deviceToJson (device: UsbDevice, hasPermission: Boolean): JSONObject {
    val obj = JSONObject()
    val deviceId = device.deviceName ?: "${device.vendorId}:${device.productId}"
    obj.put("deviceId", deviceId)
    obj.put("vendorId", device.vendorId)
    obj.put("productId", device.productId)
    obj.put("classCode", device.deviceClass)
    obj.put("subclassCode", device.deviceSubclass)
    obj.put("protocolCode", device.deviceProtocol)
    obj.put("authorized", hasPermission)

    val interfaces = JSONArray()
    for (index in 0 until device.interfaceCount) {
      val iface = device.getInterface(index) ?: continue
      val entry = JSONObject()
      entry.put("interfaceNumber", iface.id)
      entry.put("alternateSetting", iface.alternateSetting)
      entry.put("classCode", iface.interfaceClass)
      entry.put("subclassCode", iface.interfaceSubclass)
      entry.put("protocolCode", iface.interfaceProtocol)
      interfaces.put(entry)
    }
    obj.put("interfaces", interfaces)
    return obj
  }
}
