package oro.runtime.qualification.extensions

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import org.json.JSONObject
import oro.runtime.app.App
import oro.runtime.securestorage.SecureStorageManager
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/** Fixed public data through the native IPC route; export only with android.permission.DUMP. */
class PercentStorageReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val operation = intent.action?.removePrefix("oro.runtime.qualification.PERCENT.") ?: return
    val pending = goAsync()
    Thread {
      try {
        val scope = "https://${context.packageName}"
        val values = listOf("public%2Fdata", "%20", "%2520", "%2B", "%00", "%25", "%", "%2", "%GG",
          "{\"url\":\"https://public.example/a%2Fb?q=%2520\"}", "a+b c", "🙂\u0000\n", "")
        val expected = values.map { it.toByteArray(Charsets.UTF_8) } + listOf(ByteArray(256) { it.toByte() })
        var checks = 0
        for ((index, bytes) in expected.withIndex()) {
          val key = "public-percent-%2F-$index"
          when (operation) {
            "WRITE" -> {
              val encodings = if (index < values.size) listOf("utf8", "base64", "hex") else listOf("base64", "hex")
              for (encoding in encodings) {
                // Remove only this fixture's key so an earlier correct value cannot hide a failed write.
                SecureStorageManager.remove(context, scope, key)
                val value = when (encoding) {
                  "base64" -> android.util.Base64.encodeToString(bytes, android.util.Base64.NO_WRAP)
                  "hex" -> bytes.joinToString("") { (it.toInt() and 255).toString(16).padStart(2, '0') }
                  else -> values[index]
                }
                val sent = CountDownLatch(1)
                val failure = AtomicReference<Throwable?>()
                val app = App.getInstance()
                val activity = checkNotNull(app.activity)
                activity.runOnUiThread {
                  try {
                    val window = checkNotNull(activity.windowFragmentManager.getWindowFragment(0)?.window)
                    val message = Uri.Builder().scheme("ipc").authority("secureStorage.set")
                      .appendQueryParameter("seq", "public-percent-$index-$encoding")
                      .appendQueryParameter("index", "0").appendQueryParameter("scope", scope)
                      .appendQueryParameter("key", key).appendQueryParameter("encoding", encoding)
                      .appendQueryParameter("value", value).build().toString()
                    window.onMessage(0, message, null)
                  } catch (error: Throwable) { failure.set(error) } finally { sent.countDown() }
                }
                check(sent.await(3, TimeUnit.SECONDS))
                failure.get()?.let { throw it }
                val until = SystemClock.elapsedRealtime() + 3000
                while (true) {
                  val actual = SecureStorageManager.get(context, scope, key)
                  if (actual != null && actual.contentEquals(bytes)) break
                  check(SystemClock.elapsedRealtime() < until) { "Public native IPC storage write did not preserve bytes" }
                  Thread.sleep(5)
                }
                checks++
              }
            }
            "READ" -> repeat(3) {
              check(SecureStorageManager.get(context, scope, key)?.contentEquals(bytes) == true)
              checks++
            }
            "CLEAR" -> {
              check(SecureStorageManager.remove(context, scope, key))
              check(SecureStorageManager.get(context, scope, key) == null)
              checks++
            }
            else -> error("Unsupported public storage operation")
          }
        }
        pending.resultCode = -1
        pending.resultData = JSONObject().put("operation", operation).put("checks", checks)
          .put("publicDataOnly", true).toString()
      } catch (_: Throwable) {
        pending.resultCode = 0
        pending.resultData = "public-percent-storage-check-failed"
      } finally { pending.finish() }
    }.start()
  }
}
