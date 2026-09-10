import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

function fragment (filename, start, end) {
  const source = readFileSync(new URL(filename, import.meta.url), 'utf8')
  const first = source.indexOf(start)
  const last = source.indexOf(end, first)
  assert.ok(first >= 0 && last > first)
  return source.slice(first, last)
}

const reply = fragment('../../src/runtime/bridge/bridge.cc',
  'const auto invoked = this->router.invoke(message, request->body.shared(), size,',
  '      if (!invoked)')
const completion = fragment('../../src/runtime/window/win.cc',
  'const auto handled = bridge->schemeHandlers.handleRequest(req,',
  '                  if (!handled)')

for (const windows of [true, false]) {
  test(`WebView IPC replies preserve ${windows ? 'Windows UI' : 'native callback'} thread affinity`, {
    skip: !compiler ? 'A C++ compiler is required' : false
  }, () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro-response-thread-'))
    const filename = path.join(directory, 'response.cc')
    const executable = path.join(directory, process.platform === 'win32' ? 'response.exe' : 'response')
    // Compile the production callback adapters with a queued UI dispatcher and
    // reference-counted response objects, then reply from a real worker thread.
    writeFileSync(filename, `
      #include <cassert>
      #include <functional>
      #include <memory>
      #include <queue>
      #include <string>
      #include <thread>
      #define ORO_RUNTIME_PLATFORM_WINDOWS ${windows ? 1 : 0}
      namespace ipc { struct Result { std::string payload; }; }
      struct Dispatcher {
        std::thread::id owner = std::this_thread::get_id();
        std::queue<std::function<void()>> pending;
        bool dispatch (std::function<void()> callback) {
          if (std::this_thread::get_id() == owner) callback();
          else pending.push(callback);
          return true;
        }
        void drain () {
          while (!pending.empty()) {
            auto callback = pending.front();
            pending.pop();
            callback();
          }
        }
      };
      struct Router {
        std::function<void(ipc::Result)> reply;
        bool invoke (int, void*, size_t, std::function<void(ipc::Result)> callback) {
          reply = callback;
          return true;
        }
      };
      struct Body { void* shared () const { return nullptr; } };
      struct Request { Body body; };
      struct ICoreWebView2WebResourceResponse : std::enable_shared_from_this<ICoreWebView2WebResourceResponse> {};
      struct Response { ICoreWebView2WebResourceResponse* platformResponse; };
      template <typename T> struct ComPtr {
        std::shared_ptr<T> value;
        ComPtr (T* pointer) : value(pointer ? pointer->shared_from_this() : nullptr) {}
        T* Get () const { return value.get(); }
        T* operator-> () const { return value.get(); }
      };
      struct RequestArgs : std::enable_shared_from_this<RequestArgs> {
        std::thread::id owner = std::this_thread::get_id();
        bool delivered = false;
        void put_Response (ICoreWebView2WebResourceResponse* response) {
          assert(std::this_thread::get_id() == owner);
          assert(response != nullptr);
          delivered = true;
        }
      };
      struct Deferral : std::enable_shared_from_this<Deferral> {
        std::thread::id owner = std::this_thread::get_id();
        bool completed = false;
        void Complete () {
          assert(std::this_thread::get_id() == owner);
          completed = true;
        }
      };
      struct SchemeHandlers {
        std::function<void(const Response&)> complete;
        bool handleRequest (int, std::function<void(const Response&)> callback) {
          complete = callback;
          return true;
        }
      };
      struct Bridge {
        Router router;
        Dispatcher dispatcher;
        SchemeHandlers schemeHandlers;
        int replies = 0;
        std::string payload;
        bool dispatch (std::function<void()> callback) { return dispatcher.dispatch(callback); }
        void start () {
          int message = 0;
          size_t size = 0;
          auto request = std::make_shared<Request>();
          auto respond = [this](ipc::Result result) {
            #if ORO_RUNTIME_PLATFORM_WINDOWS
            assert(std::this_thread::get_id() == dispatcher.owner);
            #endif
            payload = result.payload;
            ++replies;
          };
          ${reply}
          assert(invoked);
        }
      };
      void startCompletion (std::shared_ptr<Bridge> bridge, ComPtr<RequestArgs> requestArgs, ComPtr<Deferral> deferral) {
        int req = 0;
        ${completion}
        assert(handled);
      }
      int main () {
        auto bridge = std::make_shared<Bridge>();
        bridge->start();
        std::thread worker([&]() { bridge->router.reply(ipc::Result{"worker payload"}); });
        worker.join();
        assert(bridge->replies == (ORO_RUNTIME_PLATFORM_WINDOWS ? 0 : 1));
        bridge->dispatcher.drain();
        assert(bridge->replies == 1);
        assert(bridge->payload == "worker payload");
        bridge->router.reply(ipc::Result{"UI payload"});
        assert(bridge->replies == 2);
        assert(bridge->payload == "UI payload");

        auto args = std::make_shared<RequestArgs>();
        auto deferral = std::make_shared<Deferral>();
        startCompletion(bridge, args.get(), deferral.get());
        std::weak_ptr<ICoreWebView2WebResourceResponse> responseLifetime;
        std::thread stream([&]() {
          auto platformResponse = std::make_shared<ICoreWebView2WebResourceResponse>();
          responseLifetime = platformResponse;
          bridge->schemeHandlers.complete(Response{platformResponse.get()});
        });
        stream.join();
        assert(!args->delivered && !deferral->completed);
        assert(!responseLifetime.expired());
        bridge->dispatcher.drain();
        assert(args->delivered && deferral->completed);
        assert(responseLifetime.expired());
      }
    `)
    const compiled = spawnSync(compiler, ['-std=c++20', '-pthread', filename, '-o', executable], { encoding: 'utf8' })
    assert.equal(compiled.status, 0, compiled.stderr)
    const result = spawnSync(executable, [], { encoding: 'utf8' })
    assert.equal(result.status, 0, result.stderr)
  })
}
