#ifndef ORO_RUNTIME_CORE_SERVICES_USB_H
#define ORO_RUNTIME_CORE_SERVICES_USB_H

#include "../../core.hh"
#include "../../bytes.hh"

namespace oro::runtime::core::services {
  class USB : public core::Service {
    public:
      using Callback = core::Service::Callback;

      struct RequestDeviceFilter {
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
      };

      struct DeviceSelection {
        String deviceId;
      };

      struct Backend {
        struct InterfaceDescriptor {
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
          bool opened = false;
          bool authorized = false;
          Vector<InterfaceDescriptor> interfaces;
        };

        struct EnumerateResult {
          bool ok = false;
          JSON::Object error;
          Vector<DeviceDescriptor> devices;
        };

        virtual ~Backend() = default;
        virtual void getDevices(const String& seq, const Callback cb) = 0;
        virtual void requestDevice(const String& seq, const RequestDeviceOptions& options, const Callback cb) = 0;
        virtual void forgetDevice(const String& seq, const String& deviceId, const Callback cb) = 0;
        virtual void open(const String& seq, const String& deviceId, const Callback cb) = 0;
        virtual void close(const String& seq, const String& deviceId, const Callback cb) = 0;
        virtual void selectConfiguration(const String& seq, const String& deviceId, uint8_t configurationValue, const Callback cb) = 0;
        virtual void claimInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) = 0;
        virtual void releaseInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) = 0;
        virtual void selectAlternateInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, uint8_t alternateSetting, const Callback cb) = 0;
        virtual void controlTransferIn(const String& seq, const String& deviceId, const JSON::Any& setup, uint32_t length, const Callback cb) = 0;
        virtual void controlTransferOut(const String& seq, const String& deviceId, const JSON::Any& setup, const bytes::Buffer& data, const Callback cb) = 0;
        virtual void transferIn(const String& seq, const String& deviceId, uint8_t endpointNumber, uint32_t length, const Callback cb) = 0;
        virtual void transferOut(const String& seq, const String& deviceId, uint8_t endpointNumber, const bytes::Buffer& data, const Callback cb) = 0;
        virtual void clearHalt(const String& seq, const String& deviceId, uint8_t endpointNumber, bool directionIn, const Callback cb) = 0;
        virtual void reset(const String& seq, const String& deviceId, const Callback cb) = 0;
        virtual void enumerateDevices(Function<void(const EnumerateResult&)> completion) = 0;
        virtual bool authorizeDevice(const String& deviceId) = 0;
        virtual bool isDeviceAuthorized(const String& deviceId) const = 0;
        virtual void revokeDevice(const String& deviceId) = 0;
        virtual void chooseDevice(const String& seq, const DeviceSelection& selection, const Callback cb) = 0;
        virtual void cancelRequest(const String& seq, const Callback cb) = 0;
      };

      explicit USB(const Options& options)
        : core::Service(options)
      {}

      ~USB() override = default;

      bool start() override;
      bool stop() override;

      void getDevices(const String& seq, const Callback cb);
      void requestDevice(const String& seq, const JSON::Any& options, const Callback cb);
      void forgetDevice(const String& seq, const String& deviceId, const Callback cb);
      void open(const String& seq, const String& deviceId, const Callback cb);
      void close(const String& seq, const String& deviceId, const Callback cb);
      void selectConfiguration(const String& seq, const String& deviceId, uint8_t configurationValue, const Callback cb);
      void claimInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb);
      void releaseInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb);
      void selectAlternateInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, uint8_t alternateSetting, const Callback cb);
      void controlTransferIn(const String& seq, const String& deviceId, const JSON::Any& setup, uint32_t length, const Callback cb);
      void controlTransferOut(const String& seq, const String& deviceId, const JSON::Any& setup, const bytes::Buffer& data, const Callback cb);
      void transferIn(const String& seq, const String& deviceId, uint8_t endpointNumber, uint32_t length, const Callback cb);
      void transferOut(const String& seq, const String& deviceId, uint8_t endpointNumber, const bytes::Buffer& data, const Callback cb);
      void clearHalt(const String& seq, const String& deviceId, uint8_t endpointNumber, bool directionIn, const Callback cb);
      void reset(const String& seq, const String& deviceId, const Callback cb);
      void chooseDevice(const String& seq, const DeviceSelection& selection, const Callback cb);
      void cancelRequest(const String& seq, const Callback cb);

    private:
      UniquePointer<Backend> backend;
      Backend* ensureBackend();
      Backend* requireBackend(const String& seq, const Callback& cb);
      bool serviceEnabled(const String& seq, const Callback& cb) const;
  };

  bool filterMatches(const USB::RequestDeviceFilter& filter, const USB::Backend::DeviceDescriptor& descriptor);
}

#endif
