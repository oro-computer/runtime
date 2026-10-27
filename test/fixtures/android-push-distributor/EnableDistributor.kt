package dev.oro.push.qualification

import android.app.Activity
import android.app.Instrumentation
import android.content.Context
import android.content.Intent
import android.content.ComponentName
import android.content.pm.PackageManager
import android.content.BroadcastReceiver
import android.content.IntentFilter
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.widget.Button

/** Signed public instrumentation drives only the distributor's own settings. */
class EnableDistributor : Instrumentation() {
  private var expected: String? = null
  private var transportOnly = false
  override fun onCreate (arguments: Bundle?) {
    super.onCreate(arguments)
    expected = arguments?.getString("server")
    transportOnly = arguments?.getString("transportOnly") == "true"
    start()
  }
  override fun onStart () {
    val result = Bundle()
    try {
      if (transportOnly) {
        check(targetContext.getSharedPreferences("oro.push.development.v1", Context.MODE_PRIVATE).edit()
          .putString("server", expected).putBoolean("enabled", true).commit())
        for (name in listOf("LinkActivity", "RegistrationReceiver")) {
          targetContext.packageManager.setComponentEnabledSetting(ComponentName("dev.oro.push", "dev.oro.push.$name"),
            PackageManager.COMPONENT_ENABLED_STATE_ENABLED, PackageManager.DONT_KILL_APP)
        }
        targetContext.startForegroundService(Intent().setComponent(ComponentName("dev.oro.push", "dev.oro.push.ConnectionService")))
        result.putString("transportSetup", "passed")
        sendStatus(0, result)
        val gate = java.util.concurrent.CountDownLatch(1)
        val stop = object : BroadcastReceiver() {
          override fun onReceive (context: Context, intent: Intent) { gate.countDown() }
        }
        targetContext.registerReceiver(stop, IntentFilter("dev.oro.push.qualification.STOP"), "android.permission.DUMP", null, Context.RECEIVER_EXPORTED)
        try { gate.await(5, java.util.concurrent.TimeUnit.MINUTES) } finally { targetContext.unregisterReceiver(stop) }
        finish(Activity.RESULT_OK, result)
        return
      }
      val activity = startActivitySync(Intent().setClassName("dev.oro.push", "dev.oro.push.SettingsActivity")
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
      var failure: Throwable? = null
      runOnMainSync {
        try {
          fun startButton (view: View): Button? {
            if (view is Button && view.text.toString() == "Save and start") return view
            if (view is ViewGroup) for (index in 0 until view.childCount) {
              startButton(view.getChildAt(index))?.let { return it }
            }
            return null
          }
          check(startButton(activity.window.decorView)?.performClick() == true)
        } catch (error: Throwable) { failure = error }
      }
      failure?.let { throw it }
      val preferences = targetContext.getSharedPreferences("oro.push.development.v1", Context.MODE_PRIVATE)
      check(preferences.getBoolean("enabled", false))
      check(preferences.getString("server", "") == expected)
      result.putString("settingsAction", "passed")
      finish(Activity.RESULT_OK, result)
    } catch (error: Throwable) {
      result.putString("failure", error.javaClass.simpleName)
      finish(Activity.RESULT_CANCELED, result)
    }
  }
}
