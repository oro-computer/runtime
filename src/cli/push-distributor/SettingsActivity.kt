package dev.oro.push

import android.Manifest
import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class SettingsActivity : Activity() {
  private val timer = Handler(Looper.getMainLooper())
  private lateinit var status: TextView
  private val update = object : Runnable {
    override fun run () {
      status.text = try {
        val state = Store.read(this@SettingsActivity)
        Store.preferences(this@SettingsActivity).getString("status", "Not enabled") + "\n" +
          state.getJSONArray("registrations").length() + " registrations; " + state.getJSONArray("pending").length() + " pending acknowledgements"
      } catch (_: Exception) { "Stored state could not be read; use Reset registrations to recover" }
      timer.postDelayed(this, 1000)
    }
  }

  override fun onCreate (saved: Bundle?) {
    super.onCreate(saved)
    val layout = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding(24, 48, 24, 24)
    }
    fun text (value: String) { layout.addView(TextView(this).apply { text = value; textSize = 18f }) }
    fun button (label: String, action: () -> Unit) {
      layout.addView(Button(this).apply { text = label; setOnClickListener { action() } })
    }
    text("Oro Push Dev")
    text("Local development distributor. Use public test data. A visible service keeps the connection open; Android may still limit background delivery.")
    val input = EditText(this).apply {
      hint = "https://push.dev.example:8787"
      inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_URI
      val initial = Store.preferences(this@SettingsActivity).getString("server", null) ?: try {
        assets.open("development-server.txt").bufferedReader().use { it.readText().trim() }
      } catch (_: Exception) { "" }
      setText(initial)
    }
    layout.addView(input)
    text("Changing the server resets app registrations. A CA supplied at build time is trusted only by this development APK.")
    status = TextView(this).apply { textSize = 17f }
    layout.addView(status)
    button("Save and start") {
      try {
        val server = Protocol.origin(input.text.toString().trim())
        if (Store.preferences(this).getString("server", "") != server) reset()
        check(Store.preferences(this).edit().putString("server", server).putBoolean("enabled", true).commit())
        components(true)
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
          requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        ConnectionService.start(this)
        ConnectionService.refresh()
      } catch (error: Exception) { status.text = error.message ?: "Configuration failed" }
    }
    button("Stop") {
      Store.preferences(this).edit().putBoolean("enabled", false).putString("status", "Stopped; open and start to resume").commit()
      components(false)
      stopService(Intent(this, ConnectionService::class.java))
    }
    button("Reset registrations") { reset() }
    button("Background battery settings") { startActivity(Intent(android.provider.Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)) }
    setContentView(ScrollView(this).apply { addView(layout) })
  }

  private fun components (enabled: Boolean) {
    for (name in listOf(LinkActivity::class.java, RegistrationReceiver::class.java)) {
      packageManager.setComponentEnabledSetting(ComponentName(this, name), if (enabled)
        PackageManager.COMPONENT_ENABLED_STATE_ENABLED else PackageManager.COMPONENT_ENABLED_STATE_DISABLED, PackageManager.DONT_KILL_APP)
    }
  }

  private fun reset () {
    synchronized(Store) {
      val state = try { Store.read(this) } catch (_: Exception) { Store.empty() }
      Store.records(state, "registrations").forEach { Wire.send(this, it, "UNREGISTERED") }
      Store.save(this, Store.empty())
    }
    ConnectionService.refresh()
  }
  override fun onResume () {
    super.onResume()
    timer.post(update)
    if (Store.preferences(this).getBoolean("enabled", false)) ConnectionService.start(this)
  }
  override fun onPause () { timer.removeCallbacks(update); super.onPause() }
}
