// vim: set sw=2:
package oro.runtime.window

import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.content.res.AssetManager
import android.graphics.drawable.ColorDrawable
import androidx.core.graphics.Insets
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import android.net.Uri
import android.os.Bundle
import android.util.DisplayMetrics
import android.view.WindowManager

import androidx.activity.addCallback
import androidx.activity.OnBackPressedCallback
import androidx.activity.BackEventCompat
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.commit
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.Fragment

import oro.runtime.app.App
import oro.runtime.debug.console
import oro.runtime.window.IWindowFragment
import oro.runtime.window.WindowFragment
import oro.runtime.window.WindowOptions

import __BUNDLE_IDENTIFIER__.R

/**
 * A `WindowFragmentManager` manages `WindowFragment` instances.
 */
open class WindowFragmentManager (
  protected val activity: WindowManagerActivity
) {
  open val fragments = mutableListOf<WindowFragment>()
  open val manager = activity.supportFragmentManager

  /**
   * Creates a new `WindowFragment` from `WindowOptions` if `options.index`
   * does not exist, otherwise this function is a "no-op".
   */
  fun createWindowFragment (options: WindowOptions) {
    val manager = this.manager
    if (this.hasWindowFragment(options.index)) {
      return
    }

    // If a fragment with this index already exists in the FragmentManager
    // (e.g., restored after configuration change), reuse it instead of creating a duplicate.
    val existing = manager.fragments.firstOrNull {
      it is WindowFragment && (it.arguments?.getInt("index") == options.index)
    } as? WindowFragment

    if (existing != null) {
      if (!this.fragments.contains(existing)) {
        this.fragments.add(existing)
      }
      return
    }

    val fragment = WindowFragment.newInstance(options)
    fragment.index = options.index
    this.fragments.add(fragment)
    manager.commit {
      // .setCustomAnimations(android.R.anim.slide_in_left, android.R.anim.slide_out_right)
      setReorderingAllowed(true)
      add(R.id.window, fragment)
      if (options.headless) {
        hide(fragment)
      }
    }
  }

  /**
   * Shows a `WindowFragment` for a given `index`.
   * This function returns `true` if the `WindowFragment` exists and was shown,
   * otherwise `false`.
   */
  fun showWindowFragment (index: Int): Boolean {
    val fragment = this.fragments.find { it.index == index }

    if (fragment != null) {
      this.manager.beginTransaction()
        // .setCustomAnimations(android.R.anim.slide_in_left, android.R.anim.slide_out_right)
        .show(fragment)
        .commit()
      return true
    }

    return false
  }

  /**
   * Hides a `WindowFragment` for a given `index`.
   * This function returns `true` if the `WindowFragment` exists and was hidden,
   * otherwise `false`.
   */
  fun hideWindowFragment (index: Int): Boolean {
    val fragment = this.fragments.find { it.index == index }

    if (fragment != null) {
      this.activity.runOnUiThread {
        this.manager.beginTransaction()
        // .setCustomAnimations(android.R.anim.slide_in_left, android.R.anim.slide_out_right)
          .hide(fragment)
          .commit()
      }
      return true
    }

    return false
  }

  /**
   * Closes a `WindowFragment` for a given `index`.
   * This function returns `true` if the `WindowFragment` exists and was hidden,
   * otherwise `false`.
   */
  fun closeWindowFragment (index: Int): Boolean {
    val fragment = this.fragments.find { it.index == index }

    if (fragment != null) {
      this.fragments.remove(fragment)
      this.activity.runOnUiThread {
        this.manager.beginTransaction()
          // .setCustomAnimations(android.R.anim.slide_in_left, android.R.anim.slide_out_right)
          .remove(fragment)
          .commit()
      }
      return true
    }

    return false
  }

  /**
   * Returns `true` if there is a `WindowFragment` for the given `index`,
   * otherwise `false`.
   */
  fun hasWindowFragment (index: Int): Boolean {
    return this.fragments.find { it.index == index } != null
  }

  /**
   * Gets a `WindowFragment` at `index` if one exists, otherwise
   * `null` is returned.
   */
  fun getWindowFragment (index: Int): WindowFragment? {
    return this.fragments.find { it.index == index }
  }

  /**
   * "Pops" a `WindowFragment` from the back stack, navigating to the
   * previous `WindowFragment`.
   * This function returns `true` if a `WindowFragment` was "popped"
   * from the back stack, otherwise `false`.
   */
  fun popWindowFragment (): Boolean {
    if (this.manager.backStackEntryCount > 0) {
      this.manager.popBackStack()
      return true
    }

    this.activity.finish()
    return false
  }

  /**
   * Navigates a `WindowFragment` at a given `index` window's webview to
   * a given `url`
   * This function returns `true` if a `WindowFragment` did navigate to the
   * given `url`, otherwise `false`.
   */
  fun navigateWindowFragment (index: Int, url: String): Boolean {
    val fragment = this.fragments.find { it.index == index }
    val window = fragment?.window

    if (window != null && fragment.hasWebView) {
      window.navigate(url)
      return true
    }

    return false
  }

  /**
   * Evaluates a JavaScript `source` string at a given window `index`.
   */
  fun evaluateJavaScriptInWindowFragmentView (index: Int, source: String, token: String): Boolean {
    return this.getWindowFragment(index)?.window?.evaluateJavaScript(source, token) ?: false
  }

  /**
   * Gets the window fragment width.
   */
  fun getWindowFragmentWidth (index: Int): Int {
    val fragment = this.fragments.find { it.index == index }

    if (fragment != null && fragment.hasWebView) {
      return fragment.webview.measuredWidth
    }

    return 0
  }

  /**
   * Gets the window fragment height.
   */
  fun getWindowFragmentHeight (index: Int): Int {
    val fragment = this.fragments.find { it.index == index }

    if (fragment != null && fragment.hasWebView) {
      return fragment.webview.measuredHeight
    }

    return 0
  }

  /**
   * Sets the window fragment width and height.
   */
  fun setWindowFragmentSize (index: Int, width: Int, height: Int): Boolean {
    val fragment = this.fragments.find { it.index == index } ?: return false
    val window = fragment.window ?: return false
    window.setSize(width, height)
    return true
  }

  /**
   * Gets the window fragment title
   */
  fun getWindowFragmentTitle (index: Int): String {
    val fragment = this.fragments.find { it.index == index } ?: return ""
    val window = fragment.window ?: return ""
    return window.title
  }

  /**
   * Sets the window fragment title
   */
  fun setWindowFragmentTitle (index: Int, title: String): Boolean {
    val fragment = this.fragments.find { it.index == index } ?: return false
    val window = fragment.window ?: return false
    window.title = title
    return true
  }

  /**
   * XXX
   */
  fun setWindowFragmentViewBackgroundColor (index: Int, color: Long): Boolean {
    val fragment = this.fragments.find { it.index == index } ?: return false
    if (!fragment.hasWebView) return false
    fragment.webview.setBackgroundColor(color.toInt())
    return true
  }

  /**
   * XXX
   */
  fun getWindowFragmentBackgroundColor (index: Int): Int {
    val fragment = this.fragments.find { it.index == index } ?: return 0
    if (!fragment.hasWebView) return 0
    val background = fragment.webview.background
    if (background == null) {
      return 0
    }
    val drawable = background as ColorDrawable
    val color = drawable.color
    return 0xFFFFFF and color
  }

  /**
   * XXX
   */
  fun setWindowFragmentViewPosition(index: Int, x: Float, y: Float): Boolean {
    val fragment = this.fragments.find { it.index == index } ?: return false
    if (!fragment.hasWebView) return false
    fragment.webview.setX(x)
    fragment.webview.setY(y)
    return true
  }
}

