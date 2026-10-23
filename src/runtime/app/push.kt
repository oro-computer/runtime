package oro.runtime.app

import android.app.Activity
import android.app.BroadcastOptions
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Binder
import android.os.Build
import android.os.CancellationSignal
import android.os.IBinder
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.window.Window
import java.security.MessageDigest
import java.util.UUID
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Opaque app references; transport tokens, keys and credentials are never arguments. */
data class NativePushRegistration (val id: String, val kind: String, val authorityRef: String,
  val accountRef: String, val targetRef: String)

/** Registered at Application startup, including cold receiver/service processes. */
interface NativePushHandler {
  /** Recheck current app authorization for every delivery. No foreground Activity is required. */
  fun authorize (context: Context, registration: NativePushRegistration): Boolean
  /** Return true only after durable application handling. Repeated delivery must be safe. */
  fun onMessage (context: Context, registration: NativePushRegistration, message: ByteArray,
    cancellation: CancellationSignal): Boolean
  /** Return true after server enrollment is confirmed. A false result retains pending enrollment. */
  fun onRegistrationChanged (context: Context, registration: NativePushRegistration, enrollment: JSONObject,
    cancellation: CancellationSignal): Boolean = false
}

/** Optional UnifiedPush transport with encrypted bounded registration and inbox storage. */
object NativePush {
  private const val STORE = "oro.push.v1"
  private const val CONNECTOR = "org.unifiedpush.android.connector."
  private const val DISTRIBUTOR = "org.unifiedpush.android.distributor."
  private const val SELECT = 0x4f50
  private const val TTL = 7 * 24 * 60 * 60 * 1000L
  private val handlers = mutableMapOf<String, NativePushHandler>()
  private val active = mutableMapOf<String, CancellationSignal>()
  private data class Selection (val window: Window, val document: String, val time: Long)
  private var selection: Selection? = null
  private var selectionDeadline: java.util.concurrent.ScheduledFuture<*>? = null
  private val worker = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, ArrayBlockingQueue<Runnable>(16),
    { task -> Thread(task, "oro-push").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  private val timer = ScheduledThreadPoolExecutor(1) { task -> Thread(task, "oro-push-deadlines").apply { isDaemon = true } }
    .apply { removeOnCancelPolicy = true }
  private val cancellations = ThreadPoolExecutor(0, 2, 30, TimeUnit.SECONDS, java.util.concurrent.SynchronousQueue<Runnable>(),
    { task -> Thread(task, "oro-push-cancel").apply { isDaemon = true } })

  fun available (context: Context): Boolean = App.getInstance().getUserConfigValue("push_enabled") == "true" &&
    App.getInstance().getUserConfigValue("push_provider") == "unifiedpush"
  private fun store (context: Context) = context.getSharedPreferences(STORE, Context.MODE_PRIVATE)
  private fun digest (value: String) = MessageDigest.getInstance("SHA-256")
    .digest(value.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it) }
  private fun identifier (value: String): String {
    require(Regex("[a-zA-Z0-9_-]{1,128}").matches(value))
    return value
  }
  private fun keys (context: Context, prefix: String): List<String> = store(context).all.keys.filter { it.startsWith(prefix) }.sorted()
  private fun read (context: Context, key: String): JSONObject? = store(context).getString(key, null)
    ?.let { PushCryptography.open(it, key) }
  private fun save (context: Context, key: String, value: JSONObject) {
    val create = keys(context, "registration.").isEmpty() && keys(context, "event.").isEmpty()
    check(store(context).edit().putString(key, PushCryptography.seal(value, key, create)).commit())
  }
  private fun registration (value: JSONObject) = NativePushRegistration(value.getString("id"), value.getString("kind"),
    value.getString("authorityRef"), value.getString("accountRef"), value.getString("targetRef"))

  @Synchronized
  fun registerHandler (kind: String, handler: NativePushHandler) {
    require(Regex("[a-z][a-z0-9.-]{0,63}").matches(kind) && !handlers.containsKey(kind) && handlers.size < 16)
    handlers[kind] = handler
  }

