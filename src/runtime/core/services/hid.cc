#include "hid.hh"

#include "../../debug.hh"
#if defined(__APPLE__)
#include "hid/macos_backend.hh"
#include "hid/libusb_backend.hh"
#elif defined(_WIN32)
#include "hid/windows_backend.hh"
#include "hid/libusb_backend.hh"
#else
#include "hid/libusb_backend.hh"
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

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

    bool parseRequestDeviceOptions(const JSON::Any& optionsAny, HID::RequestDeviceOptions& out, JSON::Object& error) {
      out = HID::RequestDeviceOptions{};

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
            error = makeError("TypeError", "filters[] must be objects");
            return false;
          }

          const auto& obj = entry.as<JSON::Object>();
          HID::RequestDeviceFilter filter;
          bool hasConstraint = false;

          if (obj.has("vendorId")) {
            uint32_t vendor = 0;
            if (!parseUnsigned(obj.get("vendorId"), 0xFFFFu, vendor)) {
              error = makeError("TypeError", "filters[].vendorId must be an unsigned short");
              return false;
            }
            filter.vendorId = static_cast<uint16_t>(vendor & 0xFFFFu);
            filter.hasVendorId = true;
            hasConstraint = true;
          }

          if (obj.has("productId")) {
            uint32_t product = 0;
            if (!parseUnsigned(obj.get("productId"), 0xFFFFu, product)) {
              error = makeError("TypeError", "filters[].productId must be an unsigned short");
              return false;
            }
            filter.productId = static_cast<uint16_t>(product & 0xFFFFu);
            filter.hasProductId = true;
            hasConstraint = true;
          }

          if (obj.has("usagePage")) {
            uint32_t usagePage = 0;
            if (!parseUnsigned(obj.get("usagePage"), 0xFFFFu, usagePage)) {
              error = makeError("TypeError", "filters[].usagePage must be an unsigned short");
              return false;
            }
            filter.usagePage = static_cast<uint16_t>(usagePage & 0xFFFFu);
            filter.hasUsagePage = true;
            hasConstraint = true;
          }

          if (obj.has("usage")) {
            uint32_t usage = 0;
            if (!parseUnsigned(obj.get("usage"), 0xFFFFu, usage)) {
              error = makeError("TypeError", "filters[].usage must be an unsigned short");
              return false;
            }
            filter.usage = static_cast<uint16_t>(usage & 0xFFFFu);
            filter.hasUsage = true;
            hasConstraint = true;
          }

          if (!hasConstraint) {
            error = makeError("TypeError", "filters[] must include at least one constraint");
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

    inline std::unique_ptr<HID::Backend> createBackend(HID& svc) {
#if defined(__APPLE__)
      #if TARGET_OS_IPHONE
        // iOS/iPadOS do not expose a public HID API we can target.
        (void)svc;
        return nullptr;
      #else
        if (auto native = oro::runtime::core::services::hid::makeMacHIDBackend(svc)) {
          return native;
        }
      #endif
#elif defined(_WIN32)
      if (auto native = oro::runtime::core::services::hid::makeWindowsHIDBackend(svc)) {
        return native;
      }
#endif
      return oro::runtime::core::services::hid::makeHIDBackend(svc);
    }

  }

  HID::Backend* HID::ensureBackend() {
    if (!this->backend) {
      auto backendCandidate = createBackend(*this);
      if (backendCandidate) {
        this->backend = std::move(backendCandidate);
      }
    }
    return this->backend.get();
  }

  bool HID::serviceEnabled(const String& seq, const Callback& cb) const {
    if (!this->enabled.load()) {
      cb(seq, serviceDisabled("WebHID service disabled"), QueuedResponse{});
      return false;
    }
    return true;
  }

  HID::Backend* HID::requireBackend(const String& seq, const Callback& cb) {
    auto* backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("WebHID backend not available on this platform"), QueuedResponse{});
      return nullptr;
    }
    return backendPtr;
  }

  bool HID::start() {
    return core::Service::start();
  }

  bool HID::stop() {
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

  void HID::getDevices(const String& seq, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    if (auto* runtime = this->context.getRuntime(); runtime && !runtime->hasPermission("hid")) {
      cb(seq, makeError("NotAllowedError", "HID access is disabled"), QueuedResponse{});
      return;
    }
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->getDevices(seq, cb);
  }

  void HID::requestDevice(const String& seq, const JSON::Any& options, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;

    if (auto* runtime = this->context.getRuntime(); runtime && !runtime->hasPermission("hid")) {
      cb(seq, makeError("NotAllowedError", "HID access is disabled"), QueuedResponse{});
      return;
    }

    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;

    HID::RequestDeviceOptions parsed;
    JSON::Object parseError;
    if (!parseRequestDeviceOptions(options, parsed, parseError)) {
      cb(seq, parseError, QueuedResponse{});
      return;
    }

    Map<String, String> permissionOptions;
    permissionOptions["context"] = "hid.requestDevice";

    const String permissionSeq = seq + "#hid-permission";
    this->services.permissions.request(
      permissionSeq,
      "hid",
      permissionOptions,
      [this, seq, cb, backend, parsed](const String&, JSON::Any permissionResult, QueuedResponse) mutable {
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

        if (state != "granted" && state != "prompt") {
          cb(seq, makeError("NotAllowedError", "User denied HID access"), QueuedResponse{});
          return;
        }

        backend->requestDevice(seq, parsed, cb);
      }
    );
  }

  void HID::chooseDevice(const String& seq, const DeviceSelection& selection, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->chooseDevice(seq, selection, cb);
  }

  void HID::cancelRequest(const String& seq, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->cancelRequest(seq, cb);
  }

  void HID::forgetDevice(const String& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->forgetDevice(seq, deviceId, cb);
  }

  void HID::open(const String& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->open(seq, deviceId, cb);
  }

  void HID::close(const String& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->close(seq, deviceId, cb);
  }

  void HID::sendReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->sendReport(seq, deviceId, reportId, data, cb);
  }

  void HID::sendFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->sendFeatureReport(seq, deviceId, reportId, data, cb);
  }

  void HID::receiveFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, uint16_t length, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto* backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->receiveFeatureReport(seq, deviceId, reportId, length, cb);
  }

  bool filterMatches(const HID::RequestDeviceFilter& filter, const HID::Backend::DeviceDescriptor& descriptor) {
    if (filter.hasVendorId && descriptor.vendorId != filter.vendorId) {
      return false;
    }
    if (filter.hasProductId && descriptor.productId != filter.productId) {
      return false;
    }
    if (!filter.hasUsagePage && !filter.hasUsage) {
      return true;
    }
    for (const auto& collection : descriptor.collections) {
      if (filter.hasUsagePage && collection.usagePage != filter.usagePage) {
        continue;
      }
      if (filter.hasUsage && collection.usage != filter.usage) {
        continue;
      }
      return true;
    }
    return false;
  }
}
