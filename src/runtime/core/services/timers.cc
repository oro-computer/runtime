#include "timers.hh"
#include "../../debug.hh"
#include <future>

using oro::runtime::crypto::rand64;

namespace oro::runtime::core::services {
  bool Timers::stop () {
  #if ORO_RUNTIME_PLATFORM_LINUX
    // Non-blocking on Linux to avoid deadlocks with GTK-driven loop pumping.
    this->loop.dispatch([this]() {
      Vector<ID> ids;
      {
        Lock lock(this->mutex);
        ids.reserve(this->handles.size());
        for (auto const &tuple : this->handles) {
          ids.push_back(tuple.first);
        }
      }
      for (const auto id : ids) {
        this->cancelTimer(id);
      }
    });
    return true;
  #else
    // Synchronous elsewhere so timers close before pausing the loop
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    this->loop.dispatch([=, this]() {
      Vector<ID> ids;
      {
        Lock lock(this->mutex);
        ids.reserve(this->handles.size());
        for (auto const &tuple : this->handles) {
          ids.push_back(tuple.first);
        }
      }
      for (const auto id : ids) {
        this->cancelTimer(id);
      }
      done->set_value();
    });
    fut.wait();
    return true;
  #endif
  }
  struct TimerToken {
    Timers* timers = nullptr;
    Timers::ID id = 0;
  };

  Timers::Timer::Timer (Timers* timers, ID id, Callback callback)
    : timers(timers),
      id(id),
      callback(callback)
  {}

  const Timers::ID Timers::createTimer (
    uint64_t timeout,
    uint64_t interval,
    const Callback callback
  ) {
    Lock lock(this->mutex);

    const auto id = rand64();
    auto loop = this->loop.get();
    auto handle = std::make_shared<Timer>(this, id, callback);

    if (interval > 0) {
      handle->repeat = true;
    }

    this->handles.emplace(handle->id, handle);

    this->loop.dispatch([=, this]() {
      // Initialize and start the timer on the loop thread
      if (!this->handles.contains(id)) {
        return;
      }

      auto handle = this->handles.at(id);
      if (handle == nullptr) {
        return;
      }

      const auto token = new TimerToken{ this, id };
      uv_timer_init(loop, &handle->timer);
      uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&handle->timer), reinterpret_cast<void*>(token));

      uv_timer_start(
        &handle->timer,
        [](uv_timer_t* timer) {
          auto token = static_cast<TimerToken*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(timer)));

          if (token == nullptr) {
            // Defensive: stop and close if somehow missing token
            uv_timer_stop(timer);
            uv_close(reinterpret_cast<uv_handle_t*>(timer), [](uv_handle_t* /*h*/) {
            });
            return;
          }

          Timers* timers = token->timers;
          Timers::ID id = token->id;

          std::shared_ptr<Timer> handle;
          {
            Lock lock(timers->mutex);
            if (!timers->handles.contains(id)) {
              // Was cancelled; ensure handle is closed and token released
              uv_timer_stop(timer);
              uv_close(reinterpret_cast<uv_handle_t*>(timer), [](uv_handle_t* h) {
                auto t = static_cast<TimerToken*>(uv_handle_get_data(h));
                if (t) {
                  delete t;
                  uv_handle_set_data(h, nullptr);
                }
              });
              return;
            }
            handle = timers->handles.at(id);
          }

          if (handle == nullptr) {
            uv_timer_stop(timer);
            uv_close(reinterpret_cast<uv_handle_t*>(timer), [](uv_handle_t* h) {
              auto t = static_cast<TimerToken*>(uv_handle_get_data(h));
              if (t) {
                delete t;
                uv_handle_set_data(h, nullptr);
              }
            });
            return;
          }

          // Provide cancel function to the user callback
          handle->callback([timers, id]() {
            timers->cancelTimer(id);
          });

          // For one-shot timers, cancel after execution if not already
          if (!handle->repeat && !handle->cancelled) {
            timers->cancelTimer(id);
          }
        },
        timeout,
        interval
      );
    });

    return id;
  }

  bool Timers::cancelTimer (const ID id) {
    Lock lock(this->mutex);

    if (!this->handles.contains(id)) {
      return false;
    }

    auto handle = this->handles.at(id);
    if (handle == nullptr) {
      this->handles.erase(id);
      return true;
    }

    handle->cancelled = true;
    uv_timer_stop(&handle->timer);

    // Close the handle; free token and erase on close
    uv_close(reinterpret_cast<uv_handle_t*>(&handle->timer), [](uv_handle_t* h) {
      auto token = static_cast<TimerToken*>(uv_handle_get_data(h));
      if (token) {
        auto timers = token->timers;
        const auto id = token->id;
        {
          Lock lock(timers->mutex);
          timers->handles.erase(id);
        }
        delete token;
        uv_handle_set_data(h, nullptr);
      }
    });

    return true;
  }

  const Timers::ID Timers::setTimeout (
    uint64_t timeout,
    const TimeoutCallback callback
  ) {
    Lock lock(this->mutex);
    const auto id = this->createTimer(timeout, 0, [callback] (auto _) {
      callback();
    });

    if (this->handles.contains(id)) {
      this->handles.at(id)->type = Timer::Type::Timeout;
    }

    return id;
  }

  bool Timers::clearTimeout (const ID id) {
    return this->cancelTimer(id);
  }

  const Timers::ID Timers::setInterval (
    uint64_t interval,
    const IntervalCallback callback
  ) {
    Lock lock(this->mutex);

    const auto id = this->createTimer(interval, interval, callback);

    if (this->handles.contains(id)) {
      this->handles.at(id)->type = Timer::Type::Interval;
    }

    return id;
  }

  bool Timers::clearInterval (const ID id) {
    return this->cancelTimer(id);
  }

  const Timers::ID Timers::setImmediate (const ImmediateCallback callback) {
    Lock lock(this->mutex);

    const auto id = this->createTimer(0, 0, [callback] (auto _) {
      callback();
    });

    if (this->handles.contains(id)) {
      this->handles.at(id)->type = Timer::Type::Immediate;
    }

    return id;
  }

  bool Timers::clearImmediate (const ID id) {
    return this->clearTimeout(id);
  }
}
