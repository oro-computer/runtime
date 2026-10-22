package oro.runtime.app

import android.app.job.JobInfo
import android.app.job.JobParameters
import android.app.job.JobScheduler
import android.app.job.JobService
import android.content.ComponentName
import android.content.Context
import android.os.Build
import android.os.CancellationSignal
import android.os.Handler
import android.os.Looper
import android.os.PersistableBundle
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.SynchronousQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Persisted identities only. Credential material and request bodies are excluded. */
data class DeferredReadTask (
  val id: String,
  val kind: String,
  val authorityRef: String,
  val accountRef: String,
  val targetRef: String,
  val operationRef: String
)

/** Fixed outcomes from an authorized read/reconciliation, never mutation replay. */
enum class DeferredReadOutcome { COMPLETED, RETRY, CONFLICT, UNAUTHORIZED, UNKNOWN }

/** In-memory authorization. Close must release credentials/connections after use. */
interface AuthorizedDeferredRead : AutoCloseable {
  fun reconcile (cancellation: CancellationSignal): DeferredReadOutcome
}

/** Native app code must resolve references and recheck current server IAM here. */
fun interface DeferredReadExecutor {
  fun authorize (context: Context, task: DeferredReadTask, cancellation: CancellationSignal): AuthorizedDeferredRead?
}

/** Register compiled native executors from Application.onCreate, including cold service starts. */
object DeferredSync {
  private const val STORE = "oro.deferred.reads.v1"
  private const val JOB_BASE = 0x4f530000
  private const val MAXIMUM = 32
  private val executors = mutableMapOf<String, DeferredReadExecutor>()
  private val terminal = setOf("completed", "cancelled", "conflict", "unauthorized", "unknown", "failed", "rejected", "unavailable")

  // @JvmStatic @Synchronized locks the Java class, unlike instance methods.
  // Journal changes and service hand-offs all use the Kotlin object monitor.
  internal fun <T> locked (action: () -> T): T = synchronized(this, action)

  @Synchronized
  fun registerReadExecutor (kind: String, executor: DeferredReadExecutor) {
    require(Regex("[a-z][a-z0-9.-]{0,63}").matches(kind))
    require(!executors.containsKey(kind) && executors.size < 16)
    executors[kind] = executor
  }

  private fun scheduler (context: Context): JobScheduler {
    val manager = context.getSystemService(JobScheduler::class.java)
    return if (Build.VERSION.SDK_INT >= 34) manager.forNamespace("oro.runtime.deferred") else manager
  }
  private fun read (context: Context): JSONArray = JSONArray(context.getSharedPreferences(STORE, Context.MODE_PRIVATE)
    .getString("records", "[]") ?: "[]")
  private fun save (context: Context, rows: JSONArray) {
    check(context.getSharedPreferences(STORE, Context.MODE_PRIVATE).edit().putString("records", rows.toString()).commit())
  }
  private fun find (rows: JSONArray, id: String): JSONObject? =
    (0 until rows.length()).map { rows.getJSONObject(it) }.firstOrNull { it.optString("id") == id }
  private fun identifier (value: String): String {
    require(Regex("[a-zA-Z0-9_-]{1,128}").matches(value))
    return value
  }
  private fun task (record: JSONObject) = DeferredReadTask(record.getString("id"), record.getString("kind"),
    record.getString("authorityRef"), record.getString("accountRef"), record.getString("targetRef"), record.getString("operationRef"))

  private fun job (context: Context, record: JSONObject, delay: Long): JobInfo {
    val extras = PersistableBundle().apply {
      putString("id", record.getString("id")); putString("generation", record.getString("generation"))
    }
    return JobInfo.Builder(record.getInt("jobId"), ComponentName(context, RuntimeDeferredJob::class.java))
      .setPersisted(true).setMinimumLatency(delay)
      .setRequiredNetworkType(if (record.getString("network") == "unmetered") JobInfo.NETWORK_TYPE_UNMETERED else JobInfo.NETWORK_TYPE_ANY)
      .setRequiresCharging(record.getBoolean("requiresCharging")).setRequiresBatteryNotLow(record.getBoolean("requiresBatteryNotLow"))
      .setBackoffCriteria(30000, JobInfo.BACKOFF_POLICY_EXPONENTIAL).setExtras(extras).build()
  }

