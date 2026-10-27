import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import vm from 'node:vm'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const available = stdlib && json && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

for (const [fixture, source, description] of [
  ['platform', 'platform_operations', 'native prompts bind documents, reject stale cancellation and settle once within capacity'],
  ['launcher', 'launcher', 'launcher routing survives publication, rejects revoked intents and clears account references']
]) {
  test(description, { skip: !available ? 'Kotlin, JVM, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR are required' : false }, () => {
    const fixtures = fileURLToPath(new URL(`../fixtures/android-runtime/${fixture}/`, import.meta.url))
    const native = fileURLToPath(new URL(`../../src/runtime/app/${source}.kt`, import.meta.url))
    const directory = mkdtempSync(path.join(tmpdir(), `oro-${fixture}-state-`))
    const jar = path.join(directory, 'regression.jar')
    const sources = readdirSync(fixtures).filter(name => name.endsWith('.kt')).map(name => path.join(fixtures, name))
    const compiled = spawnSync('kotlinc', [...sources, native, '-classpath', [stdlib, json].join(path.delimiter), '-d', jar], {
      encoding: 'utf8', timeout: 60000
    })
    assert.equal(compiled.status, 0, compiled.stderr)
    const result = spawnSync('java', ['-cp', [jar, stdlib, json].join(path.delimiter), 'oro.runtime.app.RegressionKt'], {
      encoding: 'utf8', timeout: 15000
    })
    assert.equal(result.status, 0, result.stderr)
  })
}

const source = readFileSync(new URL('../../api/internal/platform-request.js', import.meta.url), 'utf8')
const body = source.replace(/^import .*$/gm, '').replace('export async function', 'async function').replace('export default platformRequest', '')
test('platform requests send document-owned cancellation and remove abort listeners after completion', async () => {
  const calls = []
  let settle
  const ipc = {
    request (route, parameters) {
      calls.push(parameters)
      return parameters.feature === 'cancel' ? Promise.resolve({ data: null }) : new Promise(resolve => { settle = resolve })
    }
  }
  let sequence = 0
  const context = vm.createContext({ ipc, randomBytes: size => Buffer.alloc(size, ++sequence), AbortError: class extends Error {} })
  context.__oroNativeDocumentToken = 'a'.repeat(32)
  const request = vm.runInContext(`${body}; platformRequest`, context)
  const controller = new AbortController()
  const pending = request('authentication', 'seal', { id: 'public' }, controller.signal)
  controller.abort()
  settle({ data: { private: 'public-fixture' } })
  await assert.rejects(pending)
  assert.equal(calls[1].documentToken, calls[0].documentToken)
  assert.equal(JSON.parse(calls[1].options).id, calls[0].id)
  const completed = new AbortController()
  const result = request('launcher', 'status', {}, completed.signal)
  settle({ data: 'complete' })
  assert.equal(await result, 'complete')
  completed.abort()
  assert.equal(calls.length, 3)
  await assert.rejects(request('launcher', 'status', {}, completed.signal))
  assert.equal(calls.length, 3)
})

test('protected keys require per-operation authentication across legacy and Keystore2 metadata', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/app/authentication.kt', import.meta.url), 'utf8')
  const start = source.indexOf('  private fun verifyPolicy (')
  const end = source.indexOf('\n  private fun newKeyAllowed', start)
  assert.ok(start > 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-protected-key-policy-'))
  const filename = path.join(directory, 'Policy.kt')
  const jar = path.join(directory, 'policy.jar')
  const fixture = `
    interface Key { val algorithm: String }
    interface SecretKey : Key
    class PublicSecret : SecretKey { override val algorithm = "AES" }
    class PublicPrivate : Key { override val algorithm = "EC" }
    class KeyInfo(var isUserAuthenticationRequired: Boolean = true,
      var userAuthenticationValidityDurationSeconds: Int = 0, var userAuthenticationType: Int = 2)
    val metadata = KeyInfo()
    class Factory {
      fun getKeySpec(key: Key, type: Class<KeyInfo>) = metadata
    }
    object SecretKeyFactory { fun getInstance(algorithm: String, provider: String) = Factory() }
    object KeyFactory { fun getInstance(algorithm: String, provider: String) = Factory() }
    class PlatformFailure(val code: String) : Exception(code)
    fun keyAuthenticators(policy: String) = if (policy == "biometric") 2 else 3
    ${source.slice(start, end)}
    fun main() {
      for (key in listOf(PublicSecret(), PublicPrivate())) {
        for (duration in listOf(-1, 0)) {
          metadata.userAuthenticationValidityDurationSeconds = duration
          for ((policy, mask) in listOf("biometric" to 2, "biometric-or-device" to 3)) {
            metadata.userAuthenticationType = mask
            verifyPolicy(key, policy)
          }
        }
        for (duration in listOf(-2, 1, 30, 300)) {
          metadata.userAuthenticationValidityDurationSeconds = duration
          try { verifyPolicy(key, "biometric-or-device"); error("Non-per-operation policy accepted") }
          catch (error: PlatformFailure) { check(error.code == "EACCES") }
        }
        metadata.userAuthenticationValidityDurationSeconds = 0
        metadata.isUserAuthenticationRequired = false
        try { verifyPolicy(key, "biometric-or-device"); error("Unauthenticated key accepted") }
        catch (error: PlatformFailure) { check(error.code == "EACCES") }
        metadata.isUserAuthenticationRequired = true
        metadata.userAuthenticationType = 1
        try { verifyPolicy(key, "biometric"); error("Wrong authenticator accepted") }
        catch (error: PlatformFailure) { check(error.code == "EACCES") }
      }
    }
  `
  writeFileSync(filename, fixture)
  const compiled = spawnSync('kotlinc', [filename, '-classpath', stdlib, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib].join(path.delimiter), 'PolicyKt'], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})

