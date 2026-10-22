package android.app.job

import android.content.ComponentName
import android.content.Context
import android.os.PersistableBundle

class JobInfo(val id: Int, val extras: PersistableBundle, val persisted: Boolean, val network: Int) {
  companion object { const val NETWORK_TYPE_ANY = 1; const val NETWORK_TYPE_UNMETERED = 2; const val BACKOFF_POLICY_EXPONENTIAL = 1 }
  class Builder(private val id: Int, component: ComponentName) {
    private var extras = PersistableBundle()
    private var persisted = false
    private var network = 0
    fun setPersisted(value: Boolean): Builder { persisted = value; return this }
    fun setMinimumLatency(value: Long) = this
    fun setRequiredNetworkType(value: Int): Builder { network = value; return this }
    fun setRequiresCharging(value: Boolean) = this
    fun setRequiresBatteryNotLow(value: Boolean) = this
    fun setBackoffCriteria(delay: Long, policy: Int) = this
    fun setExtras(value: PersistableBundle): Builder { extras = value; return this }
    fun build() = JobInfo(id, extras, persisted, network)
  }
}
class JobParameters(val jobId: Int, val extras: PersistableBundle)
class JobScheduler {
  companion object { const val RESULT_SUCCESS = 1; val instance = JobScheduler() }
  val jobs = java.util.concurrent.ConcurrentHashMap<Int, JobInfo>()
  val finishes = java.util.concurrent.CopyOnWriteArrayList<Pair<JobParameters, Boolean>>()
  fun forNamespace(name: String) = this
  fun getPendingJob(id: Int): JobInfo? = jobs[id]
  fun schedule(job: JobInfo): Int { jobs[job.id] = job; return RESULT_SUCCESS }
  fun cancel(id: Int) { jobs.remove(id) }
}
open class JobService : Context() {
  open fun onStartJob(parameters: JobParameters) = false
  open fun onStopJob(parameters: JobParameters) = false
  open fun onDestroy() {}
  fun jobFinished(parameters: JobParameters, retry: Boolean) {
    val scheduler = JobScheduler.instance
    scheduler.finishes.add(parameters to retry)
    if (retry) scheduler.schedule(JobInfo(parameters.jobId, parameters.extras, true, 1))
    else if (scheduler.getPendingJob(parameters.jobId)?.extras?.getString("generation") == parameters.extras.getString("generation")) {
      scheduler.cancel(parameters.jobId)
    }
  }
}
