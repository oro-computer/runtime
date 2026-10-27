import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import vm from 'node:vm'

const source = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
const json = process.env.ORO_TEST_JSON_JAR
const available = json && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('compiled startup presentation fences frames, retains readiness/recovery and rejects stale or foreign documents', {
  skip: !available ? 'Kotlin, a JVM and ORO_TEST_JSON_JAR are required' : false
}, () => {
  const start = source.indexOf('  fun beginStartup () {')
  const end = source.indexOf('  private fun showRecovery (', start)
  assert.ok(start >= 0 && end > start)
  const operations = readFileSync(new URL('../../src/runtime/app/platform_operations.kt', import.meta.url), 'utf8')
  const app = readFileSync(new URL('../../src/runtime/app/app.kt', import.meta.url), 'utf8')
  const eventStart = app.indexOf('internal object RuntimeLifecycleEvents {')
  const eventEnd = app.indexOf('\n/**', eventStart)
  assert.ok(eventStart >= 0 && eventEnd > eventStart)
  const guardStart = operations.indexOf('  fun document (): Window {')
  const guardEnd = operations.indexOf('  fun success (', guardStart)
  assert.ok(guardStart >= 0 && guardEnd > guardStart)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro startup presentation '))
  const filename = path.join(directory, 'Startup.kt')
  const jar = path.join(directory, 'startup.jar')
  writeFileSync(filename, `
package oro.runtime.window
import java.util.concurrent.atomic.AtomicBoolean
import org.json.JSONObject
import org.json.JSONArray
object View { const val IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS = 4; const val IMPORTANT_FOR_ACCESSIBILITY_AUTO = 0 }
class Uri(val value: String) {
 val scheme get() = java.net.URI(value).scheme
 val authority get() = java.net.URI(value).rawAuthority
 companion object { fun parse(value: String) = Uri(value) }
}
class Lifecycle {
 enum class State { STARTED, RESUMED; fun isAtLeast(other: State) = this >= other }
 var currentState = State.RESUMED
}
class WebView {
 var url: String? = "https://public.app/index.html"; var isAttachedToWindow = true
 var importantForAccessibility = 0
 abstract class VisualStateCallback { abstract fun onComplete(requestId: Long) }
 val callbacks = mutableListOf<VisualStateCallback>()
 fun postVisualStateCallback(id: Long, callback: VisualStateCallback) { callbacks.add(callback) }
 fun frame() { callbacks.toList().also { callbacks.clear() }.forEach { it.onComplete(0) } }
}
class FrameLayout {
 var removals = 0; var failRemove = false
 fun removeView(view: Any) {
  if (failRemove) { failRemove = false; throw IllegalStateException("public fixture") }
  removals++
 }
}
class Handler {
 val pending = mutableListOf<Runnable>()
 fun postDelayed(task: Runnable, delay: Long) { check(delay == 30000L); pending.add(task) }
 fun removeCallbacks(task: Runnable) { pending.removeAll { it === task } }
}
object SystemClock { var now = 0L; fun elapsedRealtime() = now }
${app.slice(eventStart, eventEnd).replaceAll('android.os.SystemClock', 'SystemClock')}
object console { fun error(message: String) {} }
object R { object string { const val runtime_startup_failed = 1 } }
class Options { var headless = false }
class Presentation { fun navigation(window: Window) {} }
class Activity {
 var isFinishing = false; var isDestroyed = false; val lifecycle = Lifecycle(); val presentation = Presentation()
}
class Messages { var active = true }
class Window(val fragment: WindowFragment) {
 val activity = Activity(); val userMessageHandler = Messages(); var platformDocumentToken = "first"
 fun getBundleIdentifier() = "public.app"
 fun isTrustedOrigin(uri: Uri) = uri.authority in listOf("public.app", "approved.example")
}
class App {
 var packagedAssetsVerified = true
 fun getUserConfigValue(key: String) = "true"
 companion object { @JvmField val instance = App(); fun getInstance() = instance }
}
class PlatformFailure(val code: String): Exception(code)
class PlatformCall(val window: Window?, val app: App = App.instance) {
 val documentToken = window?.platformDocumentToken ?: ""
 val done = AtomicBoolean(false); var result: String? = null
 init { PlatformOperations.calls.add(this) }
 ${operations.slice(guardStart, guardEnd)}
 fun success() { if (done.compareAndSet(false, true)) result = "success" }
 fun failure(code: String) { if (done.compareAndSet(false, true)) result = code }
}
object PlatformOperations {
 val calls = mutableListOf<PlatformCall>()
 fun cancelWindow(window: Window) { calls.filter { it.window === window }.forEach { it.failure("ABORT_ERR") } }
}
class WindowFragment {
 val index = 0; val options = Options(); var isHidden = false
 var currentWebView: WebView? = WebView(); val hasWebView get() = currentWebView != null
 val webview get() = checkNotNull(currentWebView)
 var content: FrameLayout? = FrameLayout(); var recovery: Any? = null
 var window: Window? = Window(this)
 var failed = false; var loading = false; var startupPresented = false; var startupStartedAt = 0L
 val readyForPresentation get() = hasWebView && !loading && !failed
 val startupHandler = Handler(); val startupWatchdog = Runnable { checkStartupDeadline() }
 var errorCover = false
 fun showRecovery(error: Boolean) { recovery = Any(); errorCover = error }
 ${source.slice(start, end).replaceAll('oro.runtime.app.', '')}
}
fun expectFailure(code: String, action: () -> Unit) {
 try { action(); error("Expected " + code) } catch (error: PlatformFailure) { check(error.code == code) }
}
fun fixture(): WindowFragment {
 SystemClock.now = 0; App.instance.packagedAssetsVerified = true
 return WindowFragment().also { it.beginStartup() }
}
fun main() {
 val first = fixture(); val call = PlatformCall(first.window); first.presentStartup(call)
 check(first.recovery != null && call.result == null && first.startupHandler.pending.size == 1)
 first.webview.frame()
 check(first.recovery == null && first.loading && !first.readyForPresentation)
 check(first.startupHandler.pending.size == 1 && call.result == "success")
 val recorded = RuntimeLifecycleEvents.list()
 check((0 until recorded.length()).any { recorded.getJSONObject(it).getString("phase") == "presented" })
 check(first.webview.importantForAccessibility == View.IMPORTANT_FOR_ACCESSIBILITY_AUTO)
 val duplicate = PlatformCall(first.window); first.presentStartup(duplicate)
 check(duplicate.result == "success" && first.webview.callbacks.isEmpty())
 first.markReady(false); check(first.loading)
 SystemClock.now = 30000; first.checkStartupDeadline()
 check(first.failed && first.errorCover && first.recovery != null && !first.readyForPresentation)
 first.markReady(true); check(first.recovery != null)
 expectFailure("ABORT_ERR") { first.presentStartup(PlatformCall(first.window)) }
 val ready = fixture(); val pending = PlatformCall(ready.window); ready.presentStartup(pending)
 ready.markReady(true); check(ready.readyForPresentation && ready.startupHandler.pending.isEmpty())
 ready.webview.frame(); check(pending.result == "success" && ready.readyForPresentation)
 val next = fixture(); next.presentStartup(PlatformCall(next.window)); next.webview.frame()
 check(next.recovery == null); next.window!!.platformDocumentToken = "second"; next.beginStartup()
 check(next.recovery != null && !next.startupPresented && next.startupHandler.pending.size == 1)
 val stale = fixture(); val old = PlatformCall(stale.window); stale.presentStartup(old)
 stale.window!!.platformDocumentToken = "replacement"; stale.webview.frame()
 check(old.result == "ENOTREADY" && stale.recovery != null)
 val replaced = fixture(); val lost = PlatformCall(replaced.window); replaced.presentStartup(lost)
 val previous = replaced.webview; replaced.currentWebView = WebView(); previous.frame()
 check(lost.result == "ABORT_ERR" && replaced.recovery != null)
 val canceled = fixture(); val cancel = PlatformCall(canceled.window); canceled.presentStartup(cancel)
 cancel.failure("ABORT_ERR"); canceled.webview.frame(); check(canceled.recovery != null)
 val timed = fixture(); val late = PlatformCall(timed.window); timed.presentStartup(late)
 SystemClock.now = 30000; timed.checkStartupDeadline(); timed.webview.frame()
 check(late.result == "ABORT_ERR" && timed.errorCover && timed.recovery != null)
 val callbackFailure = fixture(); val broken = PlatformCall(callbackFailure.window)
 callbackFailure.content!!.failRemove = true
 callbackFailure.presentStartup(broken); callbackFailure.webview.frame()
 check(broken.result == "EIO" && callbackFailure.failed && callbackFailure.errorCover && callbackFailure.recovery != null)
 for (url in listOf("https://approved.example/index.html", "http://public.app/index.html", "https://public.app:443/index.html")) {
  val foreign = fixture(); foreign.webview.url = url
  expectFailure(if (url.contains("approved.example") || url.startsWith("http:")) "EACCES" else "ENOTREADY") { foreign.presentStartup(PlatformCall(foreign.window)) }
  check(foreign.recovery != null && foreign.webview.callbacks.isEmpty())
 }
 val corrupt = fixture(); App.instance.packagedAssetsVerified = false
 expectFailure("EACCES") { corrupt.presentStartup(PlatformCall(corrupt.window)) }
 val headless = fixture(); headless.options.headless = true
 expectFailure("ENOTREADY") { headless.presentStartup(PlatformCall(headless.window)) }
 val detached = fixture(); detached.webview.isAttachedToWindow = false
 expectFailure("ENOTREADY") { detached.presentStartup(PlatformCall(detached.window)) }
 val hidden = fixture(); hidden.isHidden = true; hidden.window!!.activity.lifecycle.currentState = Lifecycle.State.STARTED
 val own = PlatformCall(hidden.window); hidden.presentStartup(own); hidden.webview.frame()
 check(own.result == "success" && hidden.isHidden)
 println("Presentation ownership, frame fence, readiness and recovery passed")
}
`)
  const compiled = spawnSync('kotlinc', [filename, '-classpath', json, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, json].join(path.delimiter), 'oro.runtime.window.StartupKt'], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})

