#include "../context.hh"
#include "../debug.hh"
#include "../env.hh"
#if ORO_RUNTIME_PLATFORM_WINDOWS
#  include <windows.h>
#endif

namespace oro::runtime::context {
  DispatchContext::DispatchContext (RuntimeContext& context)
    : context(context) {
  #if ORO_RUNTIME_PLATFORM_ANDROID
    this->android = context.android;
  #endif
  }

  Dispatcher::Dispatcher (RuntimeContext& context)
    : DispatchContext(context) {
  #if ORO_RUNTIME_PLATFORM_WINDOWS
    // Capture the main thread id at construction (should be constructed on main thread)
    this->mainThreadId = GetCurrentThreadId();
    if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
      debug("Dispatcher: constructed on main thread id=%lu", this->mainThreadId);
    }
  #endif
  }

  bool Dispatcher::dispatch (const Callback callback) {
    if (callback == nullptr) {
      return false;
    }

  #if ORO_RUNTIME_PLATFORM_LINUX
    g_main_context_invoke(
      nullptr,
      +[](gpointer userData) -> gboolean {
        const auto callback = reinterpret_cast<DispatchCallback*>(userData);
        if (*callback != nullptr) {
          (*callback)();
          delete callback;
        }
        return G_SOURCE_REMOVE;
      },
      new DispatchCallback(std::move(callback))
    );

    return true;
  #elif ORO_RUNTIME_PLATFORM_APPLE
    // Captured windows must also be released on the main queue after execution.
    dispatch_async(dispatch_get_main_queue(), ^{
      callback();
    });

    return true;
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    // If we're already on the main thread, execute inline
    if (GetCurrentThreadId() == this->mainThreadId) {
      callback();
      return true;
    }

    // If not yet ready, queue the callback for later
    if (!this->ready.load(std::memory_order_acquire)) {
      Lock lock(this->mutex);
      this->pending.push(callback);
      if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
        debug("Dispatcher: queued callback (not ready)");
      }
      return true; // queued
    }

    auto threadCallback = reinterpret_cast<LPARAM>(new DispatchCallback(std::move(callback)));
    const BOOL ok = PostThreadMessage(this->mainThreadId, WM_APP, 0, threadCallback);
    if (!ok) {
      // Could not post; keep it safe by queuing for later retry
      auto cb = reinterpret_cast<DispatchCallback*>(threadCallback);
      const auto fn = *cb;
      delete cb;
      Lock lock(this->mutex);
      this->pending.push(fn);
      if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
        debug("Dispatcher: PostThreadMessage failed; queued for retry");
      }
      return false;
    }
    if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
      debug("Dispatcher: posted callback via WM_APP");
    }
    return true;
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    this->android.looper.dispatch([this, callback = std::move(callback)] () {
      const auto attachment = android::JNIEnvironmentAttachment(this->android.jvm);
      callback();
    });
    return true;
  #endif
    return false;
  }

#if ORO_RUNTIME_PLATFORM_WINDOWS
  void Dispatcher::notifyReady () {
    // Called from the main thread once the message pump is about to run
    if (GetCurrentThreadId() != this->mainThreadId) {
      return;
    }

    // Ensure the thread has a message queue
    MSG msg;
    PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);

    this->ready.store(true, std::memory_order_release);
    if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
      debug("Dispatcher: notifyReady(); flushing queued callbacks");
    }

    // Flush any pending callbacks by posting them as thread messages.
    // If posting fails, execute inline (we're on main thread).
    while (true) {
      Callback fn;
      {
        Lock lock(this->mutex);
        if (this->pending.empty()) break;
        fn = std::move(this->pending.front());
        this->pending.pop();
      }

      if (fn == nullptr) continue;

      auto lparam = reinterpret_cast<LPARAM>(new DispatchCallback(std::move(fn)));
      if (!PostThreadMessage(this->mainThreadId, WM_APP, 0, lparam)) {
        auto cb = reinterpret_cast<DispatchCallback*>(lparam);
        auto run = *cb;
        delete cb;
        if (run) run();
        if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
          debug("Dispatcher: inline executed pending callback (post failed)");
        }
      } else {
        if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
          debug("Dispatcher: flushed pending callback via WM_APP");
        }
      }
    }
    if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
      debug("Dispatcher: notifyReady() complete");
    }
  }
#endif
}
