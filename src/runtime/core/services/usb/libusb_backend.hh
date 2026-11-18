#ifndef ORO_RUNTIME_CORE_SERVICES_USB_LIBUSB_BACKEND_H
#define ORO_RUNTIME_CORE_SERVICES_USB_LIBUSB_BACKEND_H

#include "../usb.hh"

namespace oro::runtime::core::services::usb {
  std::shared_ptr<USB::Backend> makeUSBBackend(USB& service);
}

#endif