  /** Sensitive enrollment material for the app's authenticated server; private keys stay native. */
  @Synchronized
  fun getRegistration (context: Context, id: String): JSONObject {
    val value = read(context, "registration.${identifier(id)}") ?: throw PlatformFailure("ENOENT")
    val pending = keys(context, "event.").mapNotNull { read(context, it) }.filter { it.optString("registration") == id }
    return JSONObject().put("id", id).put("kind", value.getString("kind"))
      .put("phase", value.getString("phase")).put("endpoint", value.optString("endpoint"))
      .put("distributor", value.getString("distributor"))
      .put("applicationServerKey", value.getString("applicationServerKey"))
      .put("generation", value.optString("generation")).put("enrollmentPending", value.optBoolean("enrollmentPending"))
      .put("pendingMessages", pending.size).put("exhaustedMessages", pending.count { it.optInt("attempts") >= 5 })
      .put("keys", JSONObject().put("p256dh", value.getJSONObject("keys").getString("p256dh"))
        .put("auth", value.getJSONObject("keys").getString("auth")))
  }

  private fun send (context: Context, record: JSONObject, action: String, id: String? = null) {
    val intent = Intent(DISTRIBUTOR + action).setPackage(record.getString("distributor"))
      .putExtra("token", record.getString("token"))
    if (action == "REGISTER") intent.putExtra("vapid", record.getString("applicationServerKey"))
      .putExtra("message", record.getString("description"))
    if (id != null) intent.putExtra("id", id)
    if (Build.VERSION.SDK_INT >= 34) context.sendBroadcast(intent, null,
      BroadcastOptions.makeBasic().setShareIdentityEnabled(true).toBundle())
    else {
      intent.putExtra("pi", PendingIntent.getBroadcast(context, 0,
        Intent().setPackage("org.unifiedpush.dummy_app"), PendingIntent.FLAG_IMMUTABLE))
      context.sendBroadcast(intent)
    }
  }

