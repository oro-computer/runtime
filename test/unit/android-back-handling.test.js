import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { spawnSync } from 'node:child_process'
import path from 'node:path'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const available = stdlib && json && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('native application Back coalesces presses, bounds fallback and rejects replacement-document replies', {
  skip: !available ? 'Kotlin, a JVM, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR are required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-back-handling-'))
  const filename = path.join(directory, 'Back.kt')
  const jar = path.join(directory, 'back.jar')
  const source = readFileSync(new URL('../../src/runtime/window/back.kt', import.meta.url), 'utf8')
    .replace(/^package .*\n/gm, '').replace(/^import .*\n/gm, '')
  writeFileSync(filename, `
package oro.runtime.window
import org.json.JSONObject
import java.util.UUID
class Looper { companion object { fun getMainLooper() = Looper() } }
class Handler(looper: Looper) {
  companion object {
    val pending = mutableListOf<Runnable>()
    fun advance() { for (task in pending.toList()) if (pending.remove(task)) task.run() }
  }
  fun postDelayed(task: Runnable, delay: Long) { check(delay == 750L); pending.add(task) }
  fun removeCallbacks(task: Runnable) { pending.remove(task) }
}
class Lifecycle {
  enum class State { STOPPED, RESUMED; fun isAtLeast(other: State) = this >= other }
  var currentState = State.RESUMED
}
object Uri { fun parse(value: String) = value }
class Options { var headless = false }
class WebView { var isAttachedToWindow = true; var url = "https://public.example" }
class Fragment(val window: Window) {
  var isHidden = false; var isVisible = true; var hasWebView = true
  val webview = WebView(); val options = Options()
}
class Manager { val fragments = mutableListOf<Fragment>() }
class WindowManagerActivity {
  var isFinishing = false; var isDestroyed = false
  val lifecycle = Lifecycle(); val windowFragmentManager = Manager()
}
class Bridge {
  val events = mutableListOf<JSONObject>()
  fun emit(name: String, value: String) { check(name == "backrequest"); events.add(JSONObject(value)) }
}
class MessageHandler { var active = true }
class Window(val activity: WindowManagerActivity) {
  var platformDocumentToken = "first"; val userMessageHandler = MessageHandler()
  val fragment = Fragment(this); val bridge = Bridge()
  fun isTrustedOrigin(value: String) = value == "https://public.example"
}
class PlatformCall(val window: Window) {
  val documentToken get() = window.platformDocumentToken
  var result: Any? = null
  fun foreground() = window
  fun success(value: Any? = null) { result = value }
}
class PlatformFailure(val code: String): Exception()
${source}
fun main() {
  val activity = WindowManagerActivity()
  val window = Window(activity)
  activity.windowFragmentManager.fragments.add(window.fragment)
  val navigation = ApplicationBackNavigation(activity)
  fun enable(): String {
    val call = PlatformCall(window)
    navigation.request(call, "enable", JSONObject())
    return (call.result as JSONObject).getString("registration")
  }
  fun respond(registration: String, handled: Boolean) {
    navigation.request(PlatformCall(window), "respond", JSONObject()
      .put("registration", registration).put("id", window.bridge.events.last().getString("id")).put("handled", handled))
  }
  var fallbacks = 0
  val first = enable()
  check(navigation.dispatch { fallbacks++ })
  check(navigation.dispatch { fallbacks++ })
  check(window.bridge.events.size == 1)
  respond(first, true)
  Handler.advance()
  check(fallbacks == 0)
  check(navigation.dispatch { fallbacks++ })
  respond(first, false)
  check(fallbacks == 1)
  check(navigation.dispatch { fallbacks++ })
  Handler.advance()
  check(fallbacks == 2)
  check(navigation.dispatch { fallbacks++ })
  window.platformDocumentToken = "replacement"
  Handler.advance()
  check(fallbacks == 2)
  check(runCatching { respond(first, true) }.exceptionOrNull() is PlatformFailure)
  val replacement = enable()
  navigation.request(PlatformCall(window), "disable", JSONObject().put("registration", first))
  check(navigation.dispatch { fallbacks++ })
  navigation.pause()
  Handler.advance()
  check(fallbacks == 2)
  check(navigation.dispatch { fallbacks++ })
  navigation.invalidate(window)
  check(runCatching { respond(replacement, true) }.isFailure)
  Handler.advance()
  check(fallbacks == 2)
  check(!navigation.dispatch { fallbacks++ })
}
`)
  const compiled = spawnSync('kotlinc', [filename, '-classpath', [stdlib, json].join(path.delimiter), '-d', jar], {
    encoding: 'utf8', timeout: 60000
  })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib, json].join(path.delimiter), 'oro.runtime.window.BackKt'], {
    encoding: 'utf8', timeout: 15000
  })
  assert.equal(result.status, 0, result.stderr)
})
