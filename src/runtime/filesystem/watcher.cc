#include "../filesystem.hh"
#include "../debug.hh"

namespace oro::runtime::filesystem {
  static Watcher::Path resolveFileNameForContext (
    const String& filename,
    const Watcher::Context* context
  ) {
    static const auto cwd = fs::current_path();

    if (context->isDirectory) {
      return cwd / context->name / filename;
    }

    return cwd / context->name;
  }

  static void handleCallback (
    Watcher::Context* context,
    const String filename,
    const int eventTypes
  ) {
    const auto path = resolveFileNameForContext(filename, context);
    const auto now = Watcher::Clock::now();

    if (!std::filesystem::exists(path)) {
      return;
    }

    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - context->lastUpdated
    );

    if (delta.count() < context->watcher->options.debounce) {
      return;
    }

    // build events vector
    auto events = Vector<Watcher::Event>();

    if (eventTypes == 0) {
      events.push_back(Watcher::Event::CHANGE);
    } else {
      if ((eventTypes & UV_RENAME) == UV_RENAME) {
        events.push_back(Watcher::Event::RENAME);
      }

      if ((eventTypes & UV_CHANGE) == UV_CHANGE) {
        events.push_back(Watcher::Event::CHANGE);
      }
    }

    context->lastUpdated = now;
    if (events.size() == 1) {
      context->watcher->callback(path.string(), events, *context);
    }
  }

  void Watcher::handleEventCallback (
    EventHandle* handle,
    const char* eventTarget,
    int eventTypes,
    int eventStatus
  ) {
    const auto filename = String(eventTarget);
    const auto context = reinterpret_cast<Context*>(handle->data);
    handleCallback(context, filename, eventTypes);
  }

  void Watcher::handlePollCallback (
    PollHandle* handle,
    int status,
    const Stat* previousStat,
    const Stat* currentStat
  ) {
    struct PathData {
      char buffer[BUFSIZ];
      size_t size = BUFSIZ;
    };

    PathData data;

    const auto result = uv_fs_poll_getpath(handle, data.buffer, &data.size);

    if (result != 0) {
      debug(
        "Watcher: uv_fs_poll_getpath: error: %s\n",
        uv_strerror(result)
      );
      return;
    }

    const auto filename = String(data.buffer, data.size);
    const auto context = reinterpret_cast<Context*>(handle->data);
    handleCallback(context, filename, 0);
  }

  Watcher::Watcher (const String& path) {
    this->paths.push_back(path);
  }

  Watcher::Watcher (const Vector<String>& paths) {
    this->paths = paths;
  }

  Watcher::~Watcher () {
    this->paths.clear();
    this->watchedPaths.clear();

    if (this->ownsLoop && this->loop != nullptr) {
      delete this->loop;
    }

    this->ownsLoop = false;
    this->loop = nullptr;
  }

  bool Watcher::start (EventCallback callback) {
    if (this->isRunning) {
      return false;
    }

    this->callback = callback;

    // a loop may be configured for the instance already, perhaps here or
    // manually by the caller
    if (this->loop == nullptr) {
      loop::Loop::Options options;
      this->loop = new loop::Loop(options);
      this->ownsLoop = true;
    }

    for (const auto &path : this->paths) {
      bool existsInWatchedDirectory = false;
      for (const auto& existingPath : this->paths) {
        if (
          path != existingPath &&
          path.starts_with(existingPath) &&
          std::filesystem::is_directory(existingPath)
        ) {
          existsInWatchedDirectory = true;
          break;
        }
      }

      if (existsInWatchedDirectory) {
        continue;
      }

      this->watchedPaths.push_back(path);
    }

    this->loop->dispatch([this]() mutable {
      for (const auto &path : this->watchedPaths) {
        const bool exists = this->handles.contains(path);
        auto context = &this->contexts[path];
        auto handle = &this->handles[path];
        int status = 0;

        // init (or re-init) handles as needed
        // init context
        context->isDirectory = std::filesystem::is_directory(path);
        context->lastUpdated = Watcher::Clock::now();
        context->watcher = this;
        context->name = std::filesystem::absolute(path).string();

        // uv_fs_event handle for files AND directories
        if (!exists || !handle->eventInitialized.load()) {
          status = uv_fs_event_init(this->loop->get(), &handle->event);
          if (status != 0) {
            debug(
              "Watcher: uv_fs_event_init: error: %s\n",
              uv_strerror(status)
            );
          } else {
            handle->eventInitialized = true;
          }
        }

        // associate context pointers regardless
        handle->event.data = reinterpret_cast<void *>(context);
        handle->poll.data = reinterpret_cast<void *>(context);

        // uv_fs_poll only for files
        if (!context->isDirectory && (!exists || !handle->pollInitialized.load())) {
          status = uv_fs_poll_init(this->loop->get(), &handle->poll);
          if (status != 0) {
            debug(
              "Watcher: uv_fs_poll_init: error: %s\n",
              uv_strerror(status)
            );
          } else {
            handle->pollInitialized = true;
          }
        }

        // start (or restart)
        status = uv_fs_event_start(
          &handle->event,
          Watcher::handleEventCallback,
          path.c_str(),
          this->options.recursive ? UV_FS_EVENT_RECURSIVE : 0
        );

        if (status != 0) {
          debug(
            "Watcher: uv_fs_event_start: error: %s\n",
            uv_strerror(status)
          );
        }

        if (!context->isDirectory) {
          status = uv_fs_poll_start(
            &handle->poll,
            Watcher::handlePollCallback,
            path.c_str(),
            500
          );

          if (status != 0) {
            debug(
              "Watcher: uv_fs_poll_start: error: %s\n",
              uv_strerror(status)
            );
          }
        }
      }
    });

    this->isRunning = true;
    return true;
  }

  bool Watcher::stop () {
    if (!this->isRunning) {
      return false;
    }

    this->isRunning = false;

    struct CloseToken { std::shared_ptr<std::atomic<int>> pending; };
    auto pending = std::make_shared<std::atomic<int>>(0);
    for (const auto& path : this->watchedPaths) {
      if (this->handles.contains(path)) {
        const auto isDirectory = std::filesystem::is_directory(path);
        auto handle = &this->handles.at(path);
        int status = 0;

        if (handle->eventInitialized.load()) {
          status = uv_fs_event_stop(&handle->event);
        }

        if (status != 0) {
          debug(
            "Watcher: uv_fs_event_stop: error: %s\n",
            uv_strerror(status)
          );
        }

        if (!isDirectory && handle->pollInitialized.load()) {
          status = uv_fs_poll_stop(&handle->poll);
          if (status != 0) {
            debug(
              "Watcher: uv_fs_poll_stop: error: %s\n",
              uv_strerror(status)
            );
          }
        }

        // close handles on the loop thread to release OS resources
        if (handle->eventInitialized.load()) {
          ++(*pending);
          auto token = new CloseToken{ pending };
          this->loop->dispatch([h = &handle->event, token]() {
            uv_handle_set_data(reinterpret_cast<uv_handle_t*>(h), token);
            uv_close(reinterpret_cast<uv_handle_t*>(h), [](uv_handle_t* uvh) {
              auto token = static_cast<CloseToken*>(uv_handle_get_data(uvh));
              if (token && token->pending) {
                --(*token->pending);
              }
              delete token;
              uv_handle_set_data(uvh, nullptr);
              debug("Watcher: uv_fs_event handle closed");
            });
          });
          handle->eventInitialized = false;
        }

        if (!isDirectory && handle->pollInitialized.load()) {
          ++(*pending);
          auto token = new CloseToken{ pending };
          this->loop->dispatch([h = &handle->poll, token]() {
            uv_handle_set_data(reinterpret_cast<uv_handle_t*>(h), token);
            uv_close(reinterpret_cast<uv_handle_t*>(h), [](uv_handle_t* uvh) {
              auto token = static_cast<CloseToken*>(uv_handle_get_data(uvh));
              if (token && token->pending) {
                --(*token->pending);
              }
              delete token;
              uv_handle_set_data(uvh, nullptr);
              debug("Watcher: uv_fs_poll handle closed");
            });
          });
          handle->pollInitialized = false;
        }
      }
    }

    // Give the loop a brief window to process close callbacks
    if (this->ownsLoop && this->loop != nullptr) {
      const auto start = std::chrono::steady_clock::now();
      while (true) {
        // nudge the loop
        this->loop->dispatch([](){});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        // Heuristic drain: break early if no pending handles
        if (pending->load() == 0) {
          break;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start
        ).count();

        if (elapsed > 200) {
          break;
        }
      }
    }

    // Clear contexts and deduped watched paths; keep handle storage so re-init can reuse memory
    this->contexts.clear();
    this->watchedPaths.clear();

    // stop loop if owned by instance
    if (this->ownsLoop && this->loop != nullptr) {
      this->loop->stop();
    }

    return true;
  }
}
