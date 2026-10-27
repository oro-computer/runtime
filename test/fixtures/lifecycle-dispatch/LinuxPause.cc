#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "oro/platform.h"

template <typename T> using Atomic = std::atomic<T>;

namespace oro::runtime::env {
  bool has (const char*) { return false; }
}

template <typename... Args> void debug (const char*, Args...) {}

struct Loop {
  enum State { Idle, Paused, Stopped };
  State state = Idle;
  int pauses = 0;
  bool paused () const { return this->state == Paused; }
  bool stopped () const { return this->state == Stopped; }
  bool alive () const { return this->state == Idle; }
  bool resume () { this->state = Idle; return true; }
  bool stop () { this->state = Stopped; return true; }
  bool pause () {
    assert(this->state == Idle);
    ++this->pauses;
    this->state = Paused;
    return true;
  }
};

struct Services {
  bool stop () { return true; }
  bool start () { return true; }
};

struct Dispatcher {
  std::mutex mutex;
  std::vector<std::function<void()>> pending;

  void dispatch (std::function<void()> callback) {
    std::lock_guard lock(this->mutex);
    this->pending.push_back(std::move(callback));
  }

  void drain (bool reverse = false) {
    std::vector<std::function<void()>> callbacks;
    {
      std::lock_guard lock(this->mutex);
      callbacks.swap(this->pending);
    }
    if (reverse) std::reverse(callbacks.begin(), callbacks.end());
    for (const auto& callback : callbacks) callback();
  }
};

struct Runtime {
  Loop loop;
  Services services;
  Dispatcher& dispatcher;
  std::thread pauseThread;
  std::shared_ptr<Atomic<bool>> pendingPause;
  Atomic<bool> destroyed = false;
  std::map<std::string, std::string> userConfig {
    { "lifecycle_desktop_always_running", "false" }
  };

  explicit Runtime (Dispatcher& dispatcher) : dispatcher(dispatcher) {}
  ~Runtime ();
  bool pause ();
  bool resume ();
  bool stop ();
  bool destroy () { this->destroyed = true; return this->stop(); }
};

// Compile the real lifecycle methods. Only the service and UI queue are facades.
#include "linux-pause-methods.hh"

int main () {
  Dispatcher dispatcher;
  Runtime runtime(dispatcher);

  // Restore joins the worker, but its UI callback can still be pending.
  assert(runtime.pause());
  assert(runtime.resume());
  dispatcher.drain();
  assert(!runtime.loop.paused());
  assert(runtime.loop.pauses == 0);

  // An older queued pause cannot override a newer request in either order.
  assert(runtime.pause());
  assert(runtime.pause());
  runtime.pauseThread.join();
  dispatcher.drain(true);
  assert(runtime.loop.paused());
  assert(runtime.loop.pauses == 1);
  assert(runtime.resume());

  // Shutdown must also cancel the callback before joining its worker.
  assert(runtime.pause());
  assert(runtime.stop());
  dispatcher.drain();
  assert(runtime.loop.state == Loop::Stopped);

  // The UI queue can outlive the runtime; cancellation must be checked without
  // reading the destroyed runtime's fields.
  auto temporary = std::make_unique<Runtime>(dispatcher);
  assert(temporary->pause());
  temporary.reset();
  dispatcher.drain();

  std::cout << "Linux queued lifecycle pauses cancel on restore and shutdown\n";
}
