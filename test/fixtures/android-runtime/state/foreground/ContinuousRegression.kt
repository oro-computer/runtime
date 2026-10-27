package oro.runtime.app

import android.app.NotificationManager
import android.content.*
import android.os.*
import java.io.File
import java.security.Security
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.json.JSONObject

/** Actual controller and SQLite journal; Android lifecycle/notification calls use host facades. */
fun main (args: Array<String>) {
  PublicEnvironment.root = File(args[0]).apply { mkdirs() }
  FixtureKeys.directory = File(args[0], "keys").apply { mkdirs() }
  Security.addProvider(FixtureProvider())
  Looper.getMainLooper()
  val activity = AppActivity()
  val runs = AtomicInteger()
  val closes = AtomicInteger()
  val cancellationEntered = CountDownLatch(1)
  val cancellationReleased = CountDownLatch(1)
  var blockCancellation = false
  fun await (message: String, predicate: () -> Boolean) {
    val until = System.nanoTime() + TimeUnit.SECONDS.toNanos(8)
    while (System.nanoTime() < until) { Handler.pump(); if (predicate()) return; Thread.sleep(5) }
    error(message)
  }
  val executor = ForegroundExecutor { _, _, _ -> object : AuthorizedForegroundExecution {
    override fun execute (signal: CancellationSignal): ForegroundOutcome {
      runs.incrementAndGet()
      signal.setOnCancelListener {
        if (blockCancellation) { cancellationEntered.countDown(); cancellationReleased.await(5, TimeUnit.SECONDS) }
      }
      while (!signal.isCanceled) Thread.sleep(5)
      return ForegroundOutcome.UNKNOWN
    }
    override fun close () { closes.incrementAndGet() }
  } }
  ContinuousTasks.registerExecutor("public.monitor", executor)
  ForegroundTasks.registerExecutor("public.transfer", executor)
  if (args[1] == "recover") {
    check(ContinuousTasks.status(activity, "process").getString("phase") == "unknown")
    check(runs.get() == 0 && PublicEnvironment.starts == 0)
    println("continuous-restart-no-replay"); return
  }
  fun launch (id: String): JSONObject {
    var result: JSONObject? = null; var failure: String? = null; var replied = false
    ContinuousTasks.start(activity, ForegroundTask(id, "public.monitor", "authority", "account", "target", "operation"), true) {
      value, code -> result = value; failure = code; replied = true
    }
    await("continuous admission") { replied }
    check(failure == null) { "continuous admission $failure" }
    return result!!
  }
  fun settled (id: String, generation: String) {
    await("continuous cleanup") {
      try { ContinuousTasks.release(activity, id, generation); true }
      catch (error: NativeStateException) { check(error.code == "EBUSY"); false }
    }
  }
  for ((taskKind, initiated, expected) in listOf(Triple("public.transfer", true, "ENOTSUP"), Triple("public.monitor", false, "EINVAL"))) {
    var rejected: String? = null
    ContinuousTasks.start(activity, ForegroundTask("not-admitted", taskKind, "authority", "account", "target", "operation"), initiated) { _, code -> rejected = code }
    check(rejected == expected)
  }
  check(PublicEnvironment.starts == 0)
  val first = launch(if (args[1] == "seed") "process" else "first")
  await("continuous worker") { runs.get() == 1 }
  check(!first.has("durationMs"))
  if (args[1] == "seed") { println("continuous-seeded-running"); return }
  val firstStop = PublicEnvironment.notificationManager.active[ContinuousTasks.NOTIFICATION]!!.actions.single().actionIntent
  val service = PublicEnvironment.continuousService!!
  service.onTaskRemoved(null)
  activity.focused = false
  SystemClock.offset += 3600000
  Handler.pump()
  check(!service.destroyed && ContinuousTasks.status(activity, "first").getString("phase") == "running")
  activity.focused = true
  var finite: JSONObject? = null
  ForegroundTasks.start(activity, ForegroundTask("finite", "public.transfer", "authority", "account", "target", "operation"), 1000, true) {
    value, code -> check(code == null); finite = value
  }
  await("finite and continuous workers") { runs.get() == 2 && finite != null }
  check(PublicEnvironment.notificationManager.active.size == 2)
  PublicEnvironment.service!!.onTimeout(1, 1)
  await("finite timeout cleanup") { closes.get() == 1 && PublicEnvironment.service == null }
  check(PublicEnvironment.notificationManager.active.keys == setOf(ContinuousTasks.NOTIFICATION))
  check(ContinuousTasks.status(activity, "first").getString("phase") == "running")
  firstStop.send()
  settled("first", first.getString("generation"))
  check(closes.get() == 2 && PublicEnvironment.continuousService == null)
  val next = launch("replacement")
  await("replacement worker") { runs.get() == 3 }
  firstStop.send()
  PublicEnvironment.continuousService!!.onStartCommand(Intent().setAction(ContinuousTasks.STOP), 0, 1)
  check(ContinuousTasks.status(activity, "replacement").getString("phase") == "running")
  PublicEnvironment.notificationManager.channels[ContinuousTasks.CHANNEL]!!.importance = NotificationManager.IMPORTANCE_NONE
  SystemClock.offset += 1001; Handler.pump()
  await("blocked channel stops monitoring") { ContinuousTasks.status(activity, "replacement").getString("phase") == "unknown" }
  settled("replacement", next.getString("generation"))
  PublicEnvironment.notificationManager.channels[ContinuousTasks.CHANNEL]!!.importance = NotificationManager.IMPORTANCE_LOW
  blockCancellation = true
  val quarantined = launch("quarantined")
  await("quarantine worker") { runs.get() == 4 }
  ContinuousTasks.stop(activity, "quarantined", quarantined.getString("generation"))
  check(cancellationEntered.await(3, TimeUnit.SECONDS))
  await("quarantine worker closed") { closes.get() == 4 }
  var busy: String? = null
  ContinuousTasks.start(activity, ForegroundTask("overlap", "public.monitor", "authority", "account", "target", "operation"), true) { _, code -> busy = code }
  check(busy == "EBUSY")
  cancellationReleased.countDown(); blockCancellation = false
  settled("quarantined", quarantined.getString("generation"))
  val permission = launch("permission")
  await("permission worker") { runs.get() == 5 }
  PublicEnvironment.continuousPermission = false
  SystemClock.offset += 1001; Handler.pump()
  await("permission revocation") { ContinuousTasks.status(activity, "permission").getString("phase") == "unknown" }
  settled("permission", permission.getString("generation"))
  check(!ContinuousTasks.available(activity))
  PublicEnvironment.continuousPermission = true
  for (configured in listOf(false, true)) {
    App.config["foreground_continuous_enabled"] = configured.toString()
    check(ContinuousTasks.available(activity) == configured)
    check(ForegroundTasks.available(activity))
  }
  App.config["foreground_continuous_subtype"] = ""
  check(!ContinuousTasks.available(activity)); App.config["foreground_continuous_subtype"] = "Public monitoring"
  PublicEnvironment.continuousSubtype = null
  check(!ContinuousTasks.available(activity)); PublicEnvironment.continuousSubtype = "Public monitoring"
  PublicEnvironment.continuousProcess = "other-process"
  check(!ContinuousTasks.available(activity)); PublicEnvironment.continuousProcess = null
  PublicEnvironment.continuousType = 1
  check(!ContinuousTasks.available(activity)); PublicEnvironment.continuousType = android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
  val call = PlatformCall(activity)
  ForegroundTasks.request(call, "continuous.support", JSONObject())
  check(call.error == null && call.result!!.let { it as JSONObject }.isNull("maximumDurationMs"))
  check(runCatching { ContinuousTasks.request(PlatformCall(activity), "start", JSONObject()
    .put("durationMs", 0).put("userInitiated", true)) }.isFailure)
  PublicEnvironment.denyStart = true
  check(runCatching { launch("os-denied") }.isFailure)
  await("OS denial retained") { ContinuousTasks.status(activity, "os-denied").getString("phase") == "rejected" }
  check(runs.get() == 5)
  PublicEnvironment.denyStart = false
  Build.VERSION.SDK_INT = 26
  check(ContinuousTasks.available(activity))
  Build.VERSION.SDK_INT = 37
  println("continuous-lifecycle-matrix-passed")
}
