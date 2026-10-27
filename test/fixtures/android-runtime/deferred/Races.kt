package oro.runtime.app

import android.app.job.JobParameters
import android.app.job.JobScheduler
import android.content.SharedPreferences
import android.os.CancellationSignal
import android.os.Handler
import org.json.JSONObject
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

private fun invoke(action: String, id: String): JSONObject = DeferredSync.request(App.getInstance(), action,
  JSONObject().put("id", id)) as JSONObject
private fun enqueue(id: String, kind: String = "public.read"): JSONObject = DeferredSync.request(App.getInstance(), "enqueue",
  JSONObject().put("id", id).put("kind", kind).put("authorityRef", "public-server")
    .put("accountRef", "public-account").put("targetRef", "public-target").put("operationRef", "public-operation")
    .put("network", "any")) as JSONObject
private fun params(record: JSONObject): JobParameters {
  val job = checkNotNull(JobScheduler.instance.getPendingJob(record.getInt("jobId")))
  return JobParameters(job.id, job.extras)
}
private fun awaitCondition(description: String, condition: () -> Boolean) {
  val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3)
  while (!condition()) {
    check(System.nanoTime() < deadline) { description }
    Thread.sleep(1)
  }
}
private class Actor(name: String, action: () -> Unit) {
  private val failure = AtomicReference<Throwable?>()
  val thread = Thread({ try { action() } catch (error: Throwable) { failure.set(error) } }, name).apply {
    isDaemon = true
    start()
  }
  fun join() {
    thread.join(4000)
    check(!thread.isAlive) { "Actor ${thread.name} did not finish" }
    failure.get()?.let { throw it }
  }
}

/** Pause a journal snapshot inside finish, then contend from the public static API. */
private fun journalRace(release: Boolean) {
  val id = if (release) "race-release" else "race-cancel"
  val initial = enqueue(id)
  val parameters = params(initial)
  check(DeferredSync.begin(App.getInstance(), parameters) != null)
  val read = CountDownLatch(1)
  val proceed = CountDownLatch(1)
  SharedPreferences.afterRead = {
    if (Thread.currentThread().name == "journal-finish") {
      read.countDown()
      check(proceed.await(3, TimeUnit.SECONDS))
    }
  }
  val finish = Actor("journal-finish") { check(DeferredSync.finish(App.getInstance(), parameters, "retry")) }
  check(read.await(3, TimeUnit.SECONDS))
  val replacement = AtomicReference<JSONObject?>()
  val mutation = Actor("journal-mutation") {
    invoke(if (release) "release" else "cancel", id)
    if (release) replacement.set(enqueue(id))
  }
  try {
    awaitCondition("Public request did not share the finish monitor") {
      mutation.thread.state == Thread.State.BLOCKED || !mutation.thread.isAlive
    }
    check(mutation.thread.state == Thread.State.BLOCKED) { "Public request bypassed the journal lock" }
  } finally {
    proceed.countDown()
    finish.join()
    mutation.join()
    SharedPreferences.afterRead = null
  }
  if (release) {
    val current = invoke("status", id)
    check(current.getString("generation") == replacement.get()!!.getString("generation"))
    check(current.getString("generation") != initial.getString("generation"))
    check(current.getString("phase") == "scheduled" && current.getInt("attempts") == 0)
    check(DeferredSync.begin(App.getInstance(), parameters) == null)
  } else check(invoke("status", id).getString("phase") == "cancelled")
  check(!DeferredSync.finish(App.getInstance(), parameters, "completed"))
  invoke("release", id)
}

private fun queuedCompletion(release: Boolean) {
  val id = if (release) "race-post-release" else "race-post-cancel"
  val service = RuntimeDeferredJob()
  Handler.paused = true
  val record = enqueue(id, "public.retry")
  val parameters = params(record)
  val finishes = JobScheduler.instance.finishes.size
  try {
    check(service.onStartJob(parameters))
    awaitCondition("Retry completion was not queued") { Handler.pending.isNotEmpty() }
    check(invoke("status", id).getString("phase") == "retry")
    invoke(if (release) "release" else "cancel", id)
    val replacement = if (release) enqueue(id) else null
    Handler.drain()
    check(JobScheduler.instance.finishes.size == finishes) { "Cancelled queued completion rescheduled a job" }
    if (replacement != null) {
      val current = invoke("status", id)
      check(current.getString("generation") == replacement.getString("generation"))
      check(JobScheduler.instance.getPendingJob(current.getInt("jobId")) != null)
      check(!service.onStopJob(parameters))
      check(current.getString("phase") == "scheduled")
    } else {
      check(invoke("status", id).getString("phase") == "cancelled")
      check(JobScheduler.instance.getPendingJob(record.getInt("jobId")) == null)
    }
    invoke("release", id)
  } finally { Handler.drain(); service.onDestroy() }
}

