package oro.runtime.app

import android.app.job.JobInfo
import android.app.job.JobParameters
import android.app.job.JobScheduler
import android.app.job.JobService
import android.content.ComponentName
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.PersistableBundle
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.SynchronousQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import oro.runtime.window.ShareFiles

/** Only native expiry cleanup is supported; no serialized credentials or JavaScript entry points. */
internal object BackgroundTasks {
  private const val JOB_ID = 0x4f524f01
  private const val STORE = "oro.background.tasks"
  private const val KIND = "share-prune"
  private fun scheduler (context: Context): JobScheduler {
    val service = context.getSystemService(Context.JOB_SCHEDULER_SERVICE) as JobScheduler
    return if (android.os.Build.VERSION.SDK_INT >= 34) service.forNamespace("oro.runtime.background") else service
  }
  private fun preferences (context: Context) = context.getSharedPreferences(STORE, Context.MODE_PRIVATE)
  private fun read (context: Context): JSONObject = JSONObject(preferences(context).getString("state", "{}") ?: "{}")
  private fun save (context: Context, state: JSONObject) {
    check(preferences(context).edit().putString("state", state.toString()).commit()) { "Background state write failed" }
  }
  fun available (context: Context): Boolean {
    if (App.getInstance().getUserConfigValue("background_enabled") != "true" ||
        context.checkSelfPermission("android.permission.RECEIVE_BOOT_COMPLETED") != android.content.pm.PackageManager.PERMISSION_GRANTED) return false
    return try {
      @Suppress("DEPRECATION")
      val info = context.packageManager.getServiceInfo(ComponentName(context, RuntimeBackgroundJob::class.java), 0)
      info.enabled && info.permission == "android.permission.BIND_JOB_SERVICE"
    } catch (_: Exception) { false }
  }

  @Synchronized
  fun request (context: Context, action: String, source: String): String {
    fun failure (code: String, message: String) = JSONObject().put("err", JSONObject().put("code", code).put("message", message)).toString()
    return try {
      require(source.length <= 4096)
      val options = JSONObject(source)
      require(options.optString("kind", KIND) == KIND)
      val available = available(context)
      if (action == "status") {
        val state = read(context)
        val pending = scheduler(context).getPendingJob(JOB_ID) != null
        val phase = state.optString("phase", "idle")
        if (!pending && phase in listOf("scheduling", "scheduled", "running", "stopped")) state.put("phase", "interrupted")
        return JSONObject().put("data", JSONObject().put("available", available).put("kind", KIND)
          .put("supportedKinds", JSONArray(listOf(KIND))).put("pending", pending)
          .put("state", state).put("backgroundJavaScript", false).put("push", false)).toString()
      }
      if (!available && action != "cancel") return failure("ENOSYS", "Native background cleanup requires explicit configuration and its manifest service")
      when (action) {
        "register" -> return JSONObject().put("data", JSONObject().put("kind", KIND).put("available", true)
          .put("maximumAttempts", 3).put("exactTiming", false).put("survivesForceStop", false)).toString()
        "schedule" -> {
          require(options.keys().asSequence().all { it in listOf("kind", "delayMs", "requiresCharging", "requiresBatteryNotLow") })
          val rawDelay = options.opt("delayMs") ?: 0L
          require(rawDelay is Number && rawDelay.toDouble().isFinite() && rawDelay.toDouble() == rawDelay.toLong().toDouble())
          for (field in listOf("requiresCharging", "requiresBatteryNotLow")) require(!options.has(field) || options.opt(field) is Boolean)
          val delay = rawDelay.toLong()
          require(delay in 0..604800000L)
          val record = JSONObject().put("schema", 1).put("kind", KIND).put("phase", "scheduling")
            .put("generation", java.util.UUID.randomUUID().toString()).put("attempts", 0)
            .put("notBefore", System.currentTimeMillis() + delay).put("updatedAt", System.currentTimeMillis())
          val extras = PersistableBundle().apply { putInt("schema", 1); putString("generation", record.getString("generation")) }
          val job = JobInfo.Builder(JOB_ID, ComponentName(context, RuntimeBackgroundJob::class.java))
            .setPersisted(true).setMinimumLatency(delay).setRequiresCharging(options.optBoolean("requiresCharging", false))
            .setRequiresBatteryNotLow(options.optBoolean("requiresBatteryNotLow", true))
            .setBackoffCriteria(30_000, JobInfo.BACKOFF_POLICY_EXPONENTIAL).setExtras(extras).build()
          save(context, record)
          if (scheduler(context).schedule(job) != JobScheduler.RESULT_SUCCESS) {
            record.put("phase", "rejected")
            save(context, record)
            return failure("EAGAIN", "Android rejected the scheduled cleanup")
          }
          record.put("phase", "scheduled")
          try { save(context, record) } catch (error: Exception) {
            scheduler(context).cancel(JOB_ID)
            throw error
          }
          return JSONObject().put("data", record).toString()
        }
        "cancel" -> {
          scheduler(context).cancel(JOB_ID)
          val record = read(context).put("phase", "cancelled").put("updatedAt", System.currentTimeMillis())
          save(context, record)
          return JSONObject().put("data", record).toString()
        }
        else -> return failure("EINVAL", "Unknown background operation")
      }
    } catch (_: IllegalArgumentException) { failure("ENOTSUP", "Only bounded native share-prune tasks are supported")
    } catch (_: Exception) { failure("EIO", "Native background state is unavailable") }
  }

