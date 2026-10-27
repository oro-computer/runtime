// vim: set sw=2:
package oro.runtime.app

import java.io.File
import java.lang.Exception
import java.lang.UnsatisfiedLinkError
import java.lang.ref.WeakReference

import android.app.Activity
import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.AssetManager
import android.content.res.AssetFileDescriptor
import android.content.ContentResolver
import android.content.BroadcastReceiver
import android.database.Cursor
import android.graphics.drawable.Icon
import android.net.Uri
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.provider.MediaStore
import android.view.WindowManager
import android.webkit.MimeTypeMap
import android.webkit.WebView

import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

import oro.runtime.debug.console
import oro.runtime.window.WindowManagerActivity

private const val PRIMARY_RUNTIME_SCHEME = "oro"

/** Process-local timings with fixed phase names and no application payloads. */
internal object RuntimeLifecycleEvents {
  private val events = java.util.ArrayDeque<JSONObject>()
  private val started = android.os.SystemClock.elapsedRealtime()

  @Synchronized
  fun record (index: Int, phase: String, duration: Long = 0) {
    require(phase in listOf("loading", "presented", "ready", "failed", "renderer-lost", "reload"))
    if (events.size >= 64) events.removeFirst()
    events.addLast(JSONObject().put("index", index).put("phase", phase)
      .put("elapsedMs", android.os.SystemClock.elapsedRealtime() - started)
      .put("durationMs", maxOf(0, duration)))
  }

  @Synchronized
  fun list (): JSONArray = JSONArray(events.map { JSONObject(it.toString()) })
}

/** Fixed startup phases only; never records application data or wall-clock time. */
internal object RuntimeStartupTimings {
  private val phases = linkedMapOf<String, Long>()

  fun <T> measure (phase: String, operation: () -> T): T {
    require(phase in listOf("native-library", "native-allocation", "native-workflows", "native-extensions", "process-lifecycle"))
    val started = android.os.SystemClock.elapsedRealtimeNanos()
    android.os.Trace.beginSection("oro.startup.$phase")
    try {
      return operation()
    } finally {
      android.os.Trace.endSection()
      synchronized(phases) { phases[phase] = maxOf(0L, android.os.SystemClock.elapsedRealtimeNanos() - started) }
    }
  }

  fun list (): JSONArray = synchronized(phases) {
    JSONArray(phases.map { (phase, duration) -> JSONObject().put("phase", phase).put("durationMs", duration / 1000000.0) })
  }
}

/** Stores opaque notification routing identity, never notification content. */
internal object NotificationReceipts {
  private const val MAX_PENDING = 256
  private const val MAX_RESPONSES = 64
  private const val TTL = 7 * 24 * 60 * 60 * 1000L

  private fun preferences (context: Context) = context.getSharedPreferences(
    "oro.notification.receipts", Context.MODE_PRIVATE
  )

  private fun read (context: Context): JSONObject {
    return try {
      JSONObject(preferences(context).getString("state", "{}") ?: "{}")
    } catch (_: JSONException) {
      JSONObject()
    }
  }

  private fun fresh (timestamp: Long): Boolean {
    val age = System.currentTimeMillis() - timestamp
    return age in 0..TTL
  }

  @Synchronized
  @JvmOverloads
  fun createIntent (context: Context, target: Intent, id: String, action: String, tag: String = "", targetRef: String = ""): Intent? {
    if (id.length !in 1..256 || action.length !in 1..256 || tag.length > 256 || targetRef.length > 256) return null
    val state = read(context)
    val previous = state.optJSONObject("pending") ?: JSONObject()
    val pending = JSONObject()
    val keys = previous.keys().asSequence().toList().takeLast(MAX_PENDING - 1)
    for (key in keys) {
      val record = previous.optJSONObject(key) ?: continue
      if (fresh(record.optLong("createdAt"))) pending.put(key, record)
    }
    val token = java.util.UUID.randomUUID().toString()
    pending.put(token, JSONObject().put("id", id).put("action", action)
      .put("tag", tag).put("targetRef", targetRef).put("createdAt", System.currentTimeMillis()))
    state.put("pending", pending)
    if (!preferences(context).edit().putString("state", state.toString()).commit()) return null
    return target.setData(Uri.Builder().scheme("oro-notification-receipt")
      .authority(context.packageName).appendPath(token).build())
  }

  @Synchronized
  fun consume (context: Context, intent: Intent): JSONObject? {
    val uri = intent.data ?: return null
    if (uri.scheme != "oro-notification-receipt" || uri.host != context.packageName) return null
    val token = uri.pathSegments.singleOrNull() ?: return null
    val state = read(context)
    val pending = state.optJSONObject("pending") ?: return null
    val record = pending.optJSONObject(token) ?: return null
    if (!fresh(record.optLong("createdAt"))) return null
    val id = record.opt("id") as? String ?: return null
    val action = record.opt("action") as? String ?: return null
    if (id.length !in 1..256 || action.length !in 1..256) return null
    pending.remove(token)
    val receipt = JSONObject().put("receiptId", token)
      .put("id", id).put("action", action)
      .put("tag", record.optString("tag")).put("targetRef", record.optString("targetRef"))
      .put("receivedAt", System.currentTimeMillis())
    val responses = responses(state)
    val retained = JSONArray()
    for (index in maxOf(0, responses.length() - MAX_RESPONSES + 1) until responses.length()) {
      retained.put(responses.getJSONObject(index))
    }
    retained.put(receipt)
    state.put("responses", retained)
    if (!preferences(context).edit().putString("state", state.toString()).commit()) return null
    return publicReceipt(receipt)
  }

  private fun publicReceipt(value: JSONObject): JSONObject = JSONObject().put("receiptId", value.getString("receiptId"))
    .put("id", value.getString("id")).put("action", value.getString("action")).put("receivedAt", value.getLong("receivedAt"))