private fun startAndStopRaces() {
  val started = CountDownLatch(1)
  val proceed = CountDownLatch(1)
  val closed = CountDownLatch(1)
  DeferredSync.registerReadExecutor("public.race", DeferredReadExecutor { _, _, _ ->
    started.countDown()
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal): DeferredReadOutcome {
        check(proceed.await(3, TimeUnit.SECONDS))
        return DeferredReadOutcome.COMPLETED
      }
      override fun close() { closed.countDown() }
    }
  })
  val service = RuntimeDeferredJob()
  val initial = enqueue("race-start", "public.race")
  val parameters = params(initial)
  val committed = CountDownLatch(1)
  val activate = CountDownLatch(1)
  SharedPreferences.afterCommit = {
    if (Thread.currentThread().name == "job-start") {
      committed.countDown()
      check(activate.await(3, TimeUnit.SECONDS))
    }
  }
  val start = Actor("job-start") { check(service.onStartJob(parameters)) }
  check(committed.await(3, TimeUnit.SECONDS))
  val cancel = Actor("job-cancel") { invoke("cancel", "race-start") }
  try {
    awaitCondition("Cancellation did not serialize with active-run registration") {
      cancel.thread.state == Thread.State.BLOCKED || !cancel.thread.isAlive
    }
    check(cancel.thread.state == Thread.State.BLOCKED)
  } finally {
    activate.countDown()
    start.join()
    cancel.join()
    SharedPreferences.afterCommit = null
    proceed.countDown()
  }
  check(invoke("status", "race-start").getString("phase") == "cancelled")
  check(!RuntimeDeferredJob.running(initial.getString("generation")))
  check(!service.onStopJob(parameters))
  // Authorization may have been skipped when cancellation won before worker entry.
  if (started.count == 0L) check(closed.await(3, TimeUnit.SECONDS))
  invoke("release", "race-start")
  service.onDestroy()

  // A completed attempt's stop callback must not stop a new attempt of the same generation.
  val gate = CountDownLatch(1)
  val entered = CountDownLatch(1)
  val released = CountDownLatch(1)
  val calls = java.util.concurrent.atomic.AtomicInteger()
  DeferredSync.registerReadExecutor("public.stale", DeferredReadExecutor { _, _, _ ->
    val attempt = calls.incrementAndGet()
    object : AuthorizedDeferredRead {
      override fun reconcile(cancellation: CancellationSignal): DeferredReadOutcome {
        if (attempt == 1) return DeferredReadOutcome.RETRY
        entered.countDown()
        check(gate.await(3, TimeUnit.SECONDS))
        return DeferredReadOutcome.COMPLETED
      }
      override fun close() { if (attempt > 1) released.countDown() }
    }
  })
  val record = enqueue("race-stale", "public.stale")
  val first = params(record)
  check(service.onStartJob(first))
  awaitCondition("First attempt did not complete") { !RuntimeDeferredJob.running(record.getString("generation")) }
  awaitDeferredWorkerIdle()
  val second = params(record)
  check(service.onStartJob(second))
  check(entered.await(3, TimeUnit.SECONDS))
  check(!service.onStopJob(first))
  check(invoke("status", "race-stale").getString("phase") == "running")
  check(service.onStopJob(second))
  check(invoke("status", "race-stale").getString("phase") == "retry")
  invoke("cancel", "race-stale")
  gate.countDown()
  check(released.await(3, TimeUnit.SECONDS))
  check(invoke("status", "race-stale").getString("phase") == "cancelled")
  invoke("release", "race-stale")
  service.onDestroy()
}

fun deferredRaceRegressions() {
  journalRace(false)
  journalRace(true)
  queuedCompletion(false)
  queuedCompletion(true)
  startAndStopRaces()
  println("Deferred cancellation/release, start hand-off, queued completion and stale OS-stop races passed")
}
