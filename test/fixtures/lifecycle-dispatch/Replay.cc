#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "oro/platform.h"

using Mutex = std::recursive_mutex;
using UniqueLock = std::unique_lock<Mutex>;

// Exercise the shared lifecycle methods without an OS window or service backend.
// Stop fails immediately where a real service would wait on the paused loop.
struct Loop {
  bool suspended = false;
  bool paused () const { return this->suspended; }
  bool pause () {
    this->suspended = true;
    return true;
  }
  bool resume () {
    this->suspended = false;
    return true;
  }
};

struct Services {
  Loop& loop;
  int stops = 0;
  int starts = 0;

  bool stop () {
    if (this->loop.paused()) {
      throw std::runtime_error("Service shutdown cannot wait on a paused loop");
    }
    ++this->stops;
    return true;
  }

  bool start () {
    ++this->starts;
    return true;
  }
};

struct WindowManager {
  std::vector<std::string> events;
  void emit (const std::string& event) { this->events.push_back(event); }
};

struct Runtime {
  Loop loop;
  Services services { this->loop };
  WindowManager windowManager;
  std::map<std::string, std::string> userConfig {
    { "lifecycle_desktop_always_running", "false" }
  };

  bool pause ();
  bool resume ();
};

static uint64_t now_ms () {
  static uint64_t time = 1000;
  return time += 200;
}

struct App {
  Runtime runtime;
  std::atomic<bool> shouldExit = false;
  std::atomic<bool> isPaused = false;
  Mutex lifecycleMutex;
  std::atomic<uint64_t> lifecycleRevision = 0;
  std::atomic<uint64_t> lastPauseEmitMs = 0;
  std::atomic<uint64_t> lastResumeEmitMs = 0;
  std::vector<std::function<void()>> pending;

  bool stopped () const { return false; }
  bool paused () const { return this->isPaused.load(); }
  void dispatch (std::function<void()> callback) {
    this->pending.push_back(std::move(callback));
  }
  void drain () {
    for (const auto& callback : this->pending) {
      callback();
    }
    this->pending.clear();
  }
  void pause ();
  void resume ();
};

#include "lifecycle-methods.hh"

int main () {
  Runtime runtime;
  assert(runtime.pause());
  assert(runtime.pause());
  assert(runtime.services.stops == 1);
  assert(runtime.loop.paused());
  assert(runtime.resume());
  assert(runtime.pause());
  assert(runtime.services.stops == 2);

  // A restore supersedes an earlier pause that has not reached the UI queue.
  App restored;
  restored.pause();
  restored.resume();
  restored.drain();
  assert(!restored.runtime.loop.paused());
  assert(restored.runtime.services.stops == 0);

  // Concurrent native notifications can post their callbacks in a different
  // order from their state transitions. Both pauses still see the latest state.
  App minimized;
  minimized.pause();
  minimized.resume();
  minimized.pause();
  assert(minimized.pending.size() == 3);
  std::swap(minimized.pending[1], minimized.pending[2]);
  minimized.drain();
  assert(minimized.runtime.loop.paused());
  assert(minimized.runtime.services.stops == 1);
  assert(minimized.runtime.services.starts == 0);

  // The same ordering must preserve a final restore rather than applying
  // a superseded pause after it.
  minimized.resume();
  minimized.pause();
  minimized.resume();
  std::swap(minimized.pending[1], minimized.pending[2]);
  minimized.drain();
  assert(!minimized.runtime.loop.paused());
  assert(minimized.runtime.services.stops == 1);
  assert(minimized.runtime.services.starts == 1);

  // The desktop default continues to emit every explicit action.
#if ORO_RUNTIME_PLATFORM_DESKTOP
  App alwaysRunning;
  alwaysRunning.runtime.userConfig.clear();
  alwaysRunning.pause();
  alwaysRunning.pause();
  alwaysRunning.resume();
  alwaysRunning.resume();
  assert(alwaysRunning.runtime.windowManager.events.size() == 4);
  assert(alwaysRunning.pending.empty());
  assert(!alwaysRunning.paused());
#endif
}
