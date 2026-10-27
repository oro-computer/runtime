package dev.oro.push

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.os.Build
import android.os.IBinder
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.net.URLEncoder
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import javax.net.ssl.HttpsURLConnection

class ConnectionService : Service() {
  companion object {
    @Volatile private var instance: ConnectionService? = null
    fun start (context: Context): Boolean = try {
      if (instance == null) context.startForegroundService(Intent(context, ConnectionService::class.java))
      true
    } catch (_: RuntimeException) {
      Store.preferences(context).edit().putString("status", "Open Oro Push Dev to resume the connection").apply()
      false
    }
    fun refresh () { instance?.reconnect() }
    fun subscribed (topic: String): Boolean = instance?.topics?.contains(topic) == true
  }

  @Volatile private var running = true
  @Volatile private var topics = emptySet<String>()
  @Volatile private var connection: HttpsURLConnection? = null
  @Volatile private var revision = 0L
  private val monitor = Object()
  private val control = Executors.newSingleThreadScheduledExecutor()
  private val network = object : ConnectivityManager.NetworkCallback() {
    override fun onAvailable (network: Network) = reconnect()
    override fun onLost (network: Network) = reconnect()
  }

  override fun onCreate () {
    super.onCreate()
    instance = this
    val manager = getSystemService(NotificationManager::class.java)
    manager.createNotificationChannel(NotificationChannel("oro-push-dev", "Local push connection", NotificationManager.IMPORTANCE_LOW))
    val notification = android.app.Notification.Builder(this, "oro-push-dev")
      .setSmallIcon(android.R.drawable.ic_dialog_info).setContentTitle("Oro Push Dev")
      .setContentText("Local development push distributor is enabled").setOngoing(true)
      .setContentIntent(PendingIntent.getActivity(this, 0, Intent(this, SettingsActivity::class.java), PendingIntent.FLAG_IMMUTABLE)).build()
    startForeground(1, notification)
    getSystemService(ConnectivityManager::class.java).registerDefaultNetworkCallback(network)
    control.scheduleWithFixedDelay({ try { drain() } catch (_: Exception) {} }, 1, 30, TimeUnit.SECONDS)
    Thread({ loop() }, "oro-push-dev-stream").start()
  }

  override fun onStartCommand (intent: Intent?, flags: Int, startId: Int): Int {
    if (!Store.preferences(this).getBoolean("enabled", false)) stopSelf()
    return START_STICKY
  }
  override fun onBind (intent: Intent?): IBinder? = null

  private fun status (value: String) { Store.preferences(this).edit().putString("status", value).apply() }
  private fun pause (milliseconds: Long, observed: Long) {
    synchronized(monitor) { if (running && revision == observed) monitor.wait(milliseconds) }
  }
  private fun reconnect () {
    val closing = synchronized(monitor) {
      revision++
      topics = emptySet()
      monitor.notifyAll()
      connection
    }
    try {
      control.execute {
        closing?.disconnect()
      }
    } catch (_: java.util.concurrent.RejectedExecutionException) {}
  }

  private fun drain () {
    Store.expire(this).forEach { (action, record) -> Wire.send(this, record, action) }
    // Retry persistence before exposing a snapshot whose previous commit failed.
    val snapshot = synchronized(Store) { Store.read(this).also { Store.save(this, it) } }
    val records = Store.records(snapshot, "registrations").associateBy { it.getString("token") }
    for (event in Store.records(snapshot, "pending")) {
      val record = records[event.getString("token")] ?: continue
      Wire.deliver(this, record, event.getString("id"), java.util.Base64.getDecoder().decode(event.getString("payload")))
    }
  }

  private fun loop () {
    var retry = 1000L
    while (running) {
      val observed = synchronized(monitor) { revision }
      try {
        if (!Store.preferences(this).getBoolean("enabled", false)) break
        val state = synchronized(Store) { Store.read(this) }
        val records = Store.records(state, "registrations")
        if (records.isEmpty()) {
          status("Ready; no apps registered")
          pause(1000, observed)
          continue
        }
        val requested = records.map { it.getString("topic") }.toSet()
        val server = Protocol.origin(Store.preferences(this).getString("server", "") ?: "")
        val cursor = state.optString("cursor").ifEmpty { "all" }
        val stream = Wire.connect(this, server + "/" + requested.joinToString(",") + "/json?since=" + URLEncoder.encode(cursor, "UTF-8"))
        // A reconnect during TLS setup must invalidate this registration snapshot.
        val attached = synchronized(monitor) {
          if (!running || revision != observed) false else { connection = stream; true }
        }
        if (!attached) {
          stream.disconnect()
          continue
        }
        try {
          check(stream.responseCode == 200) { "Server rejected subscription" }
          BufferedInputStream(stream.inputStream).use { input ->
            while (running && revision == observed) {
              val line = ByteArrayOutputStream()
              while (true) {
                val value = input.read()
                if (value < 0) throw java.io.EOFException()
                if (value == 10) break
                check(line.size() < 8192) { "Server frame exceeds 8192 bytes" }
                line.write(value)
              }
              if (line.size() == 0) continue
              val message = JSONObject(line.toString("UTF-8"))
              when (message.optString("event")) {
                "open" -> {
                  val current = synchronized(monitor) {
                    if (running && revision == observed) { topics = requested; true } else false
                  }
                  if (!current) break
                  status("Connected")
                  retry = 1000
                  drain()
                }
                "message" -> {
                  Store.receive(this, message)
                  drain()
                }
              }
            }
          }
        } finally {
          synchronized(monitor) {
            if (connection === stream) { connection = null; topics = emptySet() }
          }
          stream.disconnect()
        }
      } catch (error: Exception) {
        if (revision == observed) {
          status(if (error is IllegalStateException) error.message ?: "Connection paused" else
            "Reconnecting (${error.javaClass.simpleName}); check the server and certificate")
          pause(retry, observed)
          retry = (retry * 2).coerceAtMost(30000)
        }
      }
    }
  }

  override fun onDestroy () {
    running = false
    topics = emptySet()
    instance = null
    getSystemService(ConnectivityManager::class.java).unregisterNetworkCallback(network)
    reconnect()
    control.shutdown()
    super.onDestroy()
  }
}
