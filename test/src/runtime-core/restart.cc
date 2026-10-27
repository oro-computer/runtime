#include "src/runtime/runtime.hh"
#include "src/runtime/loop.hh"
#include <array>
#include <fstream>
#include <future>
#include <thread>
#include "tests.hh"

namespace oro::Tests {
  void restart (Harness& t) {
    t.test("Queued responses: concurrent producers and consumers retain each body once", [](auto t) {
      runtime::context::RuntimeContext context;
      std::array<std::thread, 8> workers;
      std::atomic<bool> valid = true;
      for (size_t worker = 0; worker < workers.size(); ++worker) {
        workers[worker] = std::thread([&, worker]() {
          for (size_t index = 0; index < 128; ++index) {
            runtime::QueuedResponse response;
            response.id = worker * 128 + index + 1;
            response.length = 1;
            response.body.reset(new unsigned char[1]);
            response.body[0] = static_cast<unsigned char>(index);
            const auto id = response.id;
            context.createQueuedResponse("-1", "{}", std::move(response), true);
            const auto received = context.takeQueuedResponse(id);
            if (!received || received->length != 1 || !received->body ||
                received->body[0] != static_cast<unsigned char>(index) || context.takeQueuedResponse(id)) {
              valid = false;
            }
          }
        });
      }
      for (auto& worker : workers) {
        worker.join();
      }
      t.assert(valid && context.queuedResponses.empty(), "concurrent enqueue and consume preserve and release bodies");
      runtime::QueuedResponse response;
      response.id = 1;
      context.createQueuedResponse("-1", "{}", std::move(response));
      std::atomic<size_t> deliveries = 0;
      for (auto& worker : workers) {
        worker = std::thread([&]() {
          if (context.takeQueuedResponse(1)) {
            ++deliveries;
          }
        });
      }
      for (auto& worker : workers) {
        worker.join();
      }
      t.equals(deliveries.load(), size_t(1), "contended response is delivered exactly once");
    });

  #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_PLATFORM_ANDROID
    t.test("Bridge: replies and events retain their custom dispatcher and queue", [](auto t) {
      runtime::Runtime::Options options;
      options.autoStart = false;
      options.loop.dedicatedThread = true;
      runtime::Runtime instance(options);
      instance.loop.start();
      const auto bridge = instance.bridgeManager.get(0, {});
      size_t dispatched = 0;
      String script;
      bridge->dispatchRouterCallbacksWithBridge = true;
      bridge->dispatchHandler = [&](auto callback) {
        ++dispatched;
        callback();
      };
      bridge->evaluateJavaScriptHandler = [&](const auto& source) {
        script = source;
      };
      bridge->router.map("qualification.reply", false, [](auto message, auto router, auto reply) {
        reply(runtime::ipc::Result { message.seq, message, runtime::JSON::Object {} });
      });
      bridge->router.map("qualification.event", false, [](auto message, auto router, auto reply) {
        reply(runtime::ipc::Result { "-1", message, runtime::JSON::Object {} });
      });
      t.assert(bridge->router.invoke("ipc://qualification.reply?seq=1", nullptr, 0), "ordinary reply is routed");
      t.equals(dispatched, size_t(1), "ordinary reply uses the bridge dispatcher");
      t.assert(!script.empty(), "ordinary reply reaches JavaScript evaluation");
      bool returned = false;
      t.assert(bridge->router.invoke("ipc://qualification.event?seq=2", [&](auto) {
        returned = true;
      }), "stream event is routed");
      t.equals(dispatched, size_t(2), "stream event uses the bridge dispatcher");
      t.assert(!returned, "stream event is not returned as an ordinary reply");
      t.assert(script.find("useExtensionIPCIfAvailable: true") != String::npos,
        "native event reads its body from the extension queue");
      t.equals(instance.queuedResponses.size(), size_t(1), "stream response remains in its owning runtime");
      instance.destroy();
    });
  #endif

    t.test("Watcher: changes immediately after startup are delivered", [](auto t) {
      using runtime::filesystem::Watcher;
      const auto filename = std::filesystem::temp_directory_path() /
        ("oro-watcher-first-event-" + std::to_string(Watcher::Clock::now().time_since_epoch().count()));
      std::ofstream(filename).put('a');
      runtime::loop::Loop loop(runtime::loop::Loop::Options {
        .dedicatedThread = true
      });
      auto watcher = std::make_shared<Watcher>(filename.string());
      watcher->loop = &loop;
      watcher->options.debounce = 60000;
      std::promise<void> changed;
      auto event = changed.get_future();
      std::atomic<bool> delivered = false;
      loop.start();
      watcher->start([&](const auto&, const auto&, const auto&) {
        if (!delivered.exchange(true)) {
          changed.set_value();
        }
      });
      // start() queues handle setup before this write on the same loop.
      loop.dispatch([filename]() {
        std::ofstream(filename, std::ios::app).put('b');
      });
      t.assert(event.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "first change is delivered without waiting for the debounce interval");
      std::promise<void> stopped;
      auto closed = stopped.get_future();
      loop.dispatch([&]() {
        watcher->stop(watcher);
        stopped.set_value();
      });
      t.assert(closed.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "watcher stops on its loop thread");
      loop.shutdown();
      std::error_code error;
      std::filesystem::remove(filename, error);
    });

    t.test("Runtime: service startup can wait for platform configuration", [](auto t) {
      runtime::Runtime instance(runtime::Runtime::Options {
        .autoStart = false
      });

      t.assert(!instance.loop.started(), "loop waits for explicit startup");
      t.assert(!instance.services.state.isReady(), "state storage waits for explicit startup");
    #if !ORO_RUNTIME_PLATFORM_APPLE && !ORO_RUNTIME_PLATFORM_LINUX
      t.assert(!instance.services.networkStatus.enabled, "unavailable native network monitor is disabled");
    #endif
      t.assert(instance.destroy(), "runtime can be destroyed before startup");
      t.assert(!instance.loop.started(), "destroy does not start the loop");
    });

    t.test("UDP: repeated pause/resume retains its live endpoint", [](auto t) {
      runtime::loop::Loop loop(runtime::loop::Loop::Options { .dedicatedThread = true });
      runtime::udp::SocketManager manager({ loop });
      std::promise<bool> result;
      auto completed = result.get_future();
      loop.start();
      loop.dispatch([&]() {
        const auto socket = manager.create(runtime::udp::SOCKET_TYPE_UDP, 1);
        bool valid = socket->bind("127.0.0.1", 0) == 0;
        const auto port = socket->getLocalPeerInfo()->port;
        for (size_t index = 0; index < 3; ++index) {
          valid = socket->pause() == 0 && socket->pause() == 0 && valid;
          valid = socket->resume() == 0 && socket->resume() == 0 && valid;
          valid = socket->isBound() && socket->initLocalPeerInfo() == 0 &&
            socket->getLocalPeerInfo()->port == port && valid;
        }
        valid = socket->recvstart([](auto, const auto* buffer, const auto*) {
          if (buffer && buffer->base) {
            delete[] buffer->base;
          }
        }) == 0 && valid;
        valid = socket->pause() == 0 && socket->pause() == 0 &&
          socket->resume() == 0 && socket->hasState(runtime::udp::SOCKET_STATE_UDP_RECV_STARTED) && valid;
        valid = socket->pause() == 0 && socket->recvstop() == 0 && socket->resume() == 0 &&
          !socket->hasState(runtime::udp::SOCKET_STATE_UDP_RECV_STARTED) && valid;
        socket->close([&, valid]() { result.set_value(valid); });
      });
      const bool ready = completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
      t.assert(ready && completed.get(), "bound endpoint survives repeated pause/resume without handle replacement");
      loop.shutdown();
    });

    t.test("Loop: dispatch during pause waits for resume", [](auto t) {
      using runtime::loop::Loop;
      Loop loop(Loop::Options { .dedicatedThread = true });
      t.assert(loop.start(), "loop starts");
      t.assert(loop.pause(), "loop pauses");
      std::promise<void> delivered;
      auto completed = delivered.get_future();
      const bool accepted = loop.dispatch([&]() {
        delivered.set_value();
      });
      t.assert(accepted, "paused dispatch is retained");
      t.assert(completed.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
        "queued work does not execute during pause");
      t.assert(loop.resume(), "loop resumes");
      t.assert(completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "queued work executes after resume");
      t.assert(loop.stop(), "loop stops");
      t.assert(!loop.dispatch([]() {}), "stopped loop rejects new work");
      loop.shutdown();
    });

  #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_PLATFORM_ANDROID
    t.test("Loop: pending async wakeups cannot restart a stopped loop", [](auto t) {
      using runtime::loop::Loop;
      Loop loop(Loop::Options { .dedicatedThread = false });
      t.assert(loop.start(), "loop starts before pending work");
      bool delivered = false;
      t.assert(loop.dispatch([&]() { delivered = true; }), "work is accepted before stop");
      t.assert(loop.stop(), "loop stops with pending work");
      uv_async_send(&loop.uv.async);
      loop.uv.run(UV_RUN_NOWAIT);
      t.assert(loop.stopped(), "final async poll preserves the stopped state");
      t.assert(!delivered, "pending work does not run after stop");
      t.assert(loop.shutdown(), "stopped loop shuts down");
      t.assert(!delivered, "shutdown does not run retained work");
    });

    t.test("Loop: GTK pumping cannot execute paused work", [](auto t) {
      using runtime::loop::Loop;
      Loop loop;
      t.assert(loop.start() && loop.pause(), "GTK loop starts and pauses");
      bool delivered = false;
      t.assert(loop.dispatch([&]() { delivered = true; }), "GTK loop retains paused work");
      for (size_t index = 0; index < 20; ++index) {
        g_main_context_iteration(nullptr, false);
      }
      t.assert(!delivered && loop.paused(), "GLib leaves paused work pending");
      t.assert(loop.resume(), "GTK loop resumes");
      for (size_t index = 0; index < 100 && !delivered; ++index) {
        g_main_context_iteration(nullptr, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      t.assert(delivered, "GLib delivers retained work after resume");
      loop.shutdown();
    });
  #endif

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
