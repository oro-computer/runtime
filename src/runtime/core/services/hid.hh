#ifndef ORO_RUNTIME_CORE_SERVICES_HID_H
#define ORO_RUNTIME_CORE_SERVICES_HID_H

#include "../../core.hh"
#include "../../bytes.hh"

namespace oro::runtime::core::services {
  class HID : public core::Service {
    public:
      using Callback = core::Service::Callback;

      struct RequestDeviceFilter {
        bool hasVendorId = false;
        uint16_t vendorId = 0;
        bool hasProductId = false;
        uint16_t productId = 0;
        bool hasUsagePage = false;
        uint16_t usagePage = 0;
        bool hasUsage = false;
        uint16_t usage = 0;
      };

      struct RequestDeviceOptions {
        bool acceptAllDevices = false;
        Vector<RequestDeviceFilter> filters;
      };

      struct DeviceSelection {
        String deviceId;
      };

      struct Backend {
        struct ReportInfo {
          uint8_t reportId = 0;
          uint16_t size = 0;
        };

        struct CollectionInfo {
          uint16_t usagePage = 0;
          uint16_t usage = 0;
          String type;
          Vector<ReportInfo> inputReports;
          Vector<ReportInfo> outputReports;
          Vector<ReportInfo> featureReports;
          bool usesInputReportId = false;
          bool usesOutputReportId = false;
          bool usesFeatureReportId = false;
        };

        struct DeviceDescriptor {
          String deviceId;
          uint16_t vendorId = 0;
          uint16_t productId = 0;
          String productName;
          String manufacturerName;
          String serialNumber;
          bool opened = false;
          bool authorized = false;
          Vector<CollectionInfo> collections;
        };

        struct EnumerateResult {
          bool ok = false;
          JSON::Object error;
          Vector<DeviceDescriptor> devices;
        };

        virtual ~Backend() = default;
        virtual void getDevices(const String& seq, const Callback cb) = 0;
        virtual void requestDevice(const String& seq, const RequestDeviceOptions& options, const Callback cb) = 0;
        virtual void chooseDevice(const String& seq, const DeviceSelection& selection, const Callback cb) = 0;
        virtual void cancelRequest(const String& seq, const Callback cb) = 0;
        virtual void forgetDevice(const String& seq, const String& deviceId, const Callback cb) = 0;
        virtual void open(const String& seq, const String& deviceId, const Callback cb) = 0;
        virtual void close(const String& seq, const String& deviceId, const Callback cb) = 0;
        virtual void sendReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) = 0;
        virtual void sendFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) = 0;
        virtual void receiveFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, uint16_t length, const Callback cb) = 0;
        virtual bool authorizeDevice(const String& deviceId) = 0;
        virtual bool isDeviceAuthorized(const String& deviceId) const = 0;
        virtual void revokeDevice(const String& deviceId) = 0;
        virtual void enumerateDevices(Function<void(const EnumerateResult&)> completion) = 0;
      };

      explicit HID(const Options& options)
        : core::Service(options)
      {}

      ~HID() override = default;

      bool start() override;
      bool stop() override;

      void getDevices(const String& seq, const Callback cb);
      void requestDevice(const String& seq, const JSON::Any& options, const Callback cb);
      void chooseDevice(const String& seq, const DeviceSelection& selection, const Callback cb);
      void cancelRequest(const String& seq, const Callback cb);
      void forgetDevice(const String& seq, const String& deviceId, const Callback cb);
      void open(const String& seq, const String& deviceId, const Callback cb);
      void close(const String& seq, const String& deviceId, const Callback cb);
      void sendReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb);
      void sendFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb);
      void receiveFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, uint16_t length, const Callback cb);

    private:
      UniquePointer<Backend> backend;
      Backend* ensureBackend();
      Backend* requireBackend(const String& seq, const Callback& cb);
      bool serviceEnabled(const String& seq, const Callback& cb) const;
  };

  bool filterMatches(const HID::RequestDeviceFilter& filter, const HID::Backend::DeviceDescriptor& descriptor);
}

#endif
