#ifndef ORO_RUNTIME_RUNTIME_H
#define ORO_RUNTIME_RUNTIME_H

#include "core.hh"
#include "config.hh"
#include "bridge.hh"
#include "window.hh"
#include "options.hh"
#include "context.hh"
#include "serviceworker.hh"
#include "background/options.hh"

#include "core/services.hh"

#include <thread>

namespace oro::runtime {
  class Runtime : public context::RuntimeContext {
    public:
      using DispatchCallback = Function<void()>;
      using UserConfig = Map<String, String>;
      using Features = core::Services::Features;
      using BackgroundOptions = background::Options;

      struct Options : public runtime::Options {
        UserConfig userConfig = config::getUserConfig();
        loop::Loop::Options loop;
        Features features;
        BackgroundOptions background;
        bool autoStart = true;
      };

      struct Counters {
        int logSeq = 0;
      };

      // managers
      window::Manager windowManager;
      bridge::Manager bridgeManager;
      serviceworker::Manager serviceWorkerManager;

      context::Dispatcher dispatcher;
      UserConfig userConfig;
      BackgroundOptions background;
      core::Services services;
      Counters counters;
      Options options;
#if ORO_RUNTIME_PLATFORM_LINUX
      std::thread pauseThread;
#endif
      Atomic<bool> destroyed = false;

      Runtime (const Options&);
      Runtime () = delete;
      Runtime (const Runtime&) = delete;
      Runtime (Runtime&&) = delete;
      virtual ~Runtime();

      Runtime& operator = (const Runtime&) = delete;
      Runtime& operator = (Runtime&&) = delete;

      void init ();
      bool start ();
      bool stop ();
      bool resume ();
      bool pause();
      bool destroy ();
      bool dispatch (const DispatchCallback&);

      bool stopped () const;
      bool paused () const;

      bool hasPermission (const String& permission) const;
  };
}
#endif
