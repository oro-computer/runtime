package oro.runtime.qualification.extensions

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.CancellationSignal
import android.util.Base64
import org.json.JSONObject
import oro.runtime.app.NativeHttps
import oro.runtime.app.NativeHttpsFailure
import oro.runtime.app.NativeTlsPolicy
import java.security.MessageDigest

/** DUMP-protected public TLS fixture. URLs and headers are fixed to loopback test data. */
class TlsQualificationReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val pending = goAsync()
    Thread {
      try {
        val operation = intent.action?.substringAfterLast('.') ?: ""
        val pin = context.assets.open("public-tls-certificate.der").use {
          "sha256/" + Base64.encodeToString(MessageDigest.getInstance("SHA-256").digest(it.readBytes()), Base64.NO_WRAP)
        }
        val authority = "127.0.0.1:18443"
        val expected = "$authority $pin"
        fun approve() = NativeTlsPolicy.replace(context, expected)
        fun request(path: String = "/public", timeout: Int = 5000, maximum: Int = 4096,
            cancellation: CancellationSignal = CancellationSignal()) = NativeHttps.request(context,
          "https://$authority$path", headers = mapOf("Authorization" to "Bearer public-test-only"),
          timeoutMs = timeout, maximumResponseBytes = maximum, cancellation = cancellation)
        fun fails(code: String, run: () -> Unit) {
          try { run(); error("expected fixed failure") } catch (error: NativeHttpsFailure) { check(error.code == code) }
        }
        when (operation) {
          "APPROVE" -> approve()
          "COLD" -> {
            check(NativeTlsPolicy.value(context) == expected)
            check(request().body.decodeToString() == "public-tls-ok")
          }
          "MATCH" -> { approve(); check(request().body.decodeToString() == "public-tls-ok") }
          "MISMATCH" -> {
            NativeTlsPolicy.replace(context, "$authority sha256/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
            fails("ETRUST") { request() }
          }
          "REVOKE" -> { approve(); NativeTlsPolicy.revoke(context, authority); fails("ETRUST") { request() } }
          "PORT" -> {
            NativeTlsPolicy.replace(context, "127.0.0.1:18444 $pin")
            fails("ETRUST") { request() }
          }
          "HOST" -> { NativeTlsPolicy.replace(context, "localhost:18443 $pin"); fails("ETRUST") { request() } }
          "REDIRECT" -> { approve(); fails("EREDIRECT") { request("/redirect") } }
          "SIZE" -> { approve(); fails("E2BIG") { request("/oversized", maximum = 64) } }
          "CHUNKED" -> { approve(); fails("E2BIG") { request("/chunked", maximum = 64) } }
          "TIMEOUT" -> { approve(); fails("ETIMEDOUT") { request("/slow", timeout = 250) } }
          "CANCEL" -> {
            approve()
            val signal = CancellationSignal()
            Thread { Thread.sleep(150); signal.cancel() }.start()
            fails("ECANCELLED") { request("/slow", cancellation = signal) }
          }
          "INFLIGHT" -> {
            approve()
            var result = ""
            val worker = Thread {
              try { request("/slow") } catch (error: NativeHttpsFailure) { result = error.code }
            }
            worker.start()
            Thread.sleep(150)
            NativeTlsPolicy.revoke(context, authority)
            worker.join(6000)
            check(!worker.isAlive && result == "ETRUST")
          }
          "PRECANCEL" -> {
            approve()
            fails("ECANCELLED") { request(cancellation = CancellationSignal().apply { cancel() }) }
          }
          "INVALID" -> {
            fails("EINVAL") { NativeHttps.request(context, "http://127.0.0.1:18443/public") }
            fails("EINVAL") { NativeHttps.request(context, "https://user:password@127.0.0.1:18443/public") }
            fails("EINVAL") { NativeHttps.request(context, "https://$authority/public#fragment") }
            fails("EINVAL") { NativeHttps.request(context, "https://$authority/public", headers = mapOf("Host" to "foreign.invalid")) }
          }
          "CORRUPT" -> {
            approve()
            val file = java.io.File(context.noBackupFilesDir, "oro-tls-policy-v1.json")
            val original = file.readBytes()
            try {
              file.writeText(JSONObject(String(original)).put("value", "").toString())
              fails("ETRUST") { request() }
            } finally { file.writeBytes(original) }
          }
          "ROTATE" -> {
            approve()
            fails("ETRUST") { request() }
            val rotated = context.assets.open("public-tls-rotated.der").use {
              "sha256/" + Base64.encodeToString(MessageDigest.getInstance("SHA-256").digest(it.readBytes()), Base64.NO_WRAP)
            }
            NativeTlsPolicy.replace(context, "$authority $rotated")
            check(request().body.decodeToString() == "public-tls-ok")
          }
          "CLEAR" -> NativeTlsPolicy.replace(context, "")
          else -> error("unknown public fixture operation")
        }
        pending.setResult(-1, JSONObject().put("operation", operation).put("passed", true).toString(), null)
      } catch (_: Exception) {
        pending.setResult(0, "public-tls-check-failed", null)
      } finally { pending.finish() }
    }.start()
  }
}