  private fun responses (state: JSONObject): JSONArray {
    val previous = state.optJSONArray("responses") ?: JSONArray()
    val result = JSONArray()
    for (index in maxOf(0, previous.length() - MAX_RESPONSES) until previous.length()) {
      val receipt = previous.optJSONObject(index) ?: continue
      if (fresh(receipt.optLong("receivedAt"))) result.put(receipt)
    }
    return result
  }

  @Synchronized
  fun list (context: Context): String {
    val values = responses(read(context))
    return JSONArray((0 until values.length()).map { publicReceipt(values.getJSONObject(it)) }).toString()
  }

  @Synchronized
  fun listNative (context: Context): String = responses(read(context)).toString()

  @Synchronized
  fun target (context: Context, id: String, tag: String): String {
    val pending = read(context).optJSONObject("pending") ?: return ""
    var newest = -1L
    var target = ""
    for (key in pending.keys()) {
      val record = pending.optJSONObject(key) ?: continue
      val timestamp = record.optLong("createdAt")
      if (record.optString("id") == id && record.optString("tag") == tag && record.optString("action") == "default" &&
          fresh(timestamp) && timestamp > newest) { newest = timestamp; target = record.optString("targetRef") }
    }
    return target
  }

  @Synchronized
  fun acknowledge (context: Context, receiptId: String): Boolean {
    val state = read(context)
    val previous = responses(state)
    val retained = JSONArray()
    for (index in 0 until previous.length()) {
      val receipt = previous.getJSONObject(index)
      if (receipt.optString("receiptId") != receiptId) retained.put(receipt)
    }
    state.put("responses", retained)
    return preferences(context).edit().putString("state", state.toString()).commit()
  }
}

/** Dismissal records a receipt without opening an activity. */
class NotificationDismissReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    val receipt = NotificationReceipts.consume(context, intent) ?: return
    App.getInstance().activity?.emitNotificationResponse(receipt, intent)
  }
}

open class AppPermissionRequest (callback: (Boolean) -> Unit) {
  val id: Int = (0..16384).random().toInt()
  val callback = callback
}

open class AppPlatform (context: Context) {
  private val context = context.applicationContext

  fun isDocumentURI (url: String): Boolean {
    return DocumentsContract.isDocumentUri(
      this.context,
      Uri.parse(url)
    )
  }

  fun getDocumentID (url: String): String {
    return DocumentsContract.getDocumentId(Uri.parse(url))
  }

  fun getContentURI (baseURL: String, documentID: Long): String {
    val uri = ContentUris.withAppendedId(
      Uri.parse(baseURL),
      documentID
    )

    return uri.toString()
  }

  fun getContentMimeType (url: String): String {
    return this.context.contentResolver.getType(Uri.parse(url)) ?: ""
  }

  fun getPathnameEntriesFromContentURI (url: String): Array<String> {
    val context = this.context
    val column = MediaStore.MediaColumns.DATA
    var cursor: Cursor? = null
    val uri = Uri.parse(url)
    val results = mutableListOf<String>()

    try {
      cursor = context.contentResolver.query(
        uri,
        arrayOf(column),
        null,
        null,
        null
      )

      if (cursor != null) {
        cursor.moveToFirst()

        do {
          val result = cursor.getString(cursor.getColumnIndex(column))

          if (result != null && result.length > 0) {
            results += result
          } else {
            val tmp = try {
              cursor.getString(cursor.getColumnIndex(MediaStore.MediaColumns._ID))
            } catch (e: Exception) {
              null
            }

            if (tmp != null) {
              results += cursor.getString(cursor.getColumnIndex(column))
            }
          }
        } while (cursor.moveToNext())
      }
    } catch (_: Exception) {
      // not handled
    } finally {
      if (cursor != null) {
        cursor.close()
      }
    }

    return results.toTypedArray()
  }

  fun getPathnameFromContentURI (
    url: String,
    id: String
  ): String {
    val context = this.context
    val column = MediaStore.MediaColumns.DATA
    var cursor: Cursor? = null
    var result: String? = null
    val uri = Uri.parse(url)

    try {
      cursor = context.contentResolver.query(
        uri,
        arrayOf(column),
        null,
        null,
        null
      )

      if (cursor != null) {
        cursor.moveToFirst()
        do {
          if (id.length > 0 && (result == null || result.length == 0)) {
            result = cursor.getString(cursor.getColumnIndex(column))
            break
          } else {
            var index = cursor.getColumnIndex(MediaStore.MediaColumns._ID)
            var tmp: String? = null

            try {
              tmp = cursor.getString(index)
            } catch (e: Exception) {}

            if (tmp == id) {
              index = cursor.getColumnIndex(column)
              result = cursor.getString(index)
              break
            }
          }
        } while (cursor.moveToNext())
      }
    } catch (err: Exception) {
      return ""
    } finally {
      if (cursor != null) {
        cursor.close()
      }
    }

    return result ?: ""
  }

  fun getExternalContentURIForType (type: String): String {
    val contentURI = (
      if (type == "image") {
        MediaStore.Images.Media.EXTERNAL_CONTENT_URI
      } else if (type == "video") {
        MediaStore.Video.Media.EXTERNAL_CONTENT_URI
      } else if (type == "audio") {
        MediaStore.Audio.Media.EXTERNAL_CONTENT_URI
      } else {
        MediaStore.Files.getContentUri("external")
      }
    )

    if (contentURI == null) {
      return ""
    }

    return contentURI.toString()
  }

  fun getContentResolver (): ContentResolver {
    return this.context.contentResolver
  }

