package oro.runtime.window

import android.content.res.Configuration
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.view.View
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsAnimationCompat
import org.json.JSONObject

/** Opt-in surface ownership: existing application inset listeners remain in control until requested. */
internal class SystemAppearance (private val activity: WindowManagerActivity) {
  companion object {
    private const val STORE = "oro.runtime.appearance"

    /** Applies only a previously opted-in primary-window surface before view creation. */
    fun applySaved (activity: WindowManagerActivity) {
      val saved = activity.getSharedPreferences(STORE, android.content.Context.MODE_PRIVATE)
      val theme = saved.getString("theme", null) ?: return
      if (theme !in listOf("system", "light", "dark") || !saved.contains("color")) return
      applyColors(activity, theme, saved.getInt("color", Color.BLACK))
    }

    private fun applyColors (activity: WindowManagerActivity, theme: String, color: Int) {
      val dark = when (theme) {
        "dark" -> true
        "light" -> false
        else -> activity.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
      }
      activity.window.setBackgroundDrawable(ColorDrawable(color))
      activity.window.decorView.setBackgroundColor(color)
      activity.window.statusBarColor = color
      activity.window.navigationBarColor = color
      WindowCompat.getInsetsController(activity.window, activity.window.decorView).apply {
        isAppearanceLightStatusBars = !dark
        isAppearanceLightNavigationBars = !dark
      }
    }
  }

  private data class Options (val theme: String, val color: Int, val insets: String)
  private val options = mutableMapOf<WindowFragment, Options>()
  private var hasOwnedAppearance = false
  private val originalSoftInput = activity.window.attributes.softInputMode
  private val originalStatus = activity.window.statusBarColor
  private val originalNavigation = activity.window.navigationBarColor
  private val originalBackground = activity.window.decorView.background?.constantState?.newDrawable()?.mutate()
  private val controller = WindowCompat.getInsetsController(activity.window, activity.window.decorView)
  private val originalLightStatus = controller.isAppearanceLightStatusBars
  private val originalLightNavigation = controller.isAppearanceLightNavigationBars

  fun apply (index: Int, source: String): Boolean {
    if (source.length > 4096) return false
    val fragment = activity.windowFragmentManager.getWindowFragment(index) ?: return false
    val view = fragment.view ?: return false
    val data = try { JSONObject(source) } catch (_: Exception) { return false }
    val theme = data.optString("theme", "system")
    val ownership = data.optString("insets", "web")
    val color = data.optString("color", "")
    if (theme !in listOf("system", "light", "dark") || ownership !in listOf("native", "web") ||
        !Regex("#[a-fA-F0-9]{6}").matches(color)) return false
    val surfaceColor = Color.parseColor(color)
    if (index == 0 && !activity.getSharedPreferences(STORE, android.content.Context.MODE_PRIVATE).edit()
        .putString("theme", theme).putInt("color", surfaceColor).commit()) return false
    options[fragment] = Options(theme, surfaceColor, ownership)
    hasOwnedAppearance = true
    WindowCompat.setDecorFitsSystemWindows(activity.window, false)
    ViewCompat.setOnApplyWindowInsetsListener(view) { _, insets ->
      updateInsets(fragment, view, insets, "change", 1f)
      insets
    }
    ViewCompat.setWindowInsetsAnimationCallback(view, object : WindowInsetsAnimationCompat.Callback(DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
      override fun onPrepare (animation: WindowInsetsAnimationCompat) {
        if (animation.typeMask and WindowInsetsCompat.Type.ime() != 0) {
          ViewCompat.getRootWindowInsets(view)?.let { updateInsets(fragment, view, it, "start", 0f) }
        }
      }
      override fun onProgress (insets: WindowInsetsCompat, animations: MutableList<WindowInsetsAnimationCompat>): WindowInsetsCompat {
        val ime = animations.lastOrNull { it.typeMask and WindowInsetsCompat.Type.ime() != 0 }
        if (ime != null) updateInsets(fragment, view, insets, "progress", ime.interpolatedFraction)
        return insets
      }
      override fun onEnd (animation: WindowInsetsAnimationCompat) {
        if (animation.typeMask and WindowInsetsCompat.Type.ime() != 0) {
          ViewCompat.getRootWindowInsets(view)?.let { updateInsets(fragment, view, it, "end", animation.interpolatedFraction) }
        }
      }
    })
    refresh()
    ViewCompat.requestApplyInsets(view)
    return true
  }

  private fun updateInsets (fragment: WindowFragment, view: View, insets: WindowInsetsCompat, phase: String, progress: Float) {
    val option = options[fragment] ?: return
    if (!fragment.hasWebView) return
    val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
    val ime = insets.getInsets(WindowInsetsCompat.Type.ime())
    val bottom = maxOf(bars.bottom, ime.bottom)
    if (option.insets == "native") view.setPadding(bars.left, bars.top, bars.right, bottom)
    else view.setPadding(0, 0, 0, 0)
    val density = view.resources.displayMetrics.density.toDouble()
    val data = JSONObject().put("ownership", option.insets).put("phase", phase)
      .put("progress", progress.coerceIn(0f, 1f)).put("left", bars.left / density)
      .put("top", bars.top / density).put("right", bars.right / density)
      .put("bottom", bars.bottom / density).put("imeBottom", ime.bottom / density)
      .put("imeVisible", insets.isVisible(WindowInsetsCompat.Type.ime()))
    fragment.window?.bridge?.emit("windowinsetschange", data.toString())
  }

