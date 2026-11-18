#include "usb.hh"

#include "../../debug.hh"
#include "usb/libusb_backend.hh"

#include "../../runtime.hh"

#include <future>
#include <cmath>

namespace oro::runtime::core::services {

  namespace {
    inline JSON::Object makeError(const char* type, const String& message) {
      return JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String(type)},
          {"name", String(type)},
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

    bool parseUnsigned (const JSON::Any& value, uint32_t max, uint32_t& out) {
      if (value.type != JSON::Type::Number) return false;
      const auto number = value.as<JSON::Number>().value();
      if (!(number >= 0 && number <= max && number == std::floor(number))) return false;
      out = static_cast<uint32_t>(number);
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
        const auto str = value.as<JSON::String>().value();
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

      for (const String key : {"filters", "exclusionFilters"}) {
        if (!options.has(key)) continue;
        const auto& filtersAny = options.get(key);
        if (filtersAny.type != JSON::Type::Array) {
          error = makeError("TypeError", "filters must be an array");
          return false;
        }

        const auto& filters = filtersAny.as<JSON::Array>();
        for (size_t idx = 0; idx < filters.size(); ++idx) {
          const auto& entry = filters[idx];
          if (entry.type != JSON::Type::Object) {
            error = makeError("TypeError", "USB filter must be an object");
            return false;
          }

          const auto& obj = entry.as<JSON::Object>();
          USB::RequestDeviceFilter filter;


          if (obj.has("vendorId")) {
            uint32_t vendor = 0;
            if (!parseUnsigned(obj.get("vendorId"), 0xFFFFu, vendor)) {
              error = makeError("TypeError", "filters[].vendorId must be a number");
              return false;
            }
            filter.vendorId = static_cast<uint16_t>(vendor & 0xFFFFu);
            filter.hasVendorId = true;
          }

          if (obj.has("productId")) {
            uint32_t product = 0;
            if (!parseUnsigned(obj.get("productId"), 0xFFFFu, product)) {
              error = makeError("TypeError", "filters[].productId must be a number");
              return false;
            }
            filter.productId = static_cast<uint16_t>(product & 0xFFFFu);
            filter.hasProductId = true;
          }

          if (obj.has("classCode")) {
            uint32_t klass = 0;
            if (!parseUnsigned(obj.get("classCode"), 0xFFu, klass)) {
              error = makeError("TypeError", "filters[].classCode must be a number");
              return false;
            }
            filter.classCode = static_cast<uint8_t>(klass & 0xFFu);
            filter.hasClassCode = true;
          }

          if (obj.has("subclassCode")) {
            uint32_t subclass = 0;
            if (!parseUnsigned(obj.get("subclassCode"), 0xFFu, subclass)) {
              error = makeError("TypeError", "filters[].subclassCode must be a number");
              return false;
            }
            filter.subclassCode = static_cast<uint8_t>(subclass & 0xFFu);
            filter.hasSubclassCode = true;
          }

          if (obj.has("protocolCode")) {
            uint32_t protocol = 0;
            if (!parseUnsigned(obj.get("protocolCode"), 0xFFu, protocol)) {
              error = makeError("TypeError", "filters[].protocolCode must be a number");
              return false;
            }
            filter.protocolCode = static_cast<uint8_t>(protocol & 0xFFu);
            filter.hasProtocolCode = true;
          }

          if (obj.has("serialNumber")) {
            const auto& serial = obj.get("serialNumber");
            if (serial.type != JSON::Type::String) {
              error = makeError("TypeError", "filters[].serialNumber must be a string");
              return false;
            }
            filter.serialNumber = serial.as<JSON::String>().value();
          }
          if ((filter.hasProductId && !filter.hasVendorId) ||
              (filter.hasSubclassCode && !filter.hasClassCode) ||
              (filter.hasProtocolCode && !filter.hasSubclassCode)) {
            error = makeError("TypeError", "Invalid USB filter dependency");
            return false;
          }

          (key == "filters" ? out.filters : out.exclusionFilters).push_back(filter);
        }
      }

      if (!out.acceptAllDevices && !options.has("filters")) {
        error = makeError("TypeError", "filters is required when acceptAllDevices is false");
        return false;
      }

      return true;
    }

  }

  SharedPointer<USB::Backend> USB::ensureBackend() {
    Lock lock(backendMutex);
    if (state == State::Stopped) return nullptr;
    if (!backend && state == State::Running) {
      backend = usb::makeUSBBackend(*this);
      startMonitor();
    }
    return backend;
  }

  void USB::startMonitor () {
    if (!backend || monitor) return;
    const std::weak_ptr<Backend> weak = backend;
    auto scanning = std::make_shared<std::atomic<bool>>(false);
    monitor = services.timers.setInterval(1000, [weak, scanning](auto cancel) {
      const auto live = weak.lock();
      if (!live) return cancel();
      if (scanning->exchange(true)) return;
      live->enumerateDevices([live, scanning](const auto&) { scanning->store(false); });
    });
  }

