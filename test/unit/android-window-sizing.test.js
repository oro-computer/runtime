import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const available = ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('Android explicit reload restores failed hidden workers without reloading healthy or interactive siblings', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
  const start = source.indexOf('  private fun restartWebView () {')
  const end = source.indexOf('\n  override fun onAttach', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-explicit-reload-'))
  const filename = path.join(directory, 'Reload.kt')
  const jar = path.join(directory, 'reload.jar')
  writeFileSync(filename, `
    object Events {
      val indices = mutableListOf<Int>()
      fun record(index: Int, phase: String) { check(phase == "reload"); indices.add(index) }
    }
    class Options(val headless: Boolean)
    class ViewGroup { object LayoutParams { const val MATCH_PARENT = -1 } }
    class FrameLayout {
      class LayoutParams(val width: Int, val height: Int)
      fun addView(view: WebView, index: Int, params: LayoutParams) { check(index == 0) }
    }
    class WebView(context: Any)
    class Window { var location = ""; fun navigate(value: String) { location = value } }
    class Manager { val fragments = mutableListOf<WindowFragment>() }
    class WindowManagerActivity { val windowFragmentManager = Manager() }
    class WindowFragment(val activity: Any, val index: Int, val options: Options) {
      var content: FrameLayout? = FrameLayout()
      var startupLocation: String? = "https://public.invalid/$index"
      var window: Window? = null
      var failed = true
      var loading = false
      var replacements = 0
      fun requireContext() = this
      fun releaseWebView() {}
      fun attachWebView(view: WebView) { replacements++; window = Window() }
      fun reload() { restartWebView() }
      ${source.slice(start, end).replaceAll('oro.runtime.app.RuntimeLifecycleEvents', 'Events')}
    }
    fun main() {
      val activity = WindowManagerActivity()
      val primary = WindowFragment(activity, 0, Options(false))
      val failedWorker = WindowFragment(activity, 70, Options(true))
      val healthyWorker = WindowFragment(activity, 71, Options(true)).apply { failed = false }
      val otherPage = WindowFragment(activity, 1, Options(false))
      activity.windowFragmentManager.fragments.addAll(listOf(primary, failedWorker, healthyWorker, otherPage))
      primary.reload()
      check(Events.indices == listOf(70, 0)) { "The explicit action must restore the failed hidden worker before its page" }
      check(primary.replacements == 1 && failedWorker.replacements == 1)
      check(healthyWorker.replacements == 0 && otherPage.replacements == 0) { "Other windows retain their lifecycle" }
      primary.reload()
      check(failedWorker.replacements == 1) { "A recovered worker is not restarted again" }
      primary.startupLocation = null
      failedWorker.failed = true
      primary.reload()
      check(failedWorker.replacements == 1) { "An unavailable page reload cannot restart workers" }
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})

test('Android full-view windows follow their container after rotation', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
  const start = source.indexOf('  fun setSize (size: Size) {')
  const end = source.indexOf('\n  fun handleApplicationURL', start)
  assert.ok(start >= 0 && end > start)
  const presentation = readFileSync(new URL('../../src/runtime/window/appearance.kt', import.meta.url), 'utf8')
  const presentationStart = presentation.indexOf('  fun resize (window: Window,')
  const presentationEnd = presentation.indexOf('\n  fun showCustomView', presentationStart)
  assert.ok(presentationStart > 0 && presentationEnd > presentationStart)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-window-sizing-'))
  const filename = path.join(directory, 'Regression.kt')
  const jar = path.join(directory, 'regression.jar')
  writeFileSync(filename, `
    data class Size(val width: Int, val height: Int)
    open class View(var width: Int, var height: Int, var parent: View? = null)
    class ViewGroup {
      open class LayoutParams(val width: Int, val height: Int) {
        companion object { const val MATCH_PARENT = -1 }
      }
    }
    class FrameLayout {
      class LayoutParams(width: Int, height: Int) : ViewGroup.LayoutParams(width, height)
    }
    class WebView {
      var layoutParams: ViewGroup.LayoutParams = FrameLayout.LayoutParams(0, 0)
      fun measuredSize(parent: View) = Size(
        if (layoutParams.width == -1) parent.width else layoutParams.width,
        if (layoutParams.height == -1) parent.height else layoutParams.height
      )
    }
    class Fragment(val viewport: View) {
      var view: View? = View(0, 0, viewport)
      var hasWebView = true
      val webview = WebView()
    }
    class Presentation {
      var owner: Window? = null
      var originalLayout: ViewGroup.LayoutParams? = null
      ${presentation.slice(presentationStart, presentationEnd).replaceAll('android.view.ViewGroup.LayoutParams', 'ViewGroup.LayoutParams')}
    }
    class Activity {
      val presentation = Presentation()
      val pending = mutableListOf<() -> Unit>()
      fun runOnUiThread(callback: () -> Unit) { pending.add(callback) }
      fun flush() { pending.toList().also { pending.clear() }.forEach { it() } }
    }
    class MessageHandler { var active = true }
    class Window(val fragment: Fragment, val activity: Activity) {
      val userMessageHandler = MessageHandler()
      var screenBounds = Size(1080, 2316)
      fun getScreen() = screenBounds
      ${source.slice(start, end)}
    }
    fun main() {
      val viewport = View(1080, 2155)
      val activity = Activity()
      val background = Fragment(viewport)
      val foreground = Fragment(viewport)
      val window = Window(foreground, activity)
      window.setSize(Size(1080, 2155))
      check(foreground.webview.layoutParams.width == 0) { "Resize must run on the UI thread" }
      activity.flush()
      check(foreground.webview.measuredSize(viewport) == Size(1080, 2155))
      // Edge-to-edge containers include system bars, while a 100% IPC request
      // uses screen bounds with those insets removed.
      viewport.height = 2404
      window.screenBounds = Size(1080, 2155)
      window.setSize(Size(1080, 2155))
      activity.flush()
      viewport.width = 2404
      viewport.height = 1080
      check(foreground.webview.measuredSize(viewport) == Size(2404, 1080)) {
        "A full-view request must not retain the portrait height after rotation"
      }
      window.screenBounds = Size(1080, 2316)
      viewport.width = 1080
      viewport.height = 2155
      // The front page must cover the container without another resize request.
      viewport.width = 2243
      viewport.height = 933
      check(foreground.webview.measuredSize(viewport) == Size(2243, 933))
      check(background.webview.layoutParams.width == 0) { "Sizing must not modify another page" }
      viewport.width = 1080
      viewport.height = 2155
      check(foreground.webview.measuredSize(viewport) == Size(1080, 2155))
      // Fixed dimensions below the container size remain explicit.
      window.setSize(Size(600, 400))
      activity.flush()
      viewport.width = 2243
      viewport.height = 933
      check(foreground.webview.measuredSize(viewport) == Size(600, 400))
      window.setSize(Size(2243, 400))
      activity.flush()
      viewport.width = 1080
      check(foreground.webview.measuredSize(viewport) == Size(1080, 400))
      // Before the container is measured, full-screen requests use screen bounds.
      foreground.view = null
      window.setSize(Size(1080, 2316))
      activity.flush()
      check(foreground.webview.layoutParams.width == -1 && foreground.webview.layoutParams.height == -1)
      window.setSize(Size(500, 300))
      window.userMessageHandler.active = false
      activity.flush()
      check(foreground.webview.layoutParams.width == -1) { "Ignore a resize queued for a destroyed WebView" }
      window.userMessageHandler.active = true
      activity.presentation.owner = window
      foreground.webview.layoutParams = FrameLayout.LayoutParams(-1, -1)
      window.setSize(Size(500, 300))
      activity.flush()
      check(foreground.webview.layoutParams.width == -1) { "Fullscreen must keep filling its container" }
      check(activity.presentation.originalLayout?.width == 500 && activity.presentation.originalLayout?.height == 300)
      activity.presentation.owner = null
      foreground.webview.layoutParams = activity.presentation.originalLayout!!
      check(foreground.webview.measuredSize(viewport) == Size(500, 300)) { "Exit must restore the latest requested frame" }
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})

test('Android IPC authenticates cross-scheme document and worker requests', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
  const start = source.indexOf('  fun acceptsIpcRequest (')
  const end = source.indexOf('\n  fun startNavigation', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-bridge-policy-'))
  const filename = path.join(directory, 'Policy.kt')
  const jar = path.join(directory, 'policy.jar')
  writeFileSync(filename, `
    data class Request(val requestHeaders: Map<String, String>, val isForMainFrame: Boolean = false, val url: String = "ipc://test")
    object Uri { fun parse(value: String) = value }
    class Handler { var active = true }
    class Window {
      val userMessageHandler = Handler()
      val bridgeNonce = "per-window-token"
      fun isTrustedOrigin(value: String) = value == "https://app.example"
      ${source.slice(start, end).replace('android.webkit.WebResourceRequest', 'Request')}
    }
    fun main() {
      val window = Window()
      val document = mapOf("X-Oro-Bridge-Token" to "per-window-token", "Sec-Fetch-Site" to "cross-site")
      check(window.acceptsIpcRequest(Request(document))) { "IPC is legitimately cross-scheme" }
      check(window.acceptsIpcRequest(Request(document + ("Origin" to "https://app.example"))))
      check(!window.acceptsIpcRequest(Request(document + ("Origin" to "https://foreign.example"))))
      check(!window.acceptsIpcRequest(Request(document, true))) { "Navigation cannot execute IPC" }
      check(!window.acceptsIpcRequest(Request(document, url = "ipc://" + "x".repeat(1048577)))) { "XHR URLs have the same message limit as WebMessage" }
      check(!window.acceptsIpcRequest(Request(emptyMap()))) { "Headerless requests are not authorized" }
      check(!window.acceptsIpcRequest(Request(mapOf("X-Oro-Bridge-Token" to "obsolete-token"))))
      val worker = mapOf("Runtime-Frame-Type" to "worker", "Origin" to "https://app.example")
      check(window.acceptsIpcRequest(Request(worker)))
      check(!window.acceptsIpcRequest(Request(worker - "Origin")))
      check(!window.acceptsIpcRequest(Request(worker + ("Origin" to "null"))))
      check(!window.acceptsIpcRequest(Request(worker + ("Origin" to "https://foreign.example"))))
      window.userMessageHandler.active = false
      check(!window.acceptsIpcRequest(Request(document))) { "Detached views cannot execute IPC" }
      check(!window.acceptsIpcRequest(Request(worker)))
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})

test('Android queued evaluation cannot run in a replacement renderer', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
  const start = source.indexOf('  fun evaluateJavaScript (source: String, token: String): Boolean {')
  const end = source.indexOf('\n  fun show ()', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-evaluation-'))
  const filename = path.join(directory, 'Evaluation.kt')
  const jar = path.join(directory, 'evaluation.jar')
  writeFileSync(filename, `
    class Activity {
      val pending = mutableListOf<() -> Unit>()
      fun runOnUiThread(callback: () -> Unit) { pending.add(callback) }
      fun flush() { pending.toList().also { pending.clear() }.forEach { it() } }
    }
    class Handler { var active = true }
    class WebView {
      val sources = mutableListOf<String>()
      val callbacks = mutableListOf<(String) -> Unit>()
      fun evaluateJavascript(source: String, callback: (String) -> Unit) {
        sources.add(source)
        callbacks.add(callback)
      }
    }
    class Fragment {
      var window: Window? = null
      var hasWebView = true
      var webview = WebView()
    }
    class Window(val fragment: Fragment, val activity: Activity) {
      val index = 0
      val userMessageHandler = Handler()
      var replies = 0
      fun onEvaluateJavascriptResult(index: Int, token: String, result: String) { replies++ }
      ${source.slice(start, end)}
    }
    fun main() {
      val activity = Activity()
      val fragment = Fragment()
      val first = Window(fragment, activity)
      fragment.window = first
      check(first.evaluateJavaScript("initial", "one"))
      activity.flush()
      check(fragment.webview.sources == listOf("initial"))
      fragment.webview.callbacks.single()("true")
      check(first.replies == 1)
      check(first.evaluateJavaScript("obsolete", "two"))
      first.userMessageHandler.active = false
      val second = Window(fragment, activity)
      fragment.window = second
      fragment.webview = WebView()
      activity.flush()
      check(fragment.webview.sources.isEmpty()) { "Old queued source reached the replacement renderer" }
      check(!first.evaluateJavaScript("detached", "three"))
      check(second.evaluateJavaScript("current", "four"))
      activity.flush()
      val lateResult = fragment.webview.callbacks.single()
      second.userMessageHandler.active = false
      fragment.window = Window(fragment, activity)
      lateResult("true")
      check(second.replies == 0) { "Old renderer result reached a replacement window" }
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