/**
 * The activity that is responsible for managing various WindowFragment
 * instances.
 */
open class WindowManagerActivity : AppCompatActivity(R.layout.window_container_view) {
  companion object {
    private const val PROCESS_STATE_KEY = "oro.runtime.process"
    private val processToken = java.util.UUID.randomUUID().toString()
  }

  open val windowFragmentManager = WindowFragmentManager(this)
  open val dialog = Dialog(this)
  internal val systemAppearance by lazy { SystemAppearance(this) }
  internal val presentation by lazy { SystemPresentation(this) }

  fun setSystemAppearance (index: Int, options: String): Boolean = systemAppearance.apply(index, options)

  fun emitBackGesture (phase: String, event: BackEventCompat? = null) {
    val fragment = windowFragmentManager.fragments.lastOrNull {
      it.isVisible && !it.options.headless && it.hasWebView
    } ?: return
    fragment.window?.bridge?.emit("backgesture", org.json.JSONObject().put("phase", phase)
      .put("progress", event?.progress ?: if (phase == "commit") 1f else 0f)
      .put("edge", event?.swipeEdge ?: org.json.JSONObject.NULL).toString())
  }

  override fun onConfigurationChanged (configuration: Configuration) {
    super.onConfigurationChanged(configuration)
    systemAppearance.refresh()
  }
  private var lastHapticTime = -100L

