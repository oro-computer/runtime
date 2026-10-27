package dev.oro.push

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import org.json.JSONObject
import java.util.concurrent.SynchronousQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

class RegistrationReceiver : BroadcastReceiver() {
  companion object {
    private val requests = ThreadPoolExecutor(4, 4, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>())
  }

  @Suppress("DEPRECATION")
  override fun onReceive (context: Context, intent: Intent) {
    try {
      val shared = if (Build.VERSION.SDK_INT >= 34) sentFromPackage else null
      val sender = if (Build.VERSION.SDK_INT >= 34) sentFromUid else -1
      val application: String
      val uid: Int
      if (sender >= 0 && shared != null) { application = shared; uid = sender }
      else {
        val proof = if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra("pi", PendingIntent::class.java)
          else intent.getParcelableExtra("pi") as? PendingIntent
        if (proof == null || (Build.VERSION.SDK_INT >= 31 && !proof.isImmutable)) return
        application = proof.creatorPackage ?: return
        uid = proof.creatorUid
        if (Build.VERSION.SDK_INT >= 34 && context.packageManager.getApplicationInfo(application, 0).targetSdkVersion >= 34) return
      }
      if (context.packageManager.getApplicationInfo(application, 0).uid != uid) return
      val token = intent.getStringExtra("token")
      val action = intent.action ?: return
      if (!Protocol.text(token, 100) || !action.startsWith(Protocol.DISTRIBUTOR)) return
      if (intent.hasExtra("message") && !Protocol.text(intent.getStringExtra("message"), 100)) return
      if (intent.hasExtra("vapid") && !Protocol.vapid(intent.getStringExtra("vapid"))) return
      val caller = JSONObject().put("package", application).put("uid", uid).put("token", token)
        .put("signature", Wire.signature(context, application))
      val pending = goAsync()
      try {
        requests.execute {
          try { process(context, caller, action, intent.getStringExtra("id")) }
          catch (_: Exception) {
            if (action == Protocol.DISTRIBUTOR + "REGISTER") Wire.send(context, caller, "REGISTRATION_FAILED", reason = "INTERNAL_ERROR")
          }
          finally { pending.finish() }
        }
      } catch (_: java.util.concurrent.RejectedExecutionException) {
        pending.finish()
        if (action == Protocol.DISTRIBUTOR + "REGISTER") Wire.send(context, caller, "REGISTRATION_FAILED", reason = "INTERNAL_ERROR")
      }
    } catch (_: Exception) {
      // Ignore malformed fields and requests without a verified sender identity.
    }
  }

  private fun process (context: Context, caller: JSONObject, action: String, id: String?) {
    val token = caller.getString("token")
    val record: JSONObject
    synchronized(Store) {
      val state = Store.read(context)
      val existing = Store.registration(state, token)
      if (existing != null && listOf("package", "uid", "signature").any { existing.get(it) != caller.get(it) }) {
        if (action == Protocol.DISTRIBUTOR + "REGISTER") Wire.send(context, caller, "REGISTRATION_FAILED", reason = "INTERNAL_ERROR")
        return
      }
      when (action) {
        Protocol.DISTRIBUTOR + "MESSAGE_ACK" -> {
          if (existing != null && Protocol.text(id, 100)) Store.acknowledge(context, token, id!!)
          return
        }
        Protocol.DISTRIBUTOR + "UNREGISTER" -> {
          if (existing != null) {
            Store.remove(state, token)
            Store.save(context, state)
            Wire.send(context, existing, "UNREGISTERED")
            ConnectionService.refresh()
          }
          return
        }
        Protocol.DISTRIBUTOR + "REGISTER" -> {}
        else -> return
      }
      if (!Store.preferences(context).getBoolean("enabled", false)) {
        Wire.send(context, caller, "REGISTRATION_FAILED", reason = "ACTION_REQUIRED")
        return
      }
      if (existing != null) {
        // Capability topics stay valid across reconnects; return the same endpoint.
        Wire.send(context, existing, "NEW_ENDPOINT")
        ConnectionService.start(context)
        return
      }
      if (state.getJSONArray("registrations").length() >= Protocol.REGISTRATIONS) {
        Wire.send(context, caller, "REGISTRATION_FAILED", reason = "ACTION_REQUIRED")
        return
      }
      val topic = Protocol.topic()
      record = JSONObject(caller.toString()).put("topic", topic)
        .put("lastAcknowledgedAt", System.currentTimeMillis()).put("endpointId", java.util.UUID.randomUUID().toString())
        .put("endpoint", Store.preferences(context).getString("server", "") + "/" + topic + "?up=1")
      state.getJSONArray("registrations").put(record)
      Store.save(context, state)
    }
    if (ConnectionService.start(context)) {
      ConnectionService.refresh()
      val deadline = android.os.SystemClock.elapsedRealtime() + 6500
      while (android.os.SystemClock.elapsedRealtime() < deadline) {
        if (ConnectionService.subscribed(record.getString("topic"))) {
          synchronized(Store) {
            if (Store.registration(Store.read(context), token) != null) Wire.send(context, record, "NEW_ENDPOINT")
          }
          return
        }
        Thread.sleep(50)
      }
    }
    synchronized(Store) {
      val state = Store.read(context)
      Store.remove(state, token)
      Store.save(context, state)
      Wire.send(context, caller, "REGISTRATION_FAILED", reason = "NETWORK")
    }
    ConnectionService.refresh()
  }
}
