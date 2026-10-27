package oro.runtime.app

import android.Manifest
import android.app.ActivityManager
import android.app.Application
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.CancellationSignal
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import androidx.lifecycle.Lifecycle
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.SynchronousQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Persisted opaque references. No credentials, arbitrary URLs, bodies or JavaScript. */
data class ForegroundTask (val id: String, val kind: String, val authorityRef: String,
  val accountRef: String, val targetRef: String, val operationRef: String)

enum class ForegroundOutcome { COMPLETED, UNAUTHORIZED, UNKNOWN, FAILED }

/** Native code must honor cancellation, close resources and establish its own remote outcome. */
interface AuthorizedForegroundExecution : AutoCloseable {
  fun execute (cancellation: CancellationSignal): ForegroundOutcome
}

fun interface ForegroundExecutor {
  fun authorize (context: Context, task: ForegroundTask, cancellation: CancellationSignal): AuthorizedForegroundExecution?
}

/** Shared controller for one compiled executor in each explicitly configured service mode. */
open class ForegroundController internal constructor (internal val continuous: Boolean) {
  internal val CHANNEL = if (continuous) "oro.runtime.continuous.v1" else "oro.runtime.foreground.v1"
  internal val NOTIFICATION = if (continuous) 0x4f524f34 else 0x4f524f33
  internal val START = "oro.runtime.foreground.START"
  internal val STOP = "oro.runtime.foreground.STOP"
  private val MAX_DURATION = 1800000L
  private val process = UUID.randomUUID().toString()
  private val main = Handler(Looper.getMainLooper())
  private val control = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, ArrayBlockingQueue<Runnable>(32),
    { task -> Thread(task, "oro-foreground-journal").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  private val worker = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>(),
    { task -> Thread(task, "oro-foreground-executor").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  private val canceller = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>(),
    { task -> Thread(task, "oro-foreground-cancel").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  private val executors = mutableMapOf<String, ForegroundExecutor>()
  @Volatile private var active: Execution? = null

  internal class Execution (val task: ForegroundTask, val duration: Long, val executor: ForegroundExecutor) {
    val generation = UUID.randomUUID().toString()
    val signal = CancellationSignal()
    val stopping = AtomicBoolean(false)
    val launched = AtomicBoolean(false)
    val finished = AtomicBoolean(false)
    val entered = AtomicBoolean(false)
    val cancellationFinished = AtomicBoolean(true)
    val serviceStopped = AtomicBoolean(true)
    @Volatile var stopPhase = "unknown"
    var service: RuntimeForegroundService? = null
    var expires = Long.MAX_VALUE
  }

  @Synchronized
  fun registerExecutor (kind: String, executor: ForegroundExecutor) {
    require(Regex("[a-z][a-z0-9.-]{0,63}").matches(kind) && !executors.containsKey(kind) && executors.keys.count { it.startsWith("workflow.") == kind.startsWith("workflow.") } < 16)
    executors[kind] = executor
  }

  private fun defaultProcess (context: Context): Boolean = if (Build.VERSION.SDK_INT >= 28) {
    Application.getProcessName() == context.packageName
  } else {
    (context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).runningAppProcesses
      ?.any { it.pid == android.os.Process.myPid() && it.processName == context.packageName } == true
  }

  private fun serviceType (): Class<out RuntimeForegroundService> =
    if (continuous) RuntimeContinuousService::class.java else RuntimeForegroundService::class.java

  fun available (context: Context): Boolean {
    if (App.getInstance().getUserConfigValue(if (continuous) "foreground_continuous_enabled" else "foreground_enabled") != "true" || !defaultProcess(context) ||
        (Build.VERSION.SDK_INT >= 28 && context.checkSelfPermission(Manifest.permission.FOREGROUND_SERVICE) != PackageManager.PERMISSION_GRANTED)) return false
    return try {
      @Suppress("DEPRECATION")
      val info = context.packageManager.getServiceInfo(ComponentName(context, serviceType()), 0)
      val subtypePresent = !continuous || Build.VERSION.SDK_INT < 34 ||
        context.packageManager.getProperty("android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE", ComponentName(context, serviceType())).string?.isNotBlank() == true
      val configuredType = if (continuous) ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE else ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
      icon(context) != 0 && subtypePresent && info.enabled && !info.exported && (info.processName == null || info.processName == context.packageName) &&
        (Build.VERSION.SDK_INT < (if (continuous) 34 else 29) || info.foregroundServiceType and configuredType != 0) &&
        (Build.VERSION.SDK_INT < 34 || context.checkSelfPermission(if (continuous)
          "android.permission.FOREGROUND_SERVICE_SPECIAL_USE" else "android.permission.FOREGROUND_SERVICE_DATA_SYNC") == PackageManager.PERMISSION_GRANTED) &&
        (!continuous || App.getInstance().getUserConfigValue("foreground_continuous_subtype").isNotBlank())
    } catch (_: Exception) { false }
  }

  private fun icon (context: Context): Int {
    val configured = App.getInstance().getUserConfigValue("foreground_notification_icon")
    if (configured.isEmpty()) return android.R.drawable.stat_notify_sync
    if (configured.length > 160 || !Regex("@drawable/[a-z][a-z0-9_]*").matches(configured)) return 0
    return context.resources.getIdentifier(configured.removePrefix("@drawable/"), "drawable", context.packageName)
  }

  private fun notifications (context: Context): NotificationManager = context.getSystemService(NotificationManager::class.java)
  private fun channel (context: Context) {
    notifications(context).createNotificationChannel(NotificationChannel(CHANNEL, if (continuous) "User-started monitoring" else "User-started operations", NotificationManager.IMPORTANCE_LOW))
  }
  private fun canNotify (context: Context): Boolean {
    channel(context)
    return notifications(context).areNotificationsEnabled() &&
      notifications(context).getNotificationChannel(CHANNEL)?.importance != NotificationManager.IMPORTANCE_NONE &&
      (Build.VERSION.SDK_INT < 33 || context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED)
  }

  private fun identifier (value: String): String {
    require(Regex("[a-zA-Z0-9_-]{1,128}").matches(value))
    return value
  }
  private fun json (task: ForegroundTask): JSONObject = JSONObject().put("id", task.id).put("kind", task.kind)
    .put("authorityRef", task.authorityRef).put("accountRef", task.accountRef).put("targetRef", task.targetRef)
    .put("operationRef", task.operationRef)
  private fun public (record: JSONObject): JSONObject = JSONObject().put("id", record.getString("id"))
    .put("kind", record.getString("kind")).put("generation", record.getString("generation"))
    .put("phase", record.getString("phase")).put("outcome", record.optString("outcome", "unknown"))
    .put("updatedAt", record.getLong("updatedAt")).also {
      if (!continuous) it.put("durationMs", record.getLong("durationMs"))
    }

  private fun <T> journal (context: Context, action: (JSONObject) -> T): T {
    val store = NativeRecords.runtime(context, if (continuous) "continuous" else "foreground")
    repeat(4) {
      val snapshot = store.read()
      val bytes = snapshot.records["state"]
      val original = try { bytes?.toString(Charsets.UTF_8) ?: "{}" } finally { bytes?.fill(0) }
      val state = JSONObject(original)
      val result = action(state)
      val encoded = state.toString()
      if (encoded == original) return result
      try {
        val updated = encoded.toByteArray(Charsets.UTF_8)
        try { store.apply(snapshot.revision, mapOf("state" to updated)) } finally { updated.fill(0) }
        return result
      } catch (error: NativeStateException) { if (error.code != "ESTALE") throw error }
    }
    throw NativeStateException("EBUSY")
  }

  private fun refresh (state: JSONObject) {
    for (id in state.keys()) {
      val record = state.getJSONObject(id)
      val entry = active
      val stopping = entry?.generation == record.getString("generation") && entry.stopping.get()
      if (record.getString("phase") in setOf("starting", "running") &&
          (record.getString("owner") != process || entry?.generation != record.getString("generation") || stopping)) {
        val rejected = stopping && entry?.stopPhase == "rejected"
        record.put("phase", if (rejected) "rejected" else "unknown")
          .put("outcome", if (rejected) "not-started" else "unknown").put("updatedAt", System.currentTimeMillis())
      }
    }
  }

  /** Blocking native journal query; never restarts an interrupted operation. */
  fun status (context: Context, id: String): JSONObject = journal(context) { state ->
    identifier(id); refresh(state)
    public(state.optJSONObject(id) ?: throw NativeStateException("ENOENT"))
  }

  private fun checkActivity (activity: AppActivity) {
    if (!activity.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED) || !activity.hasWindowFocus() ||
        activity.isFinishing || activity.isDestroyed) throw NativeStateException("ENOTREADY")
  }
  private fun code (error: Exception): String = when (error) {
    is NativeStateException -> error.code
    is PlatformFailure -> error.code
    is SecurityException -> "EACCES"
    is IllegalArgumentException -> "EINVAL"
    is java.util.concurrent.RejectedExecutionException -> "EBUSY"
    else -> "EIO"
  }

  /** Native UI entry. Completion reports durable admission, not completion of the external operation. */
  fun start (activity: AppActivity, task: ForegroundTask, durationMs: Long, userInitiated: Boolean,
    completion: (JSONObject?, String?) -> Unit) = start(activity, task, durationMs, userInitiated, {}, completion)

  private fun start (activity: AppActivity, task: ForegroundTask, duration: Long, userInitiated: Boolean,
    revalidate: () -> Unit, completion: (JSONObject?, String?) -> Unit) {
    check(Looper.myLooper() == Looper.getMainLooper())
    var entry: Execution? = null
    try {
      checkActivity(activity)
      revalidate()
      if (!available(activity)) throw NativeStateException("ENOSYS")
      if (!canNotify(activity)) throw NativeStateException("EACCES")
      require(userInitiated && if (continuous) duration == 0L else duration in 1000..MAX_DURATION)
      for (value in listOf(task.id, task.authorityRef, task.accountRef, task.targetRef, task.operationRef)) identifier(value)
      val selected = synchronized(this) { executors[task.kind] } ?: throw NativeStateException("ENOTSUP")
      entry = synchronized(this) {
        if (active != null) throw NativeStateException("EBUSY")
        Execution(task, duration, selected).also { active = it }
      }
      val reserved = entry
      control.execute {
        try {
          val record = journal(activity) { state ->
            refresh(state)
            if (state.has(task.id)) throw NativeStateException("EEXIST")
            if (state.length() >= 16) throw NativeStateException("ENOSPC")
            json(task).put("generation", reserved.generation).put("owner", process).put("phase", "starting")
              .put("durationMs", duration).put("updatedAt", System.currentTimeMillis()).also { state.put(task.id, it) }
          }
          main.post {
            val error = try {
              checkActivity(activity); revalidate()
              if (!canNotify(activity) || reserved.stopping.get()) throw NativeStateException("EACCES")
              activity.startForegroundService(Intent(activity, serviceType()).setAction(START)
                .putExtra("generation", reserved.generation))
              reserved.launched.set(true)
              main.postDelayed({ if (!reserved.entered.get() && !reserved.finished.get()) stopEntry(activity, reserved, "rejected") }, 10000)
              null
            } catch (error: Exception) {
              stopEntry(activity, reserved, if (reserved.launched.get()) "unknown" else "rejected")
              code(error)
            }
            // A consumer callback cannot turn an admitted operation into a rejected start.
            completion(if (error == null) public(record) else null, error)
          }
        } catch (error: Exception) {
          synchronized(this) { if (active === reserved) active = null }
          main.post { completion(null, code(error)) }
        }
      }
    } catch (error: Exception) {
      synchronized(this) { if (active === entry) active = null }
      completion(null, code(error))
    }
  }

  /** A stop after admission is uncertain until the application reconciles the remote outcome. */
  fun stop (context: Context, id: String, generation: String): JSONObject {
    identifier(id)
    val result = journal(context) { state ->
      refresh(state)
      val record = state.optJSONObject(id) ?: throw NativeStateException("ENOENT")
      if (record.getString("generation") != generation) throw NativeStateException("ESTALE")
      if (record.getString("phase") in setOf("starting", "running")) {
        record.put("phase", "unknown").put("outcome", "unknown").put("updatedAt", System.currentTimeMillis())
      }
      public(record)
    }
    val entry = active
    if (entry?.task?.id == id && entry.generation == generation) stopEntry(context, entry, "unknown")
    return result
  }

  /** Blocking native list; current records are redacted and interrupted work is never replayed. */
  fun list (context: Context): JSONArray = journal(context) { state ->
    refresh(state)
    JSONArray(state.keys().asSequence().toList().sorted().map { public(state.getJSONObject(it)) })
  }

  /** Release only after the terminal outcome is reconciled and all resource cleanup finishes. */
  fun release (context: Context, id: String, generation: String) {
    identifier(id)
    journal(context) { state ->
      refresh(state)
      val record = state.optJSONObject(id) ?: throw NativeStateException("ENOENT")
      if (record.getString("generation") != generation) throw NativeStateException("ESTALE")
      if (record.getString("phase") in setOf("starting", "running") || active?.task?.id == id) throw NativeStateException("EBUSY")
      state.remove(id)
    }
  }

  internal fun request (call: PlatformCall, action: String, options: JSONObject) {
    if (!continuous && action.startsWith("continuous.")) {
      ContinuousTasks.request(call, action.removePrefix("continuous."), options)
      return
    }
    call.document()
    if (action == "support") {
      require(options.length() == 0)
      call.success(JSONObject().put("available", available(call.app)).put("canStart", available(call.app) &&
        synchronized(this) { executors.isNotEmpty() } && canNotify(call.app))
        .put("registeredKinds", JSONArray(synchronized(this) { executors.keys.sorted() })).put("maximumDurationMs", if (continuous) JSONObject.NULL else MAX_DURATION)
        .put("maximumTasks", 16).put("maximumConcurrentExecutors", 1).put("automaticRestart", false).put("backgroundJavaScript", false))
      return
    }
    if (!available(call.app)) throw PlatformFailure("ENOSYS")
    if (action == "start") {
      val allowed = setOf("id", "kind", "authorityRef", "accountRef", "targetRef", "operationRef", "userInitiated") +
        if (continuous) emptySet() else setOf("durationMs")
      require(options.keys().asSequence().all { it in allowed })
      val duration = if (continuous) 0L else options.opt("durationMs") ?: 900000L
      require(duration is Number && duration.toDouble() == duration.toLong().toDouble() && options.opt("userInitiated") == true)
      val activity = call.foreground().activity as? AppActivity ?: throw PlatformFailure("ENOTREADY")
      start(activity, ForegroundTask(options.getString("id"), options.getString("kind"), options.getString("authorityRef"),
        options.getString("accountRef"), options.getString("targetRef"), options.getString("operationRef")), duration.toLong(), true,
        { if (call.done.get()) throw NativeStateException("ABORT_ERR"); call.foreground() },
        { result, error -> if (error != null) call.failure(error) else call.success(result!!) })
      return
    }
    require(action in setOf("status", "list", "stop", "release"))
    require(options.keys().asSequence().all { it in if (action in setOf("stop", "release")) setOf("id", "generation") else setOf("id") })
    if (action == "list") require(options.length() == 0)
    try {
      control.execute {
        try {
          val result: Any = when (action) {
            "status" -> status(call.app, options.getString("id"))
            "stop" -> stop(call.app, options.getString("id"), options.getString("generation"))
            "release" -> { release(call.app, options.getString("id"), options.getString("generation")); JSONObject() }
            else -> list(call.app)
          }
          main.post { try { call.document(); call.success(result) } catch (error: Exception) { call.failure(code(error)) } }
        } catch (error: Exception) { call.failure(code(error)) }
      }
    } catch (_: java.util.concurrent.RejectedExecutionException) { call.failure("EBUSY") }
  }

  internal fun enter (service: RuntimeForegroundService, generation: String) {
    val entry = active
    if (entry?.generation == generation && entry.service === service && entry.entered.get()) return
    if (entry == null || entry.generation != generation || entry.stopping.get() || !entry.entered.compareAndSet(false, true)) {
      service.stopForeground(Service.STOP_FOREGROUND_REMOVE); service.stopSelf(); return
    }
    entry.serviceStopped.set(false)
    entry.service = service
    entry.expires = if (continuous) Long.MAX_VALUE else SystemClock.elapsedRealtime() + entry.duration
    try {
      control.execute {
        try {
          journal(service) { state ->
            val record = state.optJSONObject(entry.task.id) ?: throw NativeStateException("ENOENT")
            if (record.getString("generation") != generation || record.getString("phase") != "starting" || entry.stopping.get()) {
              throw NativeStateException("ESTALE")
            }
            record.put("phase", "running").put("updatedAt", System.currentTimeMillis())
          }
          worker.execute {
            var outcome = "unknown"
            try {
              entry.signal.throwIfCanceled()
              val authorized = entry.executor.authorize(service.applicationContext, entry.task, entry.signal)
              if (authorized == null) outcome = "unauthorized" else authorized.use {
                entry.signal.throwIfCanceled()
                outcome = when (it.execute(entry.signal)) {
                  ForegroundOutcome.COMPLETED -> "completed"
                  ForegroundOutcome.UNAUTHORIZED -> "unauthorized"
                  ForegroundOutcome.UNKNOWN -> "unknown"
                  ForegroundOutcome.FAILED -> "failed"
                }
              }
            } catch (_: Exception) { outcome = "unknown" }
            finally { finish(service, entry, if (entry.stopping.get() || SystemClock.elapsedRealtime() >= entry.expires) "unknown" else outcome) }
          }
        } catch (_: Exception) { stopEntry(service, entry, "unknown"); finish(service, entry, "unknown") }
      }
    } catch (_: Exception) { stopEntry(service, entry, "unknown"); finish(service, entry, "unknown") }
    val deadline = object : Runnable {
      override fun run () {
        if (entry.finished.get() || entry.stopping.get()) return
        if (SystemClock.elapsedRealtime() >= entry.expires || !runCatching { available(service) && canNotify(service) }.getOrDefault(false)) {
          stopEntry(service, entry, "unknown")
        }
        else main.postDelayed(this, 1000)
      }
    }
    main.post(deadline)
  }

  private fun finish (context: Context, entry: Execution, outcome: String) {
    try {
      control.execute {
        try {
          journal(context) { state ->
            val record = state.optJSONObject(entry.task.id)
            if (record?.optString("generation") == entry.generation && record.getString("phase") in setOf("starting", "running")) {
              val phase = if (entry.stopping.get()) "unknown" else outcome
              record.put("phase", phase).put("outcome", phase).put("updatedAt", System.currentTimeMillis())
            }
          }
        } catch (_: Exception) {
          // Keep the durable outcome unresolved; inspection fences an absent/stopped executor.
        } finally {
          entry.finished.set(true)
          releaseExecution(entry)
          main.post { entry.service?.let { it.stopForeground(Service.STOP_FOREGROUND_REMOVE); it.stopSelf() } }
        }
      }
    } catch (_: Exception) {
      entry.finished.set(true)
      releaseExecution(entry)
      main.post { entry.service?.let { it.stopForeground(Service.STOP_FOREGROUND_REMOVE); it.stopSelf() } }
    }
  }

  @Synchronized
  private fun stopEntry (context: Context, entry: Execution, phase: String) {
    if (entry.finished.get() || entry.stopping.get()) return
    entry.stopPhase = phase
    entry.stopping.set(true)
    entry.cancellationFinished.set(false)
    main.post { entry.service?.let { it.stopForeground(Service.STOP_FOREGROUND_REMOVE); it.stopSelf() } }
    try {
      canceller.execute {
        try { entry.signal.cancel() } catch (_: Exception) {
          // The outcome stays unknown; a native cancellation callback cannot crash this worker.
        } finally { entry.cancellationFinished.set(true); releaseExecution(entry) }
      }
    } catch (_: Exception) { /* Keep the occupied executor quarantined; never overlap a replacement. */ }
    try {
      control.execute {
        try {
          journal(context) { state ->
            val record = state.optJSONObject(entry.task.id)
            if (record?.optString("generation") == entry.generation && record.getString("phase") in setOf("starting", "running")) {
              record.put("phase", phase).put("outcome", if (phase == "rejected") "not-started" else "unknown")
                .put("updatedAt", System.currentTimeMillis())
            }
          }
        } catch (_: Exception) {
          // Inspection retains uncertainty if persisting stop fails.
        } finally {
          if (!entry.entered.get()) {
            entry.finished.set(true)
            releaseExecution(entry)
          }
        }
      }
    } catch (_: Exception) {
      if (!entry.entered.get()) { entry.finished.set(true); releaseExecution(entry) }
    }
  }

  private fun releaseExecution (entry: Execution) {
    if (entry.finished.get() && entry.cancellationFinished.get() && entry.serviceStopped.get()) {
      synchronized(this) { if (active === entry) active = null }
    }
  }

  internal fun destroyed (service: RuntimeForegroundService) {
    val entry = active
    if (entry?.service === service) {
      entry.serviceStopped.set(true)
      releaseExecution(entry)
    }
  }

  internal fun accepts (generation: String): Boolean = active?.let { it.generation == generation && !it.stopping.get() } == true
  internal fun owns (service: RuntimeForegroundService): Boolean = active?.service === service

  internal fun stopService (service: RuntimeForegroundService, generation: String? = null): Boolean {
    val entry = active
    if (entry?.service === service && (generation == null || generation == entry.generation)) {
      stopEntry(service, entry, "unknown")
      return true
    }
    return false
  }

  internal fun notification (context: Context, generation: String): Notification {
    channel(context)
    val stop = PendingIntent.getService(context, 0, Intent(context, serviceType())
      .setAction(STOP).setData(android.net.Uri.parse("oro-foreground://stop/$generation")).putExtra("generation", generation),
      PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    val builder = Notification.Builder(context, CHANNEL).setSmallIcon(icon(context).also { if (it == 0) throw NativeStateException("EINVAL") })
      .setContentTitle(if (continuous) "User-started monitoring" else "User-started operation")
      .setContentText(if (continuous) "Monitoring is running. Tap Stop to end it." else "An operation is running. Tap Stop to end it.")
      .setOngoing(true).setOnlyAlertOnce(true).setVisibility(if (continuous) Notification.VISIBILITY_PUBLIC else Notification.VISIBILITY_PRIVATE)
      .addAction(Notification.Action.Builder(null, "Stop", stop).build())
    context.packageManager.getLaunchIntentForPackage(context.packageName)?.let {
      builder.setContentIntent(PendingIntent.getActivity(context, 0, it, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT))
    }
    return builder.build()
  }
}

/** Finite user-started dataSync execution. No boot launch, automatic restart or mutation replay. */
object ForegroundTasks : ForegroundController(false)

/** Continuous user-started specialUse execution. Explicit admission is required after process loss. */
object ContinuousTasks : ForegroundController(true) {
  fun start (activity: AppActivity, task: ForegroundTask, userInitiated: Boolean,
    completion: (JSONObject?, String?) -> Unit) = super.start(activity, task, 0, userInitiated, completion)
}

/** Non-exported default-process service. Android owns admission and dataSync time limits. */
open class RuntimeForegroundService : Service() {
  internal open val tasks: ForegroundController get() = ForegroundTasks
  override fun onBind (intent: Intent?): IBinder? = null
  override fun onStartCommand (intent: Intent?, flags: Int, startId: Int): Int {
    if (intent?.action == tasks.STOP) {
      val generation = intent.getStringExtra("generation")
      if ((generation != null && tasks.stopService(this, generation)) || !tasks.owns(this)) stopSelf(startId)
      return START_NOT_STICKY
    }
    if (intent?.action != tasks.START || !tasks.available(this)) { stopSelf(); return START_NOT_STICKY }
    val generation = intent.getStringExtra("generation") ?: run { stopSelf(); return START_NOT_STICKY }
    if (!tasks.accepts(generation)) {
      if (!tasks.owns(this)) stopSelf(startId)
      return START_NOT_STICKY
    }
    try {
      val notification = tasks.notification(this, generation)
      if (tasks.continuous && Build.VERSION.SDK_INT >= 34) {
        startForeground(tasks.NOTIFICATION, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
      } else if (!tasks.continuous && Build.VERSION.SDK_INT >= 29) {
        startForeground(tasks.NOTIFICATION, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
      } else startForeground(tasks.NOTIFICATION, notification)
      tasks.enter(this, generation)
    } catch (_: Exception) { tasks.stopService(this); stopForeground(STOP_FOREGROUND_REMOVE); stopSelf() }
    return START_NOT_STICKY
  }
  override fun onTimeout (startId: Int, fgsType: Int) {
    tasks.stopService(this)
    stopForeground(STOP_FOREGROUND_REMOVE); stopSelf()
  }
  override fun onTaskRemoved (rootIntent: Intent?) {
    // Continuous monitoring remains user-visible after removing the app task.
    if (tasks.continuous) return
    tasks.stopService(this)
    stopForeground(STOP_FOREGROUND_REMOVE); stopSelf()
  }
  override fun onDestroy () {
    tasks.stopService(this)
    tasks.destroyed(this)
    super.onDestroy()
  }
}

/** Continuous service has its own notification, journal and executor slot, with no sticky restart. */
class RuntimeContinuousService : RuntimeForegroundService() {
  internal override val tasks: ForegroundController get() = ContinuousTasks
}
