#include "usb.hh"

#include "../../debug.hh"
#include "usb/libusb_backend.hh"

#include "../../runtime.hh"

#include <future>

namespace oro::runtime::core::services {

  namespace {
    inline JSON::Object makeError(const char* type, const String& message) {
      return JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String(type)},
          {"message", message}
        }}}
      }};
    }

    inline JSON::Object notSupported(const String& message) {
      return makeError("NotSupportedError", message);
    }

    inline JSON::Object serviceDisabled(const String& message) {
      return makeError("InvalidStateError", message);
    }

    bool parseUnsigned(const JSON::Any& value, uint32_t max, uint32_t& out) {
      try {
        switch (value.type) {
          case JSON::Type::Number:
            out = static_cast<uint32_t>(value.as<JSON::Number>().value());
            break;
          case JSON::Type::String:
            out = static_cast<uint32_t>(std::stoul(value.str(), nullptr, 0));
            break;
          case JSON::Type::Boolean:
            out = value.as<JSON::Boolean>().value() ? 1u : 0u;
            break;
          default:
            return false;
        }
      } catch (...) {
        return false;
      }

      if (out > max) {
        return false;
      }
      return true;
    }

    bool toBool(const JSON::Any& value) {
      if (value.type == JSON::Type::Boolean) {
        return value.as<JSON::Boolean>().value();
      }

      if (value.type == JSON::Type::Number) {
        return value.as<JSON::Number>().value() != 0;
      }

      if (value.type == JSON::Type::String) {
        const auto str = value.str();
        if (str == "1" || str == "true" || str == "TRUE") {
          return true;
        }
        return false;
      }

      return false;
    }

    bool parseRequestDeviceOptions(const JSON::Any& optionsAny, USB::RequestDeviceOptions& out, JSON::Object& error) {
      out = USB::RequestDeviceOptions{};

      if (optionsAny.type != JSON::Type::Object) {
        error = makeError("TypeError", "options must be an object");
        return false;
      }

      const auto& options = optionsAny.as<JSON::Object>();

      if (options.has("acceptAllDevices")) {
        out.acceptAllDevices = toBool(options.get("acceptAllDevices"));
      }

      if (options.has("filters")) {
        const auto& filtersAny = options.get("filters");
        if (filtersAny.type != JSON::Type::Array) {
          error = makeError("TypeError", "filters must be an array");
          return false;
        }

        const auto& filters = filtersAny.as<JSON::Array>();
        for (size_t idx = 0; idx < filters.size(); ++idx) {
          const auto& entry = filters[idx];
          if (entry.type != JSON::Type::Object) {
            continue;
          }

          const auto& obj = entry.as<JSON::Object>();
          USB::RequestDeviceFilter filter;
          bool hasConstraint = false;

          if (obj.has("vendorId")) {
            uint32_t vendor = 0;
            if (!parseUnsigned(obj.get("vendorId"), 0xFFFFu, vendor)) {
              error = makeError("TypeError", "filters[].vendorId must be a number");
              return false;
            }
            filter.vendorId = static_cast<uint16_t>(vendor & 0xFFFFu);
            filter.hasVendorId = true;
            hasConstraint = true;
          }

          if (obj.has("productId")) {
            uint32_t product = 0;
            if (!parseUnsigned(obj.get("productId"), 0xFFFFu, product)) {
              error = makeError("TypeError", "filters[].productId must be a number");
              return false;
            }
            filter.productId = static_cast<uint16_t>(product & 0xFFFFu);
            filter.hasProductId = true;
            hasConstraint = true;
          }

          if (obj.has("classCode")) {
            uint32_t klass = 0;
            if (!parseUnsigned(obj.get("classCode"), 0xFFu, klass)) {
              error = makeError("TypeError", "filters[].classCode must be a number");
              return false;
            }
            filter.classCode = static_cast<uint8_t>(klass & 0xFFu);
            filter.hasClassCode = true;
            hasConstraint = true;
          }

          if (obj.has("subclassCode")) {
            uint32_t subclass = 0;
            if (!parseUnsigned(obj.get("subclassCode"), 0xFFu, subclass)) {
              error = makeError("TypeError", "filters[].subclassCode must be a number");
              return false;
            }
            filter.subclassCode = static_cast<uint8_t>(subclass & 0xFFu);
            filter.hasSubclassCode = true;
            hasConstraint = true;
          }

          if (obj.has("protocolCode")) {
            uint32_t protocol = 0;
            if (!parseUnsigned(obj.get("protocolCode"), 0xFFu, protocol)) {
              error = makeError("TypeError", "filters[].protocolCode must be a number");
              return false;
            }
            filter.protocolCode = static_cast<uint8_t>(protocol & 0xFFu);
            filter.hasProtocolCode = true;
            hasConstraint = true;
          }

          if (!hasConstraint) {
            error = makeError("TypeError", "Each filter must specify vendorId/productId or class information");
            return false;
          }

          out.filters.push_back(filter);
        }
      }

      if (!out.acceptAllDevices && out.filters.empty()) {
        error = makeError("TypeError", "filters is required when acceptAllDevices is false");
        return false;
      }

      return true;
    }

  }

  USB::Backend* USB::ensureBackend() {
    if (!this->backend) {
      auto backendCandidate = oro::runtime::core::services::usb::makeUSBBackend(*this);
      if (backendCandidate) {
        this->backend = std::move(backendCandidate);
      }
    }
    return this->backend.get();
  }

  bool USB::serviceEnabled(const String& seq, const Callback& cb) const {
    if (!this->enabled.load()) {
      cb(seq, serviceDisabled("WebUSB service disabled"), QueuedResponse{});
      return false;
    }
    return true;
  }

  USB::Backend* USB::requireBackend(const String& seq, const Callback& cb) {
    auto* backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("WebUSB backend not available on this platform"), QueuedResponse{});
      return nullptr;
    }
    return backendPtr;
  }

  bool filterMatches(const USB::RequestDeviceFilter& filter, const USB::Backend::DeviceDescriptor& descriptor) {
    if (filter.hasVendorId && descriptor.vendorId != filter.vendorId) {
      return false;
    }

    if (filter.hasProductId && descriptor.productId != filter.productId) {
      return false;
    }

    const bool requiresClassMatch = filter.hasClassCode || filter.hasSubclassCode || filter.hasProtocolCode;
    if (!requiresClassMatch) {
      return true;
    }

    bool matchesDescriptor = true;
    if (filter.hasClassCode && descriptor.classCode != filter.classCode) {
      matchesDescriptor = false;
    }
    if (filter.hasSubclassCode && descriptor.subclassCode != filter.subclassCode) {
      matchesDescriptor = false;
    }
    if (filter.hasProtocolCode && descriptor.protocolCode != filter.protocolCode) {
      matchesDescriptor = false;
    }
    if (matchesDescriptor) {
      return true;
    }

    const uint8_t deviceClass = descriptor.classCode;
    const bool shouldConsultInterfaces = deviceClass == 0x00u || deviceClass == 0xEFu || deviceClass == 0x02u;
    if (!shouldConsultInterfaces) {
      return false;
    }

    for (const auto& iface : descriptor.interfaces) {
      bool matchesInterface = true;
      if (filter.hasClassCode && iface.classCode != filter.classCode) {
        matchesInterface = false;
      }
      if (filter.hasSubclassCode && iface.subclassCode != filter.subclassCode) {
        matchesInterface = false;
      }
      if (filter.hasProtocolCode && iface.protocolCode != filter.protocolCode) {
        matchesInterface = false;
      }
      if (matchesInterface) {
        return true;
      }
    }

    return false;
  }

  bool USB::start() {
    return core::Service::start();
  }

  bool USB::stop() {
    {
      auto barrier = std::make_shared<std::promise<void>>();
      auto future = barrier->get_future();
      this->queue.push([barrier]() {
        barrier->set_value();
      });
      future.wait();
    }
    backend.reset();
    return core::Service::stop();
  }

  void USB::getDevices(const String& seq, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    if (auto* runtime = this->context.getRuntime(); runtime && !runtime->hasPermission("usb")) {
      cb(seq, makeError("NotAllowedError", "USB access is disabled"), QueuedResponse{});
      return;
    }
    if (!this->requireBackend(seq, cb)) return;
    backend->getDevices(seq, cb);
  }

  void USB::requestDevice(const String& seq, const JSON::Any& options, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;

    if (auto* runtime = this->context.getRuntime(); runtime && !runtime->hasPermission("usb")) {
      cb(seq, makeError("NotAllowedError", "USB access is disabled"), QueuedResponse{});
      return;
    }

    if (!this->requireBackend(seq, cb)) return;

    USB::RequestDeviceOptions parsed;
    JSON::Object parseError;
    if (!parseRequestDeviceOptions(options, parsed, parseError)) {
      cb(seq, parseError, QueuedResponse{});
      return;
    }

    Map<String, String> permissionOptions;
    permissionOptions["context"] = "usb.requestDevice";

    const String permissionSeq = seq + "#usb-permission";
    this->services.permissions.request(
      permissionSeq,
      "usb",
      permissionOptions,
      [this, seq, cb, parsed](const String&, JSON::Any permissionResult, QueuedResponse) mutable {
        String state = "denied";
        if (permissionResult.type == JSON::Type::Object) {
          const auto& obj = permissionResult.as<JSON::Object>();
          if (obj.has("err")) {
            cb(seq, obj, QueuedResponse{});
            return;
          }
          if (obj.has("data")) {
            const auto& data = obj.get("data");
            if (data.type == JSON::Type::Object && data.as<JSON::Object>().has("state")) {
              state = data.as<JSON::Object>().get("state").str();
            }
          }
        }

        if (state != "granted") {
          cb(seq, makeError("NotAllowedError", "User denied USB access"), QueuedResponse{});
          return;
        }

        if (!this->serviceEnabled(seq, cb)) return;
        auto* currentBackend = this->requireBackend(seq, cb);
        if (!currentBackend) return;
        currentBackend->requestDevice(seq, parsed, cb);
      }
    );
  }

  void USB::forgetDevice(const String& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->forgetDevice(seq, deviceId, cb);
  }

  void USB::open(const String& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->open(seq, deviceId, cb);
  }

  void USB::close(const String& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->close(seq, deviceId, cb);
  }

  void USB::selectConfiguration(const String& seq, const String& deviceId, uint8_t configurationValue, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->selectConfiguration(seq, deviceId, configurationValue, cb);
  }

  void USB::claimInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->claimInterface(seq, deviceId, interfaceNumber, cb);
  }

  void USB::releaseInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->releaseInterface(seq, deviceId, interfaceNumber, cb);
  }

  void USB::selectAlternateInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, uint8_t alternateSetting, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->selectAlternateInterface(seq, deviceId, interfaceNumber, alternateSetting, cb);
  }

  void USB::controlTransferIn(const String& seq, const String& deviceId, const JSON::Any& setup, uint32_t length, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->controlTransferIn(seq, deviceId, setup, length, cb);
  }

  void USB::controlTransferOut(const String& seq, const String& deviceId, const JSON::Any& setup, const bytes::Buffer& data, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->controlTransferOut(seq, deviceId, setup, data, cb);
  }

  void USB::transferIn(const String& seq, const String& deviceId, uint8_t endpointNumber, uint32_t length, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->transferIn(seq, deviceId, endpointNumber, length, cb);
  }

  void USB::transferOut(const String& seq, const String& deviceId, uint8_t endpointNumber, const bytes::Buffer& data, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->transferOut(seq, deviceId, endpointNumber, data, cb);
  }

  void USB::clearHalt(const String& seq, const String& deviceId, uint8_t endpointNumber, bool directionIn, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->clearHalt(seq, deviceId, endpointNumber, directionIn, cb);
  }

  void USB::reset(const String& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->reset(seq, deviceId, cb);
  }

  void USB::chooseDevice(const String& seq, const DeviceSelection& selection, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->chooseDevice(seq, selection, cb);
  }

  void USB::cancelRequest(const String& seq, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->cancelRequest(seq, cb);
  }
}
