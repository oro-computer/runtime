import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import vm from 'node:vm'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const kotlin = stdlib && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)
const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('window mutation errors retain native codes and identity without replacing cached window state', async () => {
  const source = readFileSync(new URL('../../api/window.js', import.meta.url), 'utf8')
  const start = source.indexOf('export class ApplicationWindow extends EventTarget {')
  const end = source.indexOf('\nexport { client, hotkey }', start)
  assert.ok(start >= 0 && end > start)
  let response
  const context = vm.createContext({
    __args: { index: 0 },
    EventTarget,
    BroadcastChannel: class extends EventTarget { close () {} },
    addEventListener () {},
    removeEventListener () {},
    statuses: {},
    hotkey: {},
    ipc: { request: async () => response }
  })
  new vm.Script(`${source.slice(start, end).replace('export class ', 'class ')}\nglobalThis.Window = ApplicationWindow`).runInContext(context)
  const window = new context.Window({ index: 7, id: 'kept', title: 'before', status: 1 })
  const error = Object.assign(new Error('Window changes require a live Activity'), { code: 'ENOTREADY' })
  response = { err: error }
  for (const method of ['show', 'hide', 'focus', 'blur', 'maximize', 'minimize', 'restore', 'close', 'setTitle']) {
    await assert.rejects(window[method]('after'), value => value === error && value.code === 'ENOTREADY')
    assert.equal(window.id, 'kept')
    assert.equal(window.index, 7)
    assert.equal(window.title, 'before')
  }
  response = { data: { id: 'kept', index: 7, title: 'after', status: 2 } }
  await window.show()
  assert.equal(window.title, 'after')
})

test('Android window creation rejects saved/destroyed/off-thread Activities before fragment allocation and permits retry', {
  skip: !kotlin ? 'Kotlin, a JVM and ORO_KOTLIN_STDLIB are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/manager.kt', import.meta.url), 'utf8')
  const createStart = source.indexOf('  fun createWindowFragment (options: WindowOptions) {')
  const createEnd = source.indexOf('\n  /**', createStart)
  const gateStart = source.indexOf('  fun canMutateWindows (): Boolean =')
  const gateEnd = source.indexOf('\n\n', gateStart)
  const mutations = ['show', 'hide', 'close'].map(name => {
    const start = source.indexOf(`  fun ${name}WindowFragment (index: Int): Boolean {`)
    const end = source.indexOf('\n  /**', start)
    assert.ok(start >= 0 && end > start)
    return source.slice(start, end)
  }).join('\n')
  assert.ok(createStart >= 0 && createEnd > createStart && gateStart >= 0 && gateEnd > gateStart)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-window-creation-'))
  const filename = path.join(directory, 'Regression.kt')
  const jar = path.join(directory, 'creation.jar')
  writeFileSync(filename, `
    object Looper {
      val main = Any(); var current = main
      fun myLooper() = current
      fun getMainLooper() = main
    }
    object R { object id { const val window = 1 } }
    data class WindowOptions(val index: Int, val headless: Boolean = false)
    class Arguments(val index: Int) { fun getInt(key: String): Int { check(key == "index"); return index } }
    class WindowFragment {
      var index = -1; var arguments: Arguments? = null; var hidden = false
      companion object {
        var allocations = 0
        fun newInstance(options: WindowOptions): WindowFragment {
          allocations++; return WindowFragment().apply { arguments = Arguments(options.index) }
        }
      }
    }
    class Transactions {
      var isStateSaved = false; var isDestroyed = false; var commits = 0
      val fragments = mutableListOf<Any>()
      fun beginTransaction() = Transaction(this)
      fun commit(callback: Transaction.() -> Unit) {
        check(!isStateSaved && !isDestroyed) { "Cannot commit after onSaveInstanceState" }
        Transaction(this).callback(); commits++
      }
    }
    class Transaction(val manager: Transactions) {
      fun setReorderingAllowed(value: Boolean) { check(value) }
      fun add(container: Int, fragment: WindowFragment) { check(container == 1); manager.fragments.add(fragment) }
      fun hide(fragment: WindowFragment): Transaction { fragment.hidden = true; return this }
      fun show(fragment: WindowFragment): Transaction { fragment.hidden = false; return this }
      fun remove(fragment: WindowFragment): Transaction { manager.fragments.remove(fragment); return this }
      fun commit() { check(!manager.isStateSaved); manager.commits++ }
    }
    class Activity {
      var isFinishing = false; var isDestroyed = false
      val supportFragmentManager = Transactions()
      fun runOnUiThread(callback: () -> Unit) { callback() }
      ${source.slice(gateStart, gateEnd)}
    }
    class Manager {
      val activity = Activity(); val manager = activity.supportFragmentManager
      val fragments = mutableListOf<WindowFragment>()
      fun hasWindowFragment(index: Int) = fragments.any { it.index == index }
      ${source.slice(createStart, createEnd)}
      ${mutations}
    }
    fun main() {
      for (reason in 0..4) {
        val manager = Manager(); val activity = manager.activity
        when(reason) {
          0 -> activity.supportFragmentManager.isStateSaved = true
          1 -> activity.supportFragmentManager.isDestroyed = true
          2 -> activity.isFinishing = true
          3 -> activity.isDestroyed = true
          4 -> Looper.current = Any()
        }
        val before = WindowFragment.allocations
        check(!activity.canMutateWindows()); manager.createWindowFragment(WindowOptions(7))
        check(WindowFragment.allocations == before && manager.fragments.isEmpty())
        check(manager.manager.commits == 0)
        activity.supportFragmentManager.isStateSaved = false
        activity.supportFragmentManager.isDestroyed = false
        activity.isFinishing = false; activity.isDestroyed = false; Looper.current = Looper.main
        check(activity.canMutateWindows()); manager.createWindowFragment(WindowOptions(7))
        check(manager.fragments.size == 1 && manager.manager.commits == 1)
        manager.createWindowFragment(WindowOptions(7)); check(manager.manager.commits == 1)
        manager.manager.isStateSaved = true
        check(!manager.showWindowFragment(7) && !manager.hideWindowFragment(7) && !manager.closeWindowFragment(7))
        check(manager.fragments.size == 1 && manager.manager.commits == 1)
        manager.manager.isStateSaved = false
        check(manager.showWindowFragment(7) && manager.hideWindowFragment(7) && manager.closeWindowFragment(7))
        check(manager.fragments.isEmpty() && manager.manager.commits == 4)
      }
      Manager().apply {
        val restored = WindowFragment().apply { index = 9; arguments = Arguments(9) }
        manager.fragments.add(restored); createWindowFragment(WindowOptions(9))
        check(fragments.single() === restored && manager.commits == 0)
      }
      Manager().apply {
        createWindowFragment(WindowOptions(95, true)); check(fragments.single().hidden)
      }
    }
  `)
  const built = spawnSync('kotlinc', [filename, '-classpath', stdlib, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib].join(path.delimiter), 'RegressionKt'], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})

