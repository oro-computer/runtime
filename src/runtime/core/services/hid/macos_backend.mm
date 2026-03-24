#if defined(__APPLE__)
#include "macos_backend.hh"

#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDManager.h>

#include "../../debug.hh"
#include "../../app.hh"
#include "../../bytes.hh"
#include "../../platform/types.hh"

#include <pthread.h>
#include <utility>

#include <dispatch/dispatch.h>

namespace oro::runtime::core::services::hid {
  namespace {
    using namespace oro::runtime;

    template <class Fn>
    void runOnMain(Fn&& fn) {
      if (pthread_main_np()) {
        fn();
        return;
      }
      dispatch_sync(dispatch_get_main_queue(), ^{
        fn();
      });
    }

    String cfStringToString(CFStringRef value) {
      if (!value) return String("");
      const CFIndex length = CFStringGetLength(value);
      const CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
      Vector<char> buffer(static_cast<size_t>(maxSize));
      if (!CFStringGetCString(value, buffer.data(), maxSize, kCFStringEncodingUTF8)) {
        return String("");
      }
      return String(buffer.data());
    }

    bool getUInt(CFDictionaryRef dict, CFStringRef key, uint32_t& out) {
      if (!dict || !key) return false;
      auto value = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, key));
      if (!value) return false;
      int64_t tmp = 0;
      if (!CFNumberGetValue(value, kCFNumberSInt64Type, &tmp)) return false;
      out = static_cast<uint32_t>(tmp);
      return true;
    }

    uint32_t getUInt(IOHIDDeviceRef device, CFStringRef key, uint32_t fallback = 0) {
      if (!device || !key) return fallback;
      auto property = static_cast<CFNumberRef>(IOHIDDeviceGetProperty(device, key));
      if (!property) return fallback;
      int64_t tmp = 0;
      if (!CFNumberGetValue(property, kCFNumberSInt64Type, &tmp)) return fallback;
      return static_cast<uint32_t>(tmp);
    }

    String makeDeviceId(IOHIDDeviceRef device) {
      auto uniqueId = static_cast<CFStringRef>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDUniqueIDKey)));
      if (uniqueId) return cfStringToString(uniqueId);
      const uint32_t vendor = getUInt(device, CFSTR(kIOHIDVendorIDKey));
      const uint32_t product = getUInt(device, CFSTR(kIOHIDProductIDKey));
      const uint32_t location = getUInt(device, CFSTR(kIOHIDLocationIDKey));
      return String(std::to_string(vendor) + ":" + std::to_string(product) + ":" + std::to_string(location));
    }

    void addReport(Vector<HID::Backend::ReportInfo>& list, uint8_t reportId, uint16_t size, bool& usesReportId) {
      for (auto& entry : list) {
        if (entry.reportId == reportId) {
          if (size > entry.size) entry.size = size;
          return;
        }
      }
      HID::Backend::ReportInfo info;
      info.reportId = reportId;
      info.size = size;
      list.push_back(info);
      if (reportId != 0) {
        usesReportId = true;
      }
    }

    bool isInputElement(IOHIDElementType type) {
      return type == kIOHIDElementTypeInput_Misc || type == kIOHIDElementTypeInput_Button || type == kIOHIDElementTypeInput_Axis || type == kIOHIDElementTypeInput_ScanCodes;
    }

    bool isOutputElement(IOHIDElementType type) {
      return type == kIOHIDElementTypeOutput || type == kIOHIDElementTypeOutput_Misc;
    }

    bool isFeatureElement(IOHIDElementType type) {
      return type == kIOHIDElementTypeFeature;
    }

    void collectReportDescriptors(IOHIDDeviceRef device,
      Vector<HID::Backend::ReportInfo>& inputReports,
      Vector<HID::Backend::ReportInfo>& outputReports,
      Vector<HID::Backend::ReportInfo>& featureReports,
      bool& usesInputReportId,
      bool& usesOutputReportId,
      bool& usesFeatureReportId) {
      usesInputReportId = false;
      usesOutputReportId = false;
      usesFeatureReportId = false;
      CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, nullptr, kIOHIDOptionsTypeNone);
      if (!elements) return;
      const CFIndex count = CFArrayGetCount(elements);
      for (CFIndex idx = 0; idx < count; ++idx) {
        auto element = static_cast<IOHIDElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(elements, idx)));
        if (!element) continue;
        const IOHIDElementType type = IOHIDElementGetType(element);
        const bool isInput = isInputElement(type);
        const bool isOutput = isOutputElement(type);
        const bool isFeature = isFeatureElement(type);
        if (!isInput && !isOutput && !isFeature) continue;

        const uint32_t reportId = IOHIDElementGetReportID(element);
        const uint32_t reportCount = IOHIDElementGetReportCount(element);
        const uint32_t reportSizeBits = IOHIDElementGetReportSize(element) * (reportCount == 0 ? 1 : reportCount);
        if (reportSizeBits == 0) continue;
        const uint16_t sizeBytes = static_cast<uint16_t>((reportSizeBits + 7u) / 8u);

        if (isInput) {
          addReport(inputReports, static_cast<uint8_t>(reportId & 0xFFu), sizeBytes, usesInputReportId);
        } else if (isOutput) {
          addReport(outputReports, static_cast<uint8_t>(reportId & 0xFFu), sizeBytes, usesOutputReportId);
        } else if (isFeature) {
          addReport(featureReports, static_cast<uint8_t>(reportId & 0xFFu), sizeBytes, usesFeatureReportId);
        }
      }
      CFRelease(elements);
    }

    HID::Backend::CollectionInfo makeCollection(IOHIDDeviceRef device,
      const Vector<HID::Backend::ReportInfo>& inputReports,
      const Vector<HID::Backend::ReportInfo>& outputReports,
      const Vector<HID::Backend::ReportInfo>& featureReports,
      bool usesInputReportId,
      bool usesOutputReportId,
      bool usesFeatureReportId) {
      HID::Backend::CollectionInfo info;
      info.usagePage = static_cast<uint16_t>(getUInt(device, CFSTR(kIOHIDPrimaryUsagePageKey)) & 0xFFFFu);
      info.usage = static_cast<uint16_t>(getUInt(device, CFSTR(kIOHIDPrimaryUsageKey)) & 0xFFFFu);
      info.type = "application";
      info.inputReports = inputReports;
      info.outputReports = outputReports;
      info.featureReports = featureReports;
      info.usesInputReportId = usesInputReportId;
      info.usesOutputReportId = usesOutputReportId;
      info.usesFeatureReportId = usesFeatureReportId;
      info.children = {};
      return info;
    }

    bytes::Buffer makeReportPayload(const uint8_t* data, size_t size, size_t offset) {
      if (!data || size <= offset) return bytes::Buffer();
      bytes::Buffer buffer(size - offset);
      memcpy(buffer.data(), data + offset, buffer.size());
      return buffer;
    }

    struct CFDevice {
      IOHIDDeviceRef ref = nullptr;

      CFDevice() = default;

      explicit CFDevice(IOHIDDeviceRef device) : ref(device) {
        if (this->ref) CFRetain(this->ref);
      }

      CFDevice(const CFDevice& other) : ref(other.ref) {
        if (this->ref) CFRetain(this->ref);
      }

      CFDevice& operator = (const CFDevice& other) {
        if (this == &other) return *this;
        if (this->ref) CFRelease(this->ref);
        this->ref = other.ref;
        if (this->ref) CFRetain(this->ref);
        return *this;
      }

      ~CFDevice() {
        if (this->ref) {
          CFRelease(this->ref);
          this->ref = nullptr;
        }
      }
    };

    struct DeviceRecord {
      HID::Backend::DeviceDescriptor descriptor;
      CFDevice device;
      bool opened = false;
      bool authorized = false;
      Vector<uint8_t> inputBuffer;
      bool inputRegistered = false;
      bool usesInputReportId = false;
      bool usesOutputReportId = false;
      bool usesFeatureReportId = false;
    };

    struct RequestState {
      HID::RequestDeviceOptions options;
      Vector<HID::Backend::DeviceDescriptor> pending;
    };

    class MacHIDBackend final : public HID::Backend {
      public:
        explicit MacHIDBackend(HID& svc)
          : service(svc) {
          this->createManager();
        }

        ~MacHIDBackend() override {
          this->shutdown();
        }

        bool ready() const {
          return this->manager != nullptr;
        }

        void getDevices(const String& seq, const Callback cb) override {
          this->enumerateDevices([=](const EnumerateResult& result) {
            if (!result.ok) {
              cb(seq, result.error, QueuedResponse{});
              return;
            }
            JSON::Array::Entries entries;
            for (const auto& descriptor : result.devices) {
              if (!descriptor.authorized) continue;
              entries.push_back(this->descriptorToJson(descriptor));
            }
            JSON::Object json = JSON::Object::Entries {{
              {"data", JSON::Object::Entries {{
                {"devices", entries}
              }}}
            }};
            cb(seq, json, QueuedResponse{});
          });
        }

        void requestDevice(const String& seq, const HID::RequestDeviceOptions& options, const Callback cb) override {
          this->enumerateDevices([=](const EnumerateResult& result) {
            if (!result.ok) {
              cb(seq, result.error, QueuedResponse{});
              return;
            }

            Vector<HID::Backend::DeviceDescriptor> matches;
            for (auto descriptor : result.devices) {
              if (!options.acceptAllDevices) {
                bool matched = false;
                for (const auto& filter : options.filters) {
                  if (filterMatches(filter, descriptor)) {
                    matched = true;
                    break;
                  }
                }
                if (!matched) continue;
              }
              matches.push_back(descriptor);
            }

            if (matches.empty()) {
              cb(seq, makeError("NotFoundError", "No HID devices matched the requested filters"), QueuedResponse{});
              return;
            }

            if (matches.size() == 1) {
              auto descriptor = matches.front();
              this->authorizeDevice(descriptor.deviceId);
              descriptor.authorized = true;
              cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
              return;
            }

            JSON::Array::Entries devicesJson;
            for (const auto& descriptor : matches) {
              devicesJson.push_back(this->descriptorToJson(descriptor));
            }

            {
              Lock lock(this->mutex);
              this->pendingRequest = RequestState{ options, matches };
            }

            JSON::Object json = JSON::Object::Entries {{
              {"data", JSON::Object::Entries {{
                {"requiresSelection", true},
                {"devices", devicesJson}
              }}}
            }};
            cb(seq, json, QueuedResponse{});
          });
        }

        void chooseDevice(const String& seq, const DeviceSelection& selection, const Callback cb) override {
          RequestState state;
          {
            Lock lock(this->mutex);
            if (!this->pendingRequest.has_value()) {
              cb(seq, makeError("InvalidStateError", "No pending hid.requestDevice"), QueuedResponse{});
              return;
            }
            state = *this->pendingRequest;
            this->pendingRequest.reset();
          }

          auto it = std::find_if(state.pending.begin(), state.pending.end(), [&](const HID::Backend::DeviceDescriptor& descriptor) {
            return descriptor.deviceId == selection.deviceId;
          });

          if (it == state.pending.end()) {
            cb(seq, makeError("NotFoundError", String("Unknown HID device ") + selection.deviceId), QueuedResponse{});
            return;
          }

          auto descriptor = *it;
          this->authorizeDevice(descriptor.deviceId);
          descriptor.authorized = true;
          cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
        }

        void cancelRequest(const String& seq, const Callback cb) override {
          {
            Lock lock(this->mutex);
            this->pendingRequest.reset();
          }
          cb(seq, makeOk(), QueuedResponse{});
        }

        void forgetDevice(const String& seq, const String& deviceId, const Callback cb) override {
          this->revokeDevice(deviceId);
          {
            Lock lock(this->mutex);
            auto openIt = this->openDevices.find(deviceId);
            if (openIt != this->openDevices.end()) {
              this->stopInput(openIt->second);
              this->closeDeviceUnlocked(deviceId, openIt->second);
              this->openDevices.erase(openIt);
            }
          }
          cb(seq, makeOk(), QueuedResponse{});
        }

        void open(const String& seq, const String& deviceId, const Callback cb) override {
          JSON::Object error;
          HID::Backend::DeviceDescriptor descriptor;
          if (!this->openDevice(deviceId, descriptor, error)) {
            cb(seq, error, QueuedResponse{});
            return;
          }
          cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
        }

        void close(const String& seq, const String& deviceId, const Callback cb) override {
          HID::Backend::DeviceDescriptor descriptor;
          {
            Lock lock(this->mutex);
            auto it = this->openDevices.find(deviceId);
            if (it != this->openDevices.end()) {
              this->stopInput(it->second);
              this->closeDeviceUnlocked(deviceId, it->second);
              this->openDevices.erase(it);
            }
            auto devIt = this->devices.find(deviceId);
            if (devIt != this->devices.end()) {
              devIt->second.opened = false;
              descriptor = devIt->second.descriptor;
              descriptor.opened = false;
            }
          }
          cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
        }

        void sendReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) override {
          JSON::Object error;
          if (!this->sendReportInternal(deviceId, reportId, data, kIOHIDReportTypeOutput, error)) {
            cb(seq, error, QueuedResponse{});
            return;
          }
          cb(seq, makeOk(), QueuedResponse{});
        }

        void sendFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) override {
          JSON::Object error;
          if (!this->sendReportInternal(deviceId, reportId, data, kIOHIDReportTypeFeature, error)) {
            cb(seq, error, QueuedResponse{});
            return;
          }
          cb(seq, makeOk(), QueuedResponse{});
        }

        void receiveFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, uint16_t length, const Callback cb) override {
          JSON::Object error;
          JSON::Object response = this->receiveReportInternal(deviceId, reportId, length, error);
          if (error.has("err")) {
            cb(seq, error, QueuedResponse{});
            return;
          }
          cb(seq, response, QueuedResponse{});
        }

        bool authorizeDevice(const String& deviceId) override {
          Lock lock(this->mutex);
          auto it = this->devices.find(deviceId);
          if (it != this->devices.end()) {
            it->second.authorized = true;
            it->second.descriptor.authorized = true;
          }
          this->authorized.insert(deviceId);
          return true;
        }

        bool isDeviceAuthorized(const String& deviceId) const override {
          Lock lock(this->mutex);
          return this->authorized.count(deviceId) > 0;
        }

        void revokeDevice(const String& deviceId) override {
          Lock lock(this->mutex);
          this->authorized.erase(deviceId);
          auto it = this->devices.find(deviceId);
          if (it != this->devices.end()) {
            it->second.authorized = false;
            it->second.descriptor.authorized = false;
          }
        }

        void enumerateDevices(Function<void(const EnumerateResult&)> completion) override {
          EnumerateResult result;
          if (!this->manager) {
            result.error = makeError("NotSupportedError", "IOHIDManager unavailable");
            completion(result);
            return;
          }

          CFSetRef set = IOHIDManagerCopyDevices(this->manager);
          if (!set) {
            result.ok = true;
            completion(result);
            return;
          }

          Vector<DeviceRecord> records;
          const CFIndex count = CFSetGetCount(set);
          std::vector<IOHIDDeviceRef> devicesVec(static_cast<size_t>(count));
          CFSetGetValues(set, reinterpret_cast<const void**>(devicesVec.data()));

          for (IOHIDDeviceRef device : devicesVec) {
            if (!device) continue;
            DeviceRecord record;
            record.device = CFDevice(device);
            bool usesInputId = false;
            bool usesOutputId = false;
            bool usesFeatureId = false;
            this->populateDescriptor(device, record.descriptor, usesInputId, usesOutputId, usesFeatureId);
            record.usesInputReportId = usesInputId;
            record.usesOutputReportId = usesOutputId;
            record.usesFeatureReportId = usesFeatureId;
            {
              Lock lock(this->mutex);
              auto it = this->devices.find(record.descriptor.deviceId);
              if (it != this->devices.end()) {
                record.authorized = it->second.authorized;
                record.opened = it->second.opened;
                if (record.authorized) record.descriptor.authorized = true;
                if (record.opened) record.descriptor.opened = true;
              } else {
                record.descriptor.authorized = this->authorized.count(record.descriptor.deviceId) > 0;
              }
              this->devices[record.descriptor.deviceId] = record;
            }
            records.push_back(record);
          }

          CFRelease(set);

          result.ok = true;
          for (auto& record : records) {
            result.devices.push_back(record.descriptor);
          }
          completion(result);
        }

      private:
        HID& service;
        IOHIDManagerRef manager = nullptr;
        mutable Mutex mutex;
        Map<String, DeviceRecord> devices;
        Map<String, DeviceRecord> openDevices;
        Set<String> authorized;
        std::optional<RequestState> pendingRequest;

        JSON::Object makeOk() const {
          return JSON::Object::Entries {{ "data", JSON::Object::Entries {{ "ok", true }} }};
        }

        JSON::Object makeError(const char* type, const String& message) const {
          return JSON::Object::Entries {{
            {"err", JSON::Object::Entries {{
              {"type", String(type)},
              {"message", message}
            }}}
          }};
        }

        JSON::Object makeDeviceResponse(const HID::Backend::DeviceDescriptor& descriptor) const {
          return JSON::Object::Entries {{ "data", JSON::Object::Entries {{ "device", this->descriptorToJson(descriptor) }} }};
        }

        JSON::Object descriptorToJson(const HID::Backend::DeviceDescriptor& descriptor) const {
          JSON::Array::Entries collections;
          for (const auto& collection : descriptor.collections) {
            JSON::Object json = JSON::Object::Entries {{
              {"usagePage", static_cast<uint32_t>(collection.usagePage)},
              {"usage", static_cast<uint32_t>(collection.usage)},
              {"type", collection.type}
            }};
            JSON::Array::Entries in;
            for (const auto& r : collection.inputReports) {
              in.push_back(JSON::Object::Entries {{ {"reportId", static_cast<uint32_t>(r.reportId)}, {"size", static_cast<uint32_t>(r.size)} }});
            }
            JSON::Array::Entries out;
            for (const auto& r : collection.outputReports) {
              out.push_back(JSON::Object::Entries {{ {"reportId", static_cast<uint32_t>(r.reportId)}, {"size", static_cast<uint32_t>(r.size)} }});
            }
            JSON::Array::Entries feature;
            for (const auto& r : collection.featureReports) {
              feature.push_back(JSON::Object::Entries {{ {"reportId", static_cast<uint32_t>(r.reportId)}, {"size", static_cast<uint32_t>(r.size)} }});
            }
            json.set("inputReports", in);
            json.set("outputReports", out);
            json.set("featureReports", feature);
            json.set("usesInputReportId", collection.usesInputReportId);
            json.set("usesOutputReportId", collection.usesOutputReportId);
            json.set("usesFeatureReportId", collection.usesFeatureReportId);
            json.set("children", JSON::Array{});
            collections.push_back(json);
          }

          return JSON::Object::Entries {{
            {"deviceId", descriptor.deviceId},
            {"vendorId", static_cast<uint32_t>(descriptor.vendorId)},
            {"productId", static_cast<uint32_t>(descriptor.productId)},
            {"productName", descriptor.productName},
            {"manufacturerName", descriptor.manufacturerName},
            {"serialNumber", descriptor.serialNumber},
            {"opened", descriptor.opened},
            {"authorized", descriptor.authorized},
            {"collections", collections}
          }};
        }

        void createManager() {
          runOnMain([this]() {
            this->manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
            if (!this->manager) return;
            IOHIDManagerSetDeviceMatching(this->manager, nullptr);
            IOHIDManagerRegisterDeviceMatchingCallback(this->manager, DeviceMatched, this);
            IOHIDManagerRegisterDeviceRemovalCallback(this->manager, DeviceRemoved, this);
            IOHIDManagerScheduleWithRunLoop(this->manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            IOHIDManagerOpen(this->manager, kIOHIDOptionsTypeNone);
          });
        }

        void shutdown() {
          if (!this->manager) return;
          runOnMain([this]() {
            IOHIDManagerUnscheduleFromRunLoop(this->manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            IOHIDManagerClose(this->manager, kIOHIDOptionsTypeNone);
          });
          CFRelease(this->manager);
          this->manager = nullptr;
        }

        void populateDescriptor(IOHIDDeviceRef device,
          HID::Backend::DeviceDescriptor& descriptor,
          bool& usesInputReportId,
          bool& usesOutputReportId,
          bool& usesFeatureReportId) {
          usesInputReportId = false;
          usesOutputReportId = false;
          usesFeatureReportId = false;
          descriptor.deviceId = makeDeviceId(device);
          descriptor.vendorId = static_cast<uint16_t>(getUInt(device, CFSTR(kIOHIDVendorIDKey)) & 0xFFFFu);
          descriptor.productId = static_cast<uint16_t>(getUInt(device, CFSTR(kIOHIDProductIDKey)) & 0xFFFFu);
          descriptor.productName = cfStringToString(static_cast<CFStringRef>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey))));
          descriptor.manufacturerName = cfStringToString(static_cast<CFStringRef>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDManufacturerKey))));
          descriptor.serialNumber = cfStringToString(static_cast<CFStringRef>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDSerialNumberKey))));
          Vector<HID::Backend::ReportInfo> inputReports;
          Vector<HID::Backend::ReportInfo> outputReports;
          Vector<HID::Backend::ReportInfo> featureReports;
          collectReportDescriptors(device, inputReports, outputReports, featureReports,
            usesInputReportId, usesOutputReportId, usesFeatureReportId);

          if (inputReports.empty()) {
            const uint16_t fallback = static_cast<uint16_t>(getUInt(device, CFSTR(kIOHIDMaxInputReportSizeKey)) & 0xFFFFu);
            if (fallback) addReport(inputReports, 0, fallback, usesInputReportId);
          }
          if (outputReports.empty()) {
            const uint16_t fallback = static_cast<uint16_t>(getUInt(device, CFSTR(kIOHIDMaxOutputReportSizeKey)) & 0xFFFFu);
            if (fallback) addReport(outputReports, 0, fallback, usesOutputReportId);
          }
          if (featureReports.empty()) {
            const uint16_t fallback = static_cast<uint16_t>(getUInt(device, CFSTR(kIOHIDMaxFeatureReportSizeKey)) & 0xFFFFu);
            if (fallback) addReport(featureReports, 0, fallback, usesFeatureReportId);
          }

          descriptor.collections.clear();
          descriptor.collections.push_back(makeCollection(
            device,
            inputReports,
            outputReports,
            featureReports,
            usesInputReportId,
            usesOutputReportId,
            usesFeatureReportId
          ));
        }

        bool openDevice(const String& deviceId, HID::Backend::DeviceDescriptor& descriptor, JSON::Object& error) {
          IOHIDDeviceRef deviceRef = nullptr;
          {
            Lock lock(this->mutex);
            auto it = this->devices.find(deviceId);
            if (it == this->devices.end()) {
              error = makeError("NotFoundError", "Unknown HID device");
              return false;
            }
            if (!it->second.authorized && this->authorized.count(deviceId) == 0) {
              error = makeError("NotAllowedError", "HID device not authorized");
              return false;
            }
            if (it->second.opened) {
              descriptor = it->second.descriptor;
              descriptor.opened = true;
              return true;
            }
            deviceRef = it->second.device.ref;
          }

          if (!deviceRef) {
            error = makeError("NotFoundError", "HID device reference unavailable");
            return false;
          }

          IOReturn rc = IOHIDDeviceOpen(deviceRef, kIOHIDOptionsTypeNone);
          if (rc != kIOReturnSuccess) {
            error = makeError("OperationError", String("IOHIDDeviceOpen failed: ") + std::to_string(rc));
            return false;
          }

          uint32_t size = getUInt(deviceRef, CFSTR(kIOHIDMaxInputReportSizeKey));
          if (size == 0) size = 64;

          uint8_t* bufferPtr = nullptr;
          size_t bufferSize = size;

          {
            Lock lock(this->mutex);
            auto& stored = this->devices[deviceId];
            stored.opened = true;
            stored.descriptor.opened = true;
            stored.inputBuffer.assign(size, 0);
            stored.inputRegistered = false;
            this->openDevices[deviceId] = stored;
            auto& openRecord = this->openDevices[deviceId];
            bufferPtr = openRecord.inputBuffer.data();
            bufferSize = openRecord.inputBuffer.size();
            descriptor = stored.descriptor;
          }

          runOnMain([this, deviceRef, bufferPtr, bufferSize, deviceId]() {
            if (!bufferPtr || bufferSize == 0 || !deviceRef) return;
            IOHIDDeviceScheduleWithRunLoop(deviceRef, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
            IOHIDDeviceRegisterInputReportCallback(deviceRef, bufferPtr, bufferSize, InputReport, this);
            Lock lock(this->mutex);
            auto it = this->openDevices.find(deviceId);
            if (it != this->openDevices.end()) {
              it->second.inputRegistered = true;
            }
          });

          return true;
        }

        void stopInput(DeviceRecord& record) {
          if (!record.device.ref || !record.inputRegistered) return;
          IOHIDDeviceRef deviceRef = record.device.ref;
          runOnMain([deviceRef]() {
            IOHIDDeviceRegisterInputReportCallback(deviceRef, nullptr, 0, nullptr, nullptr);
          });
          record.inputRegistered = false;
        }

        void closeDeviceUnlocked(const String& deviceId, DeviceRecord& record) {
          if (record.device.ref) {
            IOHIDDeviceRef deviceRef = record.device.ref;
            runOnMain([deviceRef]() {
              IOHIDDeviceUnscheduleFromRunLoop(deviceRef, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
              IOHIDDeviceClose(deviceRef, kIOHIDOptionsTypeNone);
            });
            record.opened = false;
          }
          auto it = this->devices.find(deviceId);
          if (it != this->devices.end()) {
            it->second.opened = false;
            it->second.descriptor.opened = false;
          }
        }

        bool sendReportInternal(const String& deviceId, uint8_t reportId, const bytes::Buffer& data, IOHIDReportType type, JSON::Object& error) {
          IOHIDDeviceRef device = nullptr;
          bool usesReportId = false;
          {
            Lock lock(this->mutex);
            auto it = this->openDevices.find(deviceId);
            if (it == this->openDevices.end()) {
              error = makeError("InvalidStateError", "HID device is not open");
              return false;
            }
            device = it->second.device.ref;
            if (type == kIOHIDReportTypeFeature) {
              usesReportId = it->second.usesFeatureReportId;
            } else if (type == kIOHIDReportTypeOutput || type == kIOHIDReportTypeOutput_Misc) {
              usesReportId = it->second.usesOutputReportId;
            }
          }

          if (!device) {
            error = makeError("InvalidStateError", "HID device handle missing");
            return false;
          }

          Vector<uint8_t> payload;
          const bool expectReportId = (reportId != 0) || usesReportId;
          if (expectReportId) {
            payload.resize(1 + data.size());
            payload[0] = reportId;
            if (data.size() > 0) memcpy(payload.data() + 1, data.data(), data.size());
          } else {
            payload.assign(data.begin(), data.end());
          }

          IOReturn rc = IOHIDDeviceSetReport(device, type, reportId, payload.data(), payload.size());
          if (rc != kIOReturnSuccess) {
            error = makeError("OperationError", String("IOHIDDeviceSetReport failed: ") + std::to_string(rc));
            return false;
          }
          return true;
        }

        JSON::Object receiveReportInternal(const String& deviceId, uint8_t reportId, uint16_t length, JSON::Object& error) {
          IOHIDDeviceRef device = nullptr;
          uint32_t maxSize = length;
          bool usesReportId = false;
          {
            Lock lock(this->mutex);
            auto it = this->openDevices.find(deviceId);
            if (it == this->openDevices.end()) {
              error = makeError("InvalidStateError", "HID device is not open");
              return JSON::Object{};
            }
            device = it->second.device.ref;
            usesReportId = it->second.usesFeatureReportId;
            if (maxSize == 0) {
              maxSize = getUInt(device, CFSTR(kIOHIDMaxFeatureReportSizeKey));
              if (maxSize == 0) maxSize = 64;
            }
          }

          if (!device) {
            error = makeError("InvalidStateError", "HID device handle missing");
            return JSON::Object{};
          }

          const bool expectReportId = (reportId != 0) || usesReportId;
          Vector<uint8_t> buffer(maxSize + (expectReportId ? 1 : 0));
          CFIndex reportLength = buffer.size();
          IOReturn rc = IOHIDDeviceGetReport(device, kIOHIDReportTypeFeature, reportId, buffer.data(), &reportLength);
          if (rc != kIOReturnSuccess) {
            error = makeError("OperationError", String("IOHIDDeviceGetReport failed: ") + std::to_string(rc));
            return JSON::Object{};
          }

          uint8_t resolvedId = reportId;
          size_t offset = 0;
          if (reportLength > 0 && expectReportId) {
            resolvedId = buffer[0];
            offset = 1;
          }

          bytes::Buffer payload = makeReportPayload(buffer.data(), static_cast<size_t>(reportLength), offset);
          JSON::Object json = JSON::Object::Entries {{
            {"data", JSON::Object::Entries {{
              {"reportId", static_cast<uint32_t>(resolvedId)},
              {"encoding", String("base64")},
              {"data", bytes::base64::encode(payload)}
            }}}
          }};
          return json;
        }

        void handleInput(IOHIDDeviceRef device, uint32_t reportId, uint8_t* report, CFIndex reportLength) {
          if (!device || !report || reportLength <= 0) return;
          String deviceId = makeDeviceId(device);
          bool usesReportId = false;
          {
            Lock lock(this->mutex);
            auto it = this->devices.find(deviceId);
            if (it != this->devices.end()) {
              usesReportId = it->second.usesInputReportId;
            }
          }
          size_t offset = 0;
          if (reportLength > 0) {
            const bool expectReportId = (reportId != 0) || usesReportId;
            if (expectReportId) {
              reportId = report[0];
              offset = 1;
            }
          }

          bytes::Buffer payload = makeReportPayload(report, static_cast<size_t>(reportLength), offset);
          JSON::Object event = JSON::Object::Entries {{
            {"deviceId", deviceId},
            {"reportId", static_cast<uint32_t>(reportId)},
            {"encoding", String("base64")},
            {"data", bytes::base64::encode(payload)}
          }};

          const String serialized = event.str();
          this->service.dispatcher.dispatch([serialized]() {
            using oro::runtime::app::App;
            auto app = App::sharedApplication();
            if (!app) return;
            for (const auto& win : app->runtime.windowManager.windows) {
              if (win && win->bridge) {
                win->bridge->emit("hid.inputreport", serialized);
              }
            }
          });
        }

        static void DeviceMatched(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
          auto backend = static_cast<MacHIDBackend*>(context);
          if (!backend || !device) return;
          DeviceRecord record;
          record.device = CFDevice(device);
          backend->populateDescriptor(
            device,
            record.descriptor,
            record.usesInputReportId,
            record.usesOutputReportId,
            record.usesFeatureReportId
          );
          record.authorized = backend->authorized.count(record.descriptor.deviceId) > 0;
          record.descriptor.authorized = record.authorized;
          {
            Lock lock(backend->mutex);
            backend->devices[record.descriptor.deviceId] = record;
          }
          backend->dispatchDeviceEvent("hid.deviceconnect", record.descriptor);
        }

        static void DeviceRemoved(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
          auto backend = static_cast<MacHIDBackend*>(context);
          if (!backend || !device) return;
          String deviceId = makeDeviceId(device);
          DeviceRecord record;
          {
            Lock lock(backend->mutex);
            auto it = backend->openDevices.find(deviceId);
            if (it != backend->openDevices.end()) {
              backend->stopInput(it->second);
              backend->openDevices.erase(it);
            }
            auto devIt = backend->devices.find(deviceId);
            if (devIt != backend->devices.end()) {
              record = devIt->second;
              backend->devices.erase(devIt);
            }
          }
          auto descriptor = record.descriptor;
          if (descriptor.deviceId.empty()) {
            descriptor = backend->makeRemovedDescriptor(deviceId);
          }
          backend->dispatchDeviceEvent("hid.devicedisconnect", descriptor);
        }

        static void InputReport(void* context, IOReturn result, void* sender, IOHIDReportType type, uint32_t reportId, uint8_t* report, CFIndex reportLength) {
          auto backend = static_cast<MacHIDBackend*>(context);
          auto device = static_cast<IOHIDDeviceRef>(sender);
          if (!backend || !device) return;
          if (result != kIOReturnSuccess) return;
          backend->handleInput(device, reportId, report, reportLength);
        }

        HID::Backend::DeviceDescriptor makeRemovedDescriptor(const String& deviceId) const {
          HID::Backend::DeviceDescriptor descriptor;
          descriptor.deviceId = deviceId;
          descriptor.vendorId = 0;
          descriptor.productId = 0;
          return descriptor;
        }

        void dispatchDeviceEvent(const char* eventName, const HID::Backend::DeviceDescriptor& descriptor) {
          JSON::Object payload = JSON::Object::Entries {{ "device", this->descriptorToJson(descriptor) }};
          const String serialized = payload.str();
          this->service.dispatcher.dispatch([eventName, serialized]() {
            using oro::runtime::app::App;
            auto app = App::sharedApplication();
            if (!app) return;
            for (const auto& win : app->runtime.windowManager.windows) {
              if (win && win->bridge) win->bridge->emit(eventName, serialized);
            }
          });
        }
      };
  }

  std::unique_ptr<HID::Backend> makeMacHIDBackend(HID& service) {
    auto backend = std::make_unique<MacHIDBackend>(service);
    if (!backend->ready()) {
      return nullptr;
    }
    return backend;
  }
}

#endif
