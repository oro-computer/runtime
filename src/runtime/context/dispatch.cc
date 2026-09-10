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

    Lock lock(this->mutex);
    // Window messages reach the window procedure in nested COM/modal pumps.
    // Thread messages have no HWND and can be consumed without dispatching them.
    if (this->ready.load(std::memory_order_acquire) &&
        !PostMessageW(this->messageWindow, WM_APP, 0, 0)) {
      debug("Dispatcher: PostMessage failed: %lu", GetLastError());
      return false;
    }
    this->pending.push(callback);
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
  Dispatcher::~Dispatcher () {
    if (this->messageWindow != nullptr) {
      DestroyWindow(this->messageWindow);
      this->messageWindow = nullptr;
    }
  }

  LRESULT CALLBACK Dispatcher::onMessage (HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
      const auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    if (message == WM_APP) {
      const auto dispatcher = reinterpret_cast<Dispatcher*>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (dispatcher != nullptr) {
        dispatcher->drain();
      }
      return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
  }

  void Dispatcher::notifyReady () {
    if (GetCurrentThreadId() != this->mainThreadId) {
      return;
    }

    Lock lock(this->mutex);
    if (this->ready.load(std::memory_order_acquire)) {
      return;
    }

    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = Dispatcher::onMessage;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"OroRuntimeDispatcher";
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      throw std::runtime_error("Could not register the Windows dispatcher window");
    }

    this->messageWindow = CreateWindowExW(
      0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
      HWND_MESSAGE, nullptr, instance, this
    );
    if (this->messageWindow == nullptr) {
      throw std::runtime_error("Could not create the Windows dispatcher window");
    }

    this->ready.store(true, std::memory_order_release);
    if (!this->pending.empty() && !PostMessageW(this->messageWindow, WM_APP, 0, 0)) {
      throw std::runtime_error("Could not schedule pending Windows UI callbacks");
    }
  }

  void Dispatcher::drain () {
    while (true) {
      Callback fn;
      {
        Lock lock(this->mutex);
        if (this->pending.empty()) {
          break;
        }
        fn = std::move(this->pending.front());
        this->pending.pop();
      }

      if (fn != nullptr) {
        fn();
      }
    }
  }
#endif
}
