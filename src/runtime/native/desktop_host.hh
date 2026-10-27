#ifndef ORO_RUNTIME_NATIVE_DESKTOP_HOST_H
#define ORO_RUNTIME_NATIVE_DESKTOP_HOST_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "../runtime.hh"
#include "linux_platform.hh"
#include <glib-unix.h>
#include <csignal>

namespace oro::runtime::native {
  /** Trusted compiled application entry. No window, renderer, project script or arbitrary command is launched. */
  inline int runDesktopNativeHost (Runtime::UserConfig config, void (*initialize)(Runtime*)) {
    if (config["linux_native_service_enabled"] != "true" || !initialize)
      throw StateError("ENOSYS");
    config["linux_native_service_host"] = "true";
    Runtime::Options options;
    options.userConfig = std::move(config);
    options.autoStart = false;
    options.features.useAI = false;
    options.features.useUSB = false;
    options.features.useBluetooth = false;
    options.features.useHID = false;
    options.features.useHCI = false;
    options.features.useIroh = false;
    options.features.useIPFS = false;
    options.features.useGeolocation = false;
    options.features.useMediaDevices = false;
    options.features.useMCP = false;
    Runtime runtime(options);
    if (!runtime.services.state.init() || !runtime.services.secureStorage.start())
      throw StateError("ENOTREADY");
    const auto backend = runtime.services.platform.nativeLinux();
    initialize(&runtime);
    backend->initializeNativeServices();
    const auto loop = g_main_loop_new(nullptr, false);
    bool busLost = false;
    backend->serveNativeHost([&] {
      busLost = true;
      g_main_loop_quit(loop);
    });
    const auto stop = +[] (gpointer pointer) -> gboolean {
      g_main_loop_quit(static_cast<GMainLoop*>(pointer));
      return G_SOURCE_CONTINUE;
    };
    const auto interrupt = g_unix_signal_add(SIGINT, stop, loop);
    const auto terminate = g_unix_signal_add(SIGTERM, stop, loop);
    g_main_loop_run(loop);
    backend->shutdown();
    g_source_remove(interrupt);
    g_source_remove(terminate);
    g_main_loop_unref(loop);
    return busLost ? 1 : 0;
  }
}  // namespace oro::runtime::native
#endif
#endif
