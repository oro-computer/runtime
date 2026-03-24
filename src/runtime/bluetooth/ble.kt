package oro.runtime.bluetooth

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.os.ParcelUuid

@SuppressLint("MissingPermission")
class BLEManager (private val context: Context) {
  private val adapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
  private val scanner get() = adapter?.bluetoothLeScanner
  private var scanning = false
  private val gatts = mutableMapOf<String, BluetoothGatt>()
  private val services = mutableMapOf<String, List<String>>()
  private val characteristics = mutableMapOf<String, MutableMap<String, List<String>>>()
  private val pendingChars = mutableSetOf<Pair<String, String>>()

  private val callback = object : ScanCallback() {
    override fun onScanResult(callbackType: Int, result: ScanResult) {
      val dev: BluetoothDevice? = result.device
      if (dev != null) {
        val id = dev.address ?: return
        val name = dev.name ?: ""
        val services = result.scanRecord?.serviceUuids
          ?.mapNotNull { it?.uuid?.toString() }
          ?.toTypedArray()
        val record = result.scanRecord
        val manufacturerIds = mutableListOf<Int>()
        val manufacturerPayloads = mutableListOf<ByteArray>()
        if (record != null) {
          val sparse = record.manufacturerSpecificData
          for (i in 0 until sparse.size()) {
            val companyId = sparse.keyAt(i)
            val payload = sparse.valueAt(i) ?: ByteArray(0)
            manufacturerIds.add(companyId)
            manufacturerPayloads.add(payload.clone())
          }
        }
        val idsArray = if (manufacturerIds.isNotEmpty()) manufacturerIds.toIntArray() else null
        val dataArray = if (manufacturerPayloads.isNotEmpty()) manufacturerPayloads.toTypedArray() else null
        onDeviceFound(id, name, result.rssi, services, idsArray, dataArray)
      }
    }
  }

  fun isEnabled (): Boolean = adapter?.isEnabled ?: false

  fun startScan (services: List<String>?) {
    if (scanning) return
    val settings = ScanSettings.Builder()
      .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
      .build()
    val filters = services?.mapNotNull { s ->
      try { ParcelUuid.fromString(s) } catch (_: Throwable) { null }
    }?.map { uuid -> android.bluetooth.le.ScanFilter.Builder().setServiceUuid(uuid).build() }
    scanner?.startScan(filters, settings, callback)
    scanning = true
  }

  fun stopScan () {
    if (!scanning) return
    scanner?.stopScan(callback)
    scanning = false
  }

  fun connect (address: String) {
    val dev = adapter?.getRemoteDevice(address) ?: run {
      onGattConnected(address, false, "Device not found")
      return
    }
    val cb = object : BluetoothGattCallback() {
      override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
        if (newState == BluetoothProfile.STATE_CONNECTED) {
          gatts[address] = gatt
          onGattConnected(address, true, "")
          gatt.discoverServices()
        } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
          gatts.remove(address)
          onGattDisconnected(address, "Disconnected")
        }
      }

