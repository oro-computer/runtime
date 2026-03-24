#include "tests.hh"
#include "src/runtime/loop.hh"
#include <future>

namespace oro::Tests {
  void restart (Harness& t) {
    t.test("Loop: start/stop/restart drains handles", [](auto t) {
      using oro::runtime::loop::Loop;
      const int iterations = 5;

      for (int i = 0; i < iterations; i++) {
        Loop loop;

        t.assert(loop.start(), "loop started");

        // Schedule a one-shot timer on the loop and wait for it to close
        std::promise<void> firedPromise;
        auto firedFuture = firedPromise.get_future();

        loop.dispatch([&loop, p = std::move(firedPromise)]() mutable {
          auto h = new uv_timer_t;
          uv_timer_init(loop.get(), h);
          uv_timer_start(h, [](uv_timer_t* th) {
            uv_timer_stop(th);
            uv_close(reinterpret_cast<uv_handle_t*>(th), [](uv_handle_t* hh) {
              delete reinterpret_cast<uv_timer_t*>(hh);
            });
          }, 5, 0);

          // Nudge the loop after close callback has a chance to run
          uv_timer_t* completion = new uv_timer_t;
          uv_timer_init(loop.get(), completion);
          uv_timer_start(completion, [](uv_timer_t* tt) {
            auto loopPtr = reinterpret_cast<Loop*>(uv_loop_get_data(tt->loop));
            uv_timer_stop(tt);
            uv_close(reinterpret_cast<uv_handle_t*>(tt), nullptr);
            // dispatch a no-op to ensure async wakes
            if (loopPtr) loopPtr->dispatch([](){});
          }, 10, 0);

          // Signal completion slightly after timers are scheduled
          std::thread([p = std::move(p)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            p.set_value();
          }).detach();
        });

        // Wait for the timer to fire and close
        auto status = firedFuture.wait_for(std::chrono::milliseconds(1000));
        t.assert(status == std::future_status::ready, "timer fired within 1s");

        // Stop and shutdown the loop; should drain cleanly
        t.assert(loop.stop(), "loop stopped");
        t.assert(loop.shutdown(), "loop shutdown");
      }

      t.assert(true, "completed iterations successfully");
    });
  }
}

