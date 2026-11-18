#ifndef ORO_RUNTIME_IPC_USB_ROUTES_H
#define ORO_RUNTIME_IPC_USB_ROUTES_H

#include "../ipc.hh"
#include "../core/services/usb.hh"
#include <charconv>

namespace oro::runtime::ipc {
  inline bool readUSBUnsigned (const Message& message, const char* key, uint32_t maximum, uint32_t& value) {
    const auto text = message.get(key);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size() && value <= maximum;
  }

  inline Result::Err invalidUSBArgument (const Message& message, const char* reason) {
    return Result::Err{message, JSON::Object::Entries{{"name", "TypeError"}, {"type", "TypeError"}, {"message", reason}}};
  }

  template<class Service>
  void dispatchUSBControlTransfer (
    Service& service, const core::services::USB::Request& caller,
    const Message& message, bool input, const Router::ReplyCallback& reply
  ) {
    const auto fail = [&](const char* reason) {
      reply(invalidUSBArgument(message, reason));
    };
    const auto id = message.get("deviceId");
    if (id.empty()) return fail("USB device ID is required");
    JSON::Any setup;
    try {
      setup = JSON::parse(message.get("setup"));
    } catch (...) {
      return fail("Invalid USB control setup");
    }
    if (setup.type != JSON::Type::Object) return fail("USB control setup must be an object");
    const auto finish = [reply, message](auto seq, auto json, auto queued) {
      reply(Result{seq, message, json, queued});
    };
    if (input) {
      uint32_t length = 0;
      if (!readUSBUnsigned(message, "length", 65535, length)) return fail("Invalid USB control length");
      service.controlTransferIn(caller, id, setup, length, finish);
    } else {
      if (message.buffer.size() > 65535) return fail("USB control payload exceeds 65535 bytes");
      service.controlTransferOut(caller, id, setup, message.buffer, finish);
    }
  }
}
#endif
