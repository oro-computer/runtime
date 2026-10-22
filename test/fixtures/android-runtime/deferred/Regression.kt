package oro.runtime.app

import android.app.job.JobParameters
import android.app.job.JobScheduler
import android.content.Context
import android.os.CancellationSignal
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class App : Context() {
  companion object { private val instance = App(); fun getInstance() = instance }
  fun getUserConfigValue(key: String) = if (key == "background_deferred_enabled") "true" else ""
}
internal class PlatformFailure(val code: String) : Exception(code)
internal class PlatformCall(val app: App = App.getInstance()) {
  var result: Any? = null
  fun success(value: Any) { result = value }
}

private fun request(action: String, options: JSONObject = JSONObject()): Any {
  val call = PlatformCall()
  DeferredSync.request(call, action, options)
  return checkNotNull(call.result)
}
private fun options(id: String, kind: String = "public.read") = JSONObject()
  .put("id", id).put("kind", kind).put("authorityRef", "public-server").put("accountRef", "public-account")
  .put("targetRef", "public-target").put("operationRef", "public-operation").put("network", "any")
private fun state(id: String) = request("status", JSONObject().put("id", id)) as JSONObject
private fun parameters(record: JSONObject): JobParameters {
  val job = checkNotNull(JobScheduler.instance.getPendingJob(record.getInt("jobId")))
  check(job.persisted && job.network == 1)
  return JobParameters(job.id, job.extras)
}
private fun awaitPhase(id: String, phase: String) {
  repeat(500) {
    if (state(id).getString("phase") == phase) { Thread.sleep(20); return }
    Thread.sleep(10)
  }
  error("Deferred task did not reach $phase")
}

fun main() {
  val authorized = AtomicInteger()
  val closed = AtomicInteger()
  DeferredSync.registerReadExecutor("public.read", DeferredReadExecutor { _, _, _ ->
    authorized.incrementAndGet()
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal) = DeferredReadOutcome.COMPLETED
      override fun close() { closed.incrementAndGet() }
    }
  })
  val support = request("support") as JSONObject
  check(support.getBoolean("readOnly") && !support.getBoolean("queuedWrites"))
  val native = DeferredSync.request(App.getInstance(), "enqueue", options("public-native")) as JSONObject
  val nativeDuplicate = request("enqueue", options("public-native")) as JSONObject
  check(native.getString("generation") == nativeDuplicate.getString("generation"))
  val cancelledNative = DeferredSync.request(App.getInstance(), "cancel", JSONObject().put("id", "public-native")) as JSONObject
  check(cancelledNative.getString("phase") == "cancelled")
  DeferredSync.request(App.getInstance(), "release", JSONObject().put("id", "public-native"))
  try { DeferredSync.request(App.getInstance(), "list", JSONObject().put("token", "forbidden-fixture")); error("Unknown native field accepted") }
  catch (_: IllegalArgumentException) {}
  try { DeferredSync.request(App.getInstance(), "write", options("public-write")); error("Unsupported native operation accepted") }
  catch (error: PlatformFailure) { check(error.code == "EINVAL") }
  val first = request("enqueue", options("public-first")) as JSONObject
  val firstParameters = parameters(first)
  val duplicate = request("enqueue", options("public-first")) as JSONObject
  check(duplicate.getString("generation") == first.getString("generation"))
  check(JobScheduler.instance.jobs.size == 1)
  try { request("enqueue", options("public-first").put("operationRef", "changed")); error("Changed identity accepted") }
  catch (error: PlatformFailure) { check(error.code == "EEXIST") }
  try { request("enqueue", options("public-unsafe").put("token", "forbidden-fixture")); error("Credential argument accepted") }
  catch (_: IllegalArgumentException) {}
  val service = RuntimeDeferredJob()
  check(service.onStartJob(firstParameters))
  awaitPhase("public-first", "completed")
  check(authorized.get() == 1 && closed.get() == 1)
  check((request("enqueue", options("public-first")) as JSONObject).getString("phase") == "completed")
  request("release", JSONObject().put("id", "public-first"))
  val replacement = request("enqueue", options("public-first")) as JSONObject
  check(replacement.getString("generation") != first.getString("generation"))
  check(!service.onStartJob(firstParameters))
  check(!DeferredSync.finish(App.getInstance(), firstParameters, "completed"))
  JobScheduler.instance.jobs.clear()
  check(state("public-first").getString("phase") == "interrupted")
  val resumed = request("resume", JSONObject().put("id", "public-first")) as JSONObject
  check(resumed.getString("generation") != replacement.getString("generation"))
  request("cancel", JSONObject().put("id", "public-first"))
  check(state("public-first").getString("phase") == "cancelled")
  request("release", JSONObject().put("id", "public-first"))
  DeferredSync.registerReadExecutor("public.denied", DeferredReadExecutor { _, _, _ -> null })
  val denied = request("enqueue", options("public-denied", "public.denied")) as JSONObject
  check(service.onStartJob(parameters(denied)))
  awaitPhase("public-denied", "unauthorized")
  DeferredSync.registerReadExecutor("public.retry", DeferredReadExecutor { _, _, _ ->
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal) = DeferredReadOutcome.RETRY
      override fun close() {}
    }
  })
  val retry = request("enqueue", options("public-retry", "public.retry")) as JSONObject
  val retryParameters = parameters(retry)
  repeat(5) { attempt ->
    check(service.onStartJob(retryParameters))
    awaitPhase("public-retry", if (attempt == 4) "failed" else "retry")
    check(state("public-retry").getInt("attempts") == attempt + 1)
  }
  check(!service.onStartJob(retryParameters))
  val started = CountDownLatch(1)
  val released = CountDownLatch(1)
  DeferredSync.registerReadExecutor("public.cancel", DeferredReadExecutor { _, _, cancellation ->
    started.countDown()
    while (!cancellation.isCanceled) Thread.sleep(5)
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal) = DeferredReadOutcome.COMPLETED
      override fun close() { released.countDown() }
    }
  })
  val cancellable = request("enqueue", options("public-cancel", "public.cancel")) as JSONObject
  check(service.onStartJob(parameters(cancellable)))
  check(started.await(2, TimeUnit.SECONDS))
  check(!service.onStartJob(parameters(cancellable)))
  check(state("public-cancel").getInt("attempts") == 1)
  request("cancel", JSONObject().put("id", "public-cancel"))
  check(released.await(2, TimeUnit.SECONDS))
  check(state("public-cancel").getString("phase") == "cancelled")
  check(!(android.content.SharedPreferences.values["records"] ?: "").contains("forbidden-fixture"))
  check((request("list") as JSONArray).length() == 3)
  service.onDestroy()
  deferredRaceRegressions()
}