  void USB::stopMonitor () {
    if (monitor) services.timers.clearInterval(monitor);
    monitor = 0;
  }

  bool USB::serviceEnabled(const Request& seq, const Callback& cb) const {
    if (!this->enabled.load()) {
      cb(seq, serviceDisabled("WebUSB service disabled"), QueuedResponse{});
      return false;
    }
    return true;
  }

  SharedPointer<USB::Backend> USB::requireBackend(const Request& seq, const Callback& cb) {
    auto backendPtr = this->ensureBackend();
    if (!backendPtr) {
      cb(seq, notSupported("WebUSB backend not available on this platform"), QueuedResponse{});
      return nullptr;
    }
    return backendPtr;
  }

  bool filterMatches(const USB::RequestDeviceFilter& filter, const USB::Backend::DeviceDescriptor& descriptor) {
    if (filter.serialNumber && (descriptor.serialNumber.empty() || *filter.serialNumber != descriptor.serialNumber)) return false;
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

  bool isProtectedUSBClass (uint8_t code) {
    switch (code) {
      case 0x01:
      case 0x03:
      case 0x08:
      case 0x09:
      case 0x0b:
      case 0x0e:
      case 0x10:
      case 0xe0:
        return true;
      default:
        return false;
    }
  }

  bool USB::allowsOrigin (const URL& location, const Map<String, String>& config) {
    const auto bundle = config.find("meta_bundle_identifier");
    if (bundle == config.end() || bundle->second.empty()) return false;
    const auto host = string::toLowerCase(bundle->second);
    if (string::toLowerCase(location.hostname) != host) return false;
#if ORO_RUNTIME_PLATFORM_ANDROID
    if (location.scheme == "https") return location.port.empty() || location.port == "443";
#endif
    return location.scheme == "oro" && location.port.empty();
  }

  JSON::Object descriptorToJson (const USB::Backend::DeviceDescriptor& device) {
    JSON::Array configs;
    JSON::Array interfaces;
    for (auto value : device.configurations) configs.push(static_cast<int>(value));
    for (const auto& iface : device.interfaces) {
      interfaces.push(JSON::Object::Entries{
        {"configurationValue", static_cast<int>(iface.configurationValue)}, {"interfaceNumber", static_cast<int>(iface.interfaceNumber)},
        {"alternateSetting", static_cast<int>(iface.alternateSetting)}, {"classCode", static_cast<int>(iface.classCode)},
        {"subclassCode", static_cast<int>(iface.subclassCode)}, {"protocolCode", static_cast<int>(iface.protocolCode)}, {"interfaceName", iface.interfaceName}, {"endpoints", iface.endpoints}
      });
    }
    return JSON::Object::Entries{
      {"deviceId", device.deviceId}, {"vendorId", static_cast<int>(device.vendorId)}, {"productId", static_cast<int>(device.productId)},
      {"classCode", static_cast<int>(device.classCode)}, {"subclassCode", static_cast<int>(device.subclassCode)}, {"protocolCode", static_cast<int>(device.protocolCode)},
      {"productName", device.productName}, {"manufacturerName", device.manufacturerName}, {"serialNumber", device.serialNumber},
      {"usbVersion", static_cast<int>(device.usbVersion)}, {"deviceVersion", static_cast<int>(device.deviceVersion)},
      {"configurationValue", static_cast<int>(device.activeConfiguration)}, {"configurations", configs}, {"configurationNames", device.configurationNames}, {"interfaces", interfaces},
      {"authorized", device.authorized}, {"busNumber", static_cast<int>(device.busNumber)},
      {"deviceAddress", static_cast<int>(device.deviceAddress)}
    };
  }

  bool isBlockedUSBDevice (uint16_t vendor, uint16_t product) {
    // Device IDs from https://usb.spec.whatwg.org/blocklist.txt
    static const Set<uint32_t> devices = {
      0x096e0850, 0x096e0852, 0x096e0853, 0x096e0854, 0x096e0856, 0x096e0858,
      0x096e085a, 0x096e085b, 0x096e0880, 0x09c30023, 0x10500010, 0x10500018,
      0x10500030, 0x10500110, 0x10500111, 0x10500112, 0x10500113, 0x10500114,
      0x10500115, 0x10500116, 0x10500120, 0x10500200, 0x10500211, 0x10500401,
      0x10500402, 0x10500403, 0x10500404, 0x10500405, 0x10500406, 0x10500407,
      0x10500410, 0x10c48acf, 0x18d15026, 0x1a4400bb, 0x1d5060fc, 0x1e0df1ae,
      0x1e0df1d0, 0x1ea8f025, 0x20a04287, 0x24dc0101, 0x2581f1d0, 0x2abe1002,
      0x2ccf0880
    };
    return devices.contains((static_cast<uint32_t>(vendor) << 16) | product);
  }

  void USB::disconnectClient (uint64_t client) {
    Lock lock(backendMutex);
    if (backend && state != State::Stopped) backend->disconnectClient(client);
  }

  void USB::watch (const Request& seq, const Callback cb) {
    if (!serviceEnabled(seq, cb)) return;
    if (requireBackend(seq, cb)) cb(seq, JSON::Object::Entries{{"data", JSON::Object{}}}, {});
  }

  bool USB::start() {
    Lock lock(backendMutex);
    state = State::Running;
    startMonitor();
    return core::Service::start();
  }

  bool USB::pause () {
    Lock lock(backendMutex);
    if (state == State::Running) {
      state = State::Paused;
      stopMonitor();
    }
    return true;
  }

  bool USB::stop() {
    {
      Lock lock(backendMutex);
      state = State::Stopped;
      stopMonitor();
    }
    {
      auto barrier = std::make_shared<std::promise<void>>();
      auto future = barrier->get_future();
      this->queue.push([barrier]() {
        barrier->set_value();
      });
      future.wait();
    }
    Lock lock(backendMutex);
    backend.reset();
    return core::Service::stop();
  }

  void USB::getDevices(const Request& seq, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    if (auto* runtime = this->context.getRuntime(); runtime && !runtime->hasPermission("usb")) {
      cb(seq, makeError("NotAllowedError", "USB access is disabled"), QueuedResponse{});
      return;
    }
    auto backend = requireBackend(seq, cb);
    if (!backend) return;
    backend->getDevices(seq, cb);
  }

  void USB::requestDevice(const Request& seq, const JSON::Any& options, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;

    if (auto* runtime = this->context.getRuntime(); runtime && !runtime->hasPermission("usb")) {
      cb(seq, makeError("NotAllowedError", "USB access is disabled"), QueuedResponse{});
      return;
    }

    const auto backend = requireBackend(seq, cb);
    if (!backend) return;

    USB::RequestDeviceOptions parsed;
    JSON::Object parseError;
    if (!parseRequestDeviceOptions(options, parsed, parseError)) {
      cb(seq, parseError, QueuedResponse{});
      return;
    }

    backend->requestDevice(seq, parsed, cb);
  }

  void USB::forgetDevice(const Request& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->forgetDevice(seq, deviceId, cb);
  }

  void USB::open(const Request& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->open(seq, deviceId, cb);
  }

  void USB::close(const Request& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->close(seq, deviceId, cb);
  }

  void USB::selectConfiguration(const Request& seq, const String& deviceId, uint8_t configurationValue, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->selectConfiguration(seq, deviceId, configurationValue, cb);
  }

  void USB::claimInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->claimInterface(seq, deviceId, interfaceNumber, cb);
  }

