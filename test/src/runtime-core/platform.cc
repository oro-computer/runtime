#include "src/runtime/runtime.hh"
#include <array>
#include "tests.hh"
#include "src/runtime/platform.hh"

namespace oro {
  using runtime::platform;
}

namespace oro::Tests {
  #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_PLATFORM_ANDROID
  std::array<size_t, 7> linuxExtensionLayout ();
  #endif

  void platform (Harness& t) {
  #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_PLATFORM_ANDROID
    t.test("Linux core and WebProcess extension layouts agree", [](auto t) {
      const auto extension = linuxExtensionLayout();
      const std::array<size_t, 7> core = {
        sizeof(runtime::webview::IBridge),
        sizeof(runtime::window::IBridge),
        sizeof(runtime::bridge::Bridge),
        sizeof(runtime::context::RuntimeContext),
        sizeof(runtime::Runtime),
        sizeof(runtime::window::Window),
        sizeof(runtime::loop::Loop)
      };
      for (size_t index = 0; index < core.size(); ++index) {
        t.equals(core[index], extension[index], "shared class retains its core layout");
      }
    });
  #endif

    t.test("runtime loop policy reaches the base context", [](auto t) {
      for (const auto dedicated : {false, true}) {
        runtime::Runtime::Options options;
        options.autoStart = false;
        options.loop.dedicatedThread = dedicated;
        runtime::Runtime instance(options);
        t.equals(instance.loop.options.dedicatedThread, dedicated, "preserves the configured loop policy");
      }
    });

    t.test("oro::platform.arch", [](auto t) {
      t.assert(oro::platform.arch, "oro::platform.arch is not empty");
    #if defined(__x86_64__) || defined(_M_X64)
      t.equals(oro::platform.arch, "x86_64", "oro::platform.arch == \"x86_64\"");
    #elif defined(__aarch64__) || defined(_M_ARM64)
      t.equals(oro::platform.arch , "arm64", "oro::platform.arch == \"arm64\"");
    #else
      t.equals(oro::platform.arch , "unknown", "oro::platform.arch == \"unknown\"");
    #endif
    });

    t.test("oro::platform.os", [](auto t) {
      t.assert(oro::platform.os, "oro::platform.osis not empty");
    #if defined(_WIN32)
      t.equals(oro::platform.os, "win32", "oro::platform.os == \"win32\"");
    #elif defined(__APPLE__)
      #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
      t.equals(oro::platform.os, "ios", "oro::platform.os == \"ios\"");
      #else
      t.equals(oro::platform.os, "mac", "oro::platform.os == \"mac\"");
      #endif
    #elif defined(__ANDROID__)
      t.equals(oro::platform.os, "android", "oro::platform.os == \"android\"");
    #elif defined(__linux__)
      t.equals(oro::platform.os, "linux", "oro::platform.os == \"linux\"");
    #elif defined(__FreeBSD__)
      t.equals(oro::platform.os, "freebsd", "oro::platform.os == \"freebsd\"");
    #elif defined(BSD)
      t.equals(oro::platform.os, "openbsd", "oro::platform.os == \"openbsd\"");
    #endif
    });

    t.test("oro::platform.{mac,ios,win,linux,unix}", [](auto t) {
    #if defined(_WIN32)
      t.equals(oro::platform.mac, false, "oro::platform.mac = false");
      t.equals(oro::platform.ios, false, "oro::platform.ios = false");
      t.equals(oro::platform.win, true, "oro::platform.win = true");
      t.equals(oro::platform.linux, false, "oro::platform.linux = false");
      t.equals(oro::platform.android, false, "oro::platform.android = false");
    #elif defined(__APPLE__)
      #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
      t.equals(oro::platform.mac, false, "oro::platform.mac = false");
      t.equals(oro::platform.ios, true, "oro::platform.ios = true");
      t.equals(oro::platform.win, false, "oro::platform.win = false");
      t.equals(oro::platform.linux, false, "oro::platform.linux = false");
      t.equals(oro::platform.android, false, "oro::platform.android = false");
      #else
      t.equals(oro::platform.mac, true, "oro::platform.mac = true");
      t.equals(oro::platform.ios, false, "oro::platform.ios = false");
      t.equals(oro::platform.win, false, "oro::platform.win = false");
      t.equals(oro::platform.linux, false, "oro::platform.linux = false");
      t.equals(oro::platform.android, false, "oro::platform.android = false");
      #endif
    #elif defined(__ANDROID__)
      t.equals(oro::platform.mac, false, "oro::platform.mac = false");
      t.equals(oro::platform.ios, false, "oro::platform.ios = false");
      t.equals(oro::platform.win, false, "oro::platform.win = false");
      t.equals(oro::platform.linux, true, "oro::platform.linux = true");
      t.equals(oro::platform.android, true, "oro::platform.android = true");
    #elif defined(__linux__)
      t.equals(oro::platform.mac, false, "oro::platform.mac = false");
      t.equals(oro::platform.ios, false, "oro::platform.ios = false");
      t.equals(oro::platform.win, false, "oro::platform.win = false");
      t.equals(oro::platform.linux, true, "oro::platform.linux = true");
      t.equals(oro::platform.android, false, "oro::platform.android = false");
    #elif defined(__FreeBSD__)
      t.equals(oro::platform.mac, false, "oro::platform.mac = false");
      t.equals(oro::platform.ios, false, "oro::platform.ios = false");
      t.equals(oro::platform.win, false, "oro::platform.win = false");
      t.equals(oro::platform.linux, false, "oro::platform.linux = false");
      t.equals(oro::platform.android, false, "oro::platform.android = false");
    #elif defined(BSD)
      t.equals(oro::platform.mac, false, "oro::platform.mac = false");
      t.equals(oro::platform.ios, false, "oro::platform.ios = false");
      t.equals(oro::platform.win, false, "oro::platform.win = false");
      t.equals(oro::platform.linux, false, "oro::platform.linux = false");
      t.equals(oro::platform.android, false, "oro::platform.android = false");
    #endif

    #if defined(__unix__) || defined(unix) || defined(__unix)
      t.equals(oro::platform.unix, true, "oro::platform.unix = true");
    #else
      t.equals(oro::platform.unix, false, "oro::platform.unix = false");
    #endif
    });
  }
}
