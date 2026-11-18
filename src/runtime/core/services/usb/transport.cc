#include "transport.hh"

#if !ORO_RUNTIME_PLATFORM_IOS
#include "../../../app.hh"
#include <algorithm>
#include <cmath>
#include <limits>
#if ORO_RUNTIME_PLATFORM_ANDROID
#include <unistd.h>
#endif

namespace oro::runtime::core::services::usb {
  namespace {
    constexpr uint32_t maxTransfer = 16 * 1024 * 1024;
    constexpr unsigned timeout = 5000;

    JSON::Object error (const char* type, const String& message) {
      return JSON::Object::Entries{{"err", JSON::Object::Entries{{"type", type}, {"name", type}, {"message", message}}}};
    }

    void LIBUSB_CALL completeIsochronous (libusb_transfer* transfer) {
      *static_cast<int*>(transfer->user_data) = 1;
    }

    JSON::Object success (const JSON::Object& data = {}) {
      return JSON::Object::Entries{{"data", data}};
    }

    JSON::Object usbResult (int code) {
      if (code == LIBUSB_SUCCESS) return success();
      return error(code == LIBUSB_ERROR_NO_DEVICE ? "NotFoundError" : "NetworkError", libusb_error_name(code));
    }

    using Configuration = std::unique_ptr<libusb_config_descriptor, decltype(&libusb_free_config_descriptor)>;

    Configuration activeConfiguration (libusb_device_handle* handle) {
      libusb_config_descriptor* descriptor = nullptr;
      libusb_get_active_config_descriptor(libusb_get_device(handle), &descriptor);
      return Configuration(descriptor, libusb_free_config_descriptor);
    }

    const libusb_interface* findInterface (const Configuration& config, uint8_t number) {
      if (config) {
        for (unsigned i = 0; i < config->bNumInterfaces; ++i) {
          const auto& iface = config->interface[i];
          if (iface.num_altsetting && iface.altsetting[0].bInterfaceNumber == number) return &iface;
        }
      }
      return nullptr;
    }

    bool protectedInterface (const libusb_interface* iface) {
      return iface && std::any_of(iface->altsetting, iface->altsetting + iface->num_altsetting,
        [](const auto& alt) { return isProtectedUSBClass(alt.bInterfaceClass); });
    }

    JSON::Object transferResult (bool input, int rc, int count, Vector<uint8_t>& bytes) {
      if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_PIPE && !(input && rc == LIBUSB_ERROR_OVERFLOW)) return usbResult(rc);
      const String status = rc == LIBUSB_ERROR_PIPE ? "stall" : rc == LIBUSB_ERROR_OVERFLOW ? "babble" : "ok";
      JSON::Object result = JSON::Object::Entries{{"status", status}, {"transferred", count}};
      if (input && rc == LIBUSB_SUCCESS) {
        bytes.resize(count);
        result.set("data", bytes::base64::encode(bytes));
      }
      return success(result);
    }