  @JvmOverloads
  @Throws(java.io.IOException::class, SecurityException::class)
  fun openContentResolverFileDescriptor (url: String, mode: String = "r"): AssetFileDescriptor? {
    require(mode in setOf("r", "wt", "wa", "rw", "rwt"))
    val uri = Uri.parse(url)
    require(url.length in 1..4096 && uri.scheme in setOf("content", "android.resource"))
    if (mode == "r") return this.context.contentResolver.openAssetFileDescriptor(uri, mode)
    require(uri.scheme == "content") { "Application resources are read-only" }
    val parcel = this.context.contentResolver.openFileDescriptor(uri, mode) ?: return null
    return AssetFileDescriptor(parcel, 0, AssetFileDescriptor.UNKNOWN_LENGTH)
  }

  fun hasContentResolverAccess (url: String): Boolean {
    val contentResolver = this.context.contentResolver

    try {
      val fd = contentResolver.openAssetFileDescriptor(Uri.parse(url), "r")

      if (fd == null) {
        return false
      }

      fd.close()
    } catch (_: Exception) {
      return false
    }

    return true
  }
}

/**
 * The `AppActivity` represents the root activity for the application.
 * It is an extended `WindowManagerActivity` that considers application
 * and platform details like notifications, incoming intents, platform
 * permissions, and more.
 */
open class AppActivity : WindowManagerActivity() {
  open protected val TAG = "AppActivity"
  open lateinit var notificationChannel: NotificationChannel
  open lateinit var notificationManager: NotificationManager

  open val platform: AppPlatform by lazy { AppPlatform(this) }
  open val permissionRequests = mutableListOf<AppPermissionRequest>()

  open fun getRootDirectory (): String {
    return this.getExternalFilesDir(null)?.absolutePath
      ?: this.applicationContext.filesDir.absolutePath
  }

  fun getAppPlatform (): AppPlatform {
    return this.platform
  }

  fun checkPermission (permission: String): Boolean {
    val status = ContextCompat.checkSelfPermission(this.applicationContext, permission)
    return status == PackageManager.PERMISSION_GRANTED
  }

  fun getLocalNetworkPermissionState (): String {
    if (Build.VERSION.SDK_INT < 37 || this.applicationInfo.targetSdkVersion < 37) {
      return "granted"
    }

    val permission = "android.permission.ACCESS_LOCAL_NETWORK"
    if (this.checkPermission(permission)) {
      return "granted"
    }

    val requested = this.getPreferences(Context.MODE_PRIVATE)
      .getBoolean("oro.localNetworkPermissionRequested", false)
    if (requested && !ActivityCompat.shouldShowRequestPermissionRationale(this, permission)) {
      return "denied"
    }

    return "prompt"
  }

  fun requestLocalNetworkPermission () {
    this.runOnUiThread {
      if (this.getLocalNetworkPermissionState() == "granted") {
        App.getInstance().onPermissionChange("local-network", "granted")
        return@runOnUiThread
      }

      this.getPreferences(Context.MODE_PRIVATE).edit()
        .putBoolean("oro.localNetworkPermissionRequested", true).apply()
      try {
        this.requestPermissions(arrayOf("android.permission.ACCESS_LOCAL_NETWORK")) { granted ->
          val state = if (granted) "granted" else "denied"
          App.getInstance().onPermissionChange("local-network", state)
        }
      } catch (err: RuntimeException) {
        console.error("Unable to request local network permission")
        App.getInstance().onPermissionChange("local-network", "denied")
      }
    }
  }

  fun requestPermissions (permissions: Array<String>) {
    return this.requestPermissions(permissions, fun (_: Boolean) {})
  }

  fun requestPermissions (permissions: Array<String>, callback: (Boolean) -> Unit) {
    val request = AppPermissionRequest(callback)
    this.permissionRequests.add(request)
    try {
      ActivityCompat.requestPermissions(
        this,
        permissions,
        request.id
      )
    } catch (err: RuntimeException) {
      this.permissionRequests.remove(request)
      throw err
    }
  }

  fun openExternal (value: String) {
    val uri = Uri.parse(value)
    val action = Intent.ACTION_VIEW
    val intent = Intent(action, uri)
    this.startActivity(intent)
  }

  fun isNotificationManagerEnabled (): Boolean {
    return NotificationManagerCompat.from(this).areNotificationsEnabled()
  }

  fun getNotificationDeliveryStatus (channel: String): String {
    val identity = if (channel == "default") notificationChannel.id else channel
    require(identity.length in 1..256)
    val native = notificationManager.getNotificationChannel(identity)
    val enabled = App.getInstance().hasRuntimePermission("notifications") && isNotificationManagerEnabled()
    val blocked = native?.importance == NotificationManager.IMPORTANCE_NONE
    return JSONObject().put("enabled", enabled).put("channelId", identity)
      .put("exists", native != null).put("blocked", blocked)
      .put("importance", native?.importance ?: JSONObject.NULL)
      .put("sound", native?.sound != null).put("vibration", native?.shouldVibrate() ?: false)
      .put("canSubmit", enabled && native != null && !blocked)
      .put("deliveryBoundary", "submission-only").put("pushAvailable", NativePush.available(this)).toString()
  }

  fun openNotificationChannelSettings (channel: String): Boolean {
    val identity = if (channel == "default") notificationChannel.id else channel
    if (identity.length !in 1..256) return false
    return try {
      startActivity(Intent(android.provider.Settings.ACTION_CHANNEL_NOTIFICATION_SETTINGS)
        .putExtra(android.provider.Settings.EXTRA_APP_PACKAGE, packageName)
        .putExtra(android.provider.Settings.EXTRA_CHANNEL_ID, identity))
      true
    } catch (_: Exception) {
      false
    }
  }

