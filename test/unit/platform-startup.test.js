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

function fragment (file, start, end) {
  const source = read(file)
  const first = source.indexOf(start)
  const last = source.indexOf(end, first)
  assert.ok(first >= 0 && last > first)
  return source.slice(first, last)
}

function runNative (source) {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-platform-startup-'))
  const filename = path.join(directory, 'regression.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'regression.exe' : 'regression')
  writeFileSync(filename, source)
  const compiled = spawnSync(compiler, ['-std=c++20', '-Werror', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
}

test('Windows resources read complete files and handle invalid or truncated files', options, () => {
  const size = fragment('src/runtime/filesystem/resource.cc',
    '  size_t Resource::size (bool cached)', '  const unsigned char* Resource::read () const')
  const readResource = fragment('src/runtime/filesystem/resource.cc',
    '  const unsigned char* Resource::read (bool cached)', '  const String Resource::str (bool cached)')
  runNative(`
    #include <algorithm>
    #include <cassert>
    #include <cstdint>
    #include <cstring>
    #include <map>
    #include <memory>
    #include <string>
    #define ORO_RUNTIME_PLATFORM_WINDOWS 1
    using String = std::string;
    using DWORD = uint32_t;
    using HANDLE = int;
    constexpr HANDLE INVALID_HANDLE_VALUE = -1;
    constexpr int GENERIC_READ = 1, FILE_SHARE_READ = 2, OPEN_EXISTING = 3;
    constexpr DWORD MAXDWORD = UINT32_MAX;
    struct LARGE_INTEGER { int64_t QuadPart; };
    String payload = "<!doctype html><script src='main.js'></script>";
    size_t cursor = 0;
    size_t available = payload.size();
    bool openFails = false, sizeFails = false, readFails = false;
    int opens = 0, closes = 0, reads = 0;
    HANDLE CreateFileW (const wchar_t*, int, int, void*, int, int, void*) {
      cursor = 0;
      if (openFails) return INVALID_HANDLE_VALUE;
      return ++opens;
    }
    bool GetFileSizeEx (HANDLE handle, LARGE_INTEGER* size) {
      assert(handle != INVALID_HANDLE_VALUE);
      if (sizeFails) return false;
      size->QuadPart = payload.size();
      return true;
    }
    void CloseHandle (HANDLE handle) { assert(handle != INVALID_HANDLE_VALUE); ++closes; }
    bool ReadFile (HANDLE handle, void* buffer, DWORD size, DWORD* read, void* overlapped) {
      assert(handle != INVALID_HANDLE_VALUE && read != nullptr && overlapped == nullptr);
      ++reads;
      if (readFails) return false;
      *read = std::min<size_t>({size, available - cursor, 7});
      std::memcpy(buffer, payload.data() + cursor, *read);
      cursor += *read;
      return true;
    }
    template <typename... Args> void debug (Args...) {}
    struct Resource {
      struct Path {
        const wchar_t* c_str () const { return L"index.html"; }
        String string () const { return "index.html"; }
      } path;
      struct Cache { size_t size = 0; std::shared_ptr<unsigned char[]> bytes; } cache;
      struct { bool cache = true; } options;
      std::shared_ptr<unsigned char[]> bytes;
      std::map<String, Cache> caches;
      bool accessing = true;
      bool exists () { return true; }
      size_t size (bool cached = true) noexcept;
      const unsigned char* read (bool cached = true);
    };
    ${size}
    ${readResource}
    int main () {
      Resource resource;
      auto data = resource.read();
      assert(data != nullptr && String(reinterpret_cast<const char*>(data), payload.size()) == payload);
      assert(resource.size() == payload.size() && reads > 1 && opens == closes);
      const auto count = reads;
      assert(resource.read() == data && reads == count);
      openFails = true;
      Resource missing;
      assert(missing.size(false) == 0 && missing.read(false) == nullptr && opens == closes);
      openFails = false;
      sizeFails = true;
      Resource unreadable;
      assert(unreadable.size(false) == 0 && opens == closes);
      sizeFails = false;
      available = payload.size() - 1;
      Resource truncated;
      assert(truncated.read(false) == nullptr && opens == closes);
      available = payload.size();
      readFails = true;
      Resource failed;
      assert(failed.read(false) == nullptr && opens == closes);
    }
  `)
})

test('WebView2 waits for preload registration and reports synchronous and asynchronous failures', options, () => {
  const register = fragment('src/runtime/window/win.cc',
    '              const auto preloadResult = this->webview->AddScriptToExecuteOnDocumentCreated(',
    '            } while (0);')
  runNative(`
    #include <cassert>
    #include <cstdlib>
    #include <functional>
    #include <string>
    using HRESULT = long;
    using PCWSTR = const wchar_t*;
    using String = std::string;
    constexpr HRESULT S_OK = 0;
    bool FAILED (HRESULT status) { return status < 0; }
    template <typename... Args> void debug (Args...) {}
    std::wstring convertStringToWString (String s) { return std::wstring(s.begin(), s.end()); }
    using Completion = std::function<HRESULT(HRESULT, PCWSTR)>;
    struct ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler {};
    namespace Microsoft::WRL {
      struct Handler { Completion callback; Completion Get () { return callback; } };
      template <typename Interface> Handler Callback (Completion fn) { return {fn}; }
    }
    struct WebView {
      Completion complete;
      HRESULT status = S_OK;
      HRESULT AddScriptToExecuteOnDocumentCreated (PCWSTR, Completion fn) { complete = fn; return status; }
    };
    struct App { std::function<void(int)> shutdownHandler; };
    struct Window {
      WebView* webview;
      bool isReadyForNavigation = false;
      String pendingNavigationLocation = "first.html";
      String navigated;
      struct { int index = 0; } options;
      void navigate (String location) { assert(isReadyForNavigation); navigated = location; }
      HRESULT configure (App* application) {
        static auto app = application;
        struct { String str () { return "preload"; } } preloadUserScriptSource;
        ${register}
        return S_OK;
      }
    };
    int main () {
      int failures = 0;
      App app{[&](int code) { assert(code == EXIT_FAILURE); ++failures; }};
      WebView view;
      Window window{&view};
      assert(window.configure(&app) == S_OK);
      assert(!window.isReadyForNavigation && window.navigated.empty());
      window.pendingNavigationLocation = "latest.html";
      assert(view.complete(S_OK, L"script-id") == S_OK);
      assert(window.isReadyForNavigation && window.navigated == "latest.html");
      Window asynchronousFailure{&view};
      assert(asynchronousFailure.configure(&app) == S_OK);
      assert(view.complete(-10, nullptr) == -10);
      assert(!asynchronousFailure.isReadyForNavigation && failures == 1);
      Window synchronousFailure{&view};
      view.status = -20;
      assert(synchronousFailure.configure(&app) == -20);
      assert(!synchronousFailure.isReadyForNavigation && failures == 2);
    }
  `)
})

test('WebView2 browser errors are logged before JavaScript console initialization', options, () => {
  const diagnostics = fragment('src/runtime/window/win.cc',
    '            // Capture browser errors before the runtime console module loads.',
    '            // Register the preload after the request handlers.')
  runNative(`
    #include <cassert>
    #include <functional>
    #include <map>
    #include <string>
    using HRESULT = long;
    using LPWSTR = wchar_t*;
    using PCWSTR = const wchar_t*;
    using WString = std::wstring;
    constexpr HRESULT S_OK = 0;
    bool FAILED (HRESULT value) { return value < 0; }
    bool SUCCEEDED (HRESULT value) { return !FAILED(value); }
    struct EventRegistrationToken {};
    struct ICoreWebView2 {};
    struct ICoreWebView2DevToolsProtocolEventReceivedEventHandler {};
    struct ICoreWebView2CallDevToolsProtocolMethodCompletedHandler {};
    int logs = 0, freed = 0;
    std::string consoleType = R"JSON("error")JSON";
    namespace JSON {
      struct Value {
        Value operator[] (const char*) { return {}; }
        std::string str () { return consoleType; }
      };
      Value parse (std::string) { return {}; }
    }
    template <typename... Args> void debug (Args...) { ++logs; }
    std::string convertWStringToString (PCWSTR value) { assert(value); return "browser error"; }
    void CoTaskMemFree (void* value) { if (value) ++freed; }
    namespace config { bool isDebugEnabled () { return false; } }
    struct ICoreWebView2DevToolsProtocolEventReceivedEventArgs {
      HRESULT get_ParameterObjectAsJson (LPWSTR* value) {
        static wchar_t json[] = L"{\\"exceptionDetails\\":{\\"text\\":\\"SyntaxError\\"}}";
        *value = json;
        return S_OK;
      }
    };
    using Event = std::function<HRESULT(ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*)>;
    struct ICoreWebView2DevToolsProtocolEventReceiver {
      Event handler;
      HRESULT add_DevToolsProtocolEventReceived (Event event, EventRegistrationToken*) { handler = event; return S_OK; }
    };
    template <typename T> struct ComPtr {
      T* value = nullptr;
      T** operator& () { return &value; }
      T* operator-> () { return value; }
      explicit operator bool () { return value != nullptr; }
    };
    namespace Microsoft::WRL {
      template <typename T> struct Handler { T function; T Get () { return function; } };
      template <typename Interface, typename T> Handler<T> Callback (T function) { return {function}; }
    }
    struct WebView {
      bool unavailable = false;
      int enabled = 0;
      std::map<std::wstring, ICoreWebView2DevToolsProtocolEventReceiver> receivers;
      HRESULT GetDevToolsProtocolEventReceiver (PCWSTR event, ICoreWebView2DevToolsProtocolEventReceiver** receiver) {
        if (unavailable) return -1;
        *receiver = &receivers[event];
        return S_OK;
      }
      HRESULT CallDevToolsProtocolMethod (PCWSTR, PCWSTR, std::function<HRESULT(HRESULT, PCWSTR)> complete) {
        ++enabled;
        complete(unavailable ? -1 : S_OK, L"{}");
        return S_OK;
      }
    };
    struct Window {
      WebView* webview;
      struct { bool debug = true; int index = 0; } options;
      void configure () { ${diagnostics} }
    };
    int main () {
      WebView view;
      Window window{&view};
      window.configure();
      assert(view.enabled == 2 && view.receivers.size() == 3 && logs == 2);
      ICoreWebView2DevToolsProtocolEventReceivedEventArgs args;
      for (auto& [name, receiver] : view.receivers) receiver.handler(nullptr, &args);
      assert(logs == 5 && freed == 3);
      consoleType = R"JSON("log")JSON";
      view.receivers[L"Runtime.consoleAPICalled"].handler(nullptr, &args);
      assert(logs == 5 && freed == 4);
      view.unavailable = true;
      window.configure();
      assert(logs == 10);
      window.options.debug = false;
      window.configure();
      assert(logs == 10 && view.enabled == 4);
    }
  `)
})

test('WebView2 settings use queried interface pointers and tolerate unavailable features', options, () => {
  const configure = fragment('src/runtime/window/win.cc',
    '            // configure the webview settings',
    '            // enumerate all child windows')
  runNative(`
    #include <cassert>
    #include <cstdlib>
    #include <functional>
    #include <map>
    #include <string>
    using String = std::string;
    using WString = std::wstring;
    using LPWSTR = wchar_t*;
    using HRESULT = long;
    constexpr HRESULT S_OK = 0, E_POINTER = -1;
    bool FAILED (HRESULT value) { return value < 0; }
    bool SUCCEEDED (HRESULT value) { return !FAILED(value); }
    template <typename... Args> void debug (Args...) {}
    String trim (String value) { return value; }
    String replace (String value, String, String) { return value; }
    String convertWStringToString (WString value) { return String(value.begin(), value.end()); }
    WString convertStringToWString (String value) { return WString(value.begin(), value.end()); }
    void CoTaskMemFree (void*) {}
    namespace config { bool isDebugEnabled () { return false; } }
    namespace oro::runtime::version { const char* VERSION_STRING = "test"; }
    bool extensionsAvailable = true;
    int optionalCalls = 0;
    struct ICoreWebView2Settings {
      int identity = 1;
      int baseCalls = 0;
      void base () { assert(identity == 1); ++baseCalls; }
      void put_IsScriptEnabled (bool) { base(); }
      void put_IsStatusBarEnabled (bool) { base(); }
      void put_IsWebMessageEnabled (bool) { base(); }
      void put_AreDevToolsEnabled (bool) { base(); }
      void put_AreHostObjectsAllowed (bool) { base(); }
      void put_IsZoomControlEnabled (bool) { base(); }
      void put_IsBuiltInErrorPageEnabled (bool) { base(); }
      void put_AreDefaultContextMenusEnabled (bool) { base(); }
      void put_AreDefaultScriptDialogsEnabled (bool) { base(); }
      template <typename T> HRESULT QueryInterface (T** output) {
        static T extension;
        *output = extensionsAvailable ? &extension : nullptr;
        return extensionsAvailable ? S_OK : E_POINTER;
      }
    };
    struct ICoreWebView2Settings2 {
      int identity = 2;
      HRESULT get_UserAgent (LPWSTR* output) { assert(identity == 2); *output = nullptr; return S_OK; }
      void put_UserAgent (const wchar_t*) { assert(identity == 2); ++optionalCalls; }
    };
    struct ICoreWebView2Settings3 {
      int identity = 3;
      void put_AreBrowserAcceleratorKeysEnabled (bool) { assert(identity == 3); ++optionalCalls; }
    };
    struct ICoreWebView2Settings6 {
      int identity = 6;
      void put_IsPinchZoomEnabled (bool) { assert(identity == 6); ++optionalCalls; }
      void put_IsSwipeNavigationEnabled (bool) { assert(identity == 6); ++optionalCalls; }
    };
    struct ICoreWebView2Settings9 {
      int identity = 9;
      void put_IsNonClientRegionSupportEnabled (bool) { assert(identity == 9); ++optionalCalls; }
    };
    template <typename T> struct ComPtr {
      T* value = nullptr;
      T** operator& () { return &value; }
      T* operator-> () const { assert(value); return value; }
      explicit operator bool () const { return value != nullptr; }
      bool operator!= (std::nullptr_t) const { return value != nullptr; }
      template <typename U> HRESULT As (U** output) { return value->QueryInterface(output); }
    };
    struct WebView {
      ICoreWebView2Settings settings;
      HRESULT status = S_OK;
      bool nullSettings = false;
      HRESULT get_Settings (ICoreWebView2Settings** output) {
        *output = FAILED(status) || nullSettings ? nullptr : &settings;
        return status;
      }
    };
    struct App { std::function<void(int)> shutdownHandler; };
    struct Bridge { std::map<String, String> userConfig; };
    struct Window {
      struct { bool debug = false; int index = 0; } options;
      WebView* webview;
      Bridge* bridge;
      HRESULT configure (App* app) {
        ${configure}
        return S_OK;
      }
    };
    int main () {
      WebView view;
      Bridge bridge;
      Window window{{}, &view, &bridge};
      int failures = 0;
      App app{[&](int code) { assert(code == EXIT_FAILURE); ++failures; }};
      assert(window.configure(&app) == S_OK);
      assert(view.settings.baseCalls == 9 && optionalCalls == 5);
      extensionsAvailable = false;
      assert(window.configure(&app) == S_OK);
      assert(view.settings.baseCalls == 18 && optionalCalls == 5);
      view.status = -20;
      assert(window.configure(&app) == -20 && failures == 1);
      view.status = S_OK;
      view.nullSettings = true;
      assert(window.configure(&app) == E_POINTER && failures == 2);
    }
  `)
})

test('IPC requests reach native handlers even when the browser cookie store cannot reply', options, () => {
  const gate = fragment('src/runtime/webview/scheme_handlers.cc',
    '    // Only intercept schemes we explicitly registered.',
    '    // CDP Network.* events')
  runNative(`
    #include <cassert>
    #include <atomic>
    #include <cstdint>
    #include <functional>
    #include <map>
    #include <memory>
    #include <string>
    #include <vector>
    using String = std::string;
    struct Request {
      String scheme = "ipc", hostname = "fs.stat";
      struct Headers {
        std::map<String, String> values;
        bool has (String key) { return values.contains(key); }
        void set (String key, String value) { values[key] = value; }
      } headers;
      bool isCancelled () { return false; }
      String str () { return scheme + "://" + hostname; }
    };
    using Callback = std::function<void()>;
    namespace oro::runtime::bridge { struct Bridge; }
    struct Handlers {
      oro::runtime::bridge::Bridge& bridge;
      bool hasHandlerForScheme (String) { return true; }
      bool handleRequest (std::shared_ptr<Request>, Callback);
    };
    struct Runtime {
      struct {
        struct { bool wasFirstDOMContentLoadedEventDispatched = true; } platform;
        struct {
          int scheduled = 0;
          uint64_t setTimeout (int, Callback) { return ++scheduled; }
          void clearTimeout (uint64_t) {}
        } timers;
      } services;
      struct Window { std::shared_ptr<oro::runtime::bridge::Bridge> bridge; } window;
      struct Manager {
        Window* window;
        Window* getWindowForBridge (void*) { return window; }
      } windowManager{&window};
    };
    namespace app {
      struct App {
        Runtime runtime;
        static App* sharedApplication () { static App value; return &value; }
      };
    }
    namespace oro::runtime::bridge {
      struct Bridge {
        std::map<String, String> userConfig;
        Handlers schemeHandlers{*this};
        Runtime* getRuntime () { return &app::App::sharedApplication()->runtime; }
        void dispatch (Callback callback) { callback(); }
      };
    }
    int cookieLookups = 0;
    namespace cookies {
      template <typename T> void get (oro::runtime::bridge::Bridge&, String, T) {
        ++cookieLookups; // Simulate a blocked browser cookie store.
      }
    }
    bool Handlers::handleRequest (std::shared_ptr<Request> request, Callback callback) {
      ${gate}
      callback();
      return true;
    }
    int main () {
      auto bridge = std::make_shared<oro::runtime::bridge::Bridge>();
      auto& runtime = app::App::sharedApplication()->runtime;
      runtime.window.bridge = bridge;
      int completed = 0;
      auto request = std::make_shared<Request>();
      bridge->schemeHandlers.handleRequest(request, [&]() { ++completed; });
      assert(completed == 1 && cookieLookups == 0 && runtime.services.timers.scheduled == 0);
      request = std::make_shared<Request>();
      request->scheme = "oro";
      request->hostname = "app.example";
      bridge->schemeHandlers.handleRequest(request, [&]() { ++completed; });
      assert(completed == 1 && cookieLookups == 1 && runtime.services.timers.scheduled == 1);
      request->headers.set("cookie", "session=value");
      bridge->schemeHandlers.handleRequest(request, [&]() { ++completed; });
      assert(completed == 2 && cookieLookups == 1);
    }
  `)
})

test('WebView2 scheme registration admits IPC origins according to the configured CORS policy', options, () => {
  const configure = fragment('src/runtime/webview/scheme_handlers.cc',
    '            const auto hasAuthority =',
    '            registrations.emplace_back(registration);')
  runNative(`
    #include <cassert>
    #include <map>
    #include <stdexcept>
    #include <string>
    #include <sstream>
    #include <vector>
    using String = std::string;
    using WString = std::wstring;
    using LPCWSTR = const wchar_t*;
    using UINT32 = unsigned int;
    template <typename T> using Vector = std::vector<T>;
    constexpr bool TRUE = true, FALSE = false;
    bool FAILED (int result) { return result < 0; }
    String toLowerCase (String value) { return value; }
    String trim (String value) { return value; }
    WString convertStringToWString (String value) { return WString(value.begin(), value.end()); }
    Vector<String> split (String value, char delimiter) {
      std::istringstream stream(value);
      Vector<String> result;
      String entry;
      while (std::getline(stream, entry, delimiter)) result.push_back(entry);
      return result;
    }
    struct Registration {
      Vector<WString> origins;
      bool fail = false;
      void put_HasAuthorityComponent (bool) {}
      int SetAllowedOrigins (UINT32 size, LPCWSTR* values) {
        origins.clear();
        for (UINT32 i = 0; i < size; ++i) origins.emplace_back(values[i]);
        return fail ? -1 : 0;
      }
      bool permits (WString origin) {
        for (const auto& entry : origins) if (entry == L"*" || entry == origin) return true;
        return false;
      }
    };
    struct Handler {
      struct { std::map<String, String> userConfig; } bridge;
      void configure (Registration* registration) {
        String scheme = "ipc";
        ${configure}
      }
    };
    int main () {
      Handler handler;
      Registration registration;
      handler.configure(&registration);
      assert(registration.permits(L"oro://app.example"));
      assert(registration.permits(L"http://localhost:3000"));
      handler.bridge.userConfig["webview_cors_allow_all"] = "false";
      handler.configure(&registration);
      assert(!registration.permits(L"oro://app.example"));
      handler.bridge.userConfig["webview_cors_allowed_origins"] = "oro://app.example  https://trusted.example";
      handler.configure(&registration);
      assert(registration.origins.size() == 2);
      assert(registration.permits(L"oro://app.example"));
      assert(registration.permits(L"https://trusted.example"));
      assert(!registration.permits(L"https://untrusted.example"));
      registration.fail = true;
      bool threw = false;
      try { handler.configure(&registration); } catch (const std::runtime_error&) { threw = true; }
      assert(threw);
    }
  `)
})

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