  @Synchronized
  fun begin (context: Context, parameters: JobParameters): Boolean {
    if (!available(context) || parameters.extras.getInt("schema") != 1) return false
    val state = read(context)
    if (state.optString("generation") != parameters.extras.getString("generation") || state.optInt("attempts") >= 3 ||
        state.optString("phase") in listOf("cancelled", "completed", "failed")) return false
    save(context, state.put("phase", "running").put("attempts", state.optInt("attempts") + 1)
      .put("updatedAt", System.currentTimeMillis()))
    return true
  }

  @Synchronized
  fun finish (context: Context, parameters: JobParameters, phase: String): Boolean {
    val state = read(context)
    if (state.optString("generation") != parameters.extras.getString("generation") || state.optString("phase") == "cancelled") return false
    val retry = phase == "stopped" && state.optInt("attempts") < 3
    save(context, state.put("phase", if (phase == "stopped" && !retry) "failed" else phase)
      .put("updatedAt", System.currentTimeMillis()))
    return retry
  }
}

/** System-bound native worker. Android timing, Doze and force-stop policy remain authoritative. */
class RuntimeBackgroundJob : JobService() {
  companion object {
    private val worker = ThreadPoolExecutor(1, 1, 30, TimeUnit.SECONDS, SynchronousQueue<Runnable>(),
      { task -> Thread(task, "oro-native-cleanup").apply { isDaemon = true } }).apply { allowCoreThreadTimeOut(true) }
  }
  private val signals = mutableMapOf<String, AtomicBoolean>()
  override fun onStartJob (parameters: JobParameters): Boolean {
    val signal = AtomicBoolean(false)
    val generation = parameters.extras.getString("generation") ?: return false
    signals[generation] = signal
    try {
      if (!BackgroundTasks.begin(this, parameters)) {
        signals.remove(generation)
        return false
      }
      worker.execute {
        var phase = "completed"
        try { ShareFiles.prune(this) { signal.get() } } catch (_: Exception) { phase = "stopped" }
        Handler(Looper.getMainLooper()).post {
          signals.remove(generation)
          if (!signal.get()) {
            val retry = try { BackgroundTasks.finish(this, parameters, phase) } catch (_: Exception) { false }
            jobFinished(parameters, retry)
          }
        }
      }
      return true
    } catch (_: Exception) {
      signals.remove(generation)
      val retry = try { BackgroundTasks.finish(this, parameters, "stopped") } catch (_: Exception) { false }
      if (retry) Handler(Looper.getMainLooper()).post { jobFinished(parameters, true) }
      return retry
    }
  }
  override fun onDestroy () {
    signals.values.forEach { it.set(true) }
    signals.clear()
    super.onDestroy()
  }
  override fun onStopJob (parameters: JobParameters): Boolean {
    signals.remove(parameters.extras.getString("generation"))?.set(true)
    return try { BackgroundTasks.finish(this, parameters, "stopped") } catch (_: Exception) { false }
  }
}