  fun performHaptic (index: Int, kind: String): Boolean {
    val fragment = windowFragmentManager.getWindowFragment(index) ?: return false
    if (!fragment.hasWebView || !fragment.isVisible || isFinishing || isDestroyed) return false
    val now = android.os.SystemClock.elapsedRealtime()
    if (now - lastHapticTime < 100) return false
    val feedback = when (kind) {
      "selection" -> android.view.HapticFeedbackConstants.TEXT_HANDLE_MOVE
      "confirmation" -> if (android.os.Build.VERSION.SDK_INT >= 30) android.view.HapticFeedbackConstants.CONFIRM
        else android.view.HapticFeedbackConstants.VIRTUAL_KEY
      "rejection" -> if (android.os.Build.VERSION.SDK_INT >= 30) android.view.HapticFeedbackConstants.REJECT
        else android.view.HapticFeedbackConstants.LONG_PRESS
      else -> return false
    }
    lastHapticTime = now
    return fragment.webview.performHapticFeedback(feedback)
  }

  fun markWindowReady (index: Int, explicit: Boolean) {
    val owner = this.windowFragmentManager.getWindowFragment(index)?.window ?: return
    this.runOnUiThread {
      val fragment = this.windowFragmentManager.getWindowFragment(index)
      if (fragment?.window === owner && owner.userMessageHandler.active) fragment.markReady(explicit)
    }
  }

  override fun onActivityResult (
    requestCode: Int,
    resultCode: Int,
    intent: Intent?
  ) {
    super.onActivityResult(requestCode, resultCode, intent)
  }