  fun emitNotificationResponse (receipt: JSONObject, intent: Intent) {
    val payload = JSONObject(receipt.toString())
    intent.getStringExtra("data")?.let { payload.put("data", it) }
    for (fragment in this.windowFragmentManager.fragments) {
      val bridge = fragment.window?.bridge ?: continue
      bridge.emit("notificationresponse", payload.toString())
    }
  }

  fun showNotification (
    id: String,
    title: String,
    body: String,
    tag: String,
    channel: String,
    category: String,
    silent: Boolean,
    iconURL: String,
    imageURL: String,
    vibratePattern: String,
    actionsJSON: String,
    dataJSON: String,
    requireInteraction: Boolean,
    renotify: Boolean
  ): Boolean {
    return try {
      NativeNotifications.submit(this, JSONObject().put("id", id).put("title", title).put("body", body)
        .put("tag", tag).put("channel", channel).put("category", category).put("silent", silent)
        .put("icon", iconURL).put("image", imageURL).put("vibratePattern", vibratePattern)
        .put("actions", if (actionsJSON.isEmpty()) JSONArray() else JSONArray(actionsJSON))
        .put("dataJSON", dataJSON).put("requireInteraction", requireInteraction).put("renotify", renotify))
      true
    } catch (_: Exception) { false }
  }

  fun closeNotification (id: String, tag: String): Boolean {
    return NativeNotifications.cancel(this, id, tag)
  }

  override fun onCreate (savedInstanceState: Bundle?) {
    if (App.getInstance().getUserConfigValue("window_content_protection") == "true") {
      this.window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
    }
    this.supportActionBar?.hide()
    this.getWindow()?.statusBarColor = android.graphics.Color.TRANSPARENT
    oro.runtime.window.SystemAppearance.applySaved(this)

    super.onCreate(savedInstanceState)

    val filesDirPath = this.applicationContext.filesDir.absolutePath
    val externalFilesDir = this.getExternalFilesDir(null)
    val scopedExternalFilesDirectory = externalFilesDir?.absolutePath ?: filesDirPath
    val scopedExternalCacheDirectory = this.externalCacheDir?.absolutePath
      ?: this.applicationContext.cacheDir.absolutePath
    val externalStorageDirectory = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      scopedExternalFilesDirectory
    } else {
      @Suppress("DEPRECATION")
      Environment.getExternalStorageDirectory()?.absolutePath ?: scopedExternalFilesDirectory
    }
    val externalMediaDirectory = this.externalMediaDirs?.firstOrNull()?.absolutePath
      ?: File(scopedExternalFilesDirectory, "media").absolutePath
    // External storage uses FUSE and cannot preserve symbolic links. Temporary
    // files belong in the app's private cache for file and archive operations.
    val tmpDirectory = File(this.applicationContext.cacheDir, "__BUNDLE_IDENTIFIER__").absolutePath
    File(externalMediaDirectory).mkdirs()
    File(tmpDirectory).mkdirs()
    val rootDirectory = this.getRootDirectory()
    val assetManager = this.applicationContext.resources.assets
    val app = App.getInstance()

    this.notificationChannel = NotificationChannel(
      "__BUNDLE_IDENTIFIER__",
      "__BUNDLE_IDENTIFIER__ Notifications",
      NotificationManager.IMPORTANCE_DEFAULT
    )

    this.notificationManager = this.getSystemService(NOTIFICATION_SERVICE) as NotificationManager

