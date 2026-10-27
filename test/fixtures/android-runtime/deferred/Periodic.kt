package oro.runtime.app

import android.app.job.JobParameters
import android.app.job.JobScheduler
import android.os.CancellationSignal
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicInteger

/** Public native fixture for recurrence, operation restrictions and terminal outcomes. */
fun periodicAndMaintenanceRegressions () {
  val context = App.getInstance()
  fun options(id: String) = JSONObject().put("id", id).put("kind", "public.periodic")
    .put("authorityRef", "server").put("accountRef", "account").put("targetRef", "target").put("operationRef", "sync")
  fun request(action: String, value: JSONObject) = DeferredSync.request(context, action, value) as JSONObject
  fun status(id: String) = request("status", JSONObject().put("id", id))
  fun parameters(value: JSONObject) = JobParameters(value.getInt("jobId"), JobScheduler.instance.getPendingJob(value.getInt("jobId"))!!.extras)
  fun awaitOutcome(id: String, outcome: String) {
    val deadline = System.nanoTime() + 2_000_000_000L
    while (System.nanoTime() < deadline && status(id).optString("lastOutcome") != outcome) Thread.sleep(5)
    check(status(id).getString("lastOutcome") == outcome)
  }
  DeferredSync.registerReadExecutor("public.periodic", DeferredReadExecutor { _, _, _ ->
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal) = DeferredReadOutcome.COMPLETED
      override fun close() {}
    }
  })
  for (invalid in listOf(899999, 900000.5, -1)) {
    check(runCatching { request("enqueue", options("invalid-periodic").put("intervalMs", invalid)) }.isFailure)
  }
  check(runCatching { request("enqueue", options("invalid-flex").put("flexMs", 300000)) }.isFailure)
  check(runCatching { request("enqueue", options("invalid-long-flex").put("intervalMs", 86400000).put("flexMs", 300000)) }.isFailure)
  val recurring = request("enqueue", options("public-recurring").put("intervalMs", 900000).put("flexMs", 300000))
  check(JobScheduler.instance.getPendingJob(recurring.getInt("jobId"))!!.interval == 900000L)
  val service = RuntimeDeferredJob()
  check(service.onStartJob(parameters(recurring)))
  awaitOutcome("public-recurring", "completed")
  check(status("public-recurring").getString("phase") == "scheduled")
  check(status("public-recurring").getInt("attempts") == 0)
  check(status("public-recurring").getLong("completedRuns") == 1L)
  request("release", JSONObject().put("id", "public-recurring"))
  val cycles = AtomicInteger()
  DeferredSync.registerReadExecutor("public.cycle", DeferredReadExecutor { _, _, _ ->
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal) =
        if (cycles.incrementAndGet() % 2 == 1) DeferredReadOutcome.RETRY else DeferredReadOutcome.COMPLETED
      override fun close() {}
    }
  })
  val cycle = request("enqueue", options("public-cycle").put("kind", "public.cycle").put("intervalMs", 900000))
  val cycleParameters = parameters(cycle)
  for (attempt in 1..4) {
    awaitDeferredWorkerIdle()
    check(service.onStartJob(cycleParameters))
    val expected = if (attempt % 2 == 1) "retry" else "scheduled"
    val deadline = System.nanoTime() + 2_000_000_000L
    while (System.nanoTime() < deadline && status("public-cycle").getString("phase") != expected) Thread.sleep(5)
    val state = status("public-cycle")
    check(state.getString("phase") == expected)
    check(state.getInt("attempts") == if (expected == "retry") 1 else 0)
    check(JobScheduler.instance.getPendingJob(cycle.getInt("jobId"))!!.interval == 900000L)
    check(state.optLong("completedRuns") == attempt / 2L)
  }
  check(cycles.get() == 4)
  request("release", JSONObject().put("id", "public-cycle"))
  DeferredSync.registerReadExecutor("public.unknown", DeferredReadExecutor { _, _, _ ->
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal) = DeferredReadOutcome.UNKNOWN
      override fun close() {}
    }
  })
  val unknown = request("enqueue", options("public-unknown").put("kind", "public.unknown").put("intervalMs", 900000))
  val unknownParameters = parameters(unknown)
  awaitDeferredWorkerIdle()
  check(service.onStartJob(unknownParameters))
  awaitOutcome("public-unknown", "unknown")
  check(status("public-unknown").getString("phase") == "unknown")
  check(JobScheduler.instance.getPendingJob(unknown.getInt("jobId")) == null)
  check(!service.onStartJob(unknownParameters))
  check(runCatching { request("resume", JSONObject().put("id", "public-unknown")) }.isFailure)
  request("release", JSONObject().put("id", "public-unknown"))
  val maintenance = DeferredMaintenanceExecutor { _, task, _ ->
    check(task.operationRef == "revoke")
    object : AuthorizedDeferredMaintenance {
      override fun perform(cancellation: CancellationSignal) = DeferredMaintenanceOutcome.COMPLETED
      override fun close() {}
    }
  }
  check(runCatching { DeferredSync.registerMaintenanceExecutor("public.unsafe", setOf("revoke"), false, maintenance) }.isFailure)
  check(runCatching { DeferredSync.registerMaintenanceExecutor("public.write", setOf("purchase"), true, maintenance) }.isFailure)
  DeferredSync.registerMaintenanceExecutor("public.maintenance", setOf("revoke"), true, maintenance)
  val task = options("public-revoke").put("kind", "public.maintenance").put("operationRef", "revoke")
  check(runCatching { request("enqueue", task) }.isFailure)
  check(runCatching { request("enqueueMaintenance", JSONObject(task.toString()).put("intervalMs", 900000)) }.isFailure)
  val queued = request("enqueueMaintenance", task)
  awaitDeferredWorkerIdle()
  check(service.onStartJob(parameters(queued)))
  awaitOutcome("public-revoke", "completed")
  check(status("public-revoke").getString("phase") == "completed")
  request("release", JSONObject().put("id", "public-revoke"))
  service.onDestroy()
}
