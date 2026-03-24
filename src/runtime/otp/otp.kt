// vim: set sw=2:
package oro.runtime.otp

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper

import com.google.android.gms.auth.api.phone.SmsRetriever
import com.google.android.gms.common.api.CommonStatusCodes
import com.google.android.gms.common.api.Status

import java.util.concurrent.ConcurrentHashMap

import oro.runtime.app.App
import oro.runtime.debug.console

object OTPManager {
  private const val DEFAULT_TIMEOUT_MS = 60_000L

  private val pending = ConcurrentHashMap<Long, Request>()
  private val handler = Handler(Looper.getMainLooper())
  @Volatile private var receiverRegistered = false

  private data class Request(
    val id: Long,
    val host: String,
    val timeoutMs: Long,
    val timeoutRunnable: Runnable
  )

  private val receiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
      if (intent.action != SmsRetriever.SMS_RETRIEVED_ACTION) {
        return
      }

      val extras = intent.extras ?: return
      val status = extras.get(SmsRetriever.EXTRA_STATUS) as? Status ?: return

      when (status.statusCode) {
        CommonStatusCodes.SUCCESS -> {
          val message = extras.getString(SmsRetriever.EXTRA_SMS_MESSAGE)
          if (!message.isNullOrBlank()) {
            handleMessage(context, message)
          }
        }

        CommonStatusCodes.TIMEOUT -> clearAll(context)

        else -> {
          console.debug("OTPManager: unexpected status code ${status.statusCode}")
        }
      }
    }
  }

  @JvmStatic
  fun start(context: Context, requestId: Long, host: String, timeoutMs: Int): Boolean {
    val normalizedHost = host.trim().lowercase()
    if (normalizedHost.isEmpty()) {
      console.error("OTPManager: cannot start request without host")
      return false
    }

    val actualTimeout = if (timeoutMs > 0) timeoutMs.toLong() else DEFAULT_TIMEOUT_MS
    val timeoutRunnable = Runnable {
      val request = pending.remove(requestId) ?: return@Runnable
      App.getInstance().onOtpTimeout(request.id)
      maybeUnregister(context)
    }

    val previous = pending.put(requestId, Request(requestId, normalizedHost, actualTimeout, timeoutRunnable))
    previous?.let { handler.removeCallbacks(it.timeoutRunnable) }

    handler.postDelayed(timeoutRunnable, actualTimeout)

    ensureReceiver(context)

    val client = SmsRetriever.getClient(context)
    val task = client.startSmsRetriever()

    task.addOnFailureListener { err ->
      pending.remove(requestId)?.let { handler.removeCallbacks(it.timeoutRunnable) }
      val message = err.message ?: "Failed to start SMS Retriever"
      App.getInstance().onOtpError(requestId, "NotSupportedError", message)
      maybeUnregister(context)
    }

    return true
  }

  @JvmStatic
  fun finish(context: Context, requestId: Long) {
    pending.remove(requestId)?.let { handler.removeCallbacks(it.timeoutRunnable) }
    maybeUnregister(context)
  }

  private fun handleMessage(context: Context, message: String) {
    if (pending.isEmpty()) {
      console.debug("OTPManager: received SMS but no pending requests")
      return
    }

    val normalized = message.lowercase()
    val match = pending.values.firstOrNull { normalized.contains("@${it.host}") }

    if (match == null) {
      console.debug("OTPManager: no matching request for received SMS")
      return
    }

    finish(context, match.id)
    App.getInstance().onOtpMessage(match.id, message)
  }

  private fun ensureReceiver(context: Context) {
    if (receiverRegistered) {
      return
    }

    val filter = IntentFilter(SmsRetriever.SMS_RETRIEVED_ACTION)

    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
      } else {
        context.registerReceiver(receiver, filter)
      }
      receiverRegistered = true
    } catch (err: Exception) {
      console.error("OTPManager: failed to register receiver", err)
      receiverRegistered = false
    }
  }

  private fun maybeUnregister(context: Context) {
    if (!receiverRegistered || pending.isNotEmpty()) {
      return
    }

    try {
      context.unregisterReceiver(receiver)
    } catch (_: IllegalArgumentException) {
    } finally {
      receiverRegistered = false
    }
  }

  private fun clearAll(context: Context) {
    val ids = pending.keys.toList()
    pending.clear()
    handler.removeCallbacksAndMessages(null)
    maybeUnregister(context)
    ids.forEach { id -> App.getInstance().onOtpTimeout(id) }
  }
}