  void USB::releaseInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->releaseInterface(seq, deviceId, interfaceNumber, cb);
  }

  void USB::selectAlternateInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, uint8_t alternateSetting, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->selectAlternateInterface(seq, deviceId, interfaceNumber, alternateSetting, cb);
  }

  void USB::controlTransferIn(const Request& seq, const String& deviceId, const JSON::Any& setup, uint32_t length, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->controlTransferIn(seq, deviceId, setup, length, cb);
  }

  void USB::controlTransferOut(const Request& seq, const String& deviceId, const JSON::Any& setup, const bytes::Buffer& data, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->controlTransferOut(seq, deviceId, setup, data, cb);
  }

  void USB::transferIn(const Request& seq, const String& deviceId, uint8_t endpointNumber, uint32_t length, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->transferIn(seq, deviceId, endpointNumber, length, cb);
  }

  void USB::transferOut(const Request& seq, const String& deviceId, uint8_t endpointNumber, const bytes::Buffer& data, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->transferOut(seq, deviceId, endpointNumber, data, cb);
  }

  void USB::clearHalt(const Request& seq, const String& deviceId, uint8_t endpointNumber, bool directionIn, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->clearHalt(seq, deviceId, endpointNumber, directionIn, cb);
  }

  void USB::isochronous (const Request& seq, const String& id, uint8_t endpoint, bool input, const Vector<uint32_t>& lengths, const bytes::Buffer& data, const Callback cb) {
    if (!serviceEnabled(seq, cb)) return;
    if (auto backend = requireBackend(seq, cb)) backend->isochronous(seq, id, endpoint, input, lengths, data, cb);
  }

  void USB::reset(const Request& seq, const String& deviceId, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->reset(seq, deviceId, cb);
  }

  void USB::chooseDevice(const Request& seq, const DeviceSelection& selection, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->chooseDevice(seq, selection, cb);
  }

  void USB::cancelRequest(const Request& seq, const Callback cb) {
    if (!this->serviceEnabled(seq, cb)) return;
    auto backend = this->requireBackend(seq, cb);
    if (!backend) return;
    backend->cancelRequest(seq, cb);
  }
}
