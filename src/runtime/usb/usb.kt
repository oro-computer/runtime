package oro.runtime.usb

import android.content.Context
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import __BUNDLE_IDENTIFIER__.UsbService

object USBPlatform {
  private const val TAG = "USBPlatform"
  private val lock = Any()
  private val pendingRequests = mutableMapOf<Long, MutableSet<String>>()

  @JvmStatic
  fun enumerate (context: Context): String {
    val appContext = context.applicationContext
    UsbService.ensureStarted(appContext)

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

    if (ids.isEmpty()) {
      synchronized(lock) {
        pendingRequests.remove(requestId)
      }
      onPermissionResult(requestId, "", false)
      return
    }

    val appContext = context.applicationContext
    UsbService.ensureStarted(appContext)

    synchronized(lock) {
      pendingRequests[requestId] = ids
    }

    UsbService.instanceOrNull()?.let { dispatchPending(it, requestId) }
      ?: Log.d(TAG, "UsbService not yet ready; deferring permission request $requestId")
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
        if (pending.remove(normalized) || requestIdOverride != null) {
          affected.add(Pair(requestId, granted))
          if (pending.isEmpty() || requestIdOverride != null) {
            iterator.remove()
          }
        }
      }

      if (affected.isEmpty() && requestIdOverride != null) {
        affected.add(Pair(requestIdOverride, granted))
      }
    }

    for ((requestId, ok) in affected) {
      onPermissionResult(requestId, normalized, ok)
    }
  }

  @JvmStatic
  external fun onPermissionResult (requestId: Long, deviceId: String, granted: Boolean)

  internal fun onServiceReady(service: UsbService) {
    dispatchPending(service, null)
  }

  private fun dispatchPending(service: UsbService, requestId: Long?) {
    val pendingCopy = mutableMapOf<Long, List<String>>()
    synchronized(lock) {
      if (requestId != null) {
        pendingRequests[requestId]?.let { pendingCopy[requestId] = it.toList() }
      } else {
        for ((id, ids) in pendingRequests) {
          pendingCopy[id] = ids.toList()
        }
      }
    }

    for ((reqId, ids) in pendingCopy) {
      for (id in ids) {
        val ok = UsbService.requestPermissionForDevice(id)
        if (!ok) {
          removePending(reqId, id)
          notifyPermissionResult(id, granted = false, requestIdOverride = reqId)
        }
      }
    }
  }

  internal fun onServiceUnavailable() {
    val snapshot = mutableMapOf<Long, List<String>>()
    synchronized(lock) {
      for ((requestId, ids) in pendingRequests) {
        snapshot[requestId] = ids.toList()
      }
      pendingRequests.clear()
    }
    for ((requestId, ids) in snapshot) {
      if (ids.isEmpty()) {
        onPermissionResult(requestId, "", false)
      } else {
        for (id in ids) {
          onPermissionResult(requestId, id, false)
        }
      }
    }
  }

  private fun removePending(requestId: Long, deviceId: String) {
    synchronized(lock) {
      val pending = pendingRequests[requestId] ?: return
      pending.remove(deviceId)
      if (pending.isEmpty()) {
        pendingRequests.remove(requestId)
      }
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
