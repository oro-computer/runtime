package oro.runtime.usb

import android.app.Activity
import android.app.AlertDialog
import android.os.ParcelFileDescriptor
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
      try {
        array.put(deviceToJson(device, manager.hasPermission(device)))
      } catch (_: SecurityException) {
        // Android can revoke access to serial descriptors while the device detaches.
      }
    }
    return array.toString()
  }

  @JvmStatic
  fun requestPermission (context: Context, requestId: Long, deviceIds: Array<String>) {
    val ids = deviceIds
      .map { it.trim() }
      .filter { it.isNotEmpty() }
      .toMutableSet()

    if (ids.isEmpty()) {
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
      onPermissionResult(requestId, "", false)
      return
    }

    // USB permission is a user interaction, not a foreground-service workflow.
    // Enumeration and permission requests never start a service or request alerts.
    handler.post {
      val manager = appContext.getSystemService(Context.USB_SERVICE) as UsbManager
      val activity = context as? Activity
      val devices = manager.deviceList.values.filter { ids.contains(it.deviceName) }
      fun cancel () {
        if (synchronized(lock) { pendingRequests.remove(requestId) } != null) {
          onPermissionResult(requestId, "", false)
        }
      }
      if (activity == null || activity.isFinishing || activity.isDestroyed || devices.isEmpty()) {
        cancel()
        return@post
      }
      val labels = devices.map { "%s (%04x:%04x)".format(it.productName ?: "", it.vendorId, it.productId) }
      try {
        var selected = false
        val dialog = AlertDialog.Builder(activity)
          .setTitle("Allow access to a USB device")
          .setItems(labels.toTypedArray()) { _, index ->
            selected = true
            val device = devices[index]
            synchronized(lock) { pendingRequests[requestId] = mutableSetOf(device.deviceName) }
            if (manager.hasPermission(device)) notifyPermissionResult(device.deviceName, true, requestId)
            else requestDevicePermission(appContext, manager, device, requestId)
          }
          .setNegativeButton(android.R.string.cancel) { _, _ -> cancel() }
          .setOnCancelListener { cancel() }
          .create()
        val timeout = Runnable { if (dialog.isShowing) dialog.dismiss() else cancel() }
        dialog.setOnDismissListener {
          handler.removeCallbacks(timeout)
          if (!selected) cancel()
        }
        dialog.show()
        handler.postDelayed(timeout, 60_000)
      } catch (_: RuntimeException) {
        cancel()
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

  private fun notifyPermissionResult (deviceId: String, granted: Boolean, requestId: Long) {
    val pending = synchronized(lock) { pendingRequests.remove(requestId) } ?: return
    onPermissionResult(requestId, deviceId, granted && pending.contains(deviceId))
  }

  @JvmStatic
  external fun onPermissionResult (requestId: Long, deviceId: String, granted: Boolean)

  fun openFileDescriptor (context: Context, id: String): Int {
    val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    val device = manager.deviceList[id] ?: return -1
    if (!manager.hasPermission(device)) return -1
    val connection = manager.openDevice(device) ?: return -1
    return try {
      ParcelFileDescriptor.fromFd(connection.fileDescriptor).detachFd()
    } finally {
      connection.close()
    }
  }

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

    obj.put("productName", device.productName ?: "")
    obj.put("manufacturerName", device.manufacturerName ?: "")
    obj.put("serialNumber", if (hasPermission) device.serialNumber ?: "" else "")
    val version = device.version.split('.')
    obj.put("deviceVersion", ((version.firstOrNull()?.toIntOrNull(16) ?: 0) shl 8) or
      (version.getOrNull(1)?.toIntOrNull(16) ?: 0))
    val configurationNames = JSONObject()
    val configurations = JSONArray()
    val interfaces = JSONArray()
    for (index in 0 until device.configurationCount) {
      val config = device.getConfiguration(index)
      configurations.put(config.id)
      configurationNames.put(config.id.toString(), config.name ?: "")
      for (i in 0 until config.interfaceCount) {
        val iface = config.getInterface(i)
        val endpoints = JSONArray()
        for (e in 0 until iface.endpointCount) {
          val ep = iface.getEndpoint(e)
          endpoints.put(JSONObject().put("endpointNumber", ep.endpointNumber)
            .put("direction", if (ep.direction == 128) "in" else "out")
            .put("type", arrayOf("control", "isochronous", "bulk", "interrupt")[ep.type])
            .put("packetSize", ep.maxPacketSize and 2047))
        }
        interfaces.put(JSONObject().put("configurationValue", config.id)
          .put("interfaceName", iface.name ?: "").put("interfaceNumber", iface.id).put("alternateSetting", iface.alternateSetting)
          .put("classCode", iface.interfaceClass).put("subclassCode", iface.interfaceSubclass)
          .put("protocolCode", iface.interfaceProtocol).put("endpoints", endpoints))
      }
    }
    obj.put("configurationNames", configurationNames)
    obj.put("configurations", configurations)
    obj.put("interfaces", interfaces)
    return obj
  }
}