      override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
        try {
          val devAddr = address
          val list = gatt.services?.mapNotNull { it.uuid?.toString() } ?: emptyList()
          services[devAddr] = list
          val charMap = characteristics.getOrPut(devAddr) { mutableMapOf() }
          for (svc in gatt.services ?: emptyList()) {
            val s = svc.uuid?.toString() ?: continue
            val clist = svc.characteristics?.mapNotNull { it.uuid?.toString() } ?: emptyList()
            charMap[s] = clist
          }
          onServicesDiscovered(devAddr, list.toTypedArray())

          // If there are pending characteristic queries, try to satisfy them now
          val toRemove = mutableListOf<Pair<String, String>>()
          for ((dev, svc) in pendingChars) {
            if (dev == devAddr) {
              val chars = charMap[svc]
              if (chars != null) {
                onCharacteristicsDiscovered(devAddr, svc, chars.toTypedArray())
                toRemove.add(Pair(dev, svc))
              }
            }
          }
          pendingChars.removeAll(toRemove.toSet())
        } catch (_: Throwable) {}
      }

      override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
        val devAddr = address
        val svc = characteristic.service?.uuid?.toString() ?: ""
        val chr = characteristic.uuid?.toString() ?: ""
        val value = characteristic.value ?: ByteArray(0)
        onCharacteristicRead(devAddr, svc, chr, value)
      }

      override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
        val devAddr = address
        val svc = characteristic.service?.uuid?.toString() ?: ""
        val chr = characteristic.uuid?.toString() ?: ""
        val ok = status == android.bluetooth.BluetoothGatt.GATT_SUCCESS
        val msg = if (ok) null else "Write failed ($status)"
        onCharacteristicWrite(devAddr, svc, chr, ok, msg)
      }

      override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        val devAddr = address
        val svc = characteristic.service?.uuid?.toString() ?: ""
        val chr = characteristic.uuid?.toString() ?: ""
        val value = characteristic.value ?: ByteArray(0)
        onCharacteristicChanged(devAddr, svc, chr, value)
      }
    }
    try {
      dev.connectGatt(context, false, cb)
    } catch (t: Throwable) {
      onGattConnected(address, false, t.message ?: "Connection error")
    }
  }

  fun disconnect (address: String) {
    val g = gatts[address]
    try { g?.disconnect() } catch (_: Throwable) {}
    try { g?.close() } catch (_: Throwable) {}
    gatts.remove(address)
    services.remove(address)
    characteristics.remove(address)
    pendingChars.removeAll { (dev, _) -> dev == address }
  }

  fun discoverServices (address: String) {
    try { gatts[address]?.discoverServices() } catch (_: Throwable) {}
  }

  fun getServices (address: String): Array<String>? {
    return services[address]?.toTypedArray()
  }

  fun getCharacteristics (address: String, service: String): Array<String>? {
    val cm = characteristics[address]
    return cm?.get(service)?.toTypedArray()
  }

  fun getCharacteristicPropertiesFlags (address: String, service: String): IntArray? {
    val g = gatts[address] ?: return null
    val s = try { g.getService(java.util.UUID.fromString(service)) } catch (_: Throwable) { null }
    val chars = s?.characteristics ?: return null
    val flags = IntArray(chars.size)
    for (i in chars.indices) {
      try {
        flags[i] = chars[i].properties
      } catch (_: Throwable) {
        flags[i] = 0
      }
    }
    return flags
  }

  fun requestCharacteristics (address: String, service: String) {
    val chars = getCharacteristics(address, service)
    if (chars != null) {
      onCharacteristicsDiscovered(address, service, chars)
      return
    }
    pendingChars.add(Pair(address, service))
    discoverServices(address)
  }

  fun readCharacteristic (address: String, service: String, characteristic: String) {
    val g = gatts[address] ?: return onCharacteristicRead(address, service, characteristic, ByteArray(0))
    val s = g.getService(java.util.UUID.fromString(service))
    val c = s?.getCharacteristic(java.util.UUID.fromString(characteristic))
    if (c == null) return onCharacteristicRead(address, service, characteristic, ByteArray(0))
    try { g.readCharacteristic(c) } catch (_: Throwable) { onCharacteristicRead(address, service, characteristic, ByteArray(0)) }
  }

  fun writeCharacteristic (address: String, service: String, characteristic: String, value: ByteArray) {
    val g = gatts[address] ?: return onCharacteristicWrite(address, service, characteristic, false, "Not connected")
    val s = g.getService(java.util.UUID.fromString(service))
    val c = s?.getCharacteristic(java.util.UUID.fromString(characteristic))
    if (c == null) return onCharacteristicWrite(address, service, characteristic, false, "Characteristic not found")
    try {
      c.value = value
      if (!g.writeCharacteristic(c)) onCharacteristicWrite(address, service, characteristic, false, "Write failed")
    } catch (t: Throwable) {
      onCharacteristicWrite(address, service, characteristic, false, t.message ?: "Write error")
    }
  }

  private fun setCccd (g: BluetoothGatt, c: BluetoothGattCharacteristic, enable: Boolean) {
    try {
      g.setCharacteristicNotification(c, enable)
      val cccd = c.getDescriptor(java.util.UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"))
      if (cccd != null) {
        cccd.value = if (enable) BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE else BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE
        g.writeDescriptor(cccd)
      }
    } catch (_: Throwable) {}
  }

  fun startNotifications (address: String, service: String, characteristic: String) {
    val g = gatts[address] ?: return
    val s = g.getService(java.util.UUID.fromString(service))
    val c = s?.getCharacteristic(java.util.UUID.fromString(characteristic)) ?: return
    setCccd(g, c, true)
  }

  fun stopNotifications (address: String, service: String, characteristic: String) {
    val g = gatts[address] ?: return
    val s = g.getService(java.util.UUID.fromString(service))
    val c = s?.getCharacteristic(java.util.UUID.fromString(characteristic)) ?: return
    setCccd(g, c, false)
  }

  companion object {
    @JvmStatic external fun onDeviceFound (id: String, name: String, rssi: Int, services: Array<String>?, manufacturerIds: IntArray?, manufacturerData: Array<ByteArray>?)
    @JvmStatic external fun onGattConnected (id: String, ok: Boolean, message: String?)
    @JvmStatic external fun onGattDisconnected (id: String, reason: String?)
    @JvmStatic external fun onServicesDiscovered (id: String, services: Array<String>)
    @JvmStatic external fun onCharacteristicsDiscovered (id: String, service: String, characteristics: Array<String>)
    @JvmStatic external fun onCharacteristicRead (id: String, service: String, characteristic: String, value: ByteArray)
    @JvmStatic external fun onCharacteristicWrite (id: String, service: String, characteristic: String, ok: Boolean, message: String?)
    @JvmStatic external fun onCharacteristicChanged (id: String, service: String, characteristic: String, value: ByteArray)
  }
}

