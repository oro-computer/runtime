package dev.oro.push

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class BootReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    when (intent.action) {
      Intent.ACTION_BOOT_COMPLETED, Intent.ACTION_MY_PACKAGE_REPLACED -> {
        if (Store.preferences(context).getBoolean("enabled", false)) ConnectionService.start(context)
      }
      Intent.ACTION_PACKAGE_FULLY_REMOVED -> synchronized(Store) {
        val application = intent.data?.schemeSpecificPart ?: return
        val state = Store.read(context)
        Store.records(state, "registrations").filter { it.getString("package") == application }
          .forEach { Store.remove(state, it.getString("token")) }
        Store.save(context, state)
        ConnectionService.refresh()
      }
    }
  }
}
