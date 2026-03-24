// vim: set sw=2:
package __BUNDLE_IDENTIFIER__

import oro.runtime.app.App as OroRuntimeApp

open class App : OroRuntimeApp() {
  companion object {
    init {
      OroRuntimeApp.loadOroRuntime()
    }
  }
}
