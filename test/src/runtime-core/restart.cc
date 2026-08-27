#include "tests.hh"
#include "src/runtime/loop.hh"
#include <future>

namespace oro::Tests {
  void restart (Harness& t) {
    t.test("Loop: start/stop/restart drains handles", [](auto t) {
      using oro::runtime::loop::Loop;
      const int iterations = 5;

      struct TimerState {
        uv_timer_t handle {};
        std::shared_ptr<std::promise<void>> fired;
      };

      for (int i = 0; i < iterations; i++) {
        Loop loop(Loop::Options {
          .dedicatedThread = true
        });

        t.assert(loop.start(), "loop started");
      #if ORO_RUNTIME_PLATFORM_LINUX
        t.assert(loop.gtk.source == nullptr, "dedicated loop does not attach a GLib source");
      #endif

        auto firedPromise = std::make_shared<std::promise<void>>();
        auto firedFuture = firedPromise->get_future();

        loop.dispatch([&loop, firedPromise]() {
          auto timer = new TimerState {
            .fired = firedPromise
          };
          timer->handle.data = timer;
          uv_timer_init(loop.get(), &timer->handle);
          uv_timer_start(&timer->handle, [](uv_timer_t* handle) {
            auto state = reinterpret_cast<TimerState*>(handle->data);
            state->fired->set_value();
            uv_timer_stop(handle);
            uv_close(reinterpret_cast<uv_handle_t*>(handle), [](uv_handle_t* handle) {
              delete reinterpret_cast<TimerState*>(handle->data);
            });
          }, 5, 0);
        });

        auto status = firedFuture.wait_for(std::chrono::milliseconds(1000));
        t.assert(status == std::future_status::ready, "timer fired within 1s");

        t.assert(loop.stop(), "loop stopped");
        t.assert(loop.shutdown(), "loop shutdown");
      }

      t.assert(true, "completed iterations successfully");
    });

  #if ORO_RUNTIME_PLATFORM_LINUX
    t.test("Loop: Linux GLib source is detached during shutdown", [](auto t) {
      using oro::runtime::loop::Loop;

      Loop loop;
      t.assert(loop.start(), "loop started");
      t.assert(loop.gtk.source != nullptr, "main-thread loop attaches a GLib source");
      t.assert(loop.shutdown(), "loop shutdown");
      t.assert(loop.gtk.source == nullptr, "GLib source detached");
    });
  #endif

    t.test("Loop: initialized loop can shut down before start", [](auto t) {
      using oro::runtime::loop::Loop;

      Loop loop(Loop::Options {
        .dedicatedThread = true
      });
      t.assert(loop.init(), "loop initialized");
      t.assert(loop.shutdown(), "initialized loop shutdown");
    });
  }
}
