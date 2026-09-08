#include "../cwd.hh"
#include "../app.hh"
#include "../env.hh"
#include "../string.hh"
#include "../background/options.hh"
#include <chrono>

namespace oro::runtime::app {
  namespace {
    Runtime::Features resolveRuntimeFeatures (
      const App::Options& options,
      const Runtime::BackgroundOptions& backgroundOptions
    ) {
      auto features = options.features;

#if !ORO_RUNTIME_HAS_IROH_FFI
      features.useIroh = false;
#else
      if (options.userConfig.contains("services_iroh")) {
        const auto raw = oro::runtime::string::toLowerCase(options.userConfig.at("services_iroh"));
        if (raw == "false" || raw == "0" || raw == "off") {
          features.useIroh = false;
        } else if (raw == "true" || raw == "1" || raw == "on") {
          features.useIroh = true;
        }
      }
#endif

#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) || defined(ORO_RUNTIME_TLS_OPENSSL)
      const auto enableTLS = oro::runtime::env::get("ORO_ENABLE_TLS");
      const auto enableMbedTLS = oro::runtime::env::get("ORO_ENABLE_MBEDTLS");

      if (!features.useTLS) {
        if (enableTLS == "1" || enableMbedTLS == "1") {
          features.useTLS = true;
        }
      }
#endif

      features.useBackground = backgroundOptions.enabled;
      return features;
    }

    Runtime::BackgroundOptions resolveBackgroundOptions (const App::Options& options) {
      if (
        options.background.enabled ||
        options.background.defaultEntry.size() > 0 ||
        !options.background.services.empty()
      ) {
        return options.background;
      }

      return oro::runtime::background::parse(options.userConfig);
    }
  }

  static SharedPointer<App> sharedApplicationInstance = nullptr;

  SharedPointer<App> App::sharedApplication () {
    return sharedApplicationInstance;
  }

  void releaseApplication () {
    sharedApplicationInstance.reset();
  }

  App::App (const Options& options)
    : runtime([&options]() {
        Runtime::Options runtimeOptions;
        runtimeOptions.userConfig = options.userConfig;
        runtimeOptions.loop = options.loop;
        runtimeOptions.background = resolveBackgroundOptions(options);
        runtimeOptions.features = resolveRuntimeFeatures(options, runtimeOptions.background);
      #if ORO_RUNTIME_PLATFORM_ANDROID
        // The Activity configures storage paths and the JVM before services start.
        runtimeOptions.autoStart = false;
      #endif
        return runtimeOptions;
      }()) {
    if (sharedApplicationInstance == nullptr) {
      sharedApplicationInstance = SharedPointer<App>(this, [](App*) {});
    }

  #if ORO_RUNTIME_PLATFORM_WINDOWS
    this->instance = options.instanceId;
    registerWindowClass(this);
  #endif

  #if ORO_RUNTIME_PLATFORM_ANDROID
    this->runtime.android.jvm = options.env;
    this->runtime.android.self = options.self;
  #endif

  #if !ORO_RUNTIME_DESKTOP_EXTENSION
    const auto cwd = getcwd();
    uv_chdir(cwd.c_str());
  #endif

    this->init();
  }

  App::~App () {
    this->shouldExit.store(true, std::memory_order_release);

  #if ORO_RUNTIME_PLATFORM_APPLE
    if (this->delegate != nullptr) {
      [NSNotificationCenter.defaultCenter removeObserver: this->delegate];
    #if ORO_RUNTIME_PLATFORM_MACOS
      if (NSApplication.sharedApplication.delegate == this->delegate) {
        NSApplication.sharedApplication.delegate = nil;
      }
    #endif
      this->delegate.app = nullptr;
      this->delegate = nullptr;
    }
  #endif

    if (sharedApplicationInstance.get() == this) {
      sharedApplicationInstance.reset();
    }
  }

  void App::init () {
  #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_DESKTOP_EXTENSION
    // Initialize GTK on Linux desktop targets (no-op for extension builds)
    gtk_init_check(0, nullptr);
  #elif ORO_RUNTIME_PLATFORM_MACOS
    this->delegate = [OROApplicationDelegate new];
    this->delegate.app = App::sharedApplication();
    NSApplication.sharedApplication.delegate = this->delegate;
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    OleInitialize(nullptr);
  #endif
  }