  internal fun request (call: PlatformCall, action: String, options: JSONObject) {
    call.foreground()
    if (action == "support") {
      require(options.length() == 0)
      call.success(synchronized(this) {
        JSONObject().put("available", available(call.app)).put("provider", "unifiedpush")
          .put("selectedDistributor", store(call.app).getString("distributor", ""))
          .put("registeredKinds", JSONArray(handlers.keys.sorted())).put("maximumRegistrations", 16)
          .put("maximumPendingMessages", 64).put("maximumMessageBytes", 4096)
          .put("backgroundJavaScript", false).put("survivesForceStop", false)
      })
      return
    }
    if (!available(call.app)) throw PlatformFailure("ENOSYS")
    if (action == "selectDistributor") {
      require(options.length() == 0)
      synchronized(this) {
        if (keys(call.app, "registration.").isNotEmpty()) throw PlatformFailure("EBUSY")
        if (selection?.let { android.os.SystemClock.elapsedRealtime() - it.time < 90000 } == true) throw PlatformFailure("EBUSY")
        val window = call.foreground()
        require(window.activity.hasWindowFocus())
        val pending = Selection(window, call.documentToken, android.os.SystemClock.elapsedRealtime())
        selection = pending
        selectionDeadline?.cancel(false)
        selectionDeadline = timer.schedule({ synchronized(this) { if (selection === pending) selection = null } }, 90, TimeUnit.SECONDS)
        try {
          window.activity.startActivityForResult(Intent.createChooser(Intent(Intent.ACTION_VIEW,
            Uri.parse("unifiedpush://link")), window.activity.getString(__BUNDLE_IDENTIFIER__.R.string.runtime_push_distributor)), SELECT)
        } catch (error: Exception) {
          selection = null
          selectionDeadline?.cancel(false)
          throw error
        }
      }
      call.success(JSONObject().put("submitted", true))
      return
    }
    synchronized(this) {
      when (action) {
        "register" -> {
          require(options.keys().asSequence().all { it in listOf("id", "kind", "authorityRef", "accountRef", "targetRef", "applicationServerKey", "description") })
          val id = identifier(options.getString("id"))
          val kind = options.getString("kind")
          if (!handlers.containsKey(kind)) throw PlatformFailure("ENOTSUP")
          for (field in listOf("authorityRef", "accountRef", "targetRef")) identifier(options.getString(field))
          val vapid = options.getString("applicationServerKey")
          require(vapid.length == 87 && PushCryptography.decode(vapid).let { it.size == 65 && it[0] == 4.toByte() })
          PushCryptography.validatePublicKey(vapid)
          val description = options.optString("description", "Push notifications")
          require(description.toByteArray(Charsets.UTF_8).size in 1..128 && description.none { it.code < 32 })
          val distributor = store(call.app).getString("distributor", "") ?: ""
          if (distributor.isEmpty()) throw PlatformFailure("ENOTREADY")
          val key = "registration.$id"
          var record = read(call.app, key)
          if (record == null) {
            if (keys(call.app, "registration.").size >= 16) throw PlatformFailure("ENOSPC")
            record = JSONObject(options.toString()).put("description", description).put("distributor", distributor)
              .put("token", UUID.randomUUID().toString()).put("keys", PushCryptography.keys()).put("phase", "registering")
          } else {
            for (field in listOf("kind", "authorityRef", "accountRef", "targetRef", "applicationServerKey")) {
              if (record.getString(field) != options.getString(field)) throw PlatformFailure("EEXIST")
            }
            if (record.optString("phase") in setOf("registration-failed", "unregistered")) {
              record.put("token", UUID.randomUUID().toString()).put("phase", "registering")
            }
          }
          save(call.app, key, record)
          send(call.app, record, "REGISTER")
          call.success(getRegistration(call.app, id))
        }
        "registration" -> {
          require(options.length() == 1)
          call.success(getRegistration(call.app, options.getString("id")))
        }
        "unregister" -> {
          require(options.length() == 1)
          unregister(call.app, options.getString("id"))
          call.success()
        }
        "acknowledgeRegistration" -> {
          require(options.length() == 2)
          val key = "registration.${identifier(options.getString("id"))}"
          val record = read(call.app, key) ?: throw PlatformFailure("ENOENT")
          if (record.optString("phase") != "registered" || record.optString("generation") != options.getString("generation")) {
            throw PlatformFailure("EAGAIN")
          }
          save(call.app, key, record.put("enrollmentPending", false))
          call.success()
        }
        "retryPending" -> {
          require(options.length() == 0)
          for (key in keys(call.app, "event.")) {
            val event = read(call.app, key) ?: continue
            if (event.optInt("attempts") >= 5) save(call.app, key, event.put("attempts", 0))
          }
          for (key in keys(call.app, "registration.")) {
            val record = read(call.app, key) ?: continue
            if (record.optInt("enrollmentAttempts") >= 5) save(call.app, key, record.put("enrollmentAttempts", 0))
          }
          start(call.app)
          call.success(JSONObject().put("submitted", true))
        }
        else -> throw PlatformFailure("EINVAL")
      }
    }
  }

  internal fun onActivityResult (activity: AppActivity, requestCode: Int, resultCode: Int, data: Intent?) {
    if (requestCode != SELECT) return
    synchronized(this) {
      val pending = selection ?: return
      selection = null
      selectionDeadline?.cancel(false)
      val window = pending.window
      if (window.activity !== activity || window.platformDocumentToken != pending.document ||
          !window.userMessageHandler.active || window.fragment.window !== window ||
          android.os.SystemClock.elapsedRealtime() - pending.time > 90000 || resultCode != Activity.RESULT_OK) return
      val proof = if (Build.VERSION.SDK_INT >= 33) data?.getParcelableExtra("pi", PendingIntent::class.java)
        else @Suppress("DEPRECATION") (data?.getParcelableExtra("pi") as? PendingIntent)
      val name = proof?.creatorPackage ?: return
      if (name == activity.packageName || activity.packageManager.getApplicationInfo(name, 0).uid != proof.creatorUid) return
      if (keys(activity, "registration.").isNotEmpty()) return
      check(store(activity).edit().putString("distributor", name).commit())
      window.bridge.emit("pushchange", JSONObject().put("distributorSelected", true).toString())
    }
  }