  fun available (context: Context): Boolean {
    if (App.getInstance().getUserConfigValue("background_deferred_enabled") != "true" ||
        context.checkSelfPermission("android.permission.RECEIVE_BOOT_COMPLETED") != android.content.pm.PackageManager.PERMISSION_GRANTED) return false
    return try {
      @Suppress("DEPRECATION")
      val info = context.packageManager.getServiceInfo(ComponentName(context, RuntimeDeferredJob::class.java), 0)
      info.enabled && info.permission == "android.permission.BIND_JOB_SERVICE"
    } catch (_: Exception) { false }
  }

  internal fun request (call: PlatformCall, action: String, options: JSONObject) {
    call.success(request(call.app, action, options))
  }

  /** Shared durable read operations for compiled receivers/executors, without an Activity or WebView. */
  @JvmStatic
  fun request (context: Context, action: String, options: JSONObject = JSONObject()): Any = synchronized(this) {
    if (action !in setOf("support", "list", "enqueue", "status", "resume", "cancel", "release")) {
      throw PlatformFailure("EINVAL")
    }
    if (action in setOf("support", "list")) require(options.length() == 0)
    else if (action != "enqueue") require(options.keys().asSequence().all { it == "id" })
    if (action == "support") {
      return JSONObject().put("available", available(context)).put("registeredKinds", JSONArray(executors.keys.sorted()))
        .put("maximumTasks", MAXIMUM).put("maximumAttempts", 5).put("readOnly", true)
        .put("queuedWrites", false).put("backgroundJavaScript", false).put("survivesForceStop", false)
    }
    if (!available(context)) throw PlatformFailure("ENOSYS")
    val rows = read(context)
    if (action == "list") {
      for (index in 0 until rows.length()) refresh(context, rows.getJSONObject(index))
      return rows
    }
    val id = identifier(options.getString("id"))
    val existing = find(rows, id)
    if (action == "enqueue") {
      require(options.keys().asSequence().all { it in listOf("id", "kind", "authorityRef", "accountRef", "targetRef",
        "operationRef", "network", "requiresCharging", "requiresBatteryNotLow", "delayMs") })
      val kind = options.getString("kind")
      if (!executors.containsKey(kind)) throw PlatformFailure("ENOTSUP")
      for (field in listOf("authorityRef", "accountRef", "targetRef", "operationRef")) identifier(options.getString(field))
      val network = options.optString("network", "unmetered")
      require(network in listOf("any", "unmetered"))
      val delay = options.opt("delayMs") ?: 0L
      require(delay is Number && delay.toDouble() == delay.toLong().toDouble() && delay.toLong() in 0..604800000L)
      for (field in listOf("requiresCharging", "requiresBatteryNotLow")) require(!options.has(field) || options.opt(field) is Boolean)
      val normalized = JSONObject(options.toString()).put("network", network).put("delayMs", delay.toLong())
        .put("requiresCharging", options.optBoolean("requiresCharging", false))
        .put("requiresBatteryNotLow", options.optBoolean("requiresBatteryNotLow", true))
      if (existing != null) {
        for (field in listOf("kind", "authorityRef", "accountRef", "targetRef", "operationRef", "network",
            "delayMs", "requiresCharging", "requiresBatteryNotLow")) {
          val matches = if (field == "delayMs") existing.getLong(field) == normalized.getLong(field)
            else existing.opt(field) == normalized.opt(field)
          if (!matches) throw PlatformFailure("EEXIST")
        }
        refresh(context, existing)
        return existing
      }
      if (rows.length() >= MAXIMUM) throw PlatformFailure("ENOSPC")
      val used = (0 until rows.length()).map { rows.getJSONObject(it).getInt("jobId") }.toSet()
      val jobId = (JOB_BASE until JOB_BASE + MAXIMUM).first { it !in used }
      val generation = UUID.randomUUID().toString()
      val record = normalized.put("schema", 1).put("generation", generation).put("jobId", jobId)
        .put("phase", "scheduling").put("attempts", 0).put("updatedAt", System.currentTimeMillis())
      val job = job(context, record, delay.toLong())
      rows.put(record)
      save(context, rows)
      try {
        if (scheduler(context).schedule(job) != JobScheduler.RESULT_SUCCESS) {
          record.put("phase", "rejected")
          save(context, rows)
          throw PlatformFailure("EAGAIN")
        }
        record.put("phase", "scheduled")
        save(context, rows)
      } catch (error: Exception) { scheduler(context).cancel(jobId); throw error }
      return record
    }
    val record = existing ?: throw PlatformFailure("ENOENT")
    when (action) {
      "status" -> refresh(context, record)
      "resume" -> {
        refresh(context, record)
        if (record.optString("phase") !in setOf("interrupted", "unavailable") ||
            record.optInt("attempts") >= 5) throw PlatformFailure("EAGAIN")
        if (!executors.containsKey(record.getString("kind"))) throw PlatformFailure("ENOTSUP")
        record.put("phase", "scheduling").put("generation", UUID.randomUUID().toString())
          .put("updatedAt", System.currentTimeMillis())
        save(context, rows)
        try {
          if (scheduler(context).schedule(job(context, record, 0)) != JobScheduler.RESULT_SUCCESS) {
            record.put("phase", "rejected")
            save(context, rows)
            throw PlatformFailure("EAGAIN")
          }
          record.put("phase", "scheduled")
          save(context, rows)
        } catch (error: Exception) { scheduler(context).cancel(record.getInt("jobId")); throw error }
      }
      "cancel", "release" -> {
        record.put("phase", "cancelled").put("updatedAt", System.currentTimeMillis())
        if (action == "release") {
          for (index in 0 until rows.length()) if (rows.getJSONObject(index).getString("id") == id) { rows.remove(index); break }
        }
        try {
          save(context, rows)
        } finally {
          RuntimeDeferredJob.cancel(record.getString("generation"))
          scheduler(context).cancel(record.getInt("jobId"))
        }
      }
      else -> throw PlatformFailure("EINVAL")
    }
    return record
  }