test('startup presentation API requires its own window and propagates native rejection', async () => {
  const api = readFileSync(new URL('../../api/window.js', import.meta.url), 'utf8')
  const start = api.indexOf('  async markPresented () {')
  const end = api.indexOf('\n  /**', start)
  assert.ok(start >= 0 && end > start)
  const calls = []
  let failure = null
  const platformRequest = async (...args) => { calls.push(args); if (failure) throw failure }
  const Window = vm.runInNewContext(`class Window {
    #index; #senderWindowIndex = 0
    constructor(index) { this.#index = index }
    ${api.slice(start, end)}
  }; Window`, { platformRequest })
  await new Window(0).markPresented()
  assert.deepEqual(JSON.parse(JSON.stringify(calls[0])), ['startup', 'present', {}])
  await assert.rejects(new Window(1).markPresented(), /Only the current window/)
  assert.equal(calls.length, 1)
  failure = new Error('EACCES')
  await assert.rejects(new Window(0).markPresented(), error => error === failure)
})

test('compiled startup and gesture IPC guard rejects subframes, workers and a different bridge window', t => {
  const compiler = ['clang++', 'c++'].find(command =>
    spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
  if (!compiler) return t.skip('A C++ compiler is required')
  const routes = readFileSync(new URL('../../src/runtime/ipc/routes.cc', import.meta.url), 'utf8')
  const start = routes.indexOf('    if (feature == "startup" || feature == "gestures") {')
  const end = routes.indexOf('    const auto token = nextPlatformOperation.fetch_add(1);', start)
  assert.ok(start >= 0 && end > start)
  const guard = routes.slice(start, end).replace(/return reply\(Result::Err \{[\s\S]*?\}\}\);/, 'return false;')
  assert.ok(!guard.includes('reply('))
  const directory = mkdtempSync(path.join(tmpdir(), 'oro startup IPC guard '))
  const filename = path.join(directory, 'guard.cc')
  const executable = path.join(directory, 'guard')
  writeFileSync(filename, `
#include <cassert>
#include <string>
using String = std::string;
struct Window { int index = 7; };
struct Manager { Window* owner = nullptr; Window* getWindowForBridge(int*) { return owner; } };
struct App { struct { Manager windowManager; } runtime; };
struct Router { int bridge; };
struct Message { int index = 7; String frame = "top-level"; String get(const String&) { return frame; } };
bool allowed(App* app, Router* router, Message message, const String& feature) {
 ${guard}
 return true;
}
int main() {
 for (const String feature : {"startup", "gestures"}) {
   App app; Router router; Window window; Message message;
   assert(!allowed(&app, &router, message, feature));
   app.runtime.windowManager.owner = &window;
   assert(allowed(&app, &router, message, feature));
   for (const String frame : {"", "subframe", "worker"}) { message.frame = frame; assert(!allowed(&app, &router, message, feature)); }
   message.frame = "top-level"; message.index = 9; assert(!allowed(&app, &router, message, feature));
 }
}
`)
  const compiled = spawnSync(compiler, ['-std=c++20', '-Werror', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
