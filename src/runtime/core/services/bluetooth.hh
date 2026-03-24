#ifndef ORO_RUNTIME_CORE_SERVICES_BLUETOOTH_H
#define ORO_RUNTIME_CORE_SERVICES_BLUETOOTH_H

#include "../../core.hh"

namespace oro::runtime::core::services {
  class Bluetooth : public core::Service {
    public:
      using DeviceID = String;
      using ServiceID = String;
      using CharID = String;

      struct ManufacturerDataFilter {
        uint16_t companyId = 0;
        Vector<uint8_t> dataPrefix;
        Vector<uint8_t> dataMask;
        bool hasDataPrefix = false;
        bool hasDataMask = false;
      };

      struct DeviceFilter {
        Vector<String> services;
        String name;
        bool hasName = false;
        String namePrefix;
        bool hasNamePrefix = false;
        Vector<ManufacturerDataFilter> manufacturerData;
      };

      struct ParsedRequestDeviceOptions {
        enum class ServiceMatchMode {
          All,
          Any
        };

        bool acceptAllDevices = false;
        Vector<DeviceFilter> filters;
        Vector<String> optionalServices;
        ServiceMatchMode servicesMatch = ServiceMatchMode::All;
        int timeoutMs = -1;
      };

      struct Backend {
        virtual ~Backend() = default;
        virtual void getAvailability(const String& seq, const core::Service::Callback cb) const = 0;
        virtual void getDevices(const String& seq, const core::Service::Callback cb) = 0;
        virtual void requestDevice(const String& seq, const JSON::Any& options, const core::Service::Callback cb) = 0;
        virtual void gattConnect(const String& seq, const DeviceID& deviceId, const core::Service::Callback cb) = 0;
        virtual void gattDisconnect(const String& seq, const DeviceID& deviceId, const core::Service::Callback cb) = 0;
        virtual void gattGetPrimaryService(const String& seq, const DeviceID& deviceId, const String& service, const core::Service::Callback cb) = 0;
        virtual void gattGetPrimaryServices(const String& seq, const DeviceID& deviceId, const String& service, const core::Service::Callback cb) = 0;
        virtual void serviceGetCharacteristic(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const core::Service::Callback cb) = 0;
        virtual void serviceGetCharacteristics(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const core::Service::Callback cb) = 0;
        virtual void characteristicReadValue(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const core::Service::Callback cb) = 0;
        virtual void characteristicWriteValue(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const bytes::Buffer& value, const core::Service::Callback cb) = 0;
        virtual void characteristicStartNotifications(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const core::Service::Callback cb) = 0;
        virtual void characteristicStopNotifications(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const core::Service::Callback cb) = 0;
        virtual void chooseDevice(const String& seq, const DeviceID& deviceId, const core::Service::Callback cb) = 0;
        virtual void cancelRequest(const String& seq, const core::Service::Callback cb) = 0;
        virtual void deviceWatchAdvertisements(const String& seq, const DeviceID& deviceId, const core::Service::Callback cb) = 0;
        virtual void deviceForget(const String& seq, const DeviceID& deviceId, const core::Service::Callback cb) = 0;
        virtual void restartDiscovery(const String& seq, const core::Service::Callback cb) = 0;
      };

      Bluetooth (const Options& options)
        : core::Service(options) {}
      ~Bluetooth () override = default;

      // Backend factory hook (platform TU may provide an implementation)
      UniquePointer<Backend> backend;

      // Availability
      void getAvailability(const String& seq, const Callback cb) const;
      void getDevices(const String& seq, const Callback cb);

      // Spec-aligned entry points (scaffold)
      void requestDevice(const String& seq, const JSON::Any& options, const Callback cb);

      // GATT server
      void gattConnect(const String& seq, const DeviceID& deviceId, const Callback cb);
      void gattDisconnect(const String& seq, const DeviceID& deviceId, const Callback cb);
      void gattGetPrimaryService(const String& seq, const DeviceID& deviceId, const String& service, const Callback cb);
      void gattGetPrimaryServices(const String& seq, const DeviceID& deviceId, const String& service, const Callback cb);

      // Services / Characteristics
      void serviceGetCharacteristic(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb);
      void serviceGetCharacteristics(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb);
      void characteristicReadValue(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb);
      void characteristicWriteValue(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const bytes::Buffer& value, const Callback cb);
      void characteristicStartNotifications(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb);
      void characteristicStopNotifications(const String& seq, const DeviceID& deviceId, const String& service, const String& characteristic, const Callback cb);

      // Chooser control
      void chooseDevice(const String& seq, const DeviceID& deviceId, const Callback cb);
      void cancelRequest(const String& seq, const Callback cb);
      void restartDiscovery(const String& seq, const Callback cb);

      // Device helpers
      void deviceWatchAdvertisements(const String& seq, const DeviceID& deviceId, const Callback cb);
      void deviceForget(const String& seq, const DeviceID& deviceId, const Callback cb);

      static bool parseRequestDeviceOptions(
        const JSON::Any& options,
        ParsedRequestDeviceOptions& out,
        JSON::Object* error = nullptr,
        ParsedRequestDeviceOptions::ServiceMatchMode defaultMatchMode = ParsedRequestDeviceOptions::ServiceMatchMode::All
      );
      static String normalizeUUID(const String& uuid);
      static bool filterMatches(const DeviceFilter& filter, const String& name, const Vector<String>& services, const Map<uint16_t, Vector<uint8_t>>& manufacturerData, ParsedRequestDeviceOptions::ServiceMatchMode mode);
      static bool anyFilterMatches(const ParsedRequestDeviceOptions& options, const String& name, const Vector<String>& services, const Map<uint16_t, Vector<uint8_t>>& manufacturerData);

      // Notification observers
      struct CharObserver {
        uint64_t id;
        DeviceID deviceId;
        ServiceID serviceId;
        CharID characteristicId;
        Function<void(const JSON::Any&)> callback;
      };

      uint64_t addCharacteristicObserver(const DeviceID&, const ServiceID&, const CharID&, Function<void(const JSON::Any&)> cb);
      bool removeCharacteristicObserver(uint64_t id);
      size_t removeCharacteristicObserversFor(const DeviceID&, const ServiceID&, const CharID&);
      void notifyCharacteristicValue(const DeviceID&, const ServiceID&, const CharID&, const bytes::Buffer& value);

    private:
      Mutex observersMutex;
      Vector<CharObserver> observers;
      Atomic<uint64_t> nextObserverId {1};
  };
}
#endif
