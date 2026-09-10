import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const options = { skip: !compiler ? 'A C++ compiler is required' : false }

function fragment (filename, start, end) {
  const source = readFileSync(new URL(filename, import.meta.url), 'utf8')
  const first = source.indexOf(start)
  const last = source.indexOf(end, first)
  assert.ok(first >= 0 && last > first)
  return source.slice(first, last)
}

function runNative (source) {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-ipc-'))
  const filename = path.join(directory, 'regression.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'regression.exe' : 'regression')
  writeFileSync(filename, source)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
}

test('native extension parameters decode once and retain their storage', options, () => {
  const codec = readFileSync(new URL('../../src/runtime/url/codec.cc', import.meta.url), 'utf8')
    .replace('#include "../url.hh"', '')
  const accessor = fragment('../../src/runtime/ipc/message.cc',
    '  const String& Message::at (', '  const String Message::get (')
  const getter = fragment('../../src/extension/ipc.cc',
    'const char* oapi_ipc_message_get (', 'oapi_ipc_message_t* oapi_ipc_message_clone (')
  runNative(`
    #include <cassert>
    #include <cctype>
    #include <map>
    #include <string>
    #include <vector>
    using String = std::string;
    template <typename T> using Vector = std::vector<T>;
    ${codec}
    using oro::runtime::url::decodeURIComponent;
    using oro::runtime::url::encodeURIComponent;
    struct Value { String data; };
    struct URI { std::map<String, Value> searchParams; };
    struct Message {
      URI uri;
      mutable std::map<String, String> decodedValues;
      bool contains (const String& key) const { return uri.searchParams.contains(key); }
      const String& at (const String& key) const;
    };
    ${accessor}
    using oapi_ipc_message_t = Message;
    ${getter}
    int main () {
      Message message;
      const String database = "/app/tmp/space + percent%20.db";
      const String query = "SELECT '100%25 + value'\\nWHERE 1 = 1";
      message.uri.searchParams["path"] = {encodeURIComponent(database)};
      message.uri.searchParams["query"] = {encodeURIComponent(query)};
      message.uri.searchParams["empty"] = {""};
      const char* saved = oapi_ipc_message_get(&message, "path");
      assert(String(saved) == database);
      assert(String(oapi_ipc_message_get(&message, "query")) == query);
      for (int i = 0; i < 100; ++i) {
        const auto key = std::to_string(i);
        message.uri.searchParams[key] = {encodeURIComponent(key + " value")};
        oapi_ipc_message_get(&message, key.c_str());
      }
      assert(saved == oapi_ipc_message_get(&message, "path"));
      assert(String(saved) == database);
      assert(oapi_ipc_message_get(&message, "empty") == nullptr);
      assert(oapi_ipc_message_get(&message, "missing") == nullptr);
      assert(oapi_ipc_message_get(nullptr, "path") == nullptr);
    }
  `)
})

test('queued routes and replies tolerate destruction of their bridge', options, () => {
  const invoke = fragment('../../src/runtime/ipc/router.cc',
    '  bool Router::invoke (\n    const Message& message,', '\n}\n')
  runNative(`
    #include <cassert>
    #include <functional>
    #include <map>
    #include <memory>
    #include <mutex>
    #include <queue>
    #include <string>
    #include <vector>
    using String = std::string;
    template <typename T> using SharedPointer = std::shared_ptr<T>;
    template <typename T> using Vector = std::vector<T>;
    using Lock = std::lock_guard<std::mutex>;
    String toLowerCase (const String& value) { return value; }
    namespace bytes { struct ArrayBuffer { ArrayBuffer(size_t, SharedPointer<unsigned char[]>) {} }; }
    struct Buffer { void operator= (bytes::ArrayBuffer) {} };
    struct Message { String name = "test"; Buffer buffer; };
    struct Result { String seq = "1"; int queuedResponse = 0; String str() const { return "result"; } };
    struct Dispatcher {
      std::queue<std::function<void()>> pending;
      bool dispatch (std::function<void()> fn) { pending.push(fn); return true; }
      void drain () {
        while (!pending.empty()) { auto fn = pending.front(); pending.pop(); fn(); }
      }
    };
    struct Bridge;
    struct Router {
      using ResultCallback = std::function<void(Result)>;
      using MessageCallback = std::function<void(Message, Router*, ResultCallback)>;
      struct MessageCallbackContext { bool async = true; MessageCallback callback; };
      struct MessageCallbackListenerContext { MessageCallback callback; };
      std::map<String, MessageCallbackContext> table, preserved;
      std::map<String, Vector<MessageCallbackListenerContext>> listeners;
      std::mutex mutex;
      Bridge& bridge;
      Dispatcher& dispatcher;
      Router(Bridge& bridge, Dispatcher& dispatcher) : bridge(bridge), dispatcher(dispatcher) {}
      bool invoke(const Message&, SharedPointer<unsigned char[]>, size_t, const ResultCallback);
    };
    int sent = 0;
    struct Bridge : std::enable_shared_from_this<Bridge> {
      Dispatcher& dispatcher;
      Router router;
      bool dispatchRouterCallbacksWithBridge = false;
      Bridge(Dispatcher& dispatcher) : dispatcher(dispatcher), router(*this, dispatcher) {}
      bool active() const { return true; }
      bool dispatch(std::function<void()> fn) { return dispatcher.dispatch(fn); }
      void send(String, String, int) { ++sent; }
    };
    ${invoke}
    int main () {
      Dispatcher dispatcher;
      int calls = 0, replies = 0;
      Router::ResultCallback retainedReply;
      for (bool async : {true, false}) {
        auto bridge = std::make_shared<Bridge>(dispatcher);
        bridge->router.table["test"] = {async, [&](auto, auto*, auto reply) {
          ++calls; retainedReply = reply;
        }};
        bridge->router.invoke(Message{}, nullptr, 0, [&](auto) { ++replies; });
        dispatcher.drain();
        assert(calls > 0);
        retainedReply(Result{});
        assert(replies > 0);
        retainedReply(Result{"-1"});
        dispatcher.drain();
        assert(sent > 0);
        retainedReply(Result{"-1"});
        const auto count = sent;
        const auto replyCount = replies;
        bridge.reset();
        retainedReply(Result{});
        dispatcher.drain();
        assert(sent == count && replies == replyCount);
      }
      auto bridge = std::make_shared<Bridge>(dispatcher);
      bridge->router.table["test"] = {true, [&](auto, auto*, auto) { ++calls; }};
      bridge->router.invoke(Message{}, nullptr, 0, [](auto) {});
      const auto count = calls;
      bridge.reset();
      dispatcher.drain();
      assert(calls == count);
      bridge = std::make_shared<Bridge>(dispatcher);
      const auto weak = bridge->weak_from_this();
      bridge->router.table["test"] = {true, [&](auto, auto*, auto reply) {
        bridge.reset();
        assert(!weak.expired());
        reply(Result{});
      }};
      const auto replyCount = replies;
      bridge->router.invoke(Message{}, nullptr, 0, [&](auto) { ++replies; });
      dispatcher.drain();
      assert(weak.expired());
      assert(replies == replyCount + 1);
    }
  `)
})

test('Windows responses rewind the body before WebView2 reads it', options, () => {
  const finish = fragment('../../src/runtime/webview/scheme_handlers.cc',
    '  bool SchemeHandlers::Response::finish () {',
    '  void SchemeHandlers::Response::setHeader (')
    .split('#elif ORO_RUNTIME_PLATFORM_WINDOWS\n')[1]
    .split('#elif ORO_RUNTIME_PLATFORM_ANDROID\n')[0]
  runNative(`
    #include <cassert>
    #include <string>
    struct LARGE_INTEGER { long long QuadPart = 0; };
    constexpr int STREAM_SEEK_SET = 0;
    bool FAILED(int result) { return result < 0; }
    void debug(const char*, unsigned long) {}
    struct Stream {
      std::string data;
      size_t position = 0;
      int references = 2;
      void Write(std::string bytes) { data += bytes; position = data.size(); }
      int Seek(LARGE_INTEGER offset, int mode, void*) {
        assert(mode == STREAM_SEEK_SET); position = offset.QuadPart; return 0;
      }
      void Release() { --references; }
    };
    struct Response {
      Stream* platformResponseStream;
      bool finish() { ${finish} return true; }
    };
    int main () {
      Stream stream;
      stream.Write("hello "); stream.Write("world");
      Response response{&stream};
      assert(response.finish());
      assert(stream.data.substr(stream.position) == "hello world");
      assert(stream.references == 1);
      assert(response.platformResponseStream == nullptr);
    }
  `)
})
