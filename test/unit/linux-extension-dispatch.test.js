import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++', 'c++'].find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const flags = spawnSync('pkg-config', ['--cflags', '--libs', 'glib-2.0'], { encoding: 'utf8' })

test('Linux extension dispatch queues worker results until the owning main context runs and releases captures', {
  skip: process.platform !== 'linux' || !compiler || flags.status !== 0 ? 'Requires Linux, a C++ compiler and GLib headers' : false
}, () => {
  const source = readFileSync(new URL('../../src/desktop/extension/linux.cc', import.meta.url), 'utf8')
  const start = source.indexOf('  static void dispatchToWebExtensionContext (')
  const end = source.indexOf('  static SharedPointer<Bridge> getSharedBridge (', start)
  const method = source.slice(start, end)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-linux-dispatch-'))
  const filename = path.join(directory, 'dispatch.cc')
  const executable = path.join(directory, 'dispatch')
  writeFileSync(filename, `
    #include <glib.h>
    #include <atomic>
    #include <cassert>
    #include <functional>
    #include <memory>
    #include <thread>
    template<class T> using Function = std::function<T>;
    static GMainContext* getSharedMainContext () { return g_main_context_ref(g_main_context_default()); }
    ${method}
    int main () {
      const auto main = std::this_thread::get_id();
      std::atomic<int> calls = 0, released = 0;
      struct Capture { std::atomic<int>& released; ~Capture () { ++released; } };
      auto captured = std::make_shared<Capture>(released);
      std::weak_ptr<Capture> weak = captured;
      std::thread worker([&, retained = std::move(captured)] () mutable {
        dispatchToWebExtensionContext([&, retained = std::move(retained)] {
          assert(std::this_thread::get_id() == main);
          ++calls;
        });
      });
      worker.join();
      assert(calls == 0 && !weak.expired());
      while (g_main_context_iteration(g_main_context_default(), false)) {}
      assert(calls == 1 && weak.expired() && released == 1);
      dispatchToWebExtensionContext(nullptr);
      assert(calls == 1);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', '-pthread', filename, '-o', executable, ...flags.stdout.trim().split(/\s+/)], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
