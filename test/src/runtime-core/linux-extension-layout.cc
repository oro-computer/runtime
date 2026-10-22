#if defined(__linux__) && !defined(__ANDROID__)
#define ORO_RUNTIME_DESKTOP_EXTENSION 1
#include "src/runtime/runtime.hh"
#include <array>

namespace oro::Tests {
  std::array<size_t, 7> linuxExtensionLayout () {
    return {
      sizeof(runtime::webview::IBridge),
      sizeof(runtime::window::IBridge),
      sizeof(runtime::bridge::Bridge),
      sizeof(runtime::context::RuntimeContext),
      sizeof(runtime::Runtime),
      sizeof(runtime::window::Window),
      sizeof(runtime::loop::Loop)
    };
  }
}
#endif
