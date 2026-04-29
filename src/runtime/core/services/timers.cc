#include "timers.hh"
#include "../../debug.hh"
#include <future>

using oro::runtime::crypto::rand64;

namespace oro::runtime::core::services {
  struct TimerToken {
    Timers* timers = nullptr;
    Timers::ID id = 0;
  };

  static void closeTimerOnLoop (SharedPointer<Timers::Timer> handle) {
    if (handle == nullptr || !handle->initialized) {
      return;
    }

    auto timer = &handle->timer;
    auto uvHandle = reinterpret_cast<uv_handle_t*>(timer);
    if (uv_is_closing(uvHandle)) {
      return;
    }

    uv_timer_stop(timer);
    uv_close(uvHandle, [](uv_handle_t* h) {
      auto token = static_cast<TimerToken*>(uv_handle_get_data(h));
      if (token != nullptr) {
        auto timers = token->timers;
        const auto id = token->id;
        if (timers != nullptr) {
          Lock lock(timers->mutex);
          timers->handles.erase(id);
        }

        delete token;
        uv_handle_set_data(h, nullptr);
      }
    });
  }

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
        SharedPointer<Timer> handle = nullptr;
        {
          Lock lock(this->mutex);
          auto it = this->handles.find(id);
          if (it == this->handles.end()) {
            continue;
          }

          handle = it->second;
          if (handle == nullptr || !handle->initialized) {
            this->handles.erase(it);
            continue;
          }

          handle->cancelled = true;
          if (handle->closing) {
            continue;
          }

          handle->closing = true;
        }

        closeTimerOnLoop(handle);
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
        SharedPointer<Timer> handle = nullptr;
        {
          Lock lock(this->mutex);
          auto it = this->handles.find(id);
          if (it == this->handles.end()) {
            continue;
          }

          handle = it->second;
          if (handle == nullptr || !handle->initialized) {
            this->handles.erase(it);
            continue;
          }

          handle->cancelled = true;
          if (handle->closing) {
            continue;
          }

          handle->closing = true;
        }

        closeTimerOnLoop(handle);
      }
      done->set_value();
    });
    fut.wait();
    return true;
  #endif
  }

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
      SharedPointer<Timer> handle = nullptr;
      {
        Lock lock(this->mutex);
        auto it = this->handles.find(id);
        if (it == this->handles.end()) {
          return;
        }

        handle = it->second;
        if (handle == nullptr || handle->cancelled) {
          this->handles.erase(it);
          return;
        }

        const auto token = new TimerToken{ this, id };
        uv_timer_init(loop, &handle->timer);
        uv_handle_set_data(reinterpret_cast<uv_handle_t*>(&handle->timer), reinterpret_cast<void*>(token));
        handle->initialized = true;
      }

      uv_timer_start(
        &handle->timer,
        [](uv_timer_t* timer) {
          auto token = static_cast<TimerToken*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(timer)));

          if (token == nullptr) {
            auto handle = reinterpret_cast<uv_handle_t*>(timer);
            if (!uv_is_closing(handle)) {
              uv_timer_stop(timer);
              uv_close(handle, [](uv_handle_t* h) {
                uv_handle_set_data(h, nullptr);
              });
            }
            return;
          }

          Timers* timers = token->timers;
          Timers::ID id = token->id;

          SharedPointer<Timer> handle;
          {
            Lock lock(timers->mutex);
            auto it = timers->handles.find(id);
            if (it == timers->handles.end()) {
              return;
            }

            handle = it->second;
            if (handle == nullptr) {
              timers->handles.erase(it);
              return;
            }

            if (handle->cancelled || handle->closing) {
              return;
            }
          }

          if (handle == nullptr) {
            return;
          }

          // Provide cancel function to the user callback.
          handle->callback([timers, id]() {
            timers->cancelTimer(id);
          });

          bool shouldCancel = false;
          {
            Lock lock(timers->mutex);
            auto it = timers->handles.find(id);
            if (it != timers->handles.end() && it->second != nullptr) {
              shouldCancel = !it->second->repeat && !it->second->cancelled;
            }
          }

          if (shouldCancel) {
            timers->cancelTimer(id);
          }
        },
        timeout,
        interval
      );

      bool shouldClose = false;
      {
        Lock lock(this->mutex);
        auto it = this->handles.find(id);
        if (it != this->handles.end() && it->second != nullptr && it->second->cancelled) {
          if (!it->second->closing) {
            it->second->closing = true;
            shouldClose = true;
          }
        }
      }

      if (shouldClose) {
        closeTimerOnLoop(handle);
      }
    });

    return id;
  }

  bool Timers::cancelTimer (const ID id) {
    SharedPointer<Timer> handle = nullptr;

    {
      Lock lock(this->mutex);

      auto it = this->handles.find(id);
      if (it == this->handles.end()) {
        return false;
      }

      handle = it->second;
      if (handle == nullptr) {
        this->handles.erase(it);
        return true;
      }

      handle->cancelled = true;

      if (!handle->initialized) {
        this->handles.erase(it);
        return true;
      }

      if (handle->closing) {
        return true;
      }

      handle->closing = true;
    }

    this->loop.dispatch([handle]() {
      closeTimerOnLoop(handle);
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