  /** Stops future delivery and signals running handlers. Server enrollment revocation is app-owned. */
  @Synchronized
  fun unregister (context: Context, id: String) {
    val key = "registration.${identifier(id)}"
    val record = read(context, key) ?: return
    active.remove(id)?.let { cancelSignal(it) }
    val editor = store(context).edit().remove(key)
    for (event in keys(context, "event.")) if (read(context, event)?.optString("registration") == id) editor.remove(event)
    check(editor.commit())
    send(context, record, "UNREGISTER")
  }

  /** Persists authenticated transport input before ACK. No receiver starts a WebView. */
  @Synchronized
  private fun receive (context: Context, intent: Intent, senderUid: Int) {
    if (!available(context)) return
    val action = intent.action?.removePrefix(CONNECTOR) ?: return
    if (intent.action != CONNECTOR + action || action !in setOf("NEW_ENDPOINT", "MESSAGE", "REGISTRATION_FAILED", "UNREGISTERED", "TEMP_UNAVAILABLE")) return
    val token = intent.getStringExtra("token") ?: return
    if (token.toByteArray(Charsets.UTF_8).size !in 1..100) return
    val record = keys(context, "registration.").mapNotNull { read(context, it) }
      .firstOrNull { it.optString("token") == token } ?: return
    val expected = context.packageManager.getApplicationInfo(record.getString("distributor"), 0).uid
    if (senderUid >= 0 && senderUid != expected) return
    if (intent.hasExtra("pi")) {
      val proof = if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra("pi", PendingIntent::class.java)
        else @Suppress("DEPRECATION") (intent.getParcelableExtra("pi") as? PendingIntent)
      if (proof?.creatorPackage != record.getString("distributor") || proof.creatorUid != expected) return
    }
    val messageId = intent.getStringExtra("id")
    if (intent.hasExtra("id") && (messageId == null || messageId.toByteArray(Charsets.UTF_8).size !in 1..100)) return
    when (action) {
      "NEW_ENDPOINT" -> {
        val endpoint = intent.getStringExtra("endpoint") ?: return
        val uri = java.net.URI(endpoint)
        if (endpoint.length > 4096 || uri.scheme != "https" || uri.host == null || uri.userInfo != null || uri.fragment != null) return
        if (record.optString("endpoint") != endpoint) {
          record.put("generation", UUID.randomUUID().toString()).put("enrollmentPending", true).put("enrollmentAttempts", 0)
        }
        record.put("endpoint", endpoint).put("phase", "registered")
      }
      "MESSAGE" -> {
        val bytes = intent.getByteArrayExtra("bytesMessage") ?: return
        if (bytes.size !in 103..4096 || record.optString("phase") !in setOf("registered", "temporarily-unavailable")) return
        // Authenticate ciphertext before retaining it. The plaintext is native-only.
        PushCryptography.decrypt(bytes, record.getJSONObject("keys")).fill(0)
        prune(context)
        val identity = digest(token + ":" + (messageId ?: PushCryptography.encode(MessageDigest.getInstance("SHA-256").digest(bytes))))
        if (!store(context).contains("seen.$identity") && !store(context).contains("event.$identity")) {
          if (keys(context, "event.").size >= 64) return
          save(context, "event.$identity", JSONObject().put("registration", record.getString("id"))
            .put("token", token).put("payload", PushCryptography.encode(bytes)).put("createdAt", System.currentTimeMillis())
            .put("attempts", 0))
        }
      }
      "REGISTRATION_FAILED" -> {
        if (record.has("endpoint")) return
        val reason = intent.getStringExtra("reason") ?: "INTERNAL_ERROR"
        if (reason !in setOf("INTERNAL_ERROR", "NETWORK", "ACTION_REQUIRED", "VAPID_REQUIRED")) return
        record.put("phase", "registration-failed").put("reason", reason)
      }
      "UNREGISTERED" -> record.remove("endpoint").also { record.put("phase", "unregistered") }
      "TEMP_UNAVAILABLE" -> record.put("phase", "temporarily-unavailable")
    }
    save(context, "registration.${record.getString("id")}", record)
    if (messageId != null && action in setOf("NEW_ENDPOINT", "MESSAGE")) send(context, record, "MESSAGE_ACK", messageId)
    PlatformOperations.main.post {
      val activity = App.getInstance().activity as? oro.runtime.window.WindowManagerActivity ?: return@post
      for (fragment in activity.windowFragmentManager.fragments) {
        val window = fragment.window ?: continue
        if (fragment.hasWebView && !fragment.options.headless && window.userMessageHandler.active &&
            window.isTrustedOrigin(Uri.parse(fragment.webview.url ?: ""))) {
          window.bridge.emit("pushchange", JSONObject().put("id", record.getString("id"))
            .put("phase", record.getString("phase")).toString())
        }
      }
    }
  }