    bool integerField (const JSON::Object& object, const char* name, unsigned max, unsigned& output) {
      const auto& field = object.get(name);
      if (field.type != JSON::Type::Number) return false;
      const auto n = field.as<JSON::Number>().value();
      if (!(n >= 0 && n <= max && n == std::floor(n))) return false;
      output = static_cast<unsigned>(n);
      return true;
    }
  }

  TransportBackend::Session::~Session () {
    if (handle) {
      for (const auto& claim : claimed) libusb_release_interface(handle, claim.first);
      libusb_close(handle);
    }
#if ORO_RUNTIME_PLATFORM_ANDROID
    if (fd >= 0) ::close(fd);
#endif
  }

  TransportBackend::~TransportBackend () {
    sessions.clear();
    if (ctx) libusb_exit(ctx);
  }

  int TransportBackend::Session::endpointType (uint8_t address) const {
    const auto config = activeConfiguration(handle);
    for (const auto& [number, setting] : claimed) {
      const auto iface = findInterface(config, number);
      if (!iface) continue;
      for (int i = 0; i < iface->num_altsetting; ++i) {
        const auto& alt = iface->altsetting[i];
        if (alt.bAlternateSetting != setting) continue;
        for (int j = 0; j < alt.bNumEndpoints; ++j) {
          const auto& ep = alt.endpoint[j];
          if (ep.bEndpointAddress == address) return ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
        }
      }
    }
    return -1;
  }

  JSON::Object TransportBackend::describeDevice (const DeviceDescriptor& descriptor, const Request& request) const {
    auto json = descriptorToJson(descriptor);
    const auto it = sessions.find(descriptor.deviceId);
    const bool owned = it != sessions.end() && it->second->owner == request.client;
    json.set("opened", owned);
    if (it != sessions.end()) json.set("configurationValue", it->second->configuration);
    JSON::Array claims;
    if (owned) {
      for (const auto& [number, setting] : it->second->claimed) {
        claims.push(JSON::Object::Entries{{"interfaceNumber", number}, {"alternateSetting", setting}});
      }
    }
    json.set("claimedInterfaces", claims);
    return json;
  }

  void TransportBackend::emitChanges (Vector<DeviceDescriptor> added, Vector<DeviceDescriptor> removed) {
    if (added.empty() && removed.empty()) return;
    service.dispatcher.dispatch([added = std::move(added), removed = std::move(removed)] {
      auto app = app::App::sharedApplication();
      if (!app || !app->runtime.hasPermission("usb")) return;
      for (const auto& window : app->runtime.windowManager.windows) {
        if (!window || !window->bridge || !window->bridge->usbDocument.load()) continue;
        const auto emit = [&](const char* name, const auto& devices) {
          for (const auto& device : devices) {
            if (!device.authorized) continue;
            JSON::Object event = JSON::Object::Entries{{"device", descriptorToJson(device)}};
            window->bridge->emit(name, event.str());
          }
        };
        emit("usb.devicedisconnect", removed);
        emit("usb.deviceconnect", added);
      }
    });
  }

  void TransportBackend::disconnectClient (uint64_t client) {
    enqueue([this, client] {
      std::erase_if(sessions, [client](const auto& item) { return item.second->owner == client; });
    });
  }

  void TransportBackend::open (const Request& request, const String& id, const Callback cb) {
    enqueue([this, request, id, cb] {
      if (!isDeviceAuthorized(id)) return cb(request, error("SecurityError", "USB access has been revoked"), {});
      auto it = sessions.find(id);
      if (it == sessions.end()) {
        auto session = std::make_unique<Session>();
        const int rc = openDevice(id, *session);
        if (rc != LIBUSB_SUCCESS) return cb(request, usbResult(rc), {});
        const int state = libusb_get_configuration(session->handle, &session->configuration);
        if (state < 0) return cb(request, usbResult(state), {});
#if ORO_RUNTIME_PLATFORM_LINUX || ORO_RUNTIME_PLATFORM_ANDROID
        libusb_set_auto_detach_kernel_driver(session->handle, 1);
#endif
        session->owner = request.client;
        it = sessions.emplace(id, std::move(session)).first;
      }
      if (it->second->owner != request.client) return cb(request, error("InvalidStateError", "Device is open in another window"), {});
      libusb_device_descriptor descriptor{};
      const int rc = libusb_get_device_descriptor(libusb_get_device(it->second->handle), &descriptor);
      cb(request, success(JSON::Object::Entries{
        {"configurationValue", it->second->configuration}, {"usbVersion", rc == 0 ? descriptor.bcdUSB : 0}
      }), {});
    });
  }

  void TransportBackend::close (const Request& request, const String& id, const Callback cb) {
    enqueue([this, request, id, cb] {
      const auto it = sessions.find(id);
      if (it != sessions.end() && it->second->owner == request.client) sessions.erase(it);
      cb(request, success(), {});
    });
  }

  void TransportBackend::operate (const Request& request, const String& id, const Callback& cb, Operation operation) {
    enqueue([this, request, id, cb, operation = std::move(operation)] {
      if (!isDeviceAuthorized(id)) return cb(request, error("SecurityError", "Device is no longer authorized"), {});
      const auto it = sessions.find(id);
      if (it == sessions.end() || it->second->owner != request.client) return cb(request, error("InvalidStateError", "Open the device first"), {});
      JSON::Object result;
      try {
        result = operation(*it->second);
      } catch (const std::exception& ex) {
        result = error("OperationError", ex.what());
      }
      cb(request, result, {});
    });
  }

  void TransportBackend::selectConfiguration (const Request& request, const String& id, uint8_t value, const Callback cb) {
    operate(request, id, cb, [value](Session& s) {
      if (value && s.configuration == value) return success();
      libusb_config_descriptor* raw = nullptr;
      if (!value || libusb_get_config_descriptor_by_value(libusb_get_device(s.handle), value, &raw) < 0) return error("NotFoundError", "Unknown USB configuration");
      libusb_free_config_descriptor(raw);
      JSON::Array released;
      int rc = LIBUSB_SUCCESS;
      for (auto it = s.claimed.begin(); it != s.claimed.end();) {
        rc = libusb_release_interface(s.handle, it->first);
        if (rc < 0) break;
        released.push(it->first);
        it = s.claimed.erase(it);
      }
      if (rc == LIBUSB_SUCCESS) rc = libusb_set_configuration(s.handle, value);
      if (rc == LIBUSB_SUCCESS) {
        s.configuration = value;
        return success();
      }
      auto result = usbResult(rc);
      result.get("err").as<JSON::Object>().set("releasedInterfaces", released);
      return result;
    });
  }

  void TransportBackend::claimInterface (const Request& request, const String& id, uint8_t number, const Callback cb) {
    operate(request, id, cb, [number](Session& s) {
      if (!s.configuration) return error("InvalidStateError", "Select a configuration first");
      const auto config = activeConfiguration(s.handle);
      const auto iface = findInterface(config, number);
      if (!iface) return error("NotFoundError", "Unknown USB interface");
      if (protectedInterface(iface)) return error("SecurityError", "Protected USB interface");
      if (s.claimed.contains(number)) return success();
      const int rc = libusb_claim_interface(s.handle, number);
      if (rc == 0) s.claimed[number] = 0;
      return usbResult(rc);
    });
  }

  void TransportBackend::releaseInterface (const Request& request, const String& id, uint8_t number, const Callback cb) {
    operate(request, id, cb, [number](Session& s) {
      if (!s.configuration) return error("InvalidStateError", "Select a configuration first");
      if (!findInterface(activeConfiguration(s.handle), number)) return error("NotFoundError", "Unknown USB interface");
      if (!s.claimed.contains(number)) return success();
      const int rc = libusb_release_interface(s.handle, number);
      if (rc == 0) s.claimed.erase(number);
      return usbResult(rc);
    });
  }

  void TransportBackend::selectAlternateInterface (const Request& request, const String& id, uint8_t number, uint8_t setting, const Callback cb) {
    operate(request, id, cb, [number, setting](Session& s) {
      if (!s.claimed.contains(number)) return error("InvalidStateError", "Claim the interface first");
      const auto config = activeConfiguration(s.handle);
      const auto iface = findInterface(config, number);
      if (!iface || std::none_of(iface->altsetting, iface->altsetting + iface->num_altsetting,
          [setting](const auto& alt) { return alt.bAlternateSetting == setting; })) return error("NotFoundError", "Unknown alternate setting");
      const int rc = libusb_set_interface_alt_setting(s.handle, number, setting);
      if (rc == 0) s.claimed[number] = setting;
      return usbResult(rc);
    });
  }

  void TransportBackend::reset (const Request& request, const String& id, const Callback cb) {
    operate(request, id, cb, [](Session& s) { return usbResult(libusb_reset_device(s.handle)); });
  }

  void TransportBackend::clearHalt (const Request& request, const String& id, uint8_t number, bool input, const Callback cb) {
    operate(request, id, cb, [number, input](Session& s) {
      const uint8_t address = number | (input ? LIBUSB_ENDPOINT_IN : 0);
      if (s.endpointType(address) < 0) return error("NotFoundError", "Endpoint is not on a claimed interface");
      return usbResult(libusb_clear_halt(s.handle, address));
    });
  }

  void TransportBackend::control (const Request& request, const String& id, bool input, const JSON::Any& setup, uint32_t length, const bytes::Buffer& payload, const Callback& cb) {
    operate(request, id, cb, [=](Session& s) {
      if (setup.type != JSON::Type::Object || length > 65535 || payload.size() > 65535) return error("TypeError", "Invalid control transfer");
      const auto& fields = setup.as<JSON::Object>();
      unsigned type, command, value, index;
      if (!integerField(fields, "bmRequestType", 255, type) || !integerField(fields, "bRequest", 255, command) ||
          !integerField(fields, "wValue", 65535, value) || !integerField(fields, "wIndex", 65535, index) ||
          !!(type & LIBUSB_ENDPOINT_IN) != input || (type & 0x60) == 0x60 || (type & 31) > 3) return error("TypeError", "Invalid control setup fields");
      const auto recipient = type & 31;
      if ((type & 0x60) == LIBUSB_REQUEST_TYPE_STANDARD && (!input || (command != LIBUSB_REQUEST_GET_STATUS &&
            command != LIBUSB_REQUEST_GET_DESCRIPTOR && command != LIBUSB_REQUEST_GET_CONFIGURATION &&
            command != LIBUSB_REQUEST_GET_INTERFACE && command != LIBUSB_REQUEST_SYNCH_FRAME))) {
        return error("SecurityError", "Restricted standard USB request");
      }
      if (!s.configuration && (recipient == LIBUSB_RECIPIENT_INTERFACE || recipient == LIBUSB_RECIPIENT_ENDPOINT)) {
        return error("InvalidStateError", "Select a configuration first");
      }
      if (s.configuration) {
        if ((type & 0x60) == LIBUSB_REQUEST_TYPE_CLASS || recipient == LIBUSB_RECIPIENT_INTERFACE) {
          const auto config = activeConfiguration(s.handle);
          const auto iface = findInterface(config, index & 255);
          if (protectedInterface(iface)) return error("SecurityError", "Protected USB interface");
          if (recipient == LIBUSB_RECIPIENT_INTERFACE) {
            if (!iface) return error("NotFoundError", "Unknown recipient interface");
            if (!s.claimed.contains(index & 255)) return error("InvalidStateError", "Claim the recipient interface first");
          }
        }
        if (recipient == LIBUSB_RECIPIENT_ENDPOINT && s.endpointType(index & 255) < 0) return error("NotFoundError", "Unknown recipient endpoint");
      }
      Vector<uint8_t> bytes(input ? length : 0);
      auto data = input ? bytes.data() : const_cast<unsigned char*>(payload.data());
      const int rc = libusb_control_transfer(s.handle, type, command, value, index, data, input ? length : payload.size(), timeout);
      return transferResult(input, rc < 0 ? rc : 0, std::max(0, rc), bytes);
    });
  }

  void TransportBackend::controlTransferIn (const Request& r, const String& id, const JSON::Any& setup, uint32_t length, const Callback cb) {
    control(r, id, true, setup, length, {}, cb);
  }

  void TransportBackend::controlTransferOut (const Request& r, const String& id, const JSON::Any& setup, const bytes::Buffer& payload, const Callback cb) {
    control(r, id, false, setup, 0, payload, cb);
  }

  void TransportBackend::transfer (const Request& request, const String& id, uint8_t number, bool input, uint32_t length, const bytes::Buffer& payload, const Callback& cb) {
    operate(request, id, cb, [=](Session& s) {
      const uint8_t address = number | (input ? LIBUSB_ENDPOINT_IN : 0);
      const auto type = s.endpointType(address);
      if (type != LIBUSB_TRANSFER_TYPE_BULK && type != LIBUSB_TRANSFER_TYPE_INTERRUPT) return error("NotFoundError", "Unknown bulk or interrupt endpoint");
      if (length > maxTransfer || payload.size() > maxTransfer) return error("DataError", "USB transfer exceeds 16 MiB");
      Vector<uint8_t> bytes(input ? length : 0);
      auto data = input ? bytes.data() : const_cast<unsigned char*>(payload.data());
      int actual = 0;
      auto send = type == LIBUSB_TRANSFER_TYPE_BULK ? libusb_bulk_transfer : libusb_interrupt_transfer;
      const int rc = send(s.handle, address, data, input ? length : payload.size(), &actual, timeout);
      return transferResult(input, rc, actual, bytes);
    });
  }

  void TransportBackend::transferIn (const Request& r, const String& id, uint8_t number, uint32_t length, const Callback cb) {
    transfer(r, id, number, true, length, {}, cb);
  }

  void TransportBackend::transferOut (const Request& r, const String& id, uint8_t number, const bytes::Buffer& data, const Callback cb) {
    transfer(r, id, number, false, 0, data, cb);
  }
  void TransportBackend::isochronous (const Request& request, const String& id, uint8_t number, bool input,
      const Vector<uint32_t>& lengths, const bytes::Buffer& payload, const Callback cb) {
    operate(request, id, cb, [=, this](Session& s) {
      const uint8_t address = number | (input ? LIBUSB_ENDPOINT_IN : 0);
      if (s.endpointType(address) != LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) return error("NotFoundError", "Unknown isochronous endpoint");
      if (lengths.empty() || lengths.size() > 1024) return error("DataError", "Expected 1 to 1024 USB packets");
      uint32_t total = 0;
      for (const auto length : lengths) {
        if (length > maxTransfer - total) return error("DataError", "USB transfer exceeds 16 MiB");
        total += length;
      }
      if (!input && payload.size() != total) return error("DataError", "Packet lengths must match the payload");
      Vector<uint8_t> bytes(total);
      if (!input && total) std::copy_n(payload.data(), total, bytes.data());
      using Transfer = std::unique_ptr<libusb_transfer, decltype(&libusb_free_transfer)>;
      Transfer transfer(libusb_alloc_transfer(static_cast<int>(lengths.size())), libusb_free_transfer);
      if (!transfer) return error("OperationError", "Unable to allocate USB transfer");
      int completed = 0;
      libusb_fill_iso_transfer(transfer.get(), s.handle, address, bytes.data(), total, lengths.size(),
        completeIsochronous, &completed, timeout);
      for (size_t i = 0; i < lengths.size(); ++i) transfer->iso_packet_desc[i].length = lengths[i];
      const int rc = libusb_submit_transfer(transfer.get());
      if (rc < 0) return usbResult(rc);
      while (!completed) {
        const int event = libusb_handle_events_completed(ctx, &completed);
        if (event < 0 && event != LIBUSB_ERROR_INTERRUPTED) libusb_cancel_transfer(transfer.get());
      }
      if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) return usbResult(LIBUSB_ERROR_NO_DEVICE);
      if (transfer->status != LIBUSB_TRANSFER_COMPLETED) return error("NetworkError", "Isochronous transfer interrupted");
      JSON::Array packets;
      for (size_t i = 0; i < lengths.size(); ++i) {
        const auto& packet = transfer->iso_packet_desc[i];
        if (packet.status == LIBUSB_TRANSFER_NO_DEVICE) return usbResult(LIBUSB_ERROR_NO_DEVICE);
        const char* status = "ok";
        if (packet.status == LIBUSB_TRANSFER_STALL) status = "stall";
        else if (input && packet.status == LIBUSB_TRANSFER_OVERFLOW) status = "babble";
        else if (packet.status != LIBUSB_TRANSFER_COMPLETED) return error("NetworkError", "Isochronous packet failed");
        packets.push(JSON::Object::Entries{{"status", status}, {"transferred", std::min(packet.actual_length, packet.length)}});
      }
      JSON::Object result = JSON::Object::Entries{{"packets", packets}};
      if (input) result.set("data", bytes::base64::encode(bytes));
      return success(result);
    });
  }

}
#endif
