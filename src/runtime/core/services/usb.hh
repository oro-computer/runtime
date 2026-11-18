#ifndef ORO_RUNTIME_CORE_SERVICES_USB_H
#define ORO_RUNTIME_CORE_SERVICES_USB_H

#include "../../core.hh"
#include "../../bytes.hh"

namespace oro::runtime::core::services {
  class USB : public core::Service {
    public:
      using Callback = core::Service::Callback;

      struct Request {
        String sequence;
        uint64_t client;
        int window;
        operator const String& () const { return sequence; }
      };

      struct RequestDeviceFilter {
        std::optional<String> serialNumber;
        bool hasVendorId = false;
        uint16_t vendorId = 0;
        bool hasProductId = false;
        uint16_t productId = 0;
        bool hasClassCode = false;
        uint8_t classCode = 0;
        bool hasSubclassCode = false;
        uint8_t subclassCode = 0;
        bool hasProtocolCode = false;
        uint8_t protocolCode = 0;
      };

      struct RequestDeviceOptions {
        bool acceptAllDevices = false;
        Vector<RequestDeviceFilter> filters;
        Vector<RequestDeviceFilter> exclusionFilters;
      };

      struct DeviceSelection {
        String deviceId;
      };

      struct Backend : public std::enable_shared_from_this<Backend> {
        struct InterfaceDescriptor {
          uint8_t configurationValue = 0;
          JSON::Array endpoints;
          String interfaceName;
          uint8_t interfaceNumber = 0;
          uint8_t alternateSetting = 0;
          uint8_t classCode = 0;
          uint8_t subclassCode = 0;
          uint8_t protocolCode = 0;
        };

        struct DeviceDescriptor {
          String deviceId;
          uint8_t busNumber = 0;
          uint8_t deviceAddress = 0;
          uint16_t vendorId = 0;
          uint16_t productId = 0;
          uint8_t classCode = 0;
          uint8_t subclassCode = 0;
          uint8_t protocolCode = 0;
          String productName;
          String manufacturerName;
          String serialNumber;
          uint16_t usbVersion = 0;
          uint16_t deviceVersion = 0;
          uint8_t activeConfiguration = 0;
          Vector<uint8_t> configurations;
          JSON::Object configurationNames;
          bool authorized = false;
          Vector<InterfaceDescriptor> interfaces;
        };

        struct EnumerateResult {
          bool ok = false;
          JSON::Object error;
          Vector<DeviceDescriptor> devices;
        };

