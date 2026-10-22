import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const available = ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)
const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('Android native focus dispatches the focus operation and ignores detached native windows', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/android.cc', import.meta.url), 'utf8')
  const start = source.indexOf('  void Window::focus () {')
  const end = source.indexOf('\n  void Window::blur', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-focus-'))
  const filename = path.join(directory, 'focus.cc')
  const executable = path.join(directory, 'focus')
  writeFileSync(filename, `
    #include <cassert>
    #include <string>
    int calls = 0, attachments = 0;
    struct Runtime { struct { int jvm = 17, activity = 21; } android; };
    struct App {
      Runtime runtime;
      static App* sharedApplication() { static App instance; return &instance; }
    };
    namespace android {
      struct JNIEnvironmentAttachment {
        int env = 10;
        explicit JNIEnvironmentAttachment(int vm) { assert(vm == 17); attachments++; }
      };
    }
    void invoke(int env, int owner, const char* method, const char* signature, int index) {
      assert(env == 10 && owner == 21 && index == 42);
      assert(std::string(method) == "focusWindow" && std::string(signature) == "(I)Z"); calls++;
    }
    #define CallClassMethodFromAndroidEnvironment(env, kind, owner, method, signature, index) invoke(env, owner, method, signature, index)
    struct Window {
      void* self = nullptr; int index = 42;
      void focus(); void show() {}
    };
    ${source.slice(start, end)}
    int main() {
      Window window; window.focus(); assert(calls == 0 && attachments == 0);
      window.self = &window; window.focus(); assert(calls == 1 && attachments == 1);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})

test('Android focus commits foreground ordering and WebView focus without reviving stale windows', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/manager.kt', import.meta.url), 'utf8')
  const start = source.indexOf('  fun focusWindowFragment (index: Int): Boolean {')
  const end = source.indexOf('\n  /**\n   * Hides', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-window-focus-'))
  const filename = path.join(directory, 'Regression.kt')
  const jar = path.join(directory, 'focus.jar')
  writeFileSync(filename, `
    class View { var raised = false; fun bringToFront() { raised = true } }
    class WebView {
      var isAttachedToWindow = true
      var focused = false
      fun requestFocus(): Boolean { focused = true; return true }
    }
    class Options { var headless = false }
    class Handler { var active = true }
    class Window { val userMessageHandler = Handler() }
    class Fragment(val index: Int) {
      val options = Options(); var window: Window? = Window()
      var hasWebView = true; var isAdded = true; var isVisible = false
      val webview = WebView(); val view = View()
    }
    class Surfaces { var refreshed = 0; fun refresh() { refreshed++ } }
    class Activity {
      var isFinishing = false; var isDestroyed = false; var focused = true
      val presentation = Surfaces(); val systemAppearance = Surfaces()
      val pending = mutableListOf<() -> Unit>()
      fun hasWindowFocus() = focused
      fun runOnUiThread(callback: () -> Unit) { pending.add(callback) }
      fun flush() { pending.toList().also { pending.clear() }.forEach { it() } }
    }
    class Transactions {
      var isStateSaved = false; var isDestroyed = false
      val pending = mutableListOf<() -> Unit>()
      fun beginTransaction() = Transaction(this)
      fun flush() { pending.toList().also { pending.clear() }.forEach { it() } }
    }
    class Transaction(val manager: Transactions) {
      lateinit var fragment: Fragment; var callback: () -> Unit = {}
      fun show(value: Fragment): Transaction { fragment = value; return this }
      fun runOnCommit(value: () -> Unit): Transaction { callback = value; return this }
      fun commit() { manager.pending.add { fragment.isVisible = true; callback() } }
    }
    class Manager {
      val activity = Activity(); val manager = Transactions()
      val fragments = mutableListOf<Fragment>()
      ${source.slice(start, end)}
    }
    fun main() {
      Manager().apply {
        val primary = Fragment(0); val secondary = Fragment(1)
        fragments.addAll(listOf(primary, secondary))
        check(!focusWindowFragment(99))
        check(focusWindowFragment(0)); check(!primary.webview.focused)
        activity.flush(); check(!primary.webview.focused && !primary.isVisible)
        manager.flush()
        check(primary.webview.focused && primary.view.raised && primary.isVisible)
        check(fragments == listOf(secondary, primary)) { "Back, insets and credentials must agree on the foreground window" }
        check(activity.presentation.refreshed == 1 && activity.systemAppearance.refreshed == 1)
        check(focusWindowFragment(0)); activity.flush(); manager.flush()
        check(fragments.size == 2)
      }
      for (mutation in listOf("missing-window", "headless", "missing-webview")) {
        Manager().apply {
          val fragment = Fragment(0); fragments.add(fragment)
          when (mutation) {
            "missing-window" -> fragment.window = null
            "headless" -> fragment.options.headless = true
            else -> fragment.hasWebView = false
          }
          check(!focusWindowFragment(0)); check(activity.pending.isEmpty())
        }
      }
      for (phase in listOf("ui", "commit")) {
        for (mutation in listOf("removed", "replaced", "inactive", "headless", "missing-webview",
            "finishing", "destroyed", "manager-destroyed", "saved", "not-added", "unfocused", "detached")) {
          Manager().apply {
            val fragment = Fragment(0); fragments.add(fragment)
            check(focusWindowFragment(0))
            if (phase == "commit") activity.flush()
            when (mutation) {
              "removed" -> fragments.clear()
              "replaced" -> fragment.window = Window()
              "inactive" -> fragment.window!!.userMessageHandler.active = false
              "headless" -> fragment.options.headless = true
              "missing-webview" -> fragment.hasWebView = false
              "finishing" -> activity.isFinishing = true
              "destroyed" -> activity.isDestroyed = true
              "manager-destroyed" -> manager.isDestroyed = true
              "saved" -> manager.isStateSaved = true
              "not-added" -> fragment.isAdded = false
              "unfocused" -> activity.focused = false
              "detached" -> fragment.webview.isAttachedToWindow = false
            }
            activity.flush(); manager.flush()
            check(!fragment.webview.focused && !fragment.view.raised) { phase + ":" + mutation }
            check(activity.systemAppearance.refreshed == 0)
          }
        }
      }
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})
