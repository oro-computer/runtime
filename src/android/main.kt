// vim: set sw=2:
package __BUNDLE_IDENTIFIER__

import android.Manifest
import android.os.Build
import android.os.Bundle
import android.util.Log
import oro.runtime.app.AppActivity

open class MainActivity : AppActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    maybeStartUsbService()
  }

  private fun maybeStartUsbService() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
      UsbService.ensureStarted(this)
      return
    }

    if (this.checkPermission(Manifest.permission.POST_NOTIFICATIONS)) {
      UsbService.ensureStarted(this)
      return
    }

    this.requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS)) { granted ->
      if (granted) {
        UsbService.ensureStarted(this)
      } else {
        Log.w(TAG, "USB service disabled until notification permission is granted")
      }
    }
  }

  companion object {
    private const val TAG = "MainActivity"
  }
}