  fun isManaged (fragment: WindowFragment): Boolean = options.containsKey(fragment)

  fun refresh () {
    if (!hasOwnedAppearance) {
      applySaved(activity)
      return
    }
    val foreground = activity.windowFragmentManager.fragments.lastOrNull {
      it.isVisible && !it.options.headless && it.hasWebView
    }
    val option = options[foreground]
    if (option == null) {
      activity.window.statusBarColor = originalStatus
      activity.window.navigationBarColor = originalNavigation
      activity.window.setBackgroundDrawable(originalBackground)
      activity.window.decorView.background = originalBackground
      controller.isAppearanceLightStatusBars = originalLightStatus
      controller.isAppearanceLightNavigationBars = originalLightNavigation
      activity.window.setSoftInputMode(originalSoftInput)
      return
    }
    // Android 15+ draws transparent bars over this explicit surface color.
    applyColors(activity, option.theme, option.color)
    activity.window.setSoftInputMode((originalSoftInput and android.view.WindowManager.LayoutParams.SOFT_INPUT_MASK_ADJUST.inv()) or
      android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING)
  }

  fun detach (fragment: WindowFragment) {
    if (options.remove(fragment) == null) return
    fragment.view?.let {
      ViewCompat.setOnApplyWindowInsetsListener(it, null)
      ViewCompat.setWindowInsetsAnimationCallback(it, null)
    }
    refresh()
  }
}

/** Activity-wide presentation is owned by a live foreground Window, never its reusable index. */
internal class SystemPresentation (private val activity: WindowManagerActivity) {
  private var owner: Window? = null
  private var overlay: android.widget.FrameLayout? = null
  private var customContent: View? = null
  private var customPadding: IntArray? = null
  private var customCallback: android.webkit.WebChromeClient.CustomViewCallback? = null
  private var originalLayout: android.view.ViewGroup.LayoutParams? = null
  private var originalBars = 0
  private var originalBehavior = 0
  private val protectedWindows = mutableSetOf<Window>()
  private var ownsProtection = false
  private var originalSecure = false
  private val controller get() = WindowCompat.getInsetsController(activity.window, activity.window.decorView)

  private fun foreground (): Window? = activity.windowFragmentManager.fragments.lastOrNull {
    it.isVisible && !it.options.headless && it.hasWebView
  }?.window

  private fun usable (window: Window): Boolean = !activity.isFinishing && !activity.isDestroyed &&
    window.userMessageHandler.active && window.fragment.window === window && window.fragment.hasWebView && foreground() === window

  fun state (): JSONObject = JSONObject().put("supported", android.os.Build.VERSION.SDK_INT >= 30)
    .put("active", owner != null).put("owner", owner?.index ?: JSONObject.NULL)
    .put("customView", customCallback != null).put("requiresManagedInsets", true)
    .put("pointerCapture", false)

  fun request (window: Window, action: String, enabled: Boolean): String {
    if (!usable(window)) return failure("ENOTACTIVE", "A live foreground window is required")
    return when (action) {
      "fullscreen-status" -> JSONObject().put("data", state()).toString()
      "fullscreen" -> {
        if (enabled) {
          if (!enter(window)) return failure("ENOTSUP", "Fullscreen requires a ready foreground window with managed insets")
        } else if (owner != null && owner !== window) {
          return failure("EBUSY", "Another window owns fullscreen")
        } else exit("requested")
        JSONObject().put("data", state()).toString()
      }
      "protection" -> {
        if (!ownsProtection) {
          originalSecure = activity.window.attributes.flags and android.view.WindowManager.LayoutParams.FLAG_SECURE != 0
          ownsProtection = true
        }
        if (enabled) protectedWindows.add(window) else protectedWindows.remove(window)
        refreshProtection()
        JSONObject().put("data", JSONObject().put("enabled", enabled).put("activityProtected",
          activity.window.attributes.flags and android.view.WindowManager.LayoutParams.FLAG_SECURE != 0)).toString()
      }
      else -> failure("EINVAL", "Unknown presentation operation")
    }
  }

  private fun failure (code: String, message: String): String = JSONObject().put("err",
    JSONObject().put("code", code).put("message", message)).toString()

