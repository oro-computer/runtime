#include "runtime.hh"
#include "env.hh"
#include "bridge.hh"
#include "config.hh"
#include "string.hh"
#include "debug.hh"
#include <thread>

using namespace oro::runtime::core;
using namespace oro::runtime::config;
using oro::runtime::string::replace;

namespace oro::runtime {
  Runtime::Runtime (const Options& options)
    : windowManager(*this),
      bridgeManager(*this),
      serviceWorkerManager(*this, { .windowManager = this->windowManager }),
      dispatcher(*this),
      userConfig(options.userConfig),
      background(options.background),
      services(*this, { this->dispatcher, options.features, this->background }),
      options(options) {
    this->init();
  }

  Runtime::~Runtime() {
    this->destroy();
#if ORO_RUNTIME_PLATFORM_LINUX
    if (this->pauseThread.joinable()) {
      this->pauseThread.join();
    }
#endif
  }

  void Runtime::init () {
    this->start();
  }

  bool Runtime::start () {
    if (!this->loop.start()) return false;
    if (!this->resume()) return false;
    return true;
  }

  bool Runtime::stop () {
    #if defined(DEBUG)
      debug("Runtime::stop(): begin alive=%d paused=%d stopped=%d", this->loop.alive(), this->loop.paused(), this->loop.stopped());
    #endif

    if (!this->pause() || !this->loop.stop()) {
      #if defined(DEBUG)
        debug("Runtime::stop(): failed alive=%d paused=%d stopped=%d", this->loop.alive(), this->loop.paused(), this->loop.stopped());
      #endif
      return false;
    }

    #if defined(DEBUG)
      debug("Runtime::stop(): end alive=%d paused=%d stopped=%d", this->loop.alive(), this->loop.paused(), this->loop.stopped());
    #endif
    return true;
  }

  bool Runtime::resume () {
#if ORO_RUNTIME_PLATFORM_LINUX
    if (this->pauseThread.joinable()) {
      this->pauseThread.join();
    }
#endif
    if (!this->loop.resume()) {
      return false;
    }

    if (!this->services.start()) {
      return false;
    }

    #if defined(DEBUG)
      debug("Runtime::resume(): resumed alive=%d paused=%d stopped=%d", this->loop.alive(), this->loop.paused(), this->loop.stopped());
    #endif
    return true;
  }

  bool Runtime::pause () {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const bool alwaysRunning = (
      this->userConfig.contains("lifecycle_desktop_always_running")
        ? this->userConfig.at("lifecycle_desktop_always_running") != "false"
        : true
    );
    if (alwaysRunning) {
      // Always-running policy on desktop: do not pause services or loop.
      // This avoids any risk of UI deadlocks from lifecycle transitions.
      #if defined(DEBUG)
        if (oro::runtime::env::has("ORO_DEBUG_LIFECYCLE") ||
            (this->userConfig.contains("debug_lifecycle") && this->userConfig.at("debug_lifecycle") == "true")) {
          debug("Runtime::pause(): Desktop no-op (always running)");
        }
      #endif
      return true;
    }
  #endif
  #if ORO_RUNTIME_PLATFORM_LINUX
    // On Linux desktop when pausing is enabled, offload stop to avoid GTK deadlocks.
    if (this->pauseThread.joinable()) {
      this->pauseThread.join();
    }
    this->pauseThread = std::thread([this]() {
      const bool ok = this->services.stop();
      (void)ok;
      this->dispatcher.dispatch([this]() {
      #if !ORO_RUNTIME_PLATFORM_ANDROID
        const bool paused = this->loop.pause();
        #if defined(DEBUG)
          if (oro::runtime::env::has("ORO_DEBUG_LIFECYCLE") ||
              (this->userConfig.contains("debug_lifecycle") && this->userConfig.at("debug_lifecycle") == "true")) {
            debug("Runtime::pause(): async services stopped; loop pause=%d alive=%d paused=%d", paused, this->loop.alive(), this->loop.paused());
          }
        #endif
      #endif
      });
    });
    return true;
  #else
    if (!this->services.stop()) {
      return false;
    }

    #if !ORO_RUNTIME_PLATFORM_ANDROID
      const bool ok = this->loop.pause();
      #if defined(DEBUG)
        debug("Runtime::pause(): services stopped; loop pause=%d alive=%d paused=%d", ok, this->loop.alive(), this->loop.paused());
      #endif
      return ok;
    #endif

    return true;
  #endif
  }

  bool Runtime::destroy () {
#if defined(DEBUG)
    if (this->destroyed.load(std::memory_order_relaxed)) {
      debug("Runtime::destroy(): already destroyed");
    }
#endif
    bool expected = false;
    if (!this->destroyed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return true;
    }
    #if defined(DEBUG)
      debug("Runtime::destroy(): begin alive=%d paused=%d stopped=%d", this->loop.alive(), this->loop.paused(), this->loop.stopped());
    #endif

    const bool stopped = this->stop();
#if ORO_RUNTIME_PLATFORM_LINUX
    if (this->pauseThread.joinable()) {
      this->pauseThread.join();
    }
#endif
    if (stopped && this->loop.shutdown()) {
      #if defined(DEBUG)
        debug("Runtime::destroy(): end alive=%d paused=%d stopped=%d", this->loop.alive(), this->loop.paused(), this->loop.stopped());
      #endif
      return true;
    }

    #if defined(DEBUG)
      debug("Runtime::destroy(): failed alive=%d paused=%d stopped=%d", this->loop.alive(), this->loop.paused(), this->loop.stopped());
    #endif
    return false;
  }

  bool Runtime::dispatch (const DispatchCallback& callback) {
    return this->dispatcher.dispatch(callback);
  }

  bool Runtime::stopped () const {
    return this->loop.stopped();
  }

  bool Runtime::paused () const {
    return this->loop.paused();
  }

  bool Runtime::hasPermission (const String& permission) const {
    const auto key = String("permissions_allow_") + replace(permission, "-", "_");

    if (!this->userConfig.contains(key)) {
      return true;
    }

    return this->userConfig.at(key) != "false";
  }
}
