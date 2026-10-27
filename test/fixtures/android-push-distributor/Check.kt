package dev.oro.push.qualification

import android.app.Activity
import android.app.BroadcastOptions
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Binder
import android.os.Bundle
import android.os.IBinder
import android.util.AtomicFile
import android.widget.TextView
import org.json.JSONObject
import oro.runtime.app.PushCryptography
import dev.oro.push.Protocol
import java.io.File
import java.util.UUID

/** Public fixture only: no saved accounts, external files or production enrollment. */
object Check {
  private const val CONNECTOR = "org.unifiedpush.android.connector."
  private const val DISTRIBUTOR = "org.unifiedpush.android.distributor."
  fun preferences (context: Context) = context.getSharedPreferences("public-test", Context.MODE_PRIVATE)
  fun state (context: Context) = JSONObject(preferences(context).getString("public", "{}")!!)
  @Synchronized fun update (context: Context, action: (JSONObject) -> Unit) {
    val state = state(context)
    action(state)
    check(preferences(context).edit().putString("public", state.toString()).commit())
    val output = AtomicFile(File(context.filesDir, "public-results.json"))
    val stream = output.startWrite()
    try { stream.write(state.toString().toByteArray()); output.finishWrite(stream) }
    catch (error: Exception) { output.failWrite(stream); throw error }
  }
  fun send (context: Context, name: String, action: String, id: String? = null) {
    val token = preferences(context).getString("token.$name", null) ?: return
    val intent = Intent(DISTRIBUTOR + action).setPackage("dev.oro.push").putExtra("token", token)
    if (id != null) intent.putExtra("id", id)
    if (action == "REGISTER") intent.putExtra("message", "Public Oro development check")
    context.sendBroadcast(intent, null, BroadcastOptions.makeBasic().setShareIdentityEnabled(true).toBundle())
  }
  fun operate (context: Context, operation: String, name: String) {
    require(name in listOf("alpha", "beta"))
    val preferences = preferences(context)
    when (operation) {
      "protocol" -> {
        check(!Protocol.text("", 100) && !Protocol.text("bad\n", 100))
        check(Protocol.text("é".repeat(50), 100) && !Protocol.text("é".repeat(51), 100))
        check(Protocol.origin("https://127.0.0.1:8787/") == "https://127.0.0.1:8787")
        for (url in listOf("http://localhost", "https://user:pass@localhost", "https://localhost/path", "https://localhost?x", "https://localhost/#x")) {
          check(runCatching { Protocol.origin(url) }.isFailure)
        }
        val topics = (0 until 100).map { Protocol.topic() }
        check(topics.toSet().size == 100 && topics.all { Regex("up[A-Za-z0-9_-]{27}").matches(it) })
        val maximum = ByteArray(4096) { (it % 256).toByte() }
        check(Protocol.payload(java.util.Base64.getEncoder().encodeToString(maximum), "base64").contentEquals(maximum))
        check(runCatching { Protocol.payload("", "") }.isFailure)
        check(runCatching { Protocol.payload("x".repeat(4097), "") }.isFailure)
        check(runCatching { Protocol.payload("bad", "unsupported") }.isFailure)
        check(Protocol.vapid(PushCryptography.keys().getString("p256dh")))
        check(!Protocol.vapid("bad") && !Protocol.vapid(java.util.Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(65).also { it[0] = 4 })))
        Check.update(context) { it.put("protocolPassed", true).put("protocolRuns", it.optInt("protocolRuns") + 1) }
      }
      "register" -> {
        if (!preferences.contains("token.$name") || state(context).optJSONObject(name)?.optString("phase") == "failed") {
          val edit = preferences.edit().putString("token.$name", UUID.randomUUID().toString()).putBoolean("ack.$name", true)
          if (!preferences.contains("keys.$name")) edit.putString("keys.$name", PushCryptography.keys().toString())
          check(edit.commit())
        }
        val keys = JSONObject(preferences.getString("keys.$name", "{}")!!)
        update(context) { state -> state.put(name, state.optJSONObject(name) ?: JSONObject().put("count", 0))
          .getJSONObject(name).put("phase", "registering").put("token", preferences.getString("token.$name", ""))
          .put("keys", JSONObject().put("p256dh", keys.getString("p256dh")).put("auth", keys.getString("auth"))) }
        send(context, name, "REGISTER")
      }
      "hold" -> preferences.edit().putBoolean("ack.$name", false).commit()
      "ack" -> {
        preferences.edit().putBoolean("ack.$name", true).commit()
        send(context, name, "MESSAGE_ACK", state(context).getJSONObject(name).optString("id"))
      }
      "unregister" -> send(context, name, "UNREGISTER")
    }
  }
  fun receive (context: Context, intent: Intent, sender: Int) {
    if (context.packageManager.getApplicationInfo("dev.oro.push", 0).uid != sender) return
    val token = intent.getStringExtra("token") ?: return
    val preferences = preferences(context)
    val name = listOf("alpha", "beta").find { preferences.getString("token.$it", "") == token } ?: return
    val action = intent.action?.removePrefix(CONNECTOR)
    when (action) {
      "NEW_ENDPOINT" -> {
        update(context) { it.getJSONObject(name).put("phase", "registered").put("endpoint", intent.getStringExtra("endpoint")) }
        intent.getStringExtra("id")?.let { send(context, name, "MESSAGE_ACK", it) }
      }
      "UNREGISTERED" -> update(context) { it.getJSONObject(name).put("phase", "unregistered") }
      "REGISTRATION_FAILED" -> update(context) { it.getJSONObject(name).put("phase", "failed").put("reason", intent.getStringExtra("reason")) }
      "MESSAGE" -> {
        val payload = intent.getByteArrayExtra("bytesMessage") ?: return
        val text = PushCryptography.decrypt(payload, JSONObject(preferences.getString("keys.$name", "{}")!!)).toString(Charsets.UTF_8)
        check(text == "Public Oro push development check.")
        update(context) {
          val record = it.getJSONObject(name)
          record.put("count", record.optInt("count") + 1).put("plaintextMatched", true).put("id", intent.getStringExtra("id"))
        }
        if (preferences.getBoolean("ack.$name", true)) send(context, name, "MESSAGE_ACK", intent.getStringExtra("id"))
      }
    }
  }
}

