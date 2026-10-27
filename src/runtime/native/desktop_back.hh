#ifndef ORO_RUNTIME_NATIVE_DESKTOP_BACK_H
#define ORO_RUNTIME_NATIVE_DESKTOP_BACK_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "../json.hh"
namespace oro::runtime::window {
  class Window;
}
namespace oro::runtime::native {
  class DesktopBack {
  public:
    static void attach (window::Window&);
    static void reset (window::Window&);
    static void cancelPending (window::Window&);
    static JSON::Any request (window::Window&, const std::string& action, const JSON::Object&);
  };
}  // namespace oro::runtime::native
#endif
#endif
