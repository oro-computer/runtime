// vim: set sw=2:
package __BUNDLE_IDENTIFIER__

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.content.ContextCompat
import kotlin.jvm.Volatile
import oro.runtime.usb.USBPlatform

/**
 * Foreground-capable service that owns USB device discovery, permission
 * negotiation, and connection lifecycle. Public methods remain stubs so the
 * native runtime can wire in higher level transport logic later.
 */
open class UsbService : Service() {
  private val binder = UsbBinder()
  private lateinit var usbManager: UsbManager
  private lateinit var permissionIntent: PendingIntent

  private val permissionReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
      val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
      val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
      if (device == null) {
        Log.w(TAG, "Received USB permission callback without device payload")
        return
      }
      if (granted) {
        onPermissionGranted(device)
      } else {
        onPermissionDenied(device)
      }
    }
  }

  private val attachDetachReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
      when (intent.action) {
        UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
          intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)?.let {
            onDeviceAttached(it)
          }
        }
        UsbManager.ACTION_USB_DEVICE_DETACHED -> {
          intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)?.let {
            onDeviceDetached(it)
          }
        }
      }
    }
  }

  override fun onCreate() {
    super.onCreate()
    instance = this
    usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
    val permissionFlags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
    } else {
      @Suppress("DEPRECATION")
      PendingIntent.FLAG_UPDATE_CURRENT
    }
    permissionIntent = PendingIntent.getBroadcast(
      this,
      0,
      Intent(ACTION_USB_PERMISSION).setPackage(packageName),
      permissionFlags
    )
    registerUsbReceivers()
    enumerateConnectedDevices()
    started = startForegroundIfNeeded()
    if (!started) {
      USBPlatform.onServiceUnavailable()
      return
    }

    USBPlatform.onServiceReady(this)
  }

  override fun onDestroy() {
    started = false
    unregisterReceiver(permissionReceiver)
    unregisterReceiver(attachDetachReceiver)
    stopForeground(STOP_FOREGROUND_REMOVE)
    USBPlatform.onServiceUnavailable()
    instance = null
    super.onDestroy()
  }

  override fun onBind(intent: Intent?): IBinder = binder

  fun requestPermission(device: UsbDevice) {
    if (usbManager.hasPermission(device)) {
      onPermissionGranted(device)
      return
    }
    Log.i(TAG, "Requesting permission for device: ${device.deviceName}")
    usbManager.requestPermission(device, permissionIntent)
  }

  protected open fun onPermissionGranted(device: UsbDevice) {
    Log.i(TAG, "Permission granted for device: ${device.deviceName}")
    started = startForegroundIfNeeded()
    USBPlatform.notifyPermissionResult(device.deviceName ?: "", true)
    // TODO: open UsbDeviceConnection and hand off to USB transport layer.
  }

  protected open fun onPermissionDenied(device: UsbDevice) {
    Log.w(TAG, "Permission denied for device: ${device.deviceName}")
    USBPlatform.notifyPermissionResult(device.deviceName ?: "", false)
    // TODO: surface denial to runtime so the UI can inform the user.
  }

  protected open fun onDeviceAttached(device: UsbDevice) {
    Log.i(TAG, "Device attached: ${device.deviceName}")
    requestPermission(device)
  }

  protected open fun onDeviceDetached(device: UsbDevice) {
    Log.i(TAG, "Device detached: ${device.deviceName}")
    // TODO: close connections and notify runtime.
  }

  private fun enumerateConnectedDevices() {
    usbManager.deviceList.values.forEach { device ->
      Log.i(TAG, "Enumerating previously attached device: ${device.deviceName}")
      requestPermission(device)
    }
  }

  private fun requestPermissionById(deviceId: String): Boolean {
    val device = usbManager.deviceList[deviceId]
      ?: usbManager.deviceList.values.firstOrNull { it.deviceName == deviceId }
    if (device == null) {
      Log.w(TAG, "Unable to resolve USB device for id: $deviceId")
      return false
    }
    requestPermission(device)
    return true
  }

  private fun startForegroundIfNeeded(): Boolean {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      val hasPermission = ContextCompat.checkSelfPermission(
        this,
        Manifest.permission.POST_NOTIFICATIONS
      ) == PackageManager.PERMISSION_GRANTED
      if (!hasPermission) {
        Log.w(TAG, "Notification permission missing; USB service will stop to avoid SecurityException")
        stopSelf()
        return false
      }
    }

    val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      val channel = NotificationChannel(
        USB_NOTIFICATION_CHANNEL_ID,
        "USB Connections",
        NotificationManager.IMPORTANCE_LOW
      )
      manager.createNotificationChannel(channel)
    }
    if (!canPostNotifications(manager)) {
      Log.w(TAG, "Notifications disabled; attempting foreground start with silent notification")
    }
    val notification = buildServiceNotification()
    try {
      startForeground(USB_NOTIFICATION_ID, notification)
    } catch (err: SecurityException) {
      Log.e(TAG, "Unable to start foreground service; USB session may be killed", err)
      stopSelf()
      return false
    }
    return true
  }

  private fun buildServiceNotification(): Notification {
    val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      Notification.Builder(this, USB_NOTIFICATION_CHANNEL_ID)
    } else {
      @Suppress("DEPRECATION")
      Notification.Builder(this)
    }
    builder
      .setContentTitle("USB service active")
      .setContentText("Listening for USB devices")
      .setSmallIcon(android.R.drawable.stat_sys_data_usb)
      .setOngoing(true)
    return builder.build()
  }

  private fun canPostNotifications(manager: NotificationManager): Boolean {
    return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      manager.areNotificationsEnabled()
    } else {
      true
    }
  }

  private fun registerUsbReceivers() {
    val permissionFilter = IntentFilter(ACTION_USB_PERMISSION)
    val attachFilter = IntentFilter().apply {
      addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
      addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      registerReceiver(permissionReceiver, permissionFilter, Context.RECEIVER_NOT_EXPORTED)
      registerReceiver(attachDetachReceiver, attachFilter, Context.RECEIVER_NOT_EXPORTED)
    } else {
      @Suppress("DEPRECATION")
      registerReceiver(permissionReceiver, permissionFilter)
      @Suppress("DEPRECATION")
      registerReceiver(attachDetachReceiver, attachFilter)
    }
  }

  inner class UsbBinder : Binder() {
    fun getService(): UsbService = this@UsbService
  }

  companion object {
    private const val TAG = "UsbService"
    private const val ACTION_USB_PERMISSION = "oro.runtime.action.USB_PERMISSION"
    private const val USB_NOTIFICATION_CHANNEL_ID = "oro.runtime.usb"
    private const val USB_NOTIFICATION_ID = 0x534f // 'SO' marker

    private val startLock = Any()
    @Volatile
    private var started = false
    @Volatile
    private var instance: UsbService? = null

    fun ensureStarted(context: Context) {
      if (started) {
        return
      }
      synchronized(startLock) {
        if (started) {
          return
        }
        val appContext = context.applicationContext
        val intent = Intent(appContext, UsbService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
          appContext.startForegroundService(intent)
        } else {
          @Suppress("DEPRECATION")
          appContext.startService(intent)
        }
      }
    }

    fun instanceOrNull(): UsbService? = instance

    fun requestPermissionForDevice(deviceId: String): Boolean {
      val service = instance
      if (service == null) {
        Log.w(TAG, "UsbService not running; cannot request permission for $deviceId")
        return false
      }
      return service.requestPermissionById(deviceId)
    }
  }
}
