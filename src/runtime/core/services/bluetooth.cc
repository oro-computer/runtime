#include <cstdio>

#include "../../debug.hh"
#include "../../runtime.hh"
#include "../services.hh"
#include "../../string.hh"
#include "bluetooth.hh"

namespace oro::runtime::core::services {
  using oro::runtime::string::toLowerCase;

  // Backend factory provided by platform-specific TUs when available.
  // Declare as weak so linking succeeds on platforms without a backend.
  std::unique_ptr<Bluetooth::Backend> makeBluetoothBackend(Bluetooth& svc) __attribute__((weak));

#if defined(__linux__) && !defined(__ANDROID__)
  extern bool oro_runtime_bluetooth_linux_link_anchor();
  [[maybe_unused]] static const bool bluetooth_linux_backend_forced_link =
    oro_runtime_bluetooth_linux_link_anchor();
#endif

  // Ensure backend can be created lazily (helper inside namespace)
  static inline Bluetooth::Backend* ensureBackend(Bluetooth* svc) {
    if (!svc->backend) {
      if (makeBluetoothBackend) {
        debug("[bluetooth] ensureBackend creating backend via factory");
        svc->backend = makeBluetoothBackend(*svc);
        if (!svc->backend) {
          debug("[bluetooth] backend factory returned nullptr");
        }
      } else {
        debug("[bluetooth] no backend factory available; will fall back to stub");
      }
    }
    return svc->backend.get();
  }