test('native Android creation gate handles a missing Activity and clears failed JNI checks before further JNI calls', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/window/manager.cc', import.meta.url), 'utf8')
  const start = source.indexOf('  bool Manager::canMutateWindows () {')
  const end = source.indexOf('\n  SharedPointer<Manager::ManagedWindow>', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-window-creation-'))
  const filename = path.join(directory, 'creation.cc')
  const executable = path.join(directory, 'creation')
  writeFileSync(filename, `
    #include <cassert>
    #include <string>
    #define ORO_RUNTIME_PLATFORM_ANDROID 1
    bool allowed = true; int calls = 0;
    struct Environment {
      bool exception = false; int cleared = 0;
      bool ExceptionCheck() { return exception; }
      void ExceptionClear() { exception = false; cleared++; }
    } environment;
    namespace android {
      struct JNIEnvironmentAttachment {
        Environment* env = &environment;
        explicit JNIEnvironmentAttachment(int value) { assert(value == 17); }
      };
    }
    bool invoke(Environment* env, int owner, const char* name, const char* signature) {
      assert(env == &environment && owner == 21);
      assert(std::string(name) == "canMutateWindows" && std::string(signature) == "()Z"); calls++;
      return allowed;
    }
    #define CallClassMethodFromAndroidEnvironment(env, kind, owner, name, signature) invoke(env, owner, name, signature)
    struct Manager {
      bool destroyed = false;
      struct { struct { int jvm = 17, activity = 21; } android; } context;
      bool canMutateWindows();
    };
    ${source.slice(start, end)}
    int main() {
      Manager manager;
      assert(manager.canMutateWindows() && calls == 1);
      allowed = false; assert(!manager.canMutateWindows());
      environment.exception = true; allowed = true;
      assert(!manager.canMutateWindows() && !environment.exception && environment.cleared == 1);
      int previous = calls;
      manager.context.android.activity = 0; assert(!manager.canMutateWindows() && calls == previous);
      manager.context.android.activity = 21; manager.destroyed = true;
      assert(!manager.canMutateWindows() && calls == previous);
    }
  `)
  const built = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})
