#ifndef ORO_RUNTIME_CORE_SERVICES_HID_LIBUSB_BACKEND_H
#define ORO_RUNTIME_CORE_SERVICES_HID_LIBUSB_BACKEND_H

#include "../hid.hh"

namespace oro::runtime::core::services::hid {
  std::unique_ptr<HID::Backend> makeHIDBackend(HID& service);
}

#endif
