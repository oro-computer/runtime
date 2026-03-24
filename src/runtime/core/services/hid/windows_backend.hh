#ifndef ORO_RUNTIME_CORE_SERVICES_HID_WINDOWS_BACKEND_H
#define ORO_RUNTIME_CORE_SERVICES_HID_WINDOWS_BACKEND_H

#if defined(_WIN32)

#include "../hid.hh"

namespace oro::runtime::core::services::hid {
  std::unique_ptr<HID::Backend> makeWindowsHIDBackend(HID& service);
}

#endif

#endif