  int App::run (int argc, char** argv) {
    this->isStarted = true;
  #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_DESKTOP_EXTENSION
    gtk_main();
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    // MUST be acquired on "main" thread
    // `run()` should called when the main activity is created
    if (!this->runtime.android.looper.acquired()) {
      this->runtime.android.looper.acquire();
    }
  #elif ORO_RUNTIME_PLATFORM_MACOS
    [NSApp run];
  #elif ORO_RUNTIME_PLATFORM_IOS
    @autoreleasepool {
      return UIApplicationMain(
        argc,
        argv,
        nullptr,
        NSStringFromClass(OROApplicationDelegate.class)
      );
    }
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    MSG msg;
    if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
      debug("App::run(): Windows pump starting; calling dispatcher.notifyReady()");
    }
    // Mark dispatcher ready and ensure a message queue exists before loop
    this->runtime.dispatcher.notifyReady();
    // Standard message pump: keep running until WM_QUIT
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
      if (msg.hwnd) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }

      if (msg.message == WM_APP) {
        // from PostThreadMessage
        auto callback = (Function<void()> *)(msg.lParam);
        if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
          debug("App::run(): WM_APP received; executing callback");
        }
        (*callback)();
        delete callback;
      }

      if (msg.message == WM_QUIT && this->shouldExit) {
        break;
      }
    }
    if (msg.message == WM_QUIT) {
      return 1;
    }
  #endif

    return this->shouldExit ? 1 : 0;
  }

  static inline uint64_t now_ms () {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  void App::resume () {
    if (this->shouldExit.load(std::memory_order_acquire) || this->stopped()) {
      return;
    }

  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const bool alwaysRunning = (
      this->runtime.userConfig.contains("lifecycle_desktop_always_running")
        ? this->runtime.userConfig.at("lifecycle_desktop_always_running") != "false"
        : true
    );
    if (alwaysRunning) {
      // Desktop: propagate event only; do not resume runtime
      this->runtime.windowManager.emit("applicationresume");
      return;
    }
  #endif
    // Mobile lifecycle callbacks can arrive in a burst. Desktop's
    // always-running mode must deliver every explicit window action.
    const auto tnow = now_ms();
    if (tnow - this->lastResumeEmitMs.load(std::memory_order_relaxed) < 100) {
      return;
    }
    this->lastResumeEmitMs.store(tnow, std::memory_order_relaxed);
  #if !ORO_RUNTIME_PLATFORM_DESKTOP
    if (this->paused()) {
      this->isPaused = false;
      this->runtime.windowManager.emit("applicationresume");

      this->dispatch([this]() {
        if (this->shouldExit.load(std::memory_order_acquire) || this->stopped()) {
          return;
        }

        this->runtime.resume();
      });
    }
  #else
    // Desktop but not always-running: perform real resume semantics
    if (this->paused()) {
      this->isPaused = false;
      this->runtime.windowManager.emit("applicationresume");
      this->dispatch([this]() {
        if (this->shouldExit.load(std::memory_order_acquire) || this->stopped()) {
          return;
        }

        this->runtime.resume();
      });
    }
  #endif
  }

  void App::pause () {
    if (this->shouldExit.load(std::memory_order_acquire) || this->stopped()) {
      return;
    }

  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const bool alwaysRunning = (
      this->runtime.userConfig.contains("lifecycle_desktop_always_running")
        ? this->runtime.userConfig.at("lifecycle_desktop_always_running") != "false"
        : true
    );
    if (alwaysRunning) {
      // Desktop: propagate event only; do not pause runtime or mutate state
      #if defined(DEBUG)
        if (oro::runtime::env::has("ORO_DEBUG_LIFECYCLE") ||
            (this->runtime.userConfig.contains("debug_lifecycle") && this->runtime.userConfig.at("debug_lifecycle") == "true")) {
          debug("App::pause(): Desktop emit only (always running)");
        }
      #endif
      this->runtime.windowManager.emit("applicationpause");
      return;
    }
  #endif
    // Mobile lifecycle callbacks can arrive in a burst. Desktop's
    // always-running mode must deliver every explicit window action.
    const auto tnow = now_ms();
    if (tnow - this->lastPauseEmitMs.load(std::memory_order_relaxed) < 100) {
      return;
    }
    this->lastPauseEmitMs.store(tnow, std::memory_order_relaxed);
  #if !ORO_RUNTIME_PLATFORM_DESKTOP
    if (!this->paused()) {
      this->isPaused = true;
      this->runtime.windowManager.emit("applicationpause");

      this->dispatch([this]() {
        if (this->shouldExit.load(std::memory_order_acquire) || this->stopped()) {
          return;
        }

        this->runtime.pause();
      });
    }
  #else
    // Desktop but not always-running: perform real pause semantics
    if (!this->paused()) {
      this->isPaused = true;
      this->runtime.windowManager.emit("applicationpause");
      this->dispatch([this]() {
        if (this->shouldExit.load(std::memory_order_acquire) || this->stopped()) {
          return;
        }

        this->runtime.pause();
      });
    }
  #endif
  }

  void App::stop () {
    bool expected = false;
    if (!this->isStopped.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return;
    }

    const bool wasExiting = this->shouldExit.exchange(true, std::memory_order_acq_rel);
    this->isStarted = false;

    // Debounce duplicate stop emits
    {
      using namespace std::chrono;
      const auto now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
      if (now - this->lastStopEmitMs.load(std::memory_order_relaxed) >= 100) {
        this->lastStopEmitMs.store(now, std::memory_order_relaxed);
        this->runtime.windowManager.emit("applicationstop");
      }
    }
    msleep(256);

    this->isPaused = false;

    this->runtime.destroy();

  #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_DESKTOP_EXTENSION
    gtk_main_quit();
  #elif ORO_RUNTIME_PLATFORM_MACOS
    // if not launched from the cli, just use `terminate()`
    // exit code status will not be captured
    if (this->launchSource == App::LaunchSource::Platform && !wasExiting) {
      [NSApp terminate: nil];
    }
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    PostQuitMessage(0);
  #endif
  }

  bool App::paused () const {
    return this->isPaused.load(std::memory_order_relaxed);
  }

  bool App::started () const {
    return this->isStarted.load(std::memory_order_relaxed);
  }

  bool App::stopped () const {
    return this->isStopped.load(std::memory_order_relaxed);
  }

  bool App::hasRuntimePermission (const String& permission) const {
    return this->runtime.hasPermission(permission);
  }

  void App::dispatch (Function<void()> callback) {
    if (callback == nullptr || this->shouldExit.load(std::memory_order_acquire) || this->stopped()) {
      return;
    }

    this->runtime.dispatch(callback);
  }
}