  // Helper to reply NotSupported
  static inline JSON::Object NotSupported (const String& message = "Bluetooth is not supported on this platform") {
    return JSON::Object::Entries {
      {"err", JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", message}
      }}
    };
  }

  String Bluetooth::normalizeUUID (const String& uuid) {
    const auto lower = toLowerCase(uuid);
    if (lower.size() == 4) {
      char buf[37];
      snprintf(buf, sizeof(buf), "0000%s-0000-1000-8000-00805f9b34fb", lower.c_str());
      return String(buf);
    }
    return lower;
  }

  static inline void assignTypeError (JSON::Object*& errorOut, const char* message) {
    if (!errorOut) return;
    *errorOut = JSON::Object::Entries {{
      {"type", "TypeError"},
      {"message", String(message)}
    }};
  }

  static bool parseUUIDEntry (const JSON::Any& value, String& out) {
    try {
      if (value.type == JSON::Type::Number) {
        auto num = value.as<JSON::Number>().value();
        if (num < 0 || num > 0xFFFFFFFF) return false;
        char buf[9];
        snprintf(buf, sizeof(buf), "%04x", (uint32_t) num);
        out = Bluetooth::normalizeUUID(String(buf));
        return true;
      }
      if (value.type == JSON::Type::String) {
        out = Bluetooth::normalizeUUID(value.str());
        return out.size() > 0;
      }
    } catch (...) {
      return false;
    }
    return false;
  }

  bool Bluetooth::parseRequestDeviceOptions (
    const JSON::Any& optionsAny,
    ParsedRequestDeviceOptions& out,
    JSON::Object* error,
    ParsedRequestDeviceOptions::ServiceMatchMode defaultMode
  ) {
    out = ParsedRequestDeviceOptions{};
    out.servicesMatch = defaultMode;
    out.timeoutMs = -1;

    const JSON::Any* optionsResolved = &optionsAny;
    JSON::Any parsedOptions;

    if (optionsAny.type != JSON::Type::Object) {
      debug("[bluetooth] parseRequestDeviceOptions received non-object (type=%d) payload=%s", (int) optionsAny.type, optionsAny.str().c_str());
      try {
        parsedOptions = JSON::parse(optionsAny.str());
        optionsResolved = &parsedOptions;
        debug("[bluetooth] parseRequestDeviceOptions parsed payload into type=%d", (int) optionsResolved->type);
      } catch (...) {
        debug("[bluetooth] parseRequestDeviceOptions JSON parse failed");
        assignTypeError(error, "options must be an object");
        return false;
      }
    }

    if (optionsResolved->type != JSON::Type::Object) {
      assignTypeError(error, "options must be an object");
      return false;
    }

    const auto& options = optionsResolved->as<JSON::Object>();

    if (options.has("acceptAllDevices")) {
      const auto& v = options.get("acceptAllDevices");
      if (v.type == JSON::Type::Boolean) {
        out.acceptAllDevices = (bool) v.as<JSON::Boolean>().value();
      } else {
        out.acceptAllDevices = (v.str() == "true" || v.str() == "1");
      }
    }

    if (options.has("optionalServices")) {
      const auto& optional = options.get("optionalServices");
      if (optional.type != JSON::Type::Array) {
        assignTypeError(error, "optionalServices must be an array");
        return false;
      }
      const auto& arr = optional.as<JSON::Array>();
      for (size_t i = 0; i < arr.size(); ++i) {
        String uuid;
        if (!parseUUIDEntry(arr[i], uuid)) {
          assignTypeError(error, "optionalServices entries must be UUID strings or numbers");
          return false;
        }
        out.optionalServices.push_back(std::move(uuid));
      }
    }

    if (options.has("servicesMatch")) {
      const auto& modeAny = options.get("servicesMatch");
      const auto mode = toLowerCase(modeAny.str());
      if (mode == "any") {
        out.servicesMatch = ParsedRequestDeviceOptions::ServiceMatchMode::Any;
      } else if (mode.empty() || mode == "all") {
        out.servicesMatch = ParsedRequestDeviceOptions::ServiceMatchMode::All;
      } else {
        assignTypeError(error, "servicesMatch must be 'all' or 'any'");
        return false;
      }
    }

    if (options.has("timeoutMs")) {
      const auto& tm = options.get("timeoutMs");
      try {
        int value = -1;
        if (tm.type == JSON::Type::Number) {
          value = (int) tm.as<JSON::Number>().value();
        } else {
          value = std::stoi(tm.str());
        }
        if (value >= 0) {
          out.timeoutMs = value;
        }
      } catch (...) {
        out.timeoutMs = -1;
      }
    }

    if (options.has("filters")) {
      const auto& filtersAny = options.get("filters");
      if (filtersAny.type != JSON::Type::Array) {
        assignTypeError(error, "filters must be an array");
        return false;
      }
      const auto& filters = filtersAny.as<JSON::Array>();
      for (size_t idx = 0; idx < filters.size(); ++idx) {
        const auto& entry = filters[idx];
        if (entry.type != JSON::Type::Object) continue;
        const auto& obj = entry.as<JSON::Object>();
        DeviceFilter filter;

        if (obj.has("services") && obj.get("services").type == JSON::Type::Array) {
          const auto& services = obj.get("services").as<JSON::Array>();
          for (size_t s = 0; s < services.size(); ++s) {
            String uuid;
            if (parseUUIDEntry(services[s], uuid)) {
              filter.services.push_back(std::move(uuid));
            }
          }
        }

        if (obj.has("name") && obj.get("name").type == JSON::Type::String) {
          filter.name = obj.get("name").str();
          filter.hasName = true;
        }

        if (obj.has("namePrefix") && obj.get("namePrefix").type == JSON::Type::String) {
          filter.namePrefix = obj.get("namePrefix").str();
          filter.hasNamePrefix = true;
        }

        if (obj.has("manufacturerData") && obj.get("manufacturerData").type == JSON::Type::Array) {
          const auto& mArr = obj.get("manufacturerData").as<JSON::Array>();
          for (size_t m = 0; m < mArr.size(); ++m) {
            const auto& mf = mArr[m];
            if (mf.type != JSON::Type::Object) continue;
            const auto& mo = mf.as<JSON::Object>();
            if (!mo.has("companyIdentifier")) continue;
            ManufacturerDataFilter md;
            try {
              const auto& cid = mo.get("companyIdentifier");
              if (cid.type == JSON::Type::Number) {
                md.companyId = (uint16_t) cid.as<JSON::Number>().value();
              } else {
                md.companyId = (uint16_t) std::stoi(cid.str());
              }
            } catch (...) {
              continue;
            }

            if (mo.has("dataPrefix") && mo.get("dataPrefix").type == JSON::Type::Array) {
              const auto& prefix = mo.get("dataPrefix").as<JSON::Array>();
              for (size_t p = 0; p < prefix.size(); ++p) {
                try {
                  int value = (prefix[p].type == JSON::Type::Number)
                    ? (int) prefix[p].as<JSON::Number>().value()
                    : std::stoi(prefix[p].str());
                  if (value < 0) value = 0;
                  if (value > 255) value = 255;
                  md.dataPrefix.push_back((uint8_t) value);
                } catch (...) {
                  md.dataPrefix.clear();
                  break;
                }
              }
              if (!md.dataPrefix.empty()) md.hasDataPrefix = true;
            }

            if (mo.has("dataMask") && mo.get("dataMask").type == JSON::Type::Array) {
              const auto& mask = mo.get("dataMask").as<JSON::Array>();
              for (size_t p = 0; p < mask.size(); ++p) {
                try {
                  int value = (mask[p].type == JSON::Type::Number)
                    ? (int) mask[p].as<JSON::Number>().value()
                    : std::stoi(mask[p].str());
                  if (value < 0) value = 0;
                  if (value > 255) value = 255;
                  md.dataMask.push_back((uint8_t) value);
                } catch (...) {
                  md.dataMask.clear();
                  break;
                }
              }
              if (!md.dataMask.empty()) md.hasDataMask = true;
            }

            filter.manufacturerData.push_back(std::move(md));
          }
        }

        out.filters.push_back(std::move(filter));
      }
    }

    if ((out.acceptAllDevices && !out.filters.empty())) {
      assignTypeError(error, "acceptAllDevices cannot be used with filters");
      return false;
    }

    if (!out.acceptAllDevices && out.filters.empty()) {
      assignTypeError(error, "filters required when acceptAllDevices is false");
      return false;
    }

    return true;
  }

  static bool nameMatches (const String& needle, const String& haystack) {
    if (needle.empty()) return true;
    const auto n = toLowerCase(needle);
    const auto h = toLowerCase(haystack);
    return n == h;
  }

  static bool prefixMatches (const String& prefix, const String& value) {
    if (prefix.empty()) return true;
    const auto p = toLowerCase(prefix);
    const auto v = toLowerCase(value);
    return v.rfind(p, 0) == 0;
  }

  bool Bluetooth::filterMatches (
    const DeviceFilter& filter,
    const String& name,
    const Vector<String>& services,
    const Map<uint16_t, Vector<uint8_t>>& manufacturerData,
    ParsedRequestDeviceOptions::ServiceMatchMode serviceMatchMode
  ) {
    if (filter.hasName && !nameMatches(filter.name, name)) {
      return false;
    }

    if (filter.hasNamePrefix && !prefixMatches(filter.namePrefix, name)) {
      return false;
    }

    if (!filter.services.empty()) {
      if (serviceMatchMode == ParsedRequestDeviceOptions::ServiceMatchMode::All) {
        for (const auto& required : filter.services) {
          bool present = false;
          for (const auto& deviceSvc : services) {
            if (toLowerCase(deviceSvc) == toLowerCase(required)) {
              present = true;
              break;
            }
          }
          if (!present) return false;
        }
      } else {
        bool anyPresent = false;
        for (const auto& required : filter.services) {
          for (const auto& deviceSvc : services) {
            if (toLowerCase(deviceSvc) == toLowerCase(required)) {
              anyPresent = true;
              break;
            }
          }
          if (anyPresent) break;
        }
        if (!anyPresent) return false;
      }
    }

    if (!filter.manufacturerData.empty()) {
      bool anyMatch = false;
      for (const auto& md : filter.manufacturerData) {
        if (!manufacturerData.contains(md.companyId)) continue;
        const auto& data = manufacturerData.at(md.companyId);
        if (!md.hasDataPrefix) {
          anyMatch = true;
          break;
        }
        if (data.size() < md.dataPrefix.size()) continue;
        bool matched = true;
        for (size_t i = 0; i < md.dataPrefix.size(); ++i) {
          const auto mask = md.hasDataMask && i < md.dataMask.size() ? md.dataMask[i] : 0xFFu;
          if ((data[i] & mask) != (md.dataPrefix[i] & mask)) {
            matched = false;
            break;
          }
        }
        if (matched) {
          anyMatch = true;
          break;
        }
      }
      if (!anyMatch) return false;
    }

    return true;
  }

  bool Bluetooth::anyFilterMatches (
    const ParsedRequestDeviceOptions& options,
    const String& name,
    const Vector<String>& services,
    const Map<uint16_t, Vector<uint8_t>>& manufacturerData
  ) {
    if (options.acceptAllDevices) return true;
    for (const auto& filter : options.filters) {
      if (filterMatches(filter, name, services, manufacturerData, options.servicesMatch)) {
        return true;
      }
    }
    return false;
  }

  void Bluetooth::getAvailability (const String& seq, const Callback cb) const {
    this->loop.dispatch([=, this]() {
      debug("[bluetooth] getAvailability requested seq=%s", seq.c_str());
      if (auto* self = const_cast<Bluetooth*>(this)) {
        if (!self->enabled) { cb(seq, NotSupported(), QueuedResponse{}); return; }
      }
      // Dispatch to backend if present
      if (auto* be = ensureBackend(const_cast<Bluetooth*>(this))) {
        debug("[bluetooth] forwarding getAvailability to backend");
        return be->getAvailability(seq, cb);
      }

      struct StubBackend : Backend {
        void getAvailability(const String& seq, const core::Service::Callback cb) const override {
          debug("[bluetooth] stub backend getAvailability returning false");
          JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"available", false}}}};
          cb(seq, json, QueuedResponse{});
        }
        void getDevices(const String& seq, const core::Service::Callback cb) override {
          debug("[bluetooth] stub backend getDevices returning []");
          JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"devices", JSON::Array {}}}}};
          cb(seq, json, QueuedResponse{});
        }
        void requestDevice(const String&, const JSON::Any&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void gattConnect(const String&, const DeviceID&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void gattDisconnect(const String&, const DeviceID&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void gattGetPrimaryService(const String&, const DeviceID&, const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void gattGetPrimaryServices(const String&, const DeviceID&, const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void serviceGetCharacteristic(const String&, const DeviceID&, const String&, const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void serviceGetCharacteristics(const String&, const DeviceID&, const String&, const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void characteristicReadValue(const String&, const DeviceID&, const String&, const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void characteristicWriteValue(const String&, const DeviceID&, const String&, const String&, const bytes::Buffer&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void characteristicStartNotifications(const String&, const DeviceID&, const String&, const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void characteristicStopNotifications(const String&, const DeviceID&, const String&, const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void chooseDevice(const String&, const DeviceID&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void cancelRequest(const String& seq, const core::Service::Callback cb) override { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, QueuedResponse{}); }
        void deviceWatchAdvertisements(const String&, const DeviceID&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void deviceForget(const String&, const DeviceID&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
        void restartDiscovery(const String&, const core::Service::Callback cb) override { cb("", NotSupported(), QueuedResponse{}); }
      };
      static StubBackend stub;
      stub.getAvailability(seq, cb);
    });
  }

  void Bluetooth::getDevices (const String& seq, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) {
        return be->getDevices(seq, cb);
      }
      JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"devices", JSON::Array {}}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void Bluetooth::restartDiscovery (const String& seq, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) {
        return be->restartDiscovery(seq, cb);
      }
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::requestDevice (const String& seq, const JSON::Any& options, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (!this->context.getRuntime()->hasPermission("bluetooth")) {
        JSON::Object json = JSON::Object::Entries {{
          "err", JSON::Object::Entries {{
            {"type", "NotAllowedError"},
            {"message", "Bluetooth permission is disabled by runtime configuration"}
          }}
        }};
        return cb(seq, json, QueuedResponse{});
      }
      if (auto* be = ensureBackend(this)) {
        return be->requestDevice(seq, options, cb);
      }
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::gattConnect (const String& seq, const DeviceID& deviceId, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->gattConnect(seq, deviceId, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::gattDisconnect (const String& seq, const DeviceID& deviceId, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->gattDisconnect(seq, deviceId, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::gattGetPrimaryService (const String& seq, const DeviceID& deviceId, const String& service, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->gattGetPrimaryService(seq, deviceId, service, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::gattGetPrimaryServices (const String& seq, const DeviceID& deviceId, const String& service, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->gattGetPrimaryServices(seq, deviceId, service, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::serviceGetCharacteristic (const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->serviceGetCharacteristic(seq, deviceId, service, characteristic, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::serviceGetCharacteristics (const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->serviceGetCharacteristics(seq, deviceId, service, characteristic, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::characteristicReadValue (const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->characteristicReadValue(seq, deviceId, service, characteristic, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::characteristicWriteValue (const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const bytes::Buffer& value, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->characteristicWriteValue(seq, deviceId, service, characteristic, value, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::characteristicStartNotifications (const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->characteristicStartNotifications(seq, deviceId, service, characteristic, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::characteristicStopNotifications (const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->characteristicStopNotifications(seq, deviceId, service, characteristic, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::chooseDevice (const String& seq, const DeviceID& deviceId, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->chooseDevice(seq, deviceId, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::cancelRequest (const String& seq, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->cancelRequest(seq, cb);
      cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, QueuedResponse{});
    });
  }

  void Bluetooth::deviceWatchAdvertisements (const String& seq, const DeviceID& deviceId, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->deviceWatchAdvertisements(seq, deviceId, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  void Bluetooth::deviceForget (const String& seq, const DeviceID& deviceId, const Callback cb) {
    this->loop.dispatch([=, this]() {
      if (auto* be = ensureBackend(this)) return be->deviceForget(seq, deviceId, cb);
      cb(seq, NotSupported(), QueuedResponse{});
    });
  }

  uint64_t Bluetooth::addCharacteristicObserver(
    const DeviceID& deviceId,
    const ServiceID& serviceId,
    const CharID& characteristicId,
    Function<void(const JSON::Any&)> cb
  ) {
    Lock lock(this->observersMutex);
    CharObserver obs;
    obs.id = this->nextObserverId.fetch_add(1);
    obs.deviceId = deviceId;
    obs.serviceId = serviceId;
    obs.characteristicId = characteristicId;
    obs.callback = cb;
    this->observers.push_back(std::move(obs));
    return obs.id;
  }

  bool Bluetooth::removeCharacteristicObserver(uint64_t id) {
    Lock lock(this->observersMutex);
    auto it = std::remove_if(this->observers.begin(), this->observers.end(), [=](const CharObserver& o) { return o.id == id; });
    const bool removed = it != this->observers.end();
    if (removed) this->observers.erase(it, this->observers.end());
    return removed;
  }

  // Remove all observers matching the given device/service/characteristic triple.
  // Returns number of observers removed.
  static inline bool triplesEqual(const Bluetooth::CharObserver& o, const Bluetooth::DeviceID& d, const Bluetooth::ServiceID& s, const Bluetooth::CharID& c) {
    return o.deviceId == d && o.serviceId == s && o.characteristicId == c;
  }

  size_t Bluetooth::removeCharacteristicObserversFor(
    const DeviceID& deviceId,
    const ServiceID& serviceId,
    const CharID& characteristicId
  ) {
    Lock lock(this->observersMutex);
    const auto before = this->observers.size();
    this->observers.erase(
      std::remove_if(this->observers.begin(), this->observers.end(),
        [&](const CharObserver& o) { return triplesEqual(o, deviceId, serviceId, characteristicId); }
      ),
      this->observers.end()
    );
    return before - this->observers.size();
  }

  void Bluetooth::notifyCharacteristicValue(
    const DeviceID& deviceId,
    const ServiceID& serviceId,
    const CharID& characteristicId,
    const bytes::Buffer& value
  ) {
    // Build event JSON once
    JSON::Object event = JSON::Object::Entries {
      {"deviceId", deviceId},
      {"service", serviceId},
      {"characteristic", characteristicId},
      {"value", value.str(bytes::Buffer::Encoding::BASE64)},
      {"encoding", "base64"}
    };

    // Copy to allow callbacks without holding the lock
    Vector<Function<void(const JSON::Any&)>> callbacks;
    {
      Lock lock(this->observersMutex);
      for (const auto& o : this->observers) {
        if (o.deviceId == deviceId && o.serviceId == serviceId && o.characteristicId == characteristicId) {
          callbacks.push_back(o.callback);
        }
      }
    }

    for (const auto& cb : callbacks) {
      // Deliver on loop thread; this function already called from backend on main/loop threads, but keep dispatch consistent
      this->loop.dispatch([=]() { cb(event); });
    }
  }
}