    app.apply {
      setMimeTypeMap(MimeTypeMap.getSingleton())
      setAssetManager(assetManager)

      // directories
      setRootDirectory(rootDirectory)
      setExternalCacheDirectory(scopedExternalCacheDirectory)
      setExternalFilesDirectory(scopedExternalFilesDirectory)
      setExternalStorageDirectory(externalStorageDirectory)

      // build info
      setBuildInformation(
        Build.BRAND,
        Build.DEVICE,
        Build.FINGERPRINT,
        Build.HARDWARE,
        Build.MODEL,
        Build.MANUFACTURER,
        Build.PRODUCT,
        Build.VERSION.SDK_INT
      )

      setWellKnownDirectories(
        // 'Downloads/'
        this.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)?.absolutePath ?: "",
        // 'Documents/'
        this.getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS)?.absolutePath ?: "",
        // 'Pictures/'
        this.getExternalFilesDir(Environment.DIRECTORY_PICTURES)?.absolutePath ?: "",
        // 'Desktop/'
        scopedExternalFilesDirectory,
        // 'Videos/'
        this.getExternalFilesDir(Environment.DIRECTORY_DCIM)?.absolutePath ?: "",
        // "configuration directory"
        scopedExternalFilesDirectory,
        // "media directory"
        externalMediaDirectory,
        // 'Music/'
        this.getExternalFilesDir(Environment.DIRECTORY_MUSIC)?.absolutePath ?: "",
        // '~/'
        externalStorageDirectory,
        // "data directory"
        scopedExternalFilesDirectory,
        // "logs directory"
        scopedExternalFilesDirectory,
        // 'tmp/' (likely the cache directory will be used instead of this value)
        tmpDirectory
      )
    }

    app.activity = this
    app.onCreateAppActivity(this)

    if (app.hasRuntimePermission("notifications")) {
      this.notificationManager.createNotificationChannel(this.notificationChannel)
    }

    if (savedInstanceState == null) {
      WebView.setWebContentsDebuggingEnabled(app.isDebugEnabled())

      this.applicationContext
        .getSharedPreferences("WebSettings", Activity.MODE_PRIVATE)
        .edit()
        .apply {
          putString("scheme", PRIMARY_RUNTIME_SCHEME)
          putString("hostname", "__BUNDLE_IDENTIFIER__")
          apply()
        }
    }
  }

  override fun onStart () {
    super.onStart()
    if (this.intent?.action != null) {
      this.onNewIntent(this.intent)
    }
  }

  override fun onResume () {
    super.onResume()
  }

  override fun onPause () {
    super.onPause()
  }

  override fun onStop () {
    PlatformOperations.cancelActivity(this)
    super.onStop()
  }

  override fun onDestroy () {
    super.onDestroy()
    if (App.getInstance().activity === this) {
      App.getInstance().activity = null
    }
    App.getInstance().onDestroyAppActivity(this)
  }

  @Deprecated("Use registered Activity Result launchers for new extensions")
  override fun onActivityResult (requestCode: Int, resultCode: Int, data: Intent?) {
    super.onActivityResult(requestCode, resultCode, data)
    try { NativePush.onActivityResult(this, requestCode, resultCode, data)
    } catch (_: Exception) { console.error("Push distributor selection failed") }
    NativeExtensions.dispatch { it.onActivityResult(this, requestCode, resultCode, data) }
  }

  override fun onNewIntent (intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    try { RetainedComponents.activity(applicationContext, intent)
    } catch (_: Exception) { console.error("Historical routing entry was not admitted") }
    LauncherDestinations.accept(this, intent)
    NativeExtensions.dispatch { it.onNewIntent(this, intent) }
    val action = intent.action
    val data = intent.data

    when (action) {
      "android.intent.action.MAIN",
      "android.intent.action.VIEW" -> {
        val scheme = data?.scheme ?: return
        val applicationProtocol = App.getInstance().getUserConfigValue("meta_application_protocol")
        if (
          applicationProtocol.length > 0 &&
          scheme == applicationProtocol
        ) {
          for (fragment in this.windowFragmentManager.fragments) {
            val window = fragment.window ?: continue
            window.handleApplicationURL(data.toString())
          }
        }
      }

      else -> {
        if (action != null && action.startsWith("notification.response.")) {
          val receipt = NotificationReceipts.consume(this, intent) ?: return
          this.emitNotificationResponse(receipt, intent)
        }
      }
    }
  }

  override fun onRequestPermissionsResult (
    requestCode: Int,
    permissions: Array<String>,
    grantResults: IntArray
  ) {
    var i = 0
    val seen = mutableSetOf<String>()
    for (permission in permissions) {
      val granted = (
        grantResults.getOrNull(i++) == PackageManager.PERMISSION_GRANTED
      )

      var name = ""
      when (permission) {
        "android.permission.ACCESS_COARSE_LOCATION",
        "android.permission.ACCESS_FINE_LOCATION" -> {
          name = "geolocation"
        }

        "android.permission.BLUETOOTH_SCAN",
        "android.permission.BLUETOOTH_CONNECT" -> {
          name = "bluetooth"
        }

        "android.permission.POST_NOTIFICATIONS" -> {
          name = "notifications"
        }

        "android.permission.CAMERA" -> {
          name = "camera"
        }

        "android.permission.RECORD_AUDIO" -> {
          name = "microphone"
        }
      }

      if (seen.contains(name)) {
        continue
      }

      if (name.length == 0) {
        continue
      }

      seen.add(name)

      val state = if (granted) "granted" else "denied"
      App.getInstance().onPermissionChange(name, state)
    }

    for (request in this.permissionRequests) {
      if (request.id == requestCode) {
        this.permissionRequests.remove(request)
        request.callback(grantResults.isNotEmpty() && grantResults.all { r ->
          r == PackageManager.PERMISSION_GRANTED
        })
        return
      }
    }

    super.onRequestPermissionsResult(requestCode, permissions, grantResults)
  }
}

open class AppLifecycleObserver : DefaultLifecycleObserver {
  override fun onDestroy (owner: LifecycleOwner) {
  }

  override fun onStart (owner: LifecycleOwner) {
    App.getInstance().onStart()
  }

  override fun onStop (owner: LifecycleOwner) {
    App.getInstance().onStop()
  }

  override fun onPause (owner: LifecycleOwner) {
    App.getInstance().onPause()
  }

  override fun onResume (owner: LifecycleOwner) {
    App.getInstance().onResume()
  }
}

open class App : Application() {
  private val platform by lazy { AppPlatform(this) }

  fun getAppPlatform (): AppPlatform = platform

  @Volatile private var packageCompatible: Boolean? = null
  internal val packagedAssetsVerified: Boolean get() = packageCompatible == true

  private val applicationInputSha256: String? by lazy {
    try {
      val expected = JSONObject(getNativeBuildIdentity()).getString("assetManifestSha256")
      require(Regex("[a-f0-9]{64}").matches(expected))
      val bytes = readBoundedAsset("oro-build-evidence.json", 16777216)
      val digest = java.security.MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { (it.toInt() and 255).toString(16).padStart(2, '0') }
      require(digest == expected)
      val evidence = JSONObject(String(bytes, Charsets.UTF_8)).getJSONObject("inputs")
        .optJSONObject("applicationInputs")
      evidence?.optString("sha256")?.takeIf { Regex("[a-f0-9]{64}").matches(it) }
    } catch (_: Exception) { null
    } catch (_: UnsatisfiedLinkError) { null }
  }

  private fun readBoundedAsset (path: String, limit: Int): ByteArray {
    return assets.open(path).use { stream ->
      val output = java.io.ByteArrayOutputStream()
      val buffer = ByteArray(65536)
      while (true) {
        val count = stream.read(buffer)
        if (count < 0) break
        require(output.size() + count <= limit)
        output.write(buffer, 0, count)
      }
      output.toByteArray()
    }
  }

