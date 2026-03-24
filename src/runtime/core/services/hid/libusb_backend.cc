#include "libusb_backend.hh"

#include "../../../json.hh"
#include "../../../string.hh"
#include "../../../bytes.hh"
#include "../../../debug.hh"
#include "../../../app.hh"
#include "../../../platform/types.hh"

#if __has_include(<libusb-1.0/libusb.h>)
#  include <libusb-1.0/libusb.h>
#else
#  include <libusb.h>
#endif

#include <algorithm>
#include <optional>
#include <utility>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>

namespace oro::runtime::core::services::hid {
  namespace {
    JSON::Object makeErrorObject(const char* type, const String& message, int code = 0) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String(type)},
          {"message", message}
        }}}
      }};
      if (code != 0) {
        json.at("err").as<JSON::Object>().set("code", static_cast<int>(code));
      }
      return json;
    }

    JSON::Object makeOkObject() {
      return JSON::Object::Entries {{ "data", JSON::Object::Entries {{ "ok", true }} }};
    }

    String makeDeviceId(uint8_t bus, uint8_t address) {
      return String(std::to_string(bus)) + ":" + std::to_string(address);
    }

    JSON::Object makeLibusbError(const String& context, int code) {
      const char* name = libusb_error_name(code);
      const String message = context + ": " + (name ? String(name) : String("UNKNOWN"));
      return makeErrorObject("OperationError", message, code);
    }

    constexpr uint8_t HID_REQUEST_GET_REPORT = 0x01;
    constexpr uint8_t HID_REQUEST_SET_REPORT = 0x09;

    JSON::Object descriptorToJson(const HID::Backend::DeviceDescriptor& descriptor) {
      JSON::Object json = JSON::Object::Entries {{
        {"deviceId", descriptor.deviceId},
        {"vendorId", static_cast<uint32_t>(descriptor.vendorId)},
        {"productId", static_cast<uint32_t>(descriptor.productId)},
        {"productName", descriptor.productName},
        {"manufacturerName", descriptor.manufacturerName},
        {"serialNumber", descriptor.serialNumber},
        {"opened", descriptor.opened},
        {"authorized", descriptor.authorized}
      }};

      JSON::Array::Entries collections;
      collections.reserve(descriptor.collections.size());
      for (const auto& collection : descriptor.collections) {
        JSON::Object col = JSON::Object::Entries {{
          {"usagePage", static_cast<uint32_t>(collection.usagePage)},
          {"usage", static_cast<uint32_t>(collection.usage)},
          {"type", collection.type}
        }};

        auto reportsToJson = [](const Vector<HID::Backend::ReportInfo>& reports) {
          JSON::Array::Entries result;
          result.reserve(reports.size());
          for (const auto& report : reports) {
            result.push_back(JSON::Object::Entries {{
              {"reportId", static_cast<uint32_t>(report.reportId)},
              {"size", static_cast<uint32_t>(report.size)}
            }});
          }
          return result;
        };

        col.set("inputReports", reportsToJson(collection.inputReports));
        col.set("outputReports", reportsToJson(collection.outputReports));
        col.set("featureReports", reportsToJson(collection.featureReports));
        col.set("usesInputReportId", collection.usesInputReportId);
        col.set("usesOutputReportId", collection.usesOutputReportId);
        col.set("usesFeatureReportId", collection.usesFeatureReportId);
        col.set("children", JSON::Array{});
        collections.push_back(col);
      }

      json.set("collections", collections);
      return json;
    }

    struct HIDInterfaceInfo {
      uint8_t interfaceNumber = 0;
      uint8_t inEndpoint = 0;
      uint8_t outEndpoint = 0;
      uint16_t maxInputReportSize = 64;
      uint16_t maxOutputReportSize = 64;
      uint16_t maxFeatureReportSize = 64;
      bool hasInputEndpoint = false;
      bool hasOutputEndpoint = false;
      bool usesReportId = false;
      Vector<uint8_t> reportDescriptor;
    };

    struct DeviceRecord {
      HID::Backend::DeviceDescriptor descriptor;
      libusb_device* device = nullptr;
      HIDInterfaceInfo interfaceInfo;
    };

    struct DeviceState {
      libusb_device_handle* handle = nullptr;
      HIDInterfaceInfo interfaceInfo;
      std::thread inputThread;
      std::atomic<bool> running { false };
    };

    struct RequestState {
      HID::RequestDeviceOptions options;
      Vector<HID::Backend::DeviceDescriptor> pending;
    };

    Vector<HID::Backend::CollectionInfo> parseReportDescriptor(const Vector<uint8_t>& bytes, bool& usesReportId) {
      usesReportId = false;
      Vector<HID::Backend::CollectionInfo> collections;

      struct CollectionFrame {
        HID::Backend::CollectionInfo info;
      };

      Vector<CollectionFrame> stack;
      uint16_t currentUsagePage = 0;
      uint16_t currentUsage = 0;
      uint8_t currentReportId = 0;
      uint16_t currentReportSize = 0;
      uint16_t currentReportCount = 0;

      size_t index = 0;
      while (index < bytes.size()) {
        const uint8_t prefix = bytes[index++];
        if (prefix == 0xFE) { // Long item, skip
          if (index + 1 >= bytes.size()) break;
          const uint8_t size = bytes[index];
          index += static_cast<size_t>(2 + size);
          continue;
        }

        const uint8_t sizeCode = prefix & 0x03;
        uint8_t size = sizeCode == 3 ? 4 : sizeCode;
        const uint8_t type = (prefix >> 2) & 0x03;
        const uint8_t tag = (prefix >> 4) & 0x0F;

        uint32_t value = 0;
        for (uint8_t i = 0; i < size && index < bytes.size(); ++i) {
          value |= static_cast<uint32_t>(bytes[index++]) << (8 * i);
        }

        switch (type) {
          case 0: { // Main
            if (tag == 0x08 || tag == 0x09 || tag == 0x0B) { // Input, Output, Feature
              if (stack.empty()) {
                HID::Backend::CollectionInfo info;
                info.type = "application";
                stack.push_back({ info });
              }
              HID::Backend::ReportInfo report;
              report.reportId = currentReportId;
              const uint32_t bits = static_cast<uint32_t>(currentReportSize) * static_cast<uint32_t>(currentReportCount);
              report.size = static_cast<uint16_t>((bits + 7u) / 8u);
              if (report.size == 0) {
                report.size = static_cast<uint16_t>(currentReportCount);
              }
              auto& target = stack.back().info;
              target.usagePage = currentUsagePage;
              target.usage = currentUsage;
              if (tag == 0x08) {
                target.inputReports.push_back(report);
                if (report.reportId != 0) {
                  target.usesInputReportId = true;
                }
              } else if (tag == 0x09) {
                target.outputReports.push_back(report);
                if (report.reportId != 0) {
                  target.usesOutputReportId = true;
                }
              } else {
                target.featureReports.push_back(report);
                if (report.reportId != 0) {
                  target.usesFeatureReportId = true;
                }
              }
            } else if (tag == 0x0A) { // Collection
              HID::Backend::CollectionInfo info;
              info.usagePage = currentUsagePage;
              info.usage = currentUsage;
              info.type = (value == 1) ? "application" : "collection";
              stack.push_back({ info });
            } else if (tag == 0x0C) { // End Collection
              if (!stack.empty()) {
                auto finished = stack.back().info;
                stack.pop_back();
                if (stack.empty()) {
                  collections.push_back(finished);
                } else {
                  // we omit nested children structure for now
                }
              }
            }
            break;
          }
          case 1: { // Global
            switch (tag) {
              case 0x00: // Usage Page
                currentUsagePage = static_cast<uint16_t>(value & 0xFFFFu);
                break;
              case 0x07: // Report Size
                currentReportSize = static_cast<uint16_t>(value & 0xFFFFu);
                break;
              case 0x08: // Report ID
                currentReportId = static_cast<uint8_t>(value & 0xFFu);
                usesReportId = true;
                break;
              case 0x09: // Report Count
                currentReportCount = static_cast<uint16_t>(value & 0xFFFFu);
                break;
              default:
                break;
            }
            break;
          }
          case 2: { // Local
            if (tag == 0x00) { // Usage
              currentUsage = static_cast<uint16_t>(value & 0xFFFFu);
            }
            break;
          }
          default:
            break;
        }
      }

      if (collections.empty() && !stack.empty()) {
        collections.push_back(stack.front().info);
      }

      if (collections.empty()) {
        HID::Backend::CollectionInfo info;
        info.type = "application";
        collections.push_back(info);
      }

      return collections;
    }
  }

  class LibusbBackend final : public HID::Backend {
    public:
      using Callback = HID::Callback;

      explicit LibusbBackend(HID& svc)
        : service(svc) {
        const int rc = libusb_init(&this->ctx);
        if (rc != 0) {
          this->ctx = nullptr;
          debug("libusb_init failed for WebHID: %s", libusb_error_name(rc));
        } else {
          libusb_set_option(this->ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
        }
      }

      ~LibusbBackend() override {
        this->shutdown();
      }

      void getDevices(const String& seq, const Callback cb) override;
      void requestDevice(const String& seq, const HID::RequestDeviceOptions& options, const Callback cb) override;
      void chooseDevice(const String& seq, const HID::DeviceSelection& selection, const Callback cb) override;
      void cancelRequest(const String& seq, const Callback cb) override;
      void forgetDevice(const String& seq, const String& deviceId, const Callback cb) override;
      void open(const String& seq, const String& deviceId, const Callback cb) override;
      void close(const String& seq, const String& deviceId, const Callback cb) override;
      void sendReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) override;
      void sendFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) override;
      void receiveFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, uint16_t length, const Callback cb) override;

      bool authorizeDevice(const String& deviceId) override;
      bool isDeviceAuthorized(const String& deviceId) const override;
      void revokeDevice(const String& deviceId) override;

      void enumerateDevices(Function<void(const EnumerateResult&)> completion) override;

    private:
      void shutdown() {
        {
          Lock lock(this->mutex);
          for (auto& entry : this->openDevices) {
            stopInputThread(entry.second);
            closeDeviceInternal(entry.first, entry.second);
          }
          this->openDevices.clear();

          for (auto& entry : this->devices) {
            if (entry.second.device) {
              libusb_unref_device(entry.second.device);
            }
          }
          this->devices.clear();
          this->authorizedDevices.clear();
          this->pendingRequest.reset();
        }

        if (this->ctx) {
          libusb_exit(this->ctx);
          this->ctx = nullptr;
        }
      }

      EnumerateResult enumerate();
      bool refreshDeviceCache(const Vector<DeviceRecord>& newRecords, Vector<DeviceRecord>& added, Vector<DeviceRecord>& removed);
      JSON::Object makeDeviceResponse(const HID::Backend::DeviceDescriptor& descriptor) const;
      void dispatchDeviceEvent(const char* eventName, const HID::Backend::DeviceDescriptor& descriptor);
      bool populateDescriptor(libusb_device* device, const libusb_device_descriptor& desc, DeviceRecord& record);
      void startInputThread(const String& deviceId, DeviceState& state);
      void stopInputThread(DeviceState& state);
      bool openDeviceInternal(const String& deviceId, HID::Backend::DeviceDescriptor& descriptor, JSON::Object& error);
      void closeDeviceInternal(const String& deviceId, DeviceState& state);
      bool sendReportInternal(const String& deviceId, uint8_t reportId, const bytes::Buffer& data, bool feature, JSON::Object& error);
      JSON::Object receiveFeatureReportInternal(const String& deviceId, uint8_t reportId, uint16_t length, JSON::Object& error);

      HID& service;
      libusb_context* ctx = nullptr;
      mutable Mutex mutex;
      Map<String, DeviceRecord> devices;
      Map<String, DeviceState> openDevices;
      Set<String> authorizedDevices;
      std::optional<RequestState> pendingRequest;
  };

  void LibusbBackend::receiveFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, uint16_t length, const Callback cb) {
    JSON::Object error;
    JSON::Object response = this->receiveFeatureReportInternal(deviceId, reportId, length, error);
    if (error.has("err")) {
      cb(seq, error, QueuedResponse{});
      return;
    }
    cb(seq, response, QueuedResponse{});
  }

  JSON::Object LibusbBackend::receiveFeatureReportInternal(const String& deviceId, uint8_t reportId, uint16_t length, JSON::Object& error) {
    libusb_device_handle* handle = nullptr;
    HIDInterfaceInfo info;
    {
      Lock lock(this->mutex);
      auto it = this->openDevices.find(deviceId);
      if (it == this->openDevices.end() || !it->second.handle) {
        error = makeErrorObject("InvalidStateError", "HID device is not open");
        return {};
      }
      handle = it->second.handle;
      info = it->second.interfaceInfo;
    }

    const size_t prefix = (reportId != 0 || info.usesReportId) ? 1 : 0;
    const uint16_t fallbackSize = info.maxFeatureReportSize ? info.maxFeatureReportSize
      : (info.maxInputReportSize ? info.maxInputReportSize : static_cast<uint16_t>(64));
    const size_t totalLength = static_cast<size_t>((length ? length : fallbackSize) + prefix);
    Vector<unsigned char> buffer(totalLength, 0);

    const uint16_t wValue = static_cast<uint16_t>((3 << 8) | reportId);
    const uint8_t requestType = static_cast<uint8_t>(
      static_cast<uint8_t>(LIBUSB_ENDPOINT_IN) |
      static_cast<uint8_t>(LIBUSB_REQUEST_TYPE_CLASS) |
      static_cast<uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
    );
    const int rc = libusb_control_transfer(
      handle,
      requestType,
      HID_REQUEST_GET_REPORT,
      wValue,
      info.interfaceNumber,
      buffer.data(),
      static_cast<uint16_t>(buffer.size()),
      1000
    );

    if (rc < 0) {
      error = makeLibusbError("libusb_control_transfer", rc);
      return {};
    }

    uint8_t resolvedReportId = reportId;
    size_t offset = 0;
    if (info.usesReportId && rc > 0) {
      resolvedReportId = buffer[0];
      offset = 1;
    } else if (reportId != 0) {
      offset = 0;
    }

    if (offset > static_cast<size_t>(rc)) {
      offset = 0;
    }

    bytes::Buffer payload(rc > static_cast<int>(offset) ? static_cast<size_t>(rc - offset) : 0);
    if (payload.size() > 0) {
      memcpy(payload.data(), buffer.data() + offset, payload.size());
    }

    Vector<uint8_t> payloadVec(payload.begin(), payload.end());
    const String encodedPayload = bytes::base64::encode(payloadVec);

    JSON::Object json = JSON::Object::Entries {{
      {"data", JSON::Object::Entries {{
        {"reportId", static_cast<uint32_t>(resolvedReportId)},
        {"encoding", String("base64")},
        {"data", encodedPayload}
      }}}
    }};
    return json;
  }

  bool LibusbBackend::sendReportInternal(const String& deviceId, uint8_t reportId, const bytes::Buffer& data, bool feature, JSON::Object& error) {
    libusb_device_handle* handle = nullptr;
    HIDInterfaceInfo info;
    {
      Lock lock(this->mutex);
      auto it = this->openDevices.find(deviceId);
      if (it == this->openDevices.end() || !it->second.handle) {
        error = makeErrorObject("InvalidStateError", "HID device is not open");
        return false;
      }
      handle = it->second.handle;
      info = it->second.interfaceInfo;
    }

    const bool expectReportId = (reportId != 0) || info.usesReportId;
    const size_t prefix = expectReportId ? 1 : 0;
    Vector<unsigned char> payload(prefix + data.size());
    if (prefix == 1) {
      payload[0] = reportId;
    }
    if (data.size() > 0) {
      memcpy(payload.data() + prefix, data.data(), data.size());
    }

    if (!feature && info.hasOutputEndpoint) {
      int transferred = 0;
      const int rc = libusb_interrupt_transfer(
        handle,
        info.outEndpoint,
        payload.data(),
        static_cast<int>(payload.size()),
        &transferred,
        1000
      );
      if (rc == LIBUSB_SUCCESS) {
        return true;
      }
      if (rc != LIBUSB_ERROR_PIPE) {
        error = makeLibusbError("libusb_interrupt_transfer", rc);
        return false;
      }
    }

    const uint16_t reportType = feature ? 3 : 2;
    const uint16_t wValue = static_cast<uint16_t>((reportType << 8) | reportId);
    const uint8_t requestType = static_cast<uint8_t>(
      static_cast<uint8_t>(LIBUSB_ENDPOINT_OUT) |
      static_cast<uint8_t>(LIBUSB_REQUEST_TYPE_CLASS) |
      static_cast<uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
    );
    const int rc = libusb_control_transfer(
      handle,
      requestType,
      HID_REQUEST_SET_REPORT,
      wValue,
      info.interfaceNumber,
      payload.data(),
      static_cast<uint16_t>(payload.size()),
      1000
    );
    if (rc < 0) {
      error = makeLibusbError("libusb_control_transfer", rc);
      return false;
    }

    return true;
  }

  void LibusbBackend::sendReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) {
    JSON::Object error;
    if (!this->sendReportInternal(deviceId, reportId, data, false, error)) {
      cb(seq, error, QueuedResponse{});
      return;
    }
    cb(seq, makeOkObject(), QueuedResponse{});
  }

  void LibusbBackend::sendFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) {
    JSON::Object error;
    if (!this->sendReportInternal(deviceId, reportId, data, true, error)) {
      cb(seq, error, QueuedResponse{});
      return;
    }
    cb(seq, makeOkObject(), QueuedResponse{});
  }

  void LibusbBackend::close(const String& seq, const String& deviceId, const Callback cb) {
    HID::Backend::DeviceDescriptor descriptor;
    {
      Lock lock(this->mutex);
      auto it = this->openDevices.find(deviceId);
      if (it != this->openDevices.end()) {
        stopInputThread(it->second);
        closeDeviceInternal(deviceId, it->second);
        this->openDevices.erase(it);
      }
      auto deviceIt = this->devices.find(deviceId);
      if (deviceIt != this->devices.end()) {
        deviceIt->second.descriptor.opened = false;
        descriptor = deviceIt->second.descriptor;
      } else {
        descriptor.deviceId = deviceId;
        descriptor.opened = false;
      }
    }
    cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
  }

  void LibusbBackend::open(const String& seq, const String& deviceId, const Callback cb) {
    JSON::Object error;
    HID::Backend::DeviceDescriptor descriptor;
    if (!this->openDeviceInternal(deviceId, descriptor, error)) {
      cb(seq, error, QueuedResponse{});
      return;
    }
    cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
  }

  void LibusbBackend::startInputThread(const String& deviceId, DeviceState& state) {
    if (!state.handle || !state.interfaceInfo.hasInputEndpoint) {
      state.running.store(false);
      return;
    }
    if (state.inputThread.joinable()) {
      return;
    }

    state.running.store(true);
    state.inputThread = std::thread([this, deviceId]() {
      while (true) {
        HIDInterfaceInfo info;
        libusb_device_handle* handle = nullptr;
        std::atomic<bool>* runningFlag = nullptr;

        {
          Lock lock(this->mutex);
          auto it = this->openDevices.find(deviceId);
          if (it == this->openDevices.end()) {
            return;
          }
          runningFlag = &it->second.running;
          if (!runningFlag->load()) {
            break;
          }
          handle = it->second.handle;
          info = it->second.interfaceInfo;
        }

        if (!handle || !info.hasInputEndpoint) {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          continue;
        }

        const size_t bufferSize = std::max<uint16_t>(info.maxInputReportSize ? info.maxInputReportSize : 64, static_cast<uint16_t>(64));
        Vector<unsigned char> buffer(bufferSize, 0);
        int transferred = 0;
        const int rc = libusb_interrupt_transfer(
          handle,
          info.inEndpoint,
          buffer.data(),
          static_cast<int>(buffer.size()),
          &transferred,
          1000
        );

        if (!runningFlag->load()) {
          break;
        }

        if (rc == LIBUSB_ERROR_TIMEOUT) {
          continue;
        }

        if (rc == LIBUSB_ERROR_NO_DEVICE) {
          break;
        }

        if (rc != LIBUSB_SUCCESS) {
          continue;
        }

        if (transferred <= 0) {
          continue;
        }

        uint8_t reportId = 0;
        size_t offset = 0;
        if (info.usesReportId) {
          reportId = buffer[0];
          offset = 1;
        }
        if (offset > static_cast<size_t>(transferred)) {
          offset = 0;
        }

        bytes::Buffer payload(transferred > static_cast<int>(offset) ? static_cast<size_t>(transferred - offset) : 0);
        if (payload.size() > 0) {
          memcpy(payload.data(), buffer.data() + offset, payload.size());
        }

        Vector<uint8_t> payloadVec(payload.begin(), payload.end());
        JSON::Object event = JSON::Object::Entries {{
          {"deviceId", deviceId},
          {"reportId", static_cast<uint32_t>(reportId)},
          {"encoding", String("base64")},
          {"data", bytes::base64::encode(payloadVec)}
        }};

        const String serialized = event.str();
        this->service.dispatcher.dispatch([serialized]() {
          using oro::runtime::app::App;
          auto app = App::sharedApplication();
          if (!app) return;
          for (const auto& win : app->runtime.windowManager.windows) {
            if (!win || !win->bridge) continue;
            win->bridge->emit("hid.inputreport", serialized);
          }
        });
      }
    });
  }

  void LibusbBackend::stopInputThread(DeviceState& state) {
    state.running.store(false);
    if (state.inputThread.joinable()) {
      state.inputThread.join();
    }
  }

  void LibusbBackend::closeDeviceInternal(const String& deviceId, DeviceState& state) {
    if (!state.handle) return;
    libusb_release_interface(state.handle, state.interfaceInfo.interfaceNumber);
    libusb_close(state.handle);
    state.handle = nullptr;
    state.running.store(false);
    {
      Lock lock(this->mutex);
      auto it = this->devices.find(deviceId);
      if (it != this->devices.end()) {
        it->second.descriptor.opened = false;
      }
    }
  }

  bool LibusbBackend::openDeviceInternal(const String& deviceId, HID::Backend::DeviceDescriptor& descriptor, JSON::Object& error) {
    DeviceRecord record;
    {
      Lock lock(this->mutex);
      auto it = this->devices.find(deviceId);
      if (it == this->devices.end()) {
        error = makeErrorObject("NotFoundError", "Unknown HID device");
        return false;
      }
      if (!this->isDeviceAuthorized(deviceId)) {
        error = makeErrorObject("NotAllowedError", "HID device not authorized");
        return false;
      }
      auto openIt = this->openDevices.find(deviceId);
      if (openIt != this->openDevices.end() && openIt->second.handle) {
        descriptor = it->second.descriptor;
        descriptor.opened = true;
        return true;
      }
      record = it->second;
    }

    if (!record.device) {
      error = makeErrorObject("NotFoundError", "HID device reference unavailable");
      return false;
    }

    libusb_ref_device(record.device);
    libusb_device_handle* handle = nullptr;
    int rc = libusb_open(record.device, &handle);
    libusb_unref_device(record.device);
    if (rc != LIBUSB_SUCCESS || !handle) {
      error = makeLibusbError("libusb_open", rc);
      return false;
    }

#if defined(__linux__)
    libusb_set_auto_detach_kernel_driver(handle, 1);
#endif

    rc = libusb_claim_interface(handle, record.interfaceInfo.interfaceNumber);
    if (rc != LIBUSB_SUCCESS) {
      error = makeLibusbError("libusb_claim_interface", rc);
      libusb_close(handle);
      return false;
    }

    DeviceState* openState = nullptr;
    {
      Lock lock(this->mutex);
      this->devices[deviceId].descriptor.opened = true;
      auto& state = this->openDevices[deviceId];
      state.handle = handle;
      state.interfaceInfo = record.interfaceInfo;
      if (state.interfaceInfo.maxFeatureReportSize == 0) {
        state.interfaceInfo.maxFeatureReportSize = state.interfaceInfo.maxInputReportSize
          ? state.interfaceInfo.maxInputReportSize
          : static_cast<uint16_t>(64);
      }
      state.running.store(true);
      openState = &state;
    }

    if (openState) {
      this->startInputThread(deviceId, *openState);
    }

    descriptor = record.descriptor;
    descriptor.opened = true;
    return true;
  }

  void LibusbBackend::forgetDevice(const String& seq, const String& deviceId, const Callback cb) {
    this->revokeDevice(deviceId);
    {
      Lock lock(this->mutex);
      auto it = this->openDevices.find(deviceId);
      if (it != this->openDevices.end()) {
        stopInputThread(it->second);
        closeDeviceInternal(deviceId, it->second);
        this->openDevices.erase(it);
      }
    }
    cb(seq, makeOkObject(), QueuedResponse{});
  }

  void LibusbBackend::cancelRequest(const String& seq, const Callback cb) {
    {
      Lock lock(this->mutex);
      this->pendingRequest.reset();
    }
    cb(seq, makeOkObject(), QueuedResponse{});
  }

  void LibusbBackend::chooseDevice(const String& seq, const HID::DeviceSelection& selection, const Callback cb) {
    RequestState state;
    {
      Lock lock(this->mutex);
      if (!this->pendingRequest.has_value()) {
        cb(seq, makeErrorObject("InvalidStateError", "No pending hid.requestDevice"), QueuedResponse{});
        return;
      }
      state = *this->pendingRequest;
      this->pendingRequest.reset();
    }

    auto it = std::find_if(state.pending.begin(), state.pending.end(), [&](const HID::Backend::DeviceDescriptor& descriptor) {
      return descriptor.deviceId == selection.deviceId;
    });

    if (it == state.pending.end()) {
      cb(seq, makeErrorObject("NotFoundError", String("Unknown HID device ") + selection.deviceId), QueuedResponse{});
      return;
    }

    auto descriptor = *it;
    this->authorizeDevice(descriptor.deviceId);
    descriptor.authorized = true;
    cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
  }

  void LibusbBackend::requestDevice(const String& seq, const HID::RequestDeviceOptions& options, const Callback cb) {
    this->enumerateDevices([=, this](const EnumerateResult& result) {
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
          if (!matched) {
            continue;
          }
        }
        matches.push_back(descriptor);
      }

      if (matches.empty()) {
        cb(seq, makeErrorObject("NotFoundError", "No HID devices matched the requested filters"), QueuedResponse{});
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
        devicesJson.push_back(descriptorToJson(descriptor));
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

  void LibusbBackend::getDevices(const String& seq, const Callback cb) {
    this->enumerateDevices([=, this](const EnumerateResult& result) {
      if (!result.ok) {
        cb(seq, result.error, QueuedResponse{});
        return;
      }
      JSON::Array::Entries entries;
      for (const auto& descriptor : result.devices) {
        if (!descriptor.authorized) continue;
        entries.push_back(descriptorToJson(descriptor));
      }
      JSON::Object json = JSON::Object::Entries {{
        {"data", JSON::Object::Entries {{
          {"devices", entries}
        }}}
      }};
      cb(seq, json, QueuedResponse{});
    });
  }

  void LibusbBackend::enumerateDevices(Function<void(const EnumerateResult&)> completion) {
    if (!completion) return;
    completion(this->enumerate());
  }

  HID::Backend::EnumerateResult LibusbBackend::enumerate() {
    EnumerateResult result;
    if (!this->ctx) {
      result.error = makeErrorObject("NotSupportedError", "libusb context unavailable");
      return result;
    }

    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(this->ctx, &list);
    if (count < 0) {
      result.error = makeLibusbError("libusb_get_device_list", static_cast<int>(count));
      return result;
    }

    Vector<DeviceRecord> records;
    records.reserve(static_cast<size_t>(count));

    for (ssize_t idx = 0; idx < count; ++idx) {
      libusb_device* device = list[idx];
      if (!device) continue;
      libusb_device_descriptor desc;
      if (libusb_get_device_descriptor(device, &desc) != LIBUSB_SUCCESS) {
        continue;
      }
      DeviceRecord record;
      if (!populateDescriptor(device, desc, record)) {
        continue;
      }
      libusb_ref_device(device);
      records.push_back(record);
    }

    libusb_free_device_list(list, 1);

    Set<String> openIds;
    {
      Lock lock(this->mutex);
      for (const auto& entry : this->openDevices) {
        openIds.insert(entry.first);
      }
    }

    for (auto& record : records) {
      if (openIds.count(record.descriptor.deviceId) > 0) {
        record.descriptor.opened = true;
      }
    }

    Vector<DeviceRecord> added;
    Vector<DeviceRecord> removed;
    this->refreshDeviceCache(records, added, removed);

    for (const auto& entry : added) {
      this->dispatchDeviceEvent("hid.deviceconnect", entry.descriptor);
    }
    for (const auto& entry : removed) {
      this->dispatchDeviceEvent("hid.devicedisconnect", entry.descriptor);
    }

    result.ok = true;
    result.devices.reserve(records.size());
    for (const auto& record : records) {
      result.devices.push_back(record.descriptor);
    }
    return result;
  }

  bool LibusbBackend::refreshDeviceCache(const Vector<DeviceRecord>& newRecords, Vector<DeviceRecord>& added, Vector<DeviceRecord>& removed) {
    Lock lock(this->mutex);

    Map<String, DeviceRecord> next;

    for (const auto& record : newRecords) {
      const auto deviceId = record.descriptor.deviceId;
      auto it = this->devices.find(deviceId);
      if (it == this->devices.end()) {
        added.push_back(record);
      } else {
        if (it->second.device) {
          libusb_unref_device(it->second.device);
        }
      }
      next.emplace(deviceId, record);
    }

    for (auto& entry : this->devices) {
      if (!next.count(entry.first)) {
        removed.push_back(entry.second);
        auto openIt = this->openDevices.find(entry.first);
        if (openIt != this->openDevices.end()) {
          stopInputThread(openIt->second);
          closeDeviceInternal(entry.first, openIt->second);
          this->openDevices.erase(openIt);
        }
        if (entry.second.device) {
          libusb_unref_device(entry.second.device);
        }
      }
    }

    this->devices = next;
    return true;
  }

  bool LibusbBackend::populateDescriptor(libusb_device* device, const libusb_device_descriptor& desc, DeviceRecord& record) {
    if (!device) return false;

    const uint8_t bus = libusb_get_bus_number(device);
    const uint8_t address = libusb_get_device_address(device);
    record.descriptor = HID::Backend::DeviceDescriptor{};
    record.descriptor.deviceId = makeDeviceId(bus, address);
    record.descriptor.vendorId = desc.idVendor;
    record.descriptor.productId = desc.idProduct;
    record.descriptor.authorized = this->isDeviceAuthorized(record.descriptor.deviceId);
    record.device = device;

    libusb_config_descriptor* config = nullptr;
    int rc = libusb_get_active_config_descriptor(device, &config);
    if (rc != LIBUSB_SUCCESS) {
      rc = libusb_get_config_descriptor(device, 0, &config);
    }

    bool hasHIDInterface = false;
    uint16_t reportLengthHint = 0;

    if (config) {
      for (uint8_t ifaceIndex = 0; ifaceIndex < config->bNumInterfaces && !hasHIDInterface; ++ifaceIndex) {
        const auto& iface = config->interface[ifaceIndex];
        for (int altIndex = 0; altIndex < iface.num_altsetting; ++altIndex) {
          const auto& alt = iface.altsetting[altIndex];
          if (alt.bInterfaceClass != LIBUSB_CLASS_HID) {
            continue;
          }
          hasHIDInterface = true;
          record.interfaceInfo.interfaceNumber = alt.bInterfaceNumber;

          for (uint8_t epIndex = 0; epIndex < alt.bNumEndpoints; ++epIndex) {
            const auto& endpoint = alt.endpoint[epIndex];
            if ((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_INTERRUPT) {
              continue;
            }
            if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
              record.interfaceInfo.inEndpoint = endpoint.bEndpointAddress;
              record.interfaceInfo.maxInputReportSize = endpoint.wMaxPacketSize;
              record.interfaceInfo.hasInputEndpoint = true;
            } else {
              record.interfaceInfo.outEndpoint = endpoint.bEndpointAddress;
              record.interfaceInfo.maxOutputReportSize = endpoint.wMaxPacketSize;
              record.interfaceInfo.hasOutputEndpoint = true;
            }
          }

          const unsigned char* extra = alt.extra;
          int extraLength = alt.extra_length;
          while (extra && extraLength >= 2) {
            const uint8_t length = extra[0];
            const uint8_t type = extra[1];
            if (type == 0x21 && length >= 6) { // HID descriptor
              if (length + 2 > extraLength) break;
              if (length >= 6) {
                const uint8_t numDescriptors = extra[5];
                size_t offset = 6;
                for (uint8_t i = 0; i < numDescriptors && offset + 3 <= length; ++i) {
                  const uint8_t descriptorType = extra[offset];
                  const uint16_t descriptorLength = static_cast<uint16_t>(extra[offset + 1] | (extra[offset + 2] << 8));
                  if (descriptorType == 0x22) {
                    reportLengthHint = descriptorLength;
                    break;
                  }
                  offset += 3;
                }
              }
              break;
            }
            if (length == 0) break;
            extra += length;
            extraLength -= length;
          }

          break;
        }
      }
    }

    libusb_config_descriptor* configToFree = config;

    libusb_device_handle* handle = nullptr;
    if (hasHIDInterface) {
      rc = libusb_open(device, &handle);
      if (rc == LIBUSB_SUCCESS && handle) {
        if (desc.iProduct) {
          unsigned char buffer[256];
          const int len = libusb_get_string_descriptor_ascii(handle, desc.iProduct, buffer, sizeof(buffer));
          if (len > 0) record.descriptor.productName = String(reinterpret_cast<const char*>(buffer), len);
        }
        if (desc.iManufacturer) {
          unsigned char buffer[256];
          const int len = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, buffer, sizeof(buffer));
          if (len > 0) record.descriptor.manufacturerName = String(reinterpret_cast<const char*>(buffer), len);
        }
        if (desc.iSerialNumber) {
          unsigned char buffer[256];
          const int len = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, buffer, sizeof(buffer));
          if (len > 0) record.descriptor.serialNumber = String(reinterpret_cast<const char*>(buffer), len);
        }

        const uint16_t length = reportLengthHint ? reportLengthHint : 512;
        if (length > 0) {
          Vector<uint8_t> report(length);
          const uint8_t requestType = static_cast<uint8_t>(
            static_cast<uint8_t>(LIBUSB_ENDPOINT_IN) |
            static_cast<uint8_t>(LIBUSB_REQUEST_TYPE_STANDARD) |
            static_cast<uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
          );
          const int transferred = libusb_control_transfer(
            handle,
            requestType,
            LIBUSB_REQUEST_GET_DESCRIPTOR,
            (0x22u << 8) | 0,
            record.interfaceInfo.interfaceNumber,
            report.data(),
            static_cast<uint16_t>(report.size()),
            1000
          );
          if (transferred > 0) {
            report.resize(static_cast<size_t>(transferred));
            record.interfaceInfo.reportDescriptor = report;
            bool usesReportId = false;
            record.descriptor.collections = parseReportDescriptor(report, usesReportId);
            record.interfaceInfo.usesReportId = usesReportId;
            uint16_t featureMax = 0;
            for (const auto& collection : record.descriptor.collections) {
              for (const auto& feature : collection.featureReports) {
                if (feature.size > featureMax) {
                  featureMax = feature.size;
                }
              }
            }
            if (featureMax == 0) {
              featureMax = record.interfaceInfo.maxOutputReportSize
                ? record.interfaceInfo.maxOutputReportSize
                : (record.interfaceInfo.maxInputReportSize ? record.interfaceInfo.maxInputReportSize : static_cast<uint16_t>(64));
            }
            record.interfaceInfo.maxFeatureReportSize = featureMax;
          }
        }

        libusb_close(handle);
      }
    }

    if (configToFree) {
      libusb_free_config_descriptor(configToFree);
    }

    return hasHIDInterface;
  }

  bool LibusbBackend::authorizeDevice(const String& deviceId) {
    Lock lock(this->mutex);
    this->authorizedDevices.insert(deviceId);
    auto it = this->devices.find(deviceId);
    if (it != this->devices.end()) {
      it->second.descriptor.authorized = true;
    }
    return true;
  }

  bool LibusbBackend::isDeviceAuthorized(const String& deviceId) const {
    Lock lock(this->mutex);
    return this->authorizedDevices.count(deviceId) > 0;
  }

  void LibusbBackend::revokeDevice(const String& deviceId) {
    Lock lock(this->mutex);
    this->authorizedDevices.erase(deviceId);
    auto it = this->devices.find(deviceId);
    if (it != this->devices.end()) {
      it->second.descriptor.authorized = false;
    }
  }

  std::unique_ptr<HID::Backend> makeHIDBackend(HID& service) {
    return std::make_unique<LibusbBackend>(service);
  }

  JSON::Object LibusbBackend::makeDeviceResponse(const HID::Backend::DeviceDescriptor& descriptor) const {
    return JSON::Object::Entries {{ "data", JSON::Object::Entries {{ "device", descriptorToJson(descriptor) }} }};
  }

  void LibusbBackend::dispatchDeviceEvent(const char* eventName, const HID::Backend::DeviceDescriptor& descriptor) {
    JSON::Object payload = JSON::Object::Entries {{ "device", descriptorToJson(descriptor) }};
    const String serialized = payload.str();
    this->service.dispatcher.dispatch([eventName, serialized]() {
      using oro::runtime::app::App;
      auto app = App::sharedApplication();
      if (!app) return;
      for (const auto& win : app->runtime.windowManager.windows) {
        if (!win || !win->bridge) continue;
        win->bridge->emit(eventName, serialized);
      }
    });
  }
}
