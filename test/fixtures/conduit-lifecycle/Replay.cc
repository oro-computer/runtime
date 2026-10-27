#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <uv.h>

#ifndef ORO_TEST_APPLE_LOOP
#define ORO_TEST_APPLE_LOOP 0
#endif
#if ORO_TEST_APPLE_LOOP
#include <dispatch/dispatch.h>
#endif

// Compile the production loop and listener methods against real libuv. These
// facades omit the application, message codec, and WebSocket client protocol.
#define ORO_RUNTIME_PLATFORM_APPLE ORO_TEST_APPLE_LOOP
#define ORO_RUNTIME_PLATFORM_LINUX 0
#define ORO_RUNTIME_PLATFORM_ANDROID 0
#define ORO_RUNTIME_PLATFORM_WINDOWS (!ORO_TEST_APPLE_LOOP)
#define ORO_RUNTIME_DESKTOP_EXTENSION 0

namespace oro::runtime {
  struct Options {};
  namespace types {
    template <typename T> using Atomic = std::atomic<T>;
    template <typename T> using Function = std::function<T>;
    template <typename K, typename V> using Map = std::map<K, V>;
    template <typename K, typename V> using UnorderedMap = std::unordered_map<K, V>;
    template <typename T> using SharedPointer = std::shared_ptr<T>;
    template <typename T> using Vector = std::vector<T>;
    template <typename T> using Queue = std::queue<T>;
    using Mutex = std::recursive_mutex;
    using Thread = std::thread;
    using String = std::string;
  }
  using namespace types;
  using Lock = std::lock_guard<Mutex>;
  void msleep (int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  }
  namespace env {
    std::map<std::string, std::string> values;
    String get (const char* name) { return values[name]; }
    bool has (const char* name) { return !values[name].empty(); }
  }
  template <typename... Args> void debug (const char*, Args...) {}
}

#include "loop-header.hh"
#include "loop-methods.hh"

namespace oro::runtime {
  namespace bytes { struct Buffer {}; }
  namespace ipc { struct Client {}; }
  namespace core {
    struct Service {
      struct Options { loop::Loop& loop; };
      struct Dispatcher {
        bool dispatch (const std::function<void()>& callback) { callback(); return true; }
      } dispatcher;
      loop::Loop& loop;
      explicit Service (const Options& options) : loop(options.loop) {}
      virtual ~Service () = default;
      virtual bool start () { return true; }
      virtual bool stop () { return true; }
    };
  }
}

#include "conduit-header.hh"

namespace oro::runtime::core::services {
  std::atomic<int> destroyedClients = 0;
  Conduit::Conduit (const Service::Options& options) : Service(options) {}
  Conduit::~Conduit () noexcept { this->stop(); }
  // No application protocol is needed to exercise the listening socket.
  Conduit::Client::~Client () {
    assert(this->handle == nullptr);
    ++destroyedClients;
  }
  void Conduit::Client::close (const CloseCallback callback) { if (callback) callback(); }
  void Conduit::handshake (Client*, const char*) {}
  void Conduit::processFrame (Client*, const char*, ssize_t) {}
}

#include "conduit-methods.hh"

using oro::runtime::core::services::Conduit;
using oro::runtime::loop::Loop;

static void onLoop (Loop& loop, const std::function<void()>& callback) {
  auto completed = std::make_shared<std::promise<void>>();
  auto result = completed->get_future();
  assert(loop.dispatch([callback, completed]() {
    callback();
    completed->set_value();
  }));
  assert(result.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
}

static int listeners (Loop& loop) {
  int count = 0;
  uv_walk(loop.get(), [](uv_handle_t* handle, void* value) {
    if (handle->type == UV_TCP && uv_is_active(handle) && !uv_is_closing(handle)) {
      ++*static_cast<int*>(value);
    }
  }, &count);
  return count;
}

#include "Shutdown.hh"

int main (int argc, char**) {
  Loop loop;
  assert(loop.start());
  Conduit conduit({ loop });
  checkServiceShutdown(loop);

  // This is deterministic on the old implementation: stop publishes port=0
  // and then incorrectly skips the close because isActive() reads that port.
  assert(conduit.start());
  assert(conduit.port > 0);
  assert(conduit.stop());
  onLoop(loop, [&]() { assert(listeners(loop) == 0); });
  if (argc > 1) {
    assert(loop.shutdown());
    return 0;
  }

  assert(conduit.start());
  onLoop(loop, [&]() {
    auto client = new Conduit::Client(&conduit);
    client->id = 42;
    assert(uv_tcp_init(loop.get(), client->handle) == 0);
    uv_handle_set_data(reinterpret_cast<uv_handle_t*>(client->handle), client);
    conduit.clients.emplace(client->id, client);
    assert(conduit.stop());
    assert(conduit.clients.empty());
  });

  // Start and stop from the loop itself without waiting on that same loop.
  // Restart before close callbacks run to detect reuse of live handle storage.
  onLoop(loop, [&]() {
    assert(loop.isOnLoopThread());
    for (int index = 0; index < 500; ++index) {
      assert(conduit.start());
      assert(listeners(loop) == 1);
      assert(conduit.stop());
      assert(!conduit.isActive());
      assert(listeners(loop) == 0);
    }
  });

  for (int index = 0; index < 100; ++index) {
    assert(conduit.start());
    const int port = conduit.port;
    assert(conduit.start());
    assert(conduit.port == port);
    assert(conduit.stop());
    assert(loop.pause());
    // No synchronous wait may be placed behind a paused loop.
    assert(!conduit.start());
    auto retained = std::make_shared<std::promise<void>>();
    auto ready = retained->get_future();
    assert(loop.dispatch([retained]() { retained->set_value(); }));
    assert(loop.resume());
    assert(conduit.start());
    assert(ready.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
  }

  assert(conduit.stop());
  Conduit blocker({ loop });
  assert(blocker.start());
  oro::runtime::env::values["ORO_CONDUIT_PORT"] = std::to_string(blocker.port.load());
  assert(!conduit.start());
  assert(!conduit.isActive());
  assert(blocker.stop());
  assert(conduit.start());
  assert(conduit.stop());
  oro::runtime::env::values["ORO_CONDUIT_PORT"] = "65536";
  assert(!conduit.start());
  oro::runtime::env::values["ORO_CONDUIT_PORT"] = "0";
  oro::runtime::env::values["ORO_CONDUIT_HOSTNAME"] = "invalid-hostname";
  assert(!conduit.start());
  assert(!conduit.isActive());
  oro::runtime::env::values["ORO_CONDUIT_HOSTNAME"] = "127.0.0.1";
  auto firstStart = std::async(std::launch::async, [&]() { return conduit.start(); });
  auto secondStart = std::async(std::launch::async, [&]() { return conduit.start(); });
  assert(firstStart.get());
  assert(secondStart.get());
  onLoop(loop, [&]() { assert(listeners(loop) == 1); });
  assert(conduit.stop());
  onLoop(loop, [&]() { assert(listeners(loop) == 0); });
  assert(loop.shutdown());
  assert(oro::runtime::core::services::destroyedClients == 1);
  std::cout << "Lifecycle services stop without deadlock; Conduit survives repeated pause/resume cycles\n";
}