  /** Runs on the navigation worker before any application document is loaded. */
  @Synchronized
  fun verifyPackagedAssets (): Boolean {
    packageCompatible?.let { return it }
    var stage = "native-identity"
    val compatible = try {
      val identity = JSONObject(getNativeBuildIdentity())
      val expected = identity.getString("assetManifestSha256")
      require(Regex("[a-f0-9]{64}").matches(expected))
      stage = "manifest-digest"
      val manifestBytes = readBoundedAsset("oro-build-evidence.json", 16777216)
      fun hex (bytes: ByteArray): String {
        val digits = "0123456789abcdef"
        val encoded = CharArray(bytes.size * 2)
        for (index in bytes.indices) {
          val value = bytes[index].toInt() and 255
          encoded[index * 2] = digits[value ushr 4]
          encoded[index * 2 + 1] = digits[value and 15]
        }
        return String(encoded)
      }
      fun digest (bytes: ByteArray) = hex(java.security.MessageDigest.getInstance("SHA-256").digest(bytes))
      require(digest(manifestBytes) == expected)
      stage = "bridge-contract"
      val manifest = JSONObject(String(manifestBytes, Charsets.UTF_8))
      require(manifest.getInt("schema") == 1 && manifest.getInt("androidBridgeProtocol") == 5 &&
        identity.getInt("androidBridgeProtocol") == 5)
      val contract = JSONObject(String(readBoundedAsset("oro/internal/runtime-contract.json", 4096), Charsets.UTF_8))
      require(contract.getInt("androidBridgeProtocol") == 5)
      val entries = manifest.getJSONArray("assets")
      require(entries.length() <= 100000)
      val named = mutableSetOf<String>()
      val buffer = ByteArray(65536)
      stage = "asset-hash"
      for (index in 0 until entries.length()) {
        val entry = entries.getJSONObject(index)
        val path = entry.getString("path")
        require(path.length in 1..4096 && !path.startsWith("/") &&
          path.split('/').none { it == "." || it == ".." || it.isEmpty() } && named.add(path))
        val hash = java.security.MessageDigest.getInstance("SHA-256")
        var size = 0L
        assets.open(path).use { stream ->
          while (true) {
            val count = stream.read(buffer)
            if (count < 0) break
            size += count
            require(size <= entry.getLong("size"))
            hash.update(buffer, 0, count)
          }
        }
        require(size == entry.getLong("size") && hex(hash.digest()) == entry.getString("sha256"))
      }
      // AssetManager merges framework assets into its listing. Inventory only
      // this application's signed APKs, while hashing the resolved asset bytes.
      stage = "asset-inventory"
      val found = mutableSetOf<String>()
      val generatedProfiles = mutableSetOf<String>()
      var entryCount = 0
      val packages = listOf(applicationInfo.sourceDir) + applicationInfo.splitSourceDirs.orEmpty().toList()
      for (path in packages) {
        java.util.zip.ZipFile(path).use { archive ->
          val entries = archive.entries()
          while (entries.hasMoreElements()) {
            require(++entryCount <= 200000)
            val entry = entries.nextElement()
            if (entry.isDirectory || !entry.name.startsWith("assets/")) continue
            val name = entry.name.removePrefix("assets/")
            if (name == "oro-build-evidence.json") continue
            // AGP generates ART profiles after the application inventory is bound.
            if (!named.contains(name) &&
              (name == "dexopt/baseline.prof" || name == "dexopt/baseline.profm")) {
              require(entry.size in 0L..16777216L && generatedProfiles.add(name))
              continue
            }
            require(named.contains(name) && found.add(name))
          }
        }
      }
      require(found == named)
      true
    } catch (_: Exception) {
      console.error("Runtime package verification failed at $stage")
      false
    } catch (_: UnsatisfiedLinkError) {
      console.error("Runtime package verification failed at native-identity")
      false
    }
    packageCompatible = compatible
    return compatible
  }

  fun backgroundOperation (action: String, options: String): String = BackgroundTasks.request(this, action, options)

  fun startPlatformOperation (token: Long, index: Int, feature: String, action: String, options: String, id: String, documentToken: String) =
    PlatformOperations.start(this, token, index, feature, action, options, id, documentToken)

  external fun onPlatformOperationResult (token: Long, result: String)

  fun getSecureStorageStatus (): String = oro.runtime.securestorage.SecureStorageManager.status(this)

  fun getRuntimeCapabilities (): String = JSONObject()
    .put("nativeFullscreen", Build.VERSION.SDK_INT >= 30).put("fullscreenRequiresManagedInsets", true)
    .put("screenProtection", true).put("documentMetadata", true).put("documentTreeQueries", true)
    .put("documentQueryWorkers", 2).put("documentQueryDeadlineMs", 5000)
    .put("nativeBackgroundCleanup", BackgroundTasks.available(this))
    .put("backgroundJavaScript", false).put("push", NativePush.available(this))
    .put("applicationBackHandling", true).put("periodicDeferredReads", DeferredSync.available(this))
    .put("nativeDeferredMaintenance", DeferredSync.available(this))
    .put("nativeForegroundExecution", ForegroundTasks.available(this))
    .put("nativeContinuousExecution", ContinuousTasks.available(this))
    .put("nativeTransactionalRecords", true).put("nativeCoordination", true)
    .put("biometricSecretUnlock", ProtectedAuthentication.status(this).optBoolean("enabled"))
    .put("namedLauncherDestinations", getUserConfigValue("launcher_enabled") == "true")
    .put("nativeDeferredReads", DeferredSync.available(this)).put("deferredWrites", false)
    .put("webAuthentication", getUserConfigValue("webview_webauthn_enabled") == "true" &&
      androidx.webkit.WebViewFeature.isFeatureSupported(androidx.webkit.WebViewFeature.WEB_AUTHENTICATION))
    .put("nativePasswordSelection", PasswordCredentials.available(this))
    .put("nativeNotificationPresentation", true)
    .put("systemTextScaling", true)
    .put("systemGestureExclusion", Build.VERSION.SDK_INT >= 29)
    .put("nativeStartupPresentation", true)
    .put("nativeExtensions", getUserConfigValue("android_extensions").isNotBlank())
    .put("pointerCapture", false)
    .put("historicalProcessExits", Build.VERSION.SDK_INT >= 30).toString()