  override fun onCreate (savedInstanceState: Bundle?) {
    // Fragment indices are valid only while their native windows still exist.
    // A new process starts from configured windows without restoring WebView state.
    val restorableState = savedInstanceState?.takeIf {
      it.getString(PROCESS_STATE_KEY) == processToken
    }
    super.onCreate(restorableState)
    this.onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
      override fun handleOnBackStarted (backEvent: BackEventCompat) { emitBackGesture("start", backEvent) }
      override fun handleOnBackProgressed (backEvent: BackEventCompat) { emitBackGesture("progress", backEvent) }
      override fun handleOnBackCancelled () { emitBackGesture("cancel") }
      override fun handleOnBackPressed () {
        emitBackGesture("commit")
        val insets = ViewCompat.getRootWindowInsets(window.decorView)
        if (insets?.isVisible(WindowInsetsCompat.Type.ime()) == true) {
          androidx.core.view.WindowCompat.getInsetsController(window, window.decorView).hide(WindowInsetsCompat.Type.ime())
          return
        }
        if (presentation.exit("back")) return
        val fragment = windowFragmentManager.fragments.lastOrNull {
          it.isVisible && !it.options.headless
        }
        if (fragment is IWindowFragment && fragment.onBackPressed()) {
          return
        }

        if (!windowFragmentManager.popWindowFragment()) {
          isEnabled = false
          onBackPressedDispatcher.onBackPressed()
        }
      }
    })

    // Rehydrate our manager's fragment list from FragmentManager if Android restored state
    val existing = supportFragmentManager.fragments.filterIsInstance<WindowFragment>()
    if (existing.isNotEmpty()) {
      windowFragmentManager.fragments.apply {
        clear()
        addAll(existing)
      }
    }
  }

  override fun onPause () {
    presentation.exit("suspended")
    super.onPause()
  }

  override fun onDestroy () {
    presentation.destroy()
    dialog.close()
    super.onDestroy()
  }

  override fun onSaveInstanceState (outState: Bundle) {
    outState.putString(PROCESS_STATE_KEY, processToken)
    super.onSaveInstanceState(outState)
  }

  /**
   * Creates a new window at a given `index`.
   */
  fun createWindow (
    index: Int = 0,
    shouldExitApplicationOnClose: Boolean = false,
    headless: Boolean = false
  ) {
    this.windowFragmentManager.createWindowFragment(WindowOptions(
      index = index,
      shouldExitApplicationOnClose = shouldExitApplicationOnClose,
      headless = headless
    ))
  }

  /**
   * Shows a window at a given `index`.
   */
  fun showWindow (index: Int): Boolean {
    return this.windowFragmentManager.showWindowFragment(index)
  }

  /**
   * Hides a window at a given `index`.
   */
  fun hideWindow (index: Int): Boolean {
    return this.windowFragmentManager.hideWindowFragment(index)
  }

  /**
   * Closes a window at a given `index`.
   */
  fun closeWindow (index: Int): Boolean {
    return this.windowFragmentManager.closeWindowFragment(index)
  }

  /**
   * Navigates to a given `url` for a window at a given `index`.
   */
  fun navigateWindow (index: Int, url: String): Boolean {
    return this.windowFragmentManager.navigateWindowFragment(index, url)
  }

  /**
   * Get the measured window width at a given `index`.
   */
  fun getWindowWidth (index: Int): Int {
    return this.windowFragmentManager.getWindowFragmentWidth(index)
  }

  /**
   * Get the measured window height at a given `index`.
   */
  fun getWindowHeight (index: Int): Int {
    return this.windowFragmentManager.getWindowFragmentHeight(index)
  }

  /**
   * Sets the window `width` and `height` at a given `index`.
   */
  fun setWindowSize (index: Int, width: Int, height: Int): Boolean {
    return this.windowFragmentManager.setWindowFragmentSize(index, width, height)
  }

  /**
   * XXX
   */
  fun getWindowBackgroundColor (index: Int): Int {
    return this.windowFragmentManager.getWindowFragmentBackgroundColor(index)
  }

  /**
   * Get the window title at a given `index`.
   */
  fun getWindowTitle (index: Int): String {
    return this.windowFragmentManager.getWindowFragmentTitle(index)
  }

  /**
   * Sets the window title at a given `index`.
   */
  fun setWindowTitle (index: Int, title: String): Boolean {
    return this.windowFragmentManager.setWindowFragmentTitle(index, title)
  }

  /**
   * Sets the window background color at a given `index`.
   */
  fun setWindowBackgroundColor (index: Int, color: Long): Boolean {
    return this.windowFragmentManager.setWindowFragmentViewBackgroundColor(index, color)
  }

  /**
   * XXX
   */
  fun setWindowPosition (index: Int, x: Float, y: Float): Boolean {
    return this.windowFragmentManager.setWindowFragmentViewPosition(index, x, y)
  }

  /**
   * Evaluates JavaScript source in a window at a given `index`.
   */
  fun evaluateJavaScript (index: Int, source: String, token: String): Boolean {
    return this.windowFragmentManager.evaluateJavaScriptInWindowFragmentView(index, source, token)
  }

  /**
   * Gets the current screen `Insets`.
   */
  fun getScreenInsets (): Insets {
    return ViewCompat.getRootWindowInsets(window.decorView)?.getInsetsIgnoringVisibility(
      WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()
    ) ?: Insets.NONE
  }

  /**
   * Gets the current screen display metrics
   */
  fun getScreenDisplayMetrics (): DisplayMetrics {
    return this.resources.displayMetrics
  }

  /**
   * Returns `true` when the application is currently using the dark system theme.
   */
  fun isDarkMode (): Boolean {
    val uiMode = this.applicationContext.resources.configuration.uiMode
    val nightMode = uiMode and Configuration.UI_MODE_NIGHT_MASK
    return nightMode == Configuration.UI_MODE_NIGHT_YES
  }

  /**
   * Gets the screen width size.
   */
  fun getScreenSizeWidth (): Int {
    val windowManager = this.getSystemService(
      Context.WINDOW_SERVICE
    ) as WindowManager
    if (android.os.Build.VERSION.SDK_INT < 30) {
      val metrics = DisplayMetrics()
      windowManager.defaultDisplay.getMetrics(metrics)
      return metrics.widthPixels
    }
    val metrics = windowManager.currentWindowMetrics
    val insets = this.getScreenInsets()
    return metrics.bounds.width() - insets.left - insets.right
  }

  /**
   * Gets the screen height size.
   */
  fun getScreenSizeHeight (): Int {
    val windowManager = this.getSystemService(
      Context.WINDOW_SERVICE
    ) as WindowManager
    if (android.os.Build.VERSION.SDK_INT < 30) {
      val metrics = DisplayMetrics()
      windowManager.defaultDisplay.getMetrics(metrics)
      return metrics.heightPixels
    }
    val metrics = windowManager.currentWindowMetrics
    val insets = this.getScreenInsets()
    return metrics.bounds.height() - insets.top - insets.bottom
  }

  /**
   * Gets the Android asset manager for the application.
   */
  fun getAssetManager (): AssetManager {
    return this.applicationContext.resources.assets
  }
}