        virtual ~Backend() = default;
        virtual void disconnectClient (uint64_t client) {}
        virtual void getDevices(const Request& seq, const Callback cb) = 0;
        virtual void requestDevice(const Request& seq, const RequestDeviceOptions& options, const Callback cb) = 0;
        virtual void forgetDevice(const Request& seq, const String& deviceId, const Callback cb) = 0;
        virtual void open(const Request& seq, const String& deviceId, const Callback cb) = 0;
        virtual void close(const Request& seq, const String& deviceId, const Callback cb) = 0;
        virtual void selectConfiguration(const Request& seq, const String& deviceId, uint8_t configurationValue, const Callback cb) = 0;
        virtual void claimInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) = 0;
        virtual void releaseInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) = 0;
        virtual void selectAlternateInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, uint8_t alternateSetting, const Callback cb) = 0;
        virtual void controlTransferIn(const Request& seq, const String& deviceId, const JSON::Any& setup, uint32_t length, const Callback cb) = 0;
        virtual void controlTransferOut(const Request& seq, const String& deviceId, const JSON::Any& setup, const bytes::Buffer& data, const Callback cb) = 0;
        virtual void transferIn(const Request& seq, const String& deviceId, uint8_t endpointNumber, uint32_t length, const Callback cb) = 0;
        virtual void transferOut(const Request& seq, const String& deviceId, uint8_t endpointNumber, const bytes::Buffer& data, const Callback cb) = 0;
        virtual void clearHalt(const Request& seq, const String& deviceId, uint8_t endpointNumber, bool directionIn, const Callback cb) = 0;
        virtual void isochronous (const Request& request, const String&, uint8_t, bool, const Vector<uint32_t>&, const bytes::Buffer&, const Callback cb) {
          cb(request, JSON::Object::Entries{{"err", JSON::Object::Entries{{"type", "NotSupportedError"}, {"message", "Isochronous transfers unavailable"}}}}, {});
        }
        virtual void reset(const Request& seq, const String& deviceId, const Callback cb) = 0;
        virtual void enumerateDevices(Function<void(const EnumerateResult&)> completion) = 0;
        virtual bool authorizeDevice(const String& deviceId) = 0;
        virtual bool isDeviceAuthorized(const String& deviceId) const = 0;
        virtual void revokeDevice(const String& deviceId) = 0;
        virtual void chooseDevice(const Request& seq, const DeviceSelection& selection, const Callback cb) = 0;
        virtual void cancelRequest(const Request& seq, const Callback cb) = 0;
      };

      explicit USB(const Options& options)
        : core::Service(options)
      {}

      ~USB() override = default;

      static bool allowsOrigin (const URL&, const Map<String, String>&);
      void disconnectClient (uint64_t client);
      void watch (const Request&, const Callback);
      bool start() override;
      bool pause ();
      bool stop() override;

      void getDevices(const Request& seq, const Callback cb);
      void requestDevice(const Request& seq, const JSON::Any& options, const Callback cb);
      void forgetDevice(const Request& seq, const String& deviceId, const Callback cb);
      void open(const Request& seq, const String& deviceId, const Callback cb);
      void close(const Request& seq, const String& deviceId, const Callback cb);
      void selectConfiguration(const Request& seq, const String& deviceId, uint8_t configurationValue, const Callback cb);
      void claimInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb);
      void releaseInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb);
      void selectAlternateInterface(const Request& seq, const String& deviceId, uint8_t interfaceNumber, uint8_t alternateSetting, const Callback cb);
      void controlTransferIn(const Request& seq, const String& deviceId, const JSON::Any& setup, uint32_t length, const Callback cb);
      void controlTransferOut(const Request& seq, const String& deviceId, const JSON::Any& setup, const bytes::Buffer& data, const Callback cb);
      void transferIn(const Request& seq, const String& deviceId, uint8_t endpointNumber, uint32_t length, const Callback cb);
      void transferOut(const Request& seq, const String& deviceId, uint8_t endpointNumber, const bytes::Buffer& data, const Callback cb);
      void clearHalt(const Request& seq, const String& deviceId, uint8_t endpointNumber, bool directionIn, const Callback cb);
      void isochronous (const Request&, const String&, uint8_t, bool, const Vector<uint32_t>&, const bytes::Buffer&, const Callback);
      void reset(const Request& seq, const String& deviceId, const Callback cb);
      void chooseDevice(const Request& seq, const DeviceSelection& selection, const Callback cb);
      void cancelRequest(const Request& seq, const Callback cb);

    private:
      SharedPointer<Backend> backend;
      Mutex backendMutex;
      uint64_t monitor = 0;
      enum class State : uint8_t { Running, Paused, Stopped };
      State state = State::Running;
      void startMonitor ();
      void stopMonitor ();
      SharedPointer<Backend> ensureBackend();
      SharedPointer<Backend> requireBackend(const Request& seq, const Callback& cb);
      bool serviceEnabled(const Request& seq, const Callback& cb) const;
  };

  JSON::Object descriptorToJson (const USB::Backend::DeviceDescriptor&);
  bool isBlockedUSBDevice (uint16_t vendor, uint16_t product);
  bool isProtectedUSBClass (uint8_t code);
  bool filterMatches(const USB::RequestDeviceFilter& filter, const USB::Backend::DeviceDescriptor& descriptor);
}

#endif
