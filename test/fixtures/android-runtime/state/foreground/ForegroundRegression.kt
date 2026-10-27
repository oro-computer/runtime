package oro.runtime.app

import android.content.*
import android.app.*
import android.os.*
import androidx.lifecycle.Lifecycle
import org.json.JSONObject
import java.io.File
import java.security.Security
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class App: Context() {
  fun getUserConfigValue(key: String) = config[key] ?: ""
  companion object {
    val config = mutableMapOf("foreground_enabled" to "true", "foreground_continuous_enabled" to "true",
      "foreground_continuous_subtype" to "Explicit public monitoring")
    fun getInstance() = App()
  }
}
class AppActivity: Context() {
  val lifecycle = Lifecycle(); var focused = true; var isFinishing = false; var isDestroyed = false
  fun hasWindowFocus() = focused
}
class PlatformFailure(val code: String): Exception(code)
class Window(val activity: AppActivity)
class PlatformCall(val activity: AppActivity) {
  val app = App(); val done = java.util.concurrent.atomic.AtomicBoolean(); var error: String? = null
  var result: Any? = null
  fun document() { if (done.get()) throw PlatformFailure("ABORT_ERR") }
  fun foreground(): Window { document(); return Window(activity) }
  fun success(value: Any) { result = value; done.set(true) }
  fun failure(code: String) { error = code; done.set(true) }
}
private fun await(message: String, predicate: () -> Boolean) {
  val until = System.nanoTime() + TimeUnit.SECONDS.toNanos(8)
  while(System.nanoTime() < until) { Handler.pump(); if (predicate()) return; Thread.sleep(5) }
  error(message)
}
fun main(args: Array<String>) {
  PublicEnvironment.root = File(args[0]).apply { mkdirs() }
  FixtureKeys.directory = File(args[0],"keys").apply { mkdirs() }; Security.addProvider(FixtureProvider())
  Looper.getMainLooper()
  val iconContext = Context()
  for (value in listOf("../bad", "@mipmap/public_status", "@drawable/missing", "@drawable/has-dash")) {
    App.config["foreground_notification_icon"] = value
    check(!ForegroundTasks.available(iconContext) && !ContinuousTasks.available(iconContext))
  }
  App.config["foreground_notification_icon"] = "@drawable/public_status"
  check(ForegroundTasks.available(iconContext) && ContinuousTasks.available(iconContext))
  App.config.remove("foreground_notification_icon")
  val uncaught = AtomicInteger(); Thread.setDefaultUncaughtExceptionHandler { _, _ -> uncaught.incrementAndGet() }
  val activity = AppActivity(); val runs = AtomicInteger(); val closes = AtomicInteger()
  var blocking = false; var releaseCancellation = CountDownLatch(1)
  var throwCancellation = false
  ForegroundTasks.registerExecutor("public.wait", ForegroundExecutor { _, _, _ -> object: AuthorizedForegroundExecution {
    override fun execute(cancellation: CancellationSignal): ForegroundOutcome {
      runs.incrementAndGet()
      cancellation.setOnCancelListener {
        if (blocking) releaseCancellation.await(5,TimeUnit.SECONDS)
        if (throwCancellation) error("public-cancellation-callback")
      }
      while (!cancellation.isCanceled) Thread.sleep(5)
      return ForegroundOutcome.UNKNOWN
    }
    override fun close() { closes.incrementAndGet() }
  } })
  ForegroundTasks.registerExecutor("public.complete", ForegroundExecutor { _, _, _ -> object: AuthorizedForegroundExecution {
    override fun execute(cancellation: CancellationSignal): ForegroundOutcome { runs.incrementAndGet(); return ForegroundOutcome.COMPLETED }
    override fun close() { closes.incrementAndGet() }
  } })
  ForegroundTasks.registerExecutor("public.commit-fault", ForegroundExecutor { _, _, _ -> object: AuthorizedForegroundExecution {
    override fun execute(cancellation: CancellationSignal): ForegroundOutcome {
      android.database.sqlite.DatabaseFault.beforeCommit = true; return ForegroundOutcome.COMPLETED
    }
    override fun close() {}
  } })
  if(args[1] == "recover") {
    check(ForegroundTasks.status(activity,"process").getString("phase") == "unknown")
    check(runs.get() == 0 && PublicEnvironment.starts == 0)
    println("foreground-restart-no-replay");return
  }
  fun launch(id: String, kind: String = "public.wait", duration: Long = 10000): JSONObject {
    var result: JSONObject? = null; var error: String? = null; var replied = false
    ForegroundTasks.start(activity,ForegroundTask(id,kind,"authority","account","target","operation"),duration,true) { value, code -> result=value;error=code;replied=true }
    await("admission callback") { replied }; check(error == null) { "Admission $error" };return result!!
  }
  fun settled(id: String, generation: String) {
    await("terminal resource cleanup") {
      try { ForegroundTasks.release(activity,id,generation);true }
      catch (error: NativeStateException) { check(error.code == "EBUSY");false }
    }
  }
  val first = launch(if(args[1] == "seed") "process" else "first")
  await("worker running") { runs.get() == 1 }
  if(args[1] == "seed") { println("foreground-seeded-running");return }
  activity.focused = false
  check(ForegroundTasks.status(activity,"first").getString("phase") == "running")
  val stop = PublicEnvironment.notificationManager.active.values.single().actions.single().actionIntent
  stop.send()
  await("native stop closes resource") { closes.get() == 1 && PublicEnvironment.service == null }
  check(ForegroundTasks.status(activity,"first").getString("phase") == "unknown")
  activity.focused = true
  settled("first",first.getString("generation"))
  PublicEnvironment.delayDestroy = true
  val blocked = launch("blocked-cleanup","public.complete")
  await("completed") { ForegroundTasks.status(activity,"blocked-cleanup").getString("phase") == "completed" }
  var serviceBusy: String? = null
  ForegroundTasks.start(activity,ForegroundTask("before-destroy","public.wait","authority","account","target","operation"),1000,true) { _, error -> serviceBusy=error }
  check(serviceBusy == "EBUSY")
  PublicEnvironment.delayDestroy = false
  PublicEnvironment.service!!.stopSelf()
  await("complete close") { closes.get() == 2 && PublicEnvironment.service == null }
  settled("blocked-cleanup",blocked.getString("generation"))
  val next = launch("replacement")
  await("new worker") { runs.get() == 3 }
  val service = PublicEnvironment.service!!
  service.onStartCommand(Intent().setAction(ForegroundTasks.STOP).putExtra("generation",first.getString("generation")),0,1)
  check(!service.destroyed && ForegroundTasks.status(activity,"replacement").getString("phase") == "running")
  service.onTimeout(1,1)
  await("timeout closes resource") { closes.get() == 3 && PublicEnvironment.service == null }
  settled("replacement",next.getString("generation"))
  blocking = true
  launch("quarantined")
  await("quarantine worker") { runs.get() == 4 }
  PublicEnvironment.service!!.onTaskRemoved(null)
  await("worker closed before cancellation callback") { closes.get() == 4 }
  var busy: String? = null
  ForegroundTasks.start(activity,ForegroundTask("overlap","public.wait","authority","account","target","operation"),1000,true) { _, error -> busy=error }
  check(busy == "EBUSY")
  releaseCancellation.countDown(); blocking = false
  await("quarantine released") {
    val result=runCatching { launch("after-cleanup","public.complete") }
    result.isSuccess
  }
  await("cleanup complete") { closes.get() == 5 && PublicEnvironment.service == null }
  PublicEnvironment.notificationsAllowed = false
  var denied: String? = null
  ForegroundTasks.start(activity,ForegroundTask("denied","public.wait","authority","account","target","operation"),1000,true) { _, error -> denied=error }
  check(denied == "EACCES")
  PublicEnvironment.notificationsAllowed = true
  PublicEnvironment.denyStart = true
  var rejected: String? = null
  ForegroundTasks.start(activity,ForegroundTask("os-rejected","public.wait","authority","account","target","operation"),1000,true) { _, error -> rejected=error }
  await("OS rejection") { rejected != null }; check(rejected == "EACCES")
  PublicEnvironment.denyStart = false
  await("rejection durable") { ForegroundTasks.status(activity,"os-rejected").getString("phase") == "rejected" }
  check(runs.get() == 5)
  await("rejected cleanup") {
    val result = runCatching { launch("commit-fault","public.commit-fault") }
    result.isSuccess
  }
  await("failed finish remains unknown") { ForegroundTasks.status(activity,"commit-fault").getString("phase") == "unknown" }
  await("finish service destroyed") { PublicEnvironment.service == null }
  throwCancellation = true
  val throwing = launch("throwing-cancel")
  await("throwing callback worker ready") { runs.get() == 6 }
  ForegroundTasks.stop(activity,"throwing-cancel",throwing.getString("generation"))
  settled("throwing-cancel",throwing.getString("generation"))
  Build.VERSION.SDK_INT = 26; check(ForegroundTasks.available(activity)); Build.VERSION.SDK_INT = 37
  check(uncaught.get() == 0)
  println("foreground-lifecycle-matrix-passed")
}
