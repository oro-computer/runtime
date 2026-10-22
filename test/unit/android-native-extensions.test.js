import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const available = stdlib && json && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('compiled extensions coexist, receive lifecycle hooks and discard private results after cancellation', {
  skip: !available ? 'Kotlin, JVM, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR are required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-extensions-'))
  const fixtures = fileURLToPath(new URL('../fixtures/android-runtime/platform/', import.meta.url))
  const sources = []
  for (const name of readdirSync(fixtures).filter(name => name.endsWith('.kt'))) {
    let source = readFileSync(path.join(fixtures, name), 'utf8')
    if (name === 'Android.kt') {
      source = source.replace('fun getMainLooper() = Looper()', 'private val main = Looper(); fun getMainLooper() = main; fun myLooper() = main')
    }
    if (name === 'Regression.kt') {
      source = source.replace('open class AppActivity {', 'open class AppActivity : android.app.Activity() {')
        .replace('class App {', 'class App : android.app.Application() {')
        .replace(/^internal object NativeExtensions .*\n/m, '')
        .replace('fun main() {', 'fun oldRegression() {')
    }
    const filename = path.join(directory, name)
    writeFileSync(filename, source)
    sources.push(filename)
  }
  const files = {
    'Context.kt': 'package android.content\nopen class Context\nclass Intent\n',
    'Bundle.kt': 'package android.os\nclass Bundle\n',
    'Application.kt': `package android.app
      import android.os.Bundle
      open class Activity
      open class Application : android.content.Context() {
        val callbacks = mutableListOf<ActivityLifecycleCallbacks>()
        fun registerActivityLifecycleCallbacks(value: ActivityLifecycleCallbacks) { callbacks.add(value) }
        interface ActivityLifecycleCallbacks {
          fun onActivityCreated(activity: Activity, state: Bundle?)
          fun onActivityResumed(activity: Activity)
          fun onActivityStopped(activity: Activity)
          fun onActivityDestroyed(activity: Activity)
          fun onActivityStarted(activity: Activity)
          fun onActivityPaused(activity: Activity)
          fun onActivitySaveInstanceState(activity: Activity, state: Bundle)
        }
      }`,
    'ExtensionsRegression.kt': `package oro.runtime.app
      import android.content.Intent
      import org.json.JSONObject
      import oro.runtime.window.*
      class Fixture(override val name: String) : NativeExtension {
        var starts = 0; var created = 0; var resumed = 0; var stopped = 0; var destroyed = 0
        var intents = 0; var results = 0; var cleanups = 0; var waiting: NativeExtensionCall? = null
        override fun onCreate(app: App) { starts++ }
        override fun onActivityCreated(activity: AppActivity) { created++ }
        override fun onActivityResumed(activity: AppActivity) { resumed++ }
        override fun onActivityStopped(activity: AppActivity) { stopped++ }
        override fun onActivityDestroyed(activity: AppActivity) { destroyed++ }
        override fun onNewIntent(activity: AppActivity, intent: Intent) { intents++ }
        override fun onActivityResult(activity: AppActivity, requestCode: Int, resultCode: Int, data: Intent?) { results++ }
        override fun request(call: NativeExtensionCall, action: String, options: JSONObject) {
          check(call.foregroundActivity() != null)
          call.onCancel { cleanups++ }
          if (action == "wait") waiting = call else call.success(JSONObject().put("extension", name))
        }
      }
      internal object ConfiguredNativeExtensions {
        val first = Fixture("fixture.first"); val second = Fixture("fixture.second")
        var fail = false
        fun create(): List<NativeExtension> {
          if (fail) throw IllegalStateException("public-test-secret")
          return listOf(first, second)
        }
      }
      fun main() {
        oldRegression()
        val app = App(); val fragment = Fragment(0); val owner = Window(app.activity, fragment)
        fragment.window = owner; app.activity.windowFragmentManager.fragments.add(fragment)
        NativeExtensions.initialize(app)
        val a = ConfiguredNativeExtensions.first; val b = ConfiguredNativeExtensions.second
        for(callback in app.callbacks) {
          callback.onActivityCreated(app.activity, null); callback.onActivityResumed(app.activity)
          callback.onActivityStopped(app.activity); callback.onActivityDestroyed(app.activity)
        }
        NativeExtensions.dispatch { it.onNewIntent(app.activity, Intent()) }
        NativeExtensions.dispatch { it.onActivityResult(app.activity, 1, 2, null) }
        for(extension in listOf(a,b)) check(listOf(extension.starts, extension.created, extension.resumed,
          extension.stopped, extension.destroyed, extension.intents, extension.results).all { it == 1 })
        fun request(token: Long, extension: String, action: String) = PlatformOperations.start(app, token, 0,
          "extension", action, JSONObject().put("extension",extension).put("payload",JSONObject()).toString(),
          "extension-$token",owner.platformDocumentToken)
        request(1000,"fixture.first","echo"); request(1001,"fixture.second","echo")
        check(app.results[1000]!!.getJSONObject("data").getString("extension") == "fixture.first")
        check(app.results[1001]!!.getJSONObject("data").getString("extension") == "fixture.second")
        check(a.cleanups == 1 && b.cleanups == 1)
        request(1002,"missing","echo")
        check(app.results[1002]!!.getJSONObject("err").getString("code") == "ENOSYS")
        request(1003,"fixture.first","wait")
        PlatformOperations.cancelWindow(owner)
        check(a.cleanups == 2 && a.waiting!!.completed)
        owner.platformDocumentToken = "c".repeat(32)
        a.waiting!!.success(JSONObject().put("private", "public-test-secret"))
        check(app.results[1003]!!.getJSONObject("err").getString("code") == "ABORT_ERR")
        check(app.results.values.none { it.optJSONObject("data")?.has("private") == true })
        ConfiguredNativeExtensions.fail = true
        val failure = runCatching { NativeExtensions.initialize(App()) }.exceptionOrNull()
        check(failure is IllegalStateException && failure.message == "Native extension startup failed" && failure.cause == null)
      }`
  }
  for (const [name, source] of Object.entries(files)) {
    const filename = path.join(directory, name)
    writeFileSync(filename, source)
    sources.push(filename)
  }
  sources.push(...['platform_operations', 'extensions'].map(name =>
    fileURLToPath(new URL(`../../src/runtime/app/${name}.kt`, import.meta.url))))
  const jar = path.join(directory, 'extensions.jar')
  const compiled = spawnSync('kotlinc', [...sources, '-classpath', [stdlib, json].join(path.delimiter), '-d', jar], {
    encoding: 'utf8', timeout: 60000
  })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib, json].join(path.delimiter), 'oro.runtime.app.ExtensionsRegressionKt'], {
    encoding: 'utf8', timeout: 15000
  })
  assert.equal(result.status, 0, result.stderr)
})