  private fun prune (context: Context) {
    val now = System.currentTimeMillis()
    val editor = store(context).edit()
    for (key in keys(context, "event.")) {
      val created = read(context, key)?.optLong("createdAt") ?: 0
      if (now - created !in 0..TTL) editor.remove(key)
    }
    val seen = keys(context, "seen.").sortedByDescending { store(context).getLong(it, 0) }
    for ((index, key) in seen.withIndex()) if (index >= 256 || now - store(context).getLong(key, 0) !in 0..TTL) editor.remove(key)
    check(editor.commit())
  }

  /** Retries retained input on startup, new transport input or an explicit retryPending call. */
  fun start (context: Context) {
    val app = context.applicationContext
    if (!available(app)) return
    execute { signal, deadline -> drain(app, signal, deadline) }
  }

  private fun markHandled (context: Context, key: String) {
    val editor = store(context).edit().remove(key)
      .putLong("seen.${key.removePrefix("event.")}", System.currentTimeMillis())
    for (old in keys(context, "seen.").sortedByDescending { store(context).getLong(it, 0) }.drop(255)) editor.remove(old)
    check(editor.commit())
  }

  private fun checkActive (signal: CancellationSignal, deadline: Long) {
    signal.throwIfCanceled()
    if (android.os.SystemClock.elapsedRealtime() >= deadline) throw android.os.OperationCanceledException()
  }