  private fun enter (window: Window, custom: View? = null, callback: android.webkit.WebChromeClient.CustomViewCallback? = null): Boolean {
    if (android.os.Build.VERSION.SDK_INT < 30 || !usable(window) || !window.fragment.readyForPresentation || !activity.hasWindowFocus() ||
        !activity.systemAppearance.isManaged(window.fragment) || (custom?.parent != null)) return false
    if (owner != null) return owner === window && custom == null
    val insets = ViewCompat.getRootWindowInsets(activity.window.decorView) ?: return false
    originalBars = (if (insets.isVisible(WindowInsetsCompat.Type.statusBars())) WindowInsetsCompat.Type.statusBars() else 0) or
      (if (insets.isVisible(WindowInsetsCompat.Type.navigationBars())) WindowInsetsCompat.Type.navigationBars() else 0)
    originalBehavior = controller.systemBarsBehavior
    val container = activity.findViewById<android.view.ViewGroup>(android.R.id.content) ?: return false
    val layer = android.widget.FrameLayout(activity)
    if (custom != null) {
      layer.setBackgroundColor(Color.BLACK)
      customContent = custom
      customPadding = intArrayOf(custom.paddingLeft, custom.paddingTop, custom.paddingRight, custom.paddingBottom)
      layer.addView(custom, android.widget.FrameLayout.LayoutParams(-1, -1))
    }
    val exitControl = android.widget.Button(activity).apply {
      text = activity.getString(__BUNDLE_IDENTIFIER__.R.string.runtime_fullscreen_exit)
      setOnClickListener { exit("control") }
    }
    val buttonLayout = android.widget.FrameLayout.LayoutParams(-2, -2, android.view.Gravity.TOP or android.view.Gravity.END)
    layer.addView(exitControl, buttonLayout)
    ViewCompat.setOnApplyWindowInsetsListener(layer) { _, current ->
      val safe = current.getInsetsIgnoringVisibility(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
      buttonLayout.setMargins(safe.left, safe.top, safe.right, 0)
      exitControl.layoutParams = buttonLayout
      if (custom != null) {
        val cutout = current.getInsets(WindowInsetsCompat.Type.displayCutout())
        val ime = current.getInsets(WindowInsetsCompat.Type.ime())
        custom.setPadding(cutout.left, cutout.top, cutout.right, maxOf(cutout.bottom, ime.bottom))
      }
      current
    }
    owner = window
    originalLayout = window.fragment.webview.layoutParams
    window.fragment.webview.layoutParams = android.widget.FrameLayout.LayoutParams(-1, -1)
    overlay = layer
    customCallback = callback
    container.addView(layer, android.view.ViewGroup.LayoutParams(-1, -1))
    controller.systemBarsBehavior = androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    controller.hide(WindowInsetsCompat.Type.systemBars())
    ViewCompat.requestApplyInsets(layer)
    window.bridge.emit("windowfullscreenchange", state().put("reason", "requested").toString())
    return true
  }

  fun resize (window: Window, requested: android.view.ViewGroup.LayoutParams): Boolean {
    if (owner !== window) return false
    originalLayout = requested
    return true
  }

  fun showCustomView (window: Window, view: View, callback: android.webkit.WebChromeClient.CustomViewCallback) {
    if (!usable(window) || !window.isTrustedOrigin(android.net.Uri.parse(window.fragment.webview.url ?: "")) || !enter(window, view, callback)) {
      try { callback.onCustomViewHidden() } catch (_: RuntimeException) { }
    }
  }

  fun navigation (window: Window) {
    if (owner === window) exit("navigation")
  }

  fun hideCustomView (window: Window) {
    if (owner === window && customCallback != null) exit("browser")
  }

  fun exit (reason: String): Boolean {
    val previous = owner ?: return false
    owner = null
    val restore = originalLayout
    originalLayout = null
    val layer = overlay
    overlay = null
    val callback = customCallback
    customCallback = null
    val padding = customPadding
    if (padding != null) customContent?.setPadding(padding[0], padding[1], padding[2], padding[3])
    customPadding = null
    customContent = null
    if (layer != null) {
      ViewCompat.setOnApplyWindowInsetsListener(layer, null)
      layer.removeAllViews()
      (layer.parent as? android.view.ViewGroup)?.removeView(layer)
    }
    controller.systemBarsBehavior = originalBehavior
    for (kind in listOf(WindowInsetsCompat.Type.statusBars(), WindowInsetsCompat.Type.navigationBars())) {
      if (originalBars and kind != 0) controller.show(kind) else controller.hide(kind)
    }
    try { callback?.onCustomViewHidden() } catch (_: RuntimeException) { }
    if (previous.userMessageHandler.active && previous.fragment.hasWebView && previous.fragment.window === previous) {
      if (restore != null) previous.fragment.webview.layoutParams = restore
      previous.bridge.emit("windowfullscreenchange", state().put("reason", reason).toString())
      if (reason in listOf("requested", "back", "browser", "control") && foreground() === previous) previous.fragment.webview.requestFocus()
    }
    return true
  }

  fun refresh () {
    val previous = owner
    if (previous != null && !usable(previous)) exit("window-changed")
    refreshProtection()
  }

  private fun refreshProtection () {
    if (!ownsProtection) return
    if (originalSecure || protectedWindows.any { it.fragment.window === it && it.fragment.isVisible }) {
      activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
    } else activity.window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
  }

  fun detach (window: Window) {
    if (owner === window) exit("detached")
    protectedWindows.remove(window)
    refreshProtection()
  }

  fun destroy () {
    exit("destroyed")
    protectedWindows.clear()
    refreshProtection()
  }
}
