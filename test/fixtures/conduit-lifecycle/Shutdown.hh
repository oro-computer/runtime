// Use the production timer handles and shutdown methods with the real loop.
// Watcher and socket facades check scheduling without requiring OS resources.
namespace oro::runtime::crypto {
  static uint64_t rand64 () {
    static std::atomic<uint64_t> next = 1;
    return next++;
  }
}

#include "timers-header.hh"
#include "timers-methods.hh"

namespace oro::runtime::filesystem {
  struct Watcher {
    loop::Loop& loop;
    bool stopped = false;
    explicit Watcher (loop::Loop& loop) : loop(loop) {}
    bool stop (SharedPointer<Watcher>) {
      assert(this->loop.isOnLoopThread());
      this->stopped = true;
      return true;
    }
  };
}

namespace oro::runtime::core::services {
  struct FS : Service {
    Mutex mutex;
    Map<uint64_t, SharedPointer<filesystem::Watcher>> watchers;
    using Service::Service;
    bool stop () override;
  };

  struct SocketManager {
    loop::Loop& loop;
    int pauses = 0;
    void pauseOnLoopThread () {
      assert(this->loop.isOnLoopThread());
      ++this->pauses;
    }
  };

  struct UDP : Service {
    bool enabled = true;
    SocketManager manager { this->loop };
    using Service::Service;
    bool stop () override;
  };

  struct TCP : Service {
    SocketManager manager { this->loop };
    using Service::Service;
    bool stop () override;
  };
}

#include "service-stop-methods.hh"

static void checkServiceShutdown (Loop& loop) {
  using namespace oro::runtime::core::services;
  FS fs({ loop });
  Timers timers({ loop });
  UDP udp({ loop });
  TCP tcp({ loop });
  auto watcher = std::make_shared<oro::runtime::filesystem::Watcher>(loop);
  int fired = 0;
  onLoop(loop, [&]() {
    fs.watchers.emplace(1, watcher);
    timers.setTimeout(60000, [&]() { ++fired; });
  });
  // Let the timer initialization queued by setTimeout run before stopping it.
  onLoop(loop, [&]() {
    assert(!timers.handles.empty());
    // These calls used to wait for work queued behind this very callback.
    assert(fs.stop() && timers.stop() && udp.stop() && tcp.stop());
    assert(fs.watchers.empty() && watcher->stopped);
    assert(udp.manager.pauses == 1 && tcp.manager.pauses == 1);
    for (const auto& entry : timers.handles) {
      assert(uv_is_closing(reinterpret_cast<uv_handle_t*>(&entry.second->timer)));
    }
  });

  // Hold the loop while another thread requests shutdown. Service shutdown
  // must return so the caller can release the loop and reach its pause barrier.
  std::promise<void> entered;
  auto blocked = entered.get_future();
  std::promise<void> release;
  auto released = release.get_future();
  assert(loop.dispatch([&]() {
    entered.set_value();
    released.wait();
  }));
  assert(blocked.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
  assert(fs.stop() && timers.stop() && udp.stop() && tcp.stop());
  release.set_value();
  assert(loop.pause());
  assert(udp.manager.pauses == 2 && tcp.manager.pauses == 2);

  // Paused loops retain cleanup for resume without blocking the caller.
  assert(fs.stop() && timers.stop() && udp.stop() && tcp.stop());
  assert(udp.manager.pauses == 2 && tcp.manager.pauses == 2);
  assert(loop.resume());
  onLoop(loop, [&]() {
    assert(udp.manager.pauses == 3 && tcp.manager.pauses == 3);
    assert(timers.handles.empty() && fired == 0);
  });

  Loop stopped;
  assert(stopped.start() && stopped.stop());
  FS stoppedFS({ stopped });
  Timers stoppedTimers({ stopped });
  UDP stoppedUDP({ stopped });
  TCP stoppedTCP({ stopped });
  assert(!stoppedFS.stop() && !stoppedTimers.stop());
  assert(!stoppedUDP.stop() && !stoppedTCP.stop());
  assert(stopped.shutdown());
}