  private fun refresh (context: Context, record: JSONObject) {
    if (record.optString("phase") !in terminal && !RuntimeDeferredJob.running(record.optString("generation")) &&
        scheduler(context).getPendingJob(record.getInt("jobId")) == null) {
      record.put("phase", "interrupted")
    }
  }

  @Synchronized
  internal fun begin (context: Context, parameters: JobParameters): Pair<DeferredReadTask, DeferredReadExecutor>? {
    if (!available(context)) return null
    val rows = read(context)
    val record = find(rows, parameters.extras.getString("id") ?: "") ?: return null
    if (record.optString("generation") != parameters.extras.getString("generation") || record.optString("phase") in terminal ||
        record.optInt("attempts") >= 5) return null
    val executor = executors[record.getString("kind")]
    if (executor == null) {
      record.put("phase", "unavailable").put("updatedAt", System.currentTimeMillis())
      save(context, rows)
      return null
    }
    record.put("phase", "running").put("attempts", record.optInt("attempts") + 1).put("updatedAt", System.currentTimeMillis())
    save(context, rows)
    return task(record) to executor
  }

  @Synchronized
  internal fun finish (context: Context, parameters: JobParameters, phase: String): Boolean {
    val rows = read(context)
    val record = find(rows, parameters.extras.getString("id") ?: "") ?: return false
    if (record.optString("generation") != parameters.extras.getString("generation") || record.optString("phase") != "running") return false
    val retry = phase == "retry" && record.optInt("attempts") < 5
    record.put("phase", if (phase == "retry" && !retry) "failed" else phase).put("updatedAt", System.currentTimeMillis())
    save(context, rows)
    return retry
  }

  @Synchronized
  internal fun shouldRetry (context: Context, parameters: JobParameters): Boolean {
    val record = find(read(context), parameters.extras.getString("id") ?: "") ?: return false
    return record.optString("generation") == parameters.extras.getString("generation") &&
      record.optString("phase") == "retry" && record.optInt("attempts") < 5
  }
}