  private fun getProcessExits (): JSONObject {
    val result = JSONObject().put("available", false).put("entries", JSONArray())
    if (Build.VERSION.SDK_INT < 30) return result
    return try {
      val manager = getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
      val entries = JSONArray()
      for (exit in manager.getHistoricalProcessExitReasons(packageName, 0, 16).take(16)) {
        entries.put(JSONObject().put("buildIdentityKnown", false).put("reason", exit.reason).put("status", exit.status)
          .put("timestamp", exit.timestamp).put("importance", exit.importance)
          .put("pssKiB", exit.pss).put("rssKiB", exit.rss))
      }
      result.put("available", true).put("entries", entries)
    } catch (_: Exception) { result }
  }

  fun getBuildIdentity (): String {
    val identity = JSONObject(getNativeBuildIdentity())
    val provider = androidx.webkit.WebViewCompat.getCurrentWebViewPackage(this)
    return identity.put("sdk", Build.VERSION.SDK_INT)
      .put("applicationInputSha256", applicationInputSha256 ?: JSONObject.NULL)
      .put("targetSdk", applicationInfo.targetSdkVersion)
      .put("webViewVersion", provider?.versionName ?: JSONObject.NULL)
      .put("assetsVerified", packageCompatible == true)
      .put("lifecycleEvents", RuntimeLifecycleEvents.list())
      .put("startupTimings", RuntimeStartupTimings.list())
      .put("processExits", getProcessExits()).toString()
  }

  fun getNotificationResponses (): String = NotificationReceipts.list(this)

  fun acknowledgeNotificationResponse (receiptId: String): Boolean =
    NotificationReceipts.acknowledge(this, receiptId)

  external fun getNativeBuildIdentity (): String

  fun startDocumentQuery (token: Long, action: String, uri: String, options: String) =
    ScopedDocuments.start(this, token, action, uri, options)

  external fun onDocumentQueryResult (token: Long, result: String)

  @Synchronized
  @Throws(Exception::class)
  fun documentAccess (action: String, value: String, flags: Int): String {
    val resolver = contentResolver
    if (action == "list") {
      val grants = JSONArray()
      for (grant in resolver.persistedUriPermissions.take(128)) {
        grants.put(JSONObject().put("uri", grant.uri.toString()).put("read", grant.isReadPermission)
          .put("write", grant.isWritePermission).put("persistedAt", grant.persistedTime))
      }
      return grants.toString()
    }
    require(value.length in 1..4096 && value.none { it.code < 32 || it.code == 127 } && flags in 1..3)
    val uri = Uri.parse(value)
    require(uri.scheme == "content" && !uri.authority.isNullOrBlank())
    when (action) {
      "persist" -> {
        val grants = resolver.persistedUriPermissions
        require(grants.any { it.uri == uri } || grants.size < 128) { "Persisted grant limit reached" }
        resolver.takePersistableUriPermission(uri, flags)
      }
      "release" -> resolver.releasePersistableUriPermission(uri, flags)
      else -> throw IllegalArgumentException("Unknown document access operation")
    }
    return "null"
  }

  val pointer = RuntimeStartupTimings.measure("native-allocation") { alloc() }
  var activity: AppActivity? = null
  private val networkHandler = Handler(Looper.getMainLooper())
  private var networkCallback: ConnectivityManager.NetworkCallback? = null
  @Volatile private var networkGeneration = 0L
  private var networkUpdate: Runnable? = null
  private var lastNetworkStatus = ""

