package dev.oro.push

import android.app.BroadcastOptions
import android.app.PendingIntent
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import org.json.JSONObject
import java.net.URL
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManager
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager

object Wire {
  @Volatile private var sockets: SSLSocketFactory? = null
  private val main = Handler(Looper.getMainLooper())
  private class Binding {
    var connected = false
    val messages = linkedMapOf<String, () -> Unit>()
    lateinit var connection: ServiceConnection
    lateinit var release: Runnable
  }
  private val bindings = mutableMapOf<String, Binding>()

  @Suppress("DEPRECATION")
  fun signature (context: Context, application: String): String {
    val info = context.packageManager.getPackageInfo(application, if (Build.VERSION.SDK_INT >= 28)
      PackageManager.GET_SIGNING_CERTIFICATES else PackageManager.GET_SIGNATURES)
    val signatures = if (Build.VERSION.SDK_INT >= 28) info.signingInfo?.apkContentsSigners else info.signatures
    require(signatures?.size == 1) { "Single signer required" }
    return java.util.Base64.getEncoder().encodeToString(MessageDigest.getInstance("SHA-256").digest(signatures[0].toByteArray()))
  }

  fun owner (context: Context, record: JSONObject): Boolean = try {
    val application = record.getString("package")
    val info = context.packageManager.getApplicationInfo(application, 0)
    info.flags and android.content.pm.ApplicationInfo.FLAG_STOPPED == 0 && info.uid == record.getInt("uid") &&
      signature(context, application) == record.getString("signature")
  } catch (_: Exception) { false }

  fun send (context: Context, record: JSONObject, action: String, id: String? = null, payload: ByteArray? = null, reason: String? = null) {
    if (!owner(context, record)) return
    if (action in listOf("MESSAGE", "NEW_ENDPOINT")) synchronized(Store) {
      if (Store.registration(Store.read(context), record.getString("token"))?.optString("endpoint") != record.optString("endpoint")) return
    }
    val intent = Intent(Protocol.CONNECTOR + action).setPackage(record.getString("package"))
      .putExtra("token", record.getString("token"))
    val identity = id ?: if (action == "NEW_ENDPOINT") record.optString("endpointId").ifEmpty { null } else null
    if (identity != null) intent.putExtra("id", identity)
    if (payload != null) intent.putExtra("bytesMessage", payload)
    if (reason != null) intent.putExtra("reason", reason)
    if (action == "NEW_ENDPOINT") intent.putExtra("endpoint", record.getString("endpoint"))
    if (Build.VERSION.SDK_INT >= 34) context.sendBroadcast(intent, null,
      BroadcastOptions.makeBasic().setShareIdentityEnabled(true).toBundle())
    else context.sendBroadcast(intent.putExtra("pi", PendingIntent.getBroadcast(context, 0,
      Intent().setPackage("org.unifiedpush.dummy_app"), PendingIntent.FLAG_IMMUTABLE)))
  }

  /** Bind before broadcasting and retain foreground importance for five seconds. */
  fun deliver (context: Context, record: JSONObject, id: String, payload: ByteArray) {
    main.post {
      try {
        if (!owner(context, record)) return@post
        val application = record.getString("package")
        val key = record.getString("token") + ":" + id
        val delivery = { send(context, record, "MESSAGE", id, payload) }
        bindings[application]?.let { existing ->
          if (existing.connected) {
            delivery()
            main.removeCallbacks(existing.release)
            main.postDelayed(existing.release, 5000)
          } else if (existing.messages.size < Protocol.PENDING) existing.messages[key] = delivery
          return@post
        }
        val intent = Intent(Protocol.CONNECTOR + "RAISE_TO_FOREGROUND").setPackage(application)
        val services = context.packageManager.queryIntentServices(intent, 0)
        if (services.size != 1 || !services[0].serviceInfo.exported) return@post
        intent.component = ComponentName(application, services[0].serviceInfo.name)
        val binding = Binding()
        binding.messages[key] = delivery
        binding.release = Runnable {
          if (bindings[application] === binding) {
            bindings.remove(application)
            try { context.unbindService(binding.connection) } catch (_: IllegalArgumentException) {}
          }
        }
        binding.connection = object : ServiceConnection {
          override fun onServiceConnected (name: ComponentName, binder: IBinder) {
            if (bindings[application] !== binding) return
            binding.connected = true
            val messages = binding.messages.values.toList()
            binding.messages.clear()
            messages.forEach { try { it() } catch (_: Exception) {} }
            main.removeCallbacks(binding.release)
            main.postDelayed(binding.release, 5000)
          }
          override fun onServiceDisconnected (name: ComponentName) = binding.release.run()
          override fun onNullBinding (name: ComponentName) = binding.release.run()
          override fun onBindingDied (name: ComponentName) = binding.release.run()
        }
        bindings[application] = binding
        main.postDelayed(binding.release, 10000)
        if (!context.bindService(intent, binding.connection, Context.BIND_AUTO_CREATE or Context.BIND_IMPORTANT)) binding.release.run()
      } catch (_: Exception) {
        // Keep ciphertext pending when the receiving app cannot be bound.
      }
    }
  }

  private fun trust (keys: KeyStore?): X509TrustManager = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
    .apply { init(keys) }.trustManagers.filterIsInstance<X509TrustManager>().first()

  @Synchronized
  private fun sockets (context: Context): SSLSocketFactory {
    sockets?.let { return it }
    val system = trust(null)
    val development = if (context.assets.list("")?.contains("development-ca.pem") == true) {
      val keys = KeyStore.getInstance(KeyStore.getDefaultType()).apply { load(null) }
      val certificates = context.assets.open("development-ca.pem").use { CertificateFactory.getInstance("X.509").generateCertificates(it) }
      require(certificates.isNotEmpty()) { "Empty development certificate bundle" }
      certificates.forEachIndexed { index, certificate -> keys.setCertificateEntry("development-$index", certificate) }
      trust(keys)
    } else null
    return SSLContext.getInstance("TLS").apply {
      init(null, arrayOf<TrustManager>(object : X509TrustManager {
        override fun getAcceptedIssuers (): Array<X509Certificate> = system.acceptedIssuers
        override fun checkClientTrusted (chain: Array<X509Certificate>, type: String) = system.checkClientTrusted(chain, type)
        override fun checkServerTrusted (chain: Array<X509Certificate>, type: String) {
          try { system.checkServerTrusted(chain, type) } catch (error: CertificateException) {
            if (development == null) throw error
            development.checkServerTrusted(chain, type)
          }
        }
      }), null)
    }.socketFactory.also { sockets = it }
  }

  fun connect (context: Context, url: String): HttpsURLConnection = (URL(url).openConnection() as HttpsURLConnection).apply {
    sslSocketFactory = sockets(context)
    instanceFollowRedirects = false
    connectTimeout = 5000
    readTimeout = 45000
  }
}
