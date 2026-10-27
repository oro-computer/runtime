import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { spawnSync } from 'node:child_process'
import path from 'node:path'
import vm from 'node:vm'

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
  const operations = readFileSync(new URL('../../src/runtime/app/platform_operations.kt', import.meta.url), 'utf8')
  const bindingStart = operations.indexOf('  fun document (') >= 0
    ? operations.indexOf('  fun document (')
    : operations.indexOf('  fun foreground (')
  const bindingEnd = operations.indexOf('  fun success (', bindingStart)
  assert.ok(bindingStart >= 0 && bindingEnd > bindingStart)
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
  val documentToken = window.platformDocumentToken
  var result: Any? = null
  ${operations.slice(bindingStart, bindingEnd)}
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
  for (phase in listOf("hidden", "paused")) {
    val registration = enable()
    if (phase == "hidden") window.fragment.isHidden = true
    else activity.lifecycle.currentState = Lifecycle.State.STOPPED
    navigation.request(PlatformCall(window), "disable", JSONObject().put("registration", registration))
    window.fragment.isHidden = false
    activity.lifecycle.currentState = Lifecycle.State.RESUMED
    check(!navigation.dispatch { fallbacks++ }) { "Removing a $phase handler must not leave a native registration" }
  }
  val staleRegistration = enable()
  val staleCall = PlatformCall(window)
  window.platformDocumentToken = "third-document"
  val latest = enable()
  check(runCatching { navigation.request(staleCall, "disable",
    JSONObject().put("registration", staleRegistration)) }.exceptionOrNull() is PlatformFailure)
  check(navigation.dispatch { fallbacks++ })
  respond(latest, true)
  Handler.advance()
  check(fallbacks == 2)
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

function backApi (request) {
  const source = readFileSync(new URL('../../api/window/back.js', import.meta.url), 'utf8')
    .replace(/^import .*$/gm, '').replace('export async function setHandler', 'async function setHandler')
    .replace('export default { setHandler }', '')
  const listeners = new Map()
  const context = vm.createContext({
    request,
    DOMException,
    addEventListener (name, callback) {
      if (!listeners.has(name)) listeners.set(name, new Set())
      listeners.get(name).add(callback)
    },
    removeEventListener (name, callback) { listeners.get(name)?.delete(callback) }
  })
  return {
    setHandler: vm.runInContext(`${source}\nsetHandler`, context),
    emit: value => Promise.all([...listeners.get('backrequest') ?? []].map(listener => listener({ detail: value }))),
    listenerCount: () => listeners.get('backrequest')?.size ?? 0
  }
}

test('application Back dispatches only its registration and removes listeners once', async () => {
  const calls = []
  const api = backApi(async (feature, action, options) => {
    calls.push({ feature, action, options })
    return action === 'enable' ? { registration: 'public-handler', deadlineMs: 750 } : null
  })
  let handled = 0
  const remove = await api.setHandler(value => {
    assert.equal(value.deadlineMs, 750)
    handled++
    return true
  })
  await api.emit({ registration: 'foreign-handler', id: 'foreign', deadlineMs: 750 })
  assert.equal(handled, 0)
  await api.emit({ registration: 'public-handler', id: 'public-back', deadlineMs: 750 })
  assert.equal(handled, 1)
  assert.equal(calls[1].options.handled, true)
  assert.equal(calls[1].options.id, 'public-back')
  await remove()
  await remove()
  assert.equal(api.listenerCount(), 0)
  assert.deepEqual(calls.map(call => call.action), ['enable', 'respond', 'disable'])
})

test('application Back replacement drops old abort ownership and falls back on handler errors', async () => {
  const calls = []
  let next = 0
  const api = backApi(async (feature, action, options) => {
    calls.push({ action, options })
    return action === 'enable' ? { registration: `public-${++next}`, deadlineMs: 750 } : null
  })
  const first = new AbortController()
  await api.setHandler(() => true, { signal: first.signal })
  const remove = await api.setHandler(() => { throw new Error('Public handler failure') })
  first.abort()
  await api.emit({ registration: 'public-2', id: 'public-back', deadlineMs: 750 })
  assert.deepEqual(calls.map(call => call.action), ['enable', 'disable', 'enable', 'respond'])
  assert.equal(calls.at(-1).options.handled, false)
  assert.equal(api.listenerCount(), 1)
  await remove()
  assert.equal(api.listenerCount(), 0)
})

test('application Back abort during registration releases the returned native handler', async () => {
  const calls = []
  let complete
  const started = new Promise(resolve => { complete = resolve })
  let finish
  const api = backApi(async (feature, action) => {
    calls.push(action)
    if (action !== 'enable') return null
    complete()
    return new Promise(resolve => { finish = resolve })
  })
  const controller = new AbortController()
  const pending = api.setHandler(() => true, { signal: controller.signal })
  await started
  controller.abort()
  finish({ registration: 'public-interrupted', deadlineMs: 750 })
  const remove = await pending
  await remove()
  assert.deepEqual(calls, ['enable', 'disable'])
  assert.equal(api.listenerCount(), 0)
})

test('application Back rejects an already aborted handler without registering', async () => {
  let calls = 0
  const api = backApi(async () => { calls++ })
  const controller = new AbortController()
  controller.abort()
  await assert.rejects(api.setHandler(() => true, { signal: controller.signal }), { name: 'AbortError' })
  assert.equal(calls, 0)
  assert.equal(api.listenerCount(), 0)
})