test('document-start handshake restores ownership and authenticates same-origin child frame IPC', () => {
  const native = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
  const marker = 'bridgeScript = WebViewCompat.addDocumentStartJavaScript(this, """'
  const start = native.indexOf(marker)
  const end = native.indexOf('""".trimIndent(), trustedOrigins)', start)
  assert.ok(start > 0 && end > start)
  const script = native.slice(start + marker.length, end).replaceAll('$bridgeNonce', 'public-fixture-nonce')
  const messages = []
  const prompts = []
  const origin = 'https://computer.oro.runtime.tests'
  function createRealm (top, frameOrigin = origin) {
    const listeners = new Map()
    class XMLHttpRequest {
      headers = new Map()
      open (method, url, async) { this.url = url; this.async = async }
      setRequestHeader (name, value) { this.headers.set(name, value) }
    }
    const context = vm.createContext({
      crypto: { getRandomValues (bytes) { bytes.fill(42); return bytes } },
      __oroNativeBridge: { postMessage: value => messages.push(value) },
      prompt: (message, value) => {
        assert.equal(top, undefined, 'sandboxed child frames must map bodies through the top-level prompt')
        prompts.push({ message, value })
        return 'true'
      },
      location: { href: `${frameOrigin}/oro/vm/world.html`, origin: frameOrigin },
      XMLHttpRequest,
      URL,
      TextEncoder,
      btoa,
      addEventListener: (name, callback) => listeners.set(name, callback)
    })
    if (top) context.top = top
    else vm.runInContext('globalThis.top = globalThis', context)
    vm.runInContext(script, context)
    return { context, listeners, XMLHttpRequest }
  }
  const { context, listeners } = createRealm()
  assert.match(messages[0], /^oro:document:[a-f0-9]{32}$/)
  listeners.get('pageshow')()
  assert.equal(messages[1], messages[0], 'restoring a document announces its own retained token')

  const child = createRealm(context)
  assert.equal(messages.length, 2, 'child frames cannot announce a top-level document')
  assert.equal(child.context.__oroNativeDocumentToken, undefined)
  assert.equal(child.listeners.has('pageshow'), false)
  const request = new child.XMLHttpRequest()
  request.open('GET', 'ipc://platform.primordials?runtime-frame-type=nested', false)
  assert.equal(request.async, false)
  assert.equal(request.headers.get('X-Oro-Bridge-Token'), 'public-fixture-nonce')
  child.context.external.postMessage('ipc://platform.event?runtime-frame-type=nested')
  assert.equal(messages.at(-1), 'ipc://platform.event?runtime-frame-type=nested')
  child.context.external.postMessage('ipc://buffer.map?seq=child', new Uint8Array([1, 2]))
  assert.equal(prompts.at(-1).message, 'oro:ipc:public-fixture-nonce')
  assert.deepEqual(JSON.parse(prompts.at(-1).value), { message: 'ipc://buffer.map?seq=child', bytes: 'AQI=' })
  const resource = new child.XMLHttpRequest()
  resource.open('GET', `${origin}/data.json`, true)
  assert.equal(resource.headers.size, 0, 'resource requests do not disclose the IPC token')

  for (const foreign of [createRealm(context, 'https://foreign.example'), createRealm({})]) {
    assert.equal(foreign.context.external, undefined, 'foreign or inaccessible ancestors do not supply a bridge')
    assert.equal(foreign.context.__oroNativeBridgeToken, undefined)
    assert.equal(foreign.context.__oroNativeDocumentToken, undefined)
  }
})

test('headless WebViews never enable credential provider or Autofill UI', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
  const start = source.indexOf('      importantForAutofill = if (')
  const end = source.indexOf('\n      settings.domStorageEnabled', start)
  assert.ok(start > 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-headless-credentials-'))
  const filename = path.join(directory, 'Credentials.kt')
  const jar = path.join(directory, 'credentials.jar')
  writeFileSync(filename, `
    object View { const val IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS = 8; const val IMPORTANT_FOR_AUTOFILL_YES = 1 }
    object WebViewFeature { const val WEB_AUTHENTICATION = "authentication"; fun isFeatureSupported(feature: String) = true }
    class Settings { var authentication = -1 }
    object WebSettingsCompat {
      const val WEB_AUTHENTICATION_SUPPORT_FOR_APP = 1; const val WEB_AUTHENTICATION_SUPPORT_NONE = 0
      fun setWebAuthenticationSupport(settings: Settings, value: Int) { settings.authentication = value }
    }
    class Options(val headless: Boolean)
    class Fragment(val options: Options)
    class App(val enabled: Boolean) { fun getUserConfigValue(key: String) = if (enabled) "true" else "false" }
    class Window(val fragment: Fragment, val app: App) {
      var importantForAutofill = -1
      val settings = Settings()
      fun configure() { ${source.slice(start, end)} }
    }
    fun main() {
      for (headless in listOf(false, true)) for (enabled in listOf(false, true)) {
        val window = Window(Fragment(Options(headless)), App(enabled)); window.configure()
        check(window.importantForAutofill == if (!headless && enabled) 1 else 8)
        check(window.settings.authentication == if (!headless && enabled) 1 else 0)
      }
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-classpath', stdlib, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib].join(path.delimiter), 'CredentialsKt'], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})
