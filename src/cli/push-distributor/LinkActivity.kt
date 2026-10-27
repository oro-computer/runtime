package dev.oro.push

import android.app.Activity
import android.app.PendingIntent
import android.content.Intent
import android.os.Bundle

class LinkActivity : Activity() {
  override fun onCreate (saved: Bundle?) {
    super.onCreate(saved)
    if (intent.action == Intent.ACTION_VIEW && intent.data?.scheme == "unifiedpush" && intent.data?.host == "link" &&
      callingPackage != null && Store.preferences(this).getBoolean("enabled", false)) {
      setResult(RESULT_OK, Intent().putExtra("pi", PendingIntent.getBroadcast(this, 0,
        Intent().setPackage("org.unifiedpush.dummy_app"), PendingIntent.FLAG_IMMUTABLE)))
    } else setResult(RESULT_CANCELED)
    finish()
  }
}