object BLE {
  private var manager: BLEManager? = null

  @JvmStatic fun ensure (context: Context) {
    if (manager == null) manager = BLEManager(context.applicationContext)
  }

  @JvmStatic fun isEnabled (context: Context): Boolean {
    ensure(context)
    return manager?.isEnabled() ?: false
  }

  @JvmStatic fun startScan (context: Context, services: Array<String>?) {
    ensure(context)
    manager?.startScan(services?.toList())
  }

  @JvmStatic fun stopScan (context: Context) {
    manager?.stopScan()
  }

  @JvmStatic fun connect (context: Context, address: String) {
    ensure(context)
    manager?.connect(address)
  }

  @JvmStatic fun disconnect (context: Context, address: String) {
    ensure(context)
    manager?.disconnect(address)
  }

  @JvmStatic fun discoverServices (context: Context, address: String) {
    ensure(context)
    manager?.discoverServices(address)
  }

  @JvmStatic fun getServices (context: Context, address: String): Array<String>? {
    ensure(context)
    return manager?.getServices(address)
  }

  @JvmStatic fun requestCharacteristics (context: Context, address: String, service: String) {
    ensure(context)
    manager?.requestCharacteristics(address, service)
  }

  @JvmStatic fun getCharacteristics (context: Context, address: String, service: String): Array<String>? {
    ensure(context)
    return manager?.getCharacteristics(address, service)
  }

  @JvmStatic fun getCharacteristicPropertiesFlags (context: Context, address: String, service: String): IntArray? {
    ensure(context)
    return manager?.getCharacteristicPropertiesFlags(address, service)
  }

  @JvmStatic fun readCharacteristic (context: Context, address: String, service: String, characteristic: String) {
    ensure(context)
    manager?.readCharacteristic(address, service, characteristic)
  }

  @JvmStatic fun writeCharacteristic (context: Context, address: String, service: String, characteristic: String, value: ByteArray) {
    ensure(context)
    manager?.writeCharacteristic(address, service, characteristic, value)
  }

  @JvmStatic fun startNotifications (context: Context, address: String, service: String, characteristic: String) {
    ensure(context)
    manager?.startNotifications(address, service, characteristic)
  }

  @JvmStatic fun stopNotifications (context: Context, address: String, service: String, characteristic: String) {
    ensure(context)
    manager?.stopNotifications(address, service, characteristic)
  }
}