/** Bounded native execution. No JS closures, serialized credentials or write replay. */
class RuntimeDeferredJob : JobService() {
  private class Run (val parameters: JobParameters, val owner: RuntimeDeferredJob) {
    val signal = CancellationSignal()
    val done = AtomicBoolean(false)
    var deadline: java.util.concurrent.ScheduledFuture<*>? = null
  }
  companion object {
    private val worker = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>(),
      { task -> Thread(task, "oro-deferred-read").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
    private val cancellations = ThreadPoolExecutor(0, 2, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>(),
      { task -> Thread(task, "oro-deferred-cancel").apply { isDaemon = true } })
    private val timer = Executors.newSingleThreadScheduledExecutor { task -> Thread(task, "oro-deferred-deadlines").apply { isDaemon = true } }
    private val active = mutableMapOf<String, Run>()
    fun running (generation: String): Boolean = synchronized(active) { active.containsKey(generation) }
    fun cancel (generation: String) = DeferredSync.locked {
      val run = synchronized(active) { active.remove(generation) } ?: return@locked
      if (run.owner.generations[generation] === run) run.owner.generations.remove(generation)
      run.done.set(true)
      run.deadline?.cancel(false)
      try { cancellations.execute { run.signal.cancel() } } catch (_: java.util.concurrent.RejectedExecutionException) {}
    }
  }
  private val main = Handler(Looper.getMainLooper())
  private val generations = mutableMapOf<String, Run>()

  override fun onStartJob (parameters: JobParameters): Boolean = DeferredSync.locked start@{
    val generation = parameters.extras.getString("generation") ?: return@start false
    if (running(generation)) return@start false
    val run = Run(parameters, this)
    val execution = try { DeferredSync.begin(this, parameters) } catch (_: Exception) { null } ?: return@start false
    synchronized(active) { active[generation] = run }
    generations[generation] = run
    fun finish (phase: String) {
      DeferredSync.locked complete@{
        if (!run.done.compareAndSet(false, true)) return@complete
        run.deadline?.cancel(false)
        val retry = try { DeferredSync.finish(this, parameters, phase) } catch (_: Exception) { false }
        main.post {
          DeferredSync.locked delivery@{
            if (generations[generation] !== run) return@delivery
            generations.remove(generation)
            // Cancellation may have removed this run while the callback was queued.
            if (synchronized(active) { active[generation] !== run }) return@delivery
            synchronized(active) { active.remove(generation) }
            val reschedule = retry && try { DeferredSync.shouldRetry(this, parameters) } catch (_: Exception) { false }
            jobFinished(parameters, reschedule)
          }
        }
      }
    }
    run.deadline = timer.schedule({
      finish("retry")
      try { cancellations.execute { run.signal.cancel() } } catch (_: java.util.concurrent.RejectedExecutionException) {}
    }, 60, TimeUnit.SECONDS)
    try {
      worker.execute {
        val phase = try {
          run.signal.throwIfCanceled()
          val authorized = execution.second.authorize(applicationContext, execution.first, run.signal)
          if (authorized == null) "unauthorized" else authorized.use {
            run.signal.throwIfCanceled()
            when (it.reconcile(run.signal)) {
              DeferredReadOutcome.COMPLETED -> "completed"
              DeferredReadOutcome.RETRY -> "retry"
              DeferredReadOutcome.CONFLICT -> "conflict"
              DeferredReadOutcome.UNAUTHORIZED -> "unauthorized"
              DeferredReadOutcome.UNKNOWN -> "unknown"
            }
          }
        } catch (_: java.io.IOException) { "retry"
        } catch (_: android.os.OperationCanceledException) { "retry"
        } catch (_: SecurityException) { "unauthorized"
        } catch (_: Exception) { "failed" }
        finish(phase)
      }
    } catch (_: java.util.concurrent.RejectedExecutionException) { finish("retry") }
    true
  }

  override fun onStopJob (parameters: JobParameters): Boolean = DeferredSync.locked {
    val generation = parameters.extras.getString("generation") ?: return@locked false
    val run = generations[generation] ?: return@locked false
    if (run.parameters !== parameters) return@locked false
    cancel(generation)
    generations.remove(generation)
    try {
      DeferredSync.finish(this, parameters, "retry") || DeferredSync.shouldRetry(this, parameters)
    } catch (_: Exception) { false }
  }
  override fun onDestroy () {
    DeferredSync.locked {
      generations.keys.toList().forEach { cancel(it) }
      generations.clear()
    }
    super.onDestroy()
  }
}