  private fun drain (context: Context, signal: CancellationSignal, deadline: Long) {
    val registrations = synchronized(this) {
      if (!available(context)) return
      keys(context, "registration.")
    }
    for (key in registrations) {
      checkActive(signal, deadline)
      val snapshot = synchronized(this) {
        val record = read(context, key) ?: return@synchronized null
        if (record.optString("phase") != "registered" || !record.optBoolean("enrollmentPending") ||
            record.optInt("enrollmentAttempts") >= 5) return@synchronized null
        val handler = handlers[record.getString("kind")] ?: return@synchronized null
        record to handler
      } ?: continue
      val (record, handler) = snapshot
      val reference = registration(record)
      if (!runCatching { handler.authorize(context, reference) }.getOrDefault(false)) continue
      val accepted = synchronized(this) {
        val current = read(context, key)
        if (current?.optString("generation") != record.getString("generation") || signal.isCanceled ||
            android.os.SystemClock.elapsedRealtime() >= deadline) false
        else {
          save(context, key, current.put("enrollmentAttempts", current.optInt("enrollmentAttempts") + 1))
          active[reference.id] = signal
          true
        }
      }
      if (!accepted) continue
      try {
        val handled = handler.onRegistrationChanged(context, reference, getRegistration(context, reference.id), signal)
        checkActive(signal, deadline)
        if (handled && handler.authorize(context, reference)) synchronized(this) {
          val current = read(context, key)
          if (current?.optString("generation") == record.getString("generation")) {
            checkActive(signal, deadline)
            save(context, key, current.put("enrollmentPending", false))
          }
        }
      } catch (error: android.os.OperationCanceledException) { throw error
      } catch (_: Exception) {
        // Retain this enrollment attempt while allowing other registrations to drain.
      } finally { synchronized(this) { if (active[reference.id] === signal) active.remove(reference.id) } }
    }
    val pending = synchronized(this) {
      if (!available(context)) return
      prune(context)
      keys(context, "event.")
    }
    for (key in pending) {
      checkActive(signal, deadline)
      val snapshot = synchronized(this) {
        val event = read(context, key) ?: return@synchronized null
        if (event.optInt("attempts") >= 5) return@synchronized null
        val record = read(context, "registration.${event.getString("registration")}") ?: return@synchronized null
        if (record.getString("token") != event.getString("token")) return@synchronized null
        val handler = handlers[record.getString("kind")] ?: return@synchronized null
        Triple(event, record, handler)
      } ?: continue
      val (event, record, handler) = snapshot
      val reference = registration(record)
      if (!runCatching { handler.authorize(context, reference) }.getOrDefault(false)) continue
      val accepted = synchronized(this) {
        val current = read(context, "registration.${reference.id}")
        if (current?.optString("token") != event.getString("token") || !store(context).contains(key) || signal.isCanceled ||
            android.os.SystemClock.elapsedRealtime() >= deadline) false
        else {
          event.put("attempts", event.optInt("attempts") + 1)
          save(context, key, event)
          active[reference.id] = signal
          true
        }
      }
      if (!accepted) continue
      try {
        val bytes = PushCryptography.decrypt(PushCryptography.decode(event.getString("payload")), record.getJSONObject("keys"))
        val handled = try { handler.onMessage(context, reference, bytes, signal) } finally { bytes.fill(0) }
        checkActive(signal, deadline)
        if (handled && handler.authorize(context, reference)) synchronized(this) {
          val current = read(context, "registration.${reference.id}")
          if (current?.optString("token") == event.getString("token") && store(context).contains(key)) {
            checkActive(signal, deadline)
            markHandled(context, key)
          }
        }
      } catch (error: android.os.OperationCanceledException) { throw error
      } catch (_: Exception) {
        // Retain this message attempt while allowing unrelated messages to drain.
      } finally { synchronized(this) { if (active[reference.id] === signal) active.remove(reference.id) } }
    }
  }

  private fun cancelSignal (signal: CancellationSignal) {
    try { cancellations.execute { signal.cancel() } } catch (_: java.util.concurrent.RejectedExecutionException) {}
  }

  private fun execute (finish: () -> Unit = {}, action: (CancellationSignal, Long) -> Unit) {
    val signal = CancellationSignal()
    val expiresAt = android.os.SystemClock.elapsedRealtime() + 8000
    val done = AtomicBoolean(false)
    fun complete () { if (done.compareAndSet(false, true)) finish() }
    val deadline = timer.schedule({
      complete()
      cancelSignal(signal)
    }, 8, TimeUnit.SECONDS)
    try {
      worker.execute {
        try {
          checkActive(signal, expiresAt)
          action(signal, expiresAt)
        } catch (_: Exception) {
          // Invalid input and provider failures never expose payloads or key material.
        } finally {
          deadline.cancel(false)
          complete()
        }
      }
    } catch (_: java.util.concurrent.RejectedExecutionException) {
      deadline.cancel(false)
      complete()
    }
  }

  internal fun receiveAsync (context: Context, intent: Intent, senderUid: Int, finish: () -> Unit) {
    execute(finish) { signal, deadline ->
      receive(context, intent, senderUid)
      drain(context, signal, deadline)
    }
  }
}

class RuntimePushReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    val pending = goAsync()
    val sender = if (Build.VERSION.SDK_INT >= 34) sentFromUid else -1
    NativePush.receiveAsync(context.applicationContext, intent, sender) { pending.finish() }
  }
}

/** UnifiedPush distributors bind briefly to grant foreground process importance. */
class RuntimePushImportance : Service() {
  override fun onBind (intent: Intent?): IBinder = Binder()
}