class MainActivity : Activity() {
  override fun onCreate (saved: Bundle?) {
    super.onCreate(saved)
    if (intent.getStringExtra("operation") == "unstop") { finish(); return }
    setContentView(TextView(this).apply { text = "Public Oro push verification\nNo personal data or saved accounts are used."; textSize = 20f })
    val operation = intent.getStringExtra("operation") ?: "link"
    if (operation == "link") startActivityForResult(Intent(Intent.ACTION_VIEW, Uri.parse("unifiedpush://link")).setPackage("dev.oro.push"), 1)
    else Check.operate(this, operation, intent.getStringExtra("name") ?: "alpha")
  }
  override fun onActivityResult (request: Int, result: Int, data: Intent?) {
    super.onActivityResult(request, result, data)
    val proof = data?.getParcelableExtra("pi", PendingIntent::class.java)
    Check.update(this) { it.put("linkPassed", request == 1 && result == RESULT_OK && proof?.creatorPackage == "dev.oro.push" && proof.isImmutable)
      .put("linkCount", it.optInt("linkCount") + 1) }
  }
}

class ConnectorReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    try { Check.receive(context, intent, sentFromUid) }
    catch (_: Exception) { Check.update(context) { it.put("failure", "Public message verification failed") } }
  }
}

class ControlReceiver : BroadcastReceiver() {
  companion object { private val worker = java.util.concurrent.Executors.newSingleThreadExecutor() }
  override fun onReceive (context: Context, intent: Intent) {
    val pending = goAsync()
    worker.execute {
      try { Check.operate(context, intent.getStringExtra("operation") ?: "", intent.getStringExtra("name") ?: "alpha") }
      catch (_: Exception) { Check.update(context) { it.put("failure", "Public fixture operation failed") } }
      finally { pending.finish() }
    }
  }
}

class ForegroundService : Service() {
  override fun onBind (intent: Intent): IBinder = Binder()
}