  fun getNetworkStatus (): String {
    val status = JSONObject().put("available", true).put("connected", false)
      .put("validated", false).put("captivePortal", false).put("metered", false)
      .put("transports", JSONArray()).put("name", "offline")
    try {
      val manager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
      val network = manager.activeNetwork
      val capabilities = network?.let { manager.getNetworkCapabilities(it) }
      val validated = capabilities?.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) == true
      val transports = JSONArray()
      val transportKinds = mapOf(
        NetworkCapabilities.TRANSPORT_WIFI to "wifi",
        NetworkCapabilities.TRANSPORT_CELLULAR to "cellular",
        NetworkCapabilities.TRANSPORT_ETHERNET to "ethernet",
        NetworkCapabilities.TRANSPORT_BLUETOOTH to "bluetooth",
        NetworkCapabilities.TRANSPORT_VPN to "vpn"
      )
      for ((kind, name) in transportKinds) {
        if (capabilities?.hasTransport(kind) == true) transports.put(name)
      }
      status.put("connected", network != null).put("validated", validated)
        .put("captivePortal", capabilities?.hasCapability(NetworkCapabilities.NET_CAPABILITY_CAPTIVE_PORTAL) == true)
        .put("metered", network != null && manager.isActiveNetworkMetered)
        .put("transports", transports).put("name", if (validated) "online" else "offline")
    } catch (_: SecurityException) {
      status.put("available", false)
    }
    return status.toString()
  }

  @Synchronized
  fun startNetworkStatusMonitor (): Boolean {
    if (this.networkCallback != null) return true
    val generation = ++this.networkGeneration
    val manager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    val callback = object : ConnectivityManager.NetworkCallback() {
      private fun changed () {
        networkHandler.post {
          if (networkGeneration != generation) return@post
          networkUpdate?.let { networkHandler.removeCallbacks(it) }
          networkUpdate = Runnable {
            if (networkGeneration == generation) {
              val status = getNetworkStatus()
              if (status != lastNetworkStatus) {
                lastNetworkStatus = status
                onNetworkStatus(status)
              }
            }
          }.also { networkHandler.post(it) }
        }
      }
      override fun onAvailable (network: Network) = changed()
      override fun onLost (network: Network) = changed()
      override fun onCapabilitiesChanged (network: Network, capabilities: NetworkCapabilities) = changed()
      override fun onBlockedStatusChanged (network: Network, blocked: Boolean) = changed()
    }
    this.networkCallback = callback
    try {
      manager.registerDefaultNetworkCallback(callback)
      this.lastNetworkStatus = getNetworkStatus()
      onNetworkStatus(this.lastNetworkStatus)
    } catch (_: SecurityException) {
      this.networkCallback = null
      // Missing capability is observable without preventing application startup.
      onNetworkStatus(getNetworkStatus())
    }
    return true
  }

  @Synchronized
  fun stopNetworkStatusMonitor (): Boolean {
    ++this.networkGeneration
    this.networkUpdate?.let { this.networkHandler.removeCallbacks(it) }
    this.networkUpdate = null
    val callback = this.networkCallback ?: return true
    this.networkCallback = null
    val manager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    manager.unregisterNetworkCallback(callback)
    this.lastNetworkStatus = ""
    return true
  }

  external fun onNetworkStatus (status: String): Unit

  external fun getDevelopmentOrigin (): String

  protected val lifecycleListener: AppLifecycleObserver by lazy {
    AppLifecycleObserver()
  }

  companion object {
    private val runtimeLibraryCandidates = arrayOf("oro-runtime")
    @Volatile private var runtimeLibraryLoaded = false
    private val runtimeLibraryLock = Any()
    lateinit var appInstance: App

    fun getInstance () : App {
      return appInstance
    }

    fun loadOroRuntime () {
      loadNativeRuntime()
    }

    private fun loadNativeRuntime () {
      if (runtimeLibraryLoaded) {
        return
      }

      synchronized(runtimeLibraryLock) {
        if (runtimeLibraryLoaded) {
          return
        }

        RuntimeStartupTimings.measure("native-library") { System.loadLibrary(runtimeLibraryCandidates.first()) }
        runtimeLibraryLoaded = true
      }
    }
  }

  init {
    App.Companion.appInstance = this
  }

  override fun onCreate () {
    super.onCreate()
    RuntimeStartupTimings.measure("native-workflows") { NativeWorkflows.initialize(this) }
    RuntimeStartupTimings.measure("native-extensions") { NativeExtensions.initialize(this) }
    NativePush.start(this)

    val lifecycleListener = this.lifecycleListener

    RuntimeStartupTimings.measure("process-lifecycle") {
      ProcessLifecycleOwner.get().lifecycle.addObserver(lifecycleListener)
    }
  }

  @Throws(Exception::class)
  protected external fun alloc (): Long

  @Throws(Exception::class)
  external fun setRootDirectory (rootDirectory: String): Boolean

  @Throws(Exception::class)
  external fun setExternalCacheDirectory (cacheDirectory: String): Boolean

  @Throws(Exception::class)
  external fun setExternalStorageDirectory (directory: String): Boolean

  @Throws(Exception::class)
  external fun setExternalFilesDirectory (directory: String): Boolean

  @Throws(Exception::class)
  external fun setAssetManager (assetManager: AssetManager): Boolean

  @Throws(Exception::class)
  external fun setMimeTypeMap (mimeTypeMap: MimeTypeMap): Boolean

  @Throws(Exception::class)
  external fun setBuildInformation (
    brand: String,
    device: String,
    fingerprint: String,
    hardware: String,
    model: String,
    manufacturer: String,
    product: String,
    sdk: Int
  ): Unit

  @Throws(Exception::class)
  external fun setWellKnownDirectories (
    downloads: String = "",
    documents: String = "",
    pictures: String = "",
    desktop: String = "",
    videos: String = "",
    config: String = "",
    media: String = "",
    music: String = "",
    home: String = "",
    data: String = "",
    log: String = "",
    tmp: String = ""
  ): Unit

  /** Shared foreground/native trust survives cold native service starts. */
  fun getWebviewTlsPolicy (): String = NativeTlsPolicy.value(this)

  fun setWebviewTlsPolicy (value: String): Boolean = try {
    NativeTlsPolicy.replace(this, value)
    true
  } catch (_: Exception) { false }

  @Throws(Exception::class)
  external fun getUserConfigValue (key: String): String

  @Throws(Exception::class)
  external fun hasRuntimePermission (permission: String): Boolean

  @Throws(Exception::class)
  external fun isDebugEnabled (): Boolean

  @Throws(Exception::class)
  external fun onCreateAppActivity (activity: AppActivity): Unit

  @Throws(Exception::class)
  external fun onDestroyAppActivity (activity: AppActivity): Unit

  @Throws(Exception::class)
  external fun onStart (): Unit

  @Throws(Exception::class)
  external fun onStop (): Unit

  @Throws(Exception::class)
  external fun onResume (): Unit

  @Throws(Exception::class)
  external fun onPause (): Unit

  @Throws(Exception::class)
  external fun onDestroy (): Unit

  @Throws(Exception::class)
  external fun onPermissionChange (name: String, state: String): Unit

  @Throws(Exception::class)
  external fun onOtpMessage (requestId: Long, message: String): Unit

  @Throws(Exception::class)
  external fun onOtpCode (requestId: Long, code: String): Unit

  @Throws(Exception::class)
  external fun onOtpTimeout (requestId: Long): Unit

  @Throws(Exception::class)
  external fun onOtpError (requestId: Long, type: String, message: String): Unit
}
