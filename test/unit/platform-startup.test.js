import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const options = { skip: !compiler ? 'A C++ compiler is required' : false }
const read = file => readFileSync(new URL(`../../${file}`, import.meta.url), 'utf8')

function runNative (source) {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-platform-startup-'))
  const filename = path.join(directory, 'regression.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'regression.exe' : 'regression')
  writeFileSync(filename, source)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
}

test('Android asset paths normalize dot components and match complete directory names', options, () => {
  const source = read('src/runtime/filesystem/resource.cc')
  const start = source.indexOf('  static Path getRelativeAndroidAssetManagerPath (')
  const end = source.indexOf('\n#endif', start)
  runNative(`
    #include <cassert>
    #include <filesystem>
    using Path = std::filesystem::path;
    using String = std::string;
    String replace (String input, const String& from, const String& to) {
      size_t offset = 0;
      while ((offset = input.find(from, offset)) != String::npos) {
        input.replace(offset, from.size(), to);
        offset += to.size();
      }
      return input;
    }
    struct Resource { static Path getResourcesPath () { return "/app/files"; } };
    ${source.slice(start, end)}
    int main () {
      assert(getRelativeAndroidAssetManagerPath("/app/files/./webassembly/program.wasm") == "webassembly/program.wasm");
      assert(getRelativeAndroidAssetManagerPath("/app/files/sub/../webassembly/program.wasm") == "webassembly/program.wasm");
      assert(getRelativeAndroidAssetManagerPath("./webassembly/program.wasm") == "webassembly/program.wasm");
      assert(getRelativeAndroidAssetManagerPath("/oro/ipc.js") == "oro/ipc.js");
      assert(getRelativeAndroidAssetManagerPath("/app/files-other/data") == "app/files-other/data");
      assert(getRelativeAndroidAssetManagerPath("/app/files").empty());
      assert(getRelativeAndroidAssetManagerPath(".").empty());
    }
  `)
})

test('Windows UI callbacks survive a nested pump that only dispatches window messages', options, () => {
  const header = read('src/runtime/context.hh')
  const declaration = header.slice(header.indexOf('  class Dispatcher :'), header.indexOf('\n}', header.indexOf('  class Dispatcher :')))
  const implementation = read('src/runtime/context/dispatch.cc').replace(/^\s*#\s*include[^\n]*$/gm, '')
  runNative(`
    #include <cassert>
    #include <atomic>
    #include <functional>
    #include <memory>
    #include <mutex>
    #include <queue>
    #include <stdexcept>
    #include <vector>
    #define ORO_RUNTIME_PLATFORM_WINDOWS 1
    #define CALLBACK
    using UINT = unsigned int;
    using WPARAM = unsigned long;
    using LPARAM = long long;
    using LONG_PTR = long long;
    using LRESULT = long long;
    using BOOL = bool;
    struct Window;
    using HWND = Window*;
    using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
    struct Window { WNDPROC proc; LONG_PTR data = 0; bool alive = true; };
    struct WNDCLASSW { WNDPROC lpfnWndProc; void* hInstance; const wchar_t* lpszClassName; };
    struct CREATESTRUCTW { void* lpCreateParams; };
    struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; };
    const UINT WM_APP = 0x8000, WM_NCCREATE = 0x81, PM_NOREMOVE = 0;
    const int GWLP_USERDATA = 0, ERROR_CLASS_ALREADY_EXISTS = 1410;
    HWND HWND_MESSAGE = nullptr;
    unsigned long currentThread = 1;
    bool postFails = false;
    std::queue<MSG> messages;
    std::vector<std::unique_ptr<Window>> windows;
    WNDPROC registeredProc;
    unsigned long GetCurrentThreadId () { return currentThread; }
    unsigned long GetLastError () { return 8; }
    void* GetModuleHandleW (void*) { return nullptr; }
    bool RegisterClassW (WNDCLASSW* wc) { registeredProc = wc->lpfnWndProc; return true; }
    LONG_PTR GetWindowLongPtrW (HWND window, int) { return window->data; }
    void SetWindowLongPtrW (HWND window, int, LONG_PTR data) { window->data = data; }
    LRESULT DefWindowProcW (HWND, UINT, WPARAM, LPARAM) { return 1; }
    HWND CreateWindowExW (int, const wchar_t*, const wchar_t*, int, int, int, int, int, HWND, void*, void*, void* data) {
      windows.push_back(std::make_unique<Window>(Window{registeredProc}));
      auto window = windows.back().get();
      CREATESTRUCTW create{data};
      window->proc(window, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&create));
      return window;
    }
    bool DestroyWindow (HWND window) { window->alive = false; return true; }
    bool PostMessageW (HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
      if (postFails) return false;
      messages.push({window, message, wparam, lparam});
      return true;
    }
    bool PostThreadMessage (unsigned long, UINT message, WPARAM wparam, LPARAM lparam) {
      return PostMessageW(nullptr, message, wparam, lparam);
    }
    void PeekMessage (MSG*, HWND, int, int, UINT) {}
    void pump () {
      assert(currentThread == 1);
      while (!messages.empty()) {
        auto message = messages.front();
        messages.pop();
        if (message.hwnd && message.hwnd->alive) {
          message.hwnd->proc(message.hwnd, message.message, message.wParam, message.lParam);
        }
      }
    }
    namespace oro::runtime {
      using Mutex = std::recursive_mutex;
      using Lock = std::lock_guard<Mutex>;
      template <typename T> using Atomic = std::atomic<T>;
      template <typename T> using Function = std::function<T>;
      template <typename... Args> void debug (Args...) {}
      namespace env { bool has (const char*) { return false; } }
      namespace context {
        struct RuntimeContext {};
        struct DispatchContext { RuntimeContext& context; DispatchContext(RuntimeContext&); };
        ${declaration}
      }
    }
    ${implementation}
    int main () {
      oro::runtime::context::RuntimeContext runtime;
      oro::runtime::context::Dispatcher dispatcher(runtime);
      int calls = 0;
      auto callback = [&]() { assert(currentThread == 1); ++calls; };
      currentThread = 2;
      assert(dispatcher.dispatch(callback));
      currentThread = 1;
      dispatcher.notifyReady();
      dispatcher.notifyReady();
      pump();
      assert(calls == 1);
      currentThread = 2;
      assert(dispatcher.dispatch(callback));
      currentThread = 1;
      pump();
      assert(calls == 2);
      assert(dispatcher.dispatch(callback));
      assert(calls == 3);
      currentThread = 2;
      postFails = true;
      assert(!dispatcher.dispatch(callback));
      postFails = false;
      assert(dispatcher.dispatch(callback));
      currentThread = 1;
      pump();
      assert(calls == 4);
    }
  `)
})
