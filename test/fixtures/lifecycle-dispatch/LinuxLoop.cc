#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include <glib.h>
#include <uv.h>

#include "oro/platform.h"

#define ORO_RUNTIME_DESKTOP_EXTENSION 0

namespace oro::runtime {
  struct Options {};
  namespace types {
    template <typename T> using Atomic = std::atomic<T>;
    template <typename T> using Function = std::function<T>;
    template <typename T> using Queue = std::queue<T>;
    using Mutex = std::recursive_mutex;
    using Thread = std::thread;
  }
  using namespace types;
  using Lock = std::lock_guard<Mutex>;
  void msleep (int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  }
  template <typename... Args> void debug (const char*, Args...) {}
}

// Exercise the production GTK source and loop transitions with real GLib/libuv.
#include "loop-header.hh"
#include "loop-methods.hh"

static void checkUIProgress () {
  bool delivered = false;
  g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer data) -> gboolean {
    *static_cast<bool*>(data) = true;
    return G_SOURCE_REMOVE;
  }, &delivered, nullptr);

  for (int iteration = 0; iteration < 100 && !delivered; ++iteration) {
    g_main_context_iteration(nullptr, false);
  }
  assert(delivered && "Inactive libuv must not starve WebKit/GTK callbacks");
  assert(!g_main_context_pending(nullptr) && "Inactive libuv must not busy-poll");
}

int main () {
  oro::runtime::loop::Loop loop;
  assert(loop.start());

  for (int iteration = 0; iteration < 50; ++iteration) {
    // Register the internal async descriptor in epoll before pausing.
    uv_run(loop.get(), UV_RUN_NOWAIT);
    assert(loop.pause());

    // A pending wakeup or network event can make the backend readable after
    // pause. GTK must still deliver minimize replies and the restore request.
    assert(uv_async_send(&loop.uv.async) == 0);
    bool executed = false;
    assert(loop.dispatch([&executed]() { executed = true; }));
    checkUIProgress();
    assert(!executed);

    assert(loop.resume());
    for (int poll = 0; poll < 100 && !executed; ++poll) {
      g_main_context_iteration(nullptr, false);
    }
    assert(executed && "Restore must reattach polling and deliver retained work");
  }

  assert(loop.stop());
  assert(uv_async_send(&loop.uv.async) == 0);
  checkUIProgress();
  assert(loop.shutdown());
  checkUIProgress();
  std::cout << "Linux paused and stopped loops preserve GTK progress and resume work\n";
}
