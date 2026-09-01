#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "windows_backend.hh"

#include "../../../app.hh"
#include "../../../bytes.hh"
#include "../../../debug.hh"
#include "../../../string.hh"
#include "../../../platform/types.hh"

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <hidclass.h>
#include <dbt.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <unordered_map>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace oro::runtime::core::services::hid {
  namespace {
    using namespace oro::runtime;

    String wideToString(const wchar_t* value) {
      if (!value) return String("");
      return string::convertWStringToString(std::wstring(value));
    }

    String hidString(HANDLE handle, ULONG which) {
      wchar_t buffer[256];
      BOOL ok = FALSE;
      switch (which) {
        case 0: ok = HidD_GetProductString(handle, buffer, sizeof(buffer)); break;
        case 1: ok = HidD_GetManufacturerString(handle, buffer, sizeof(buffer)); break;
        case 2: ok = HidD_GetSerialNumberString(handle, buffer, sizeof(buffer)); break;
        default: ok = FALSE; break;
      }
      if (!ok) return String("");
      return wideToString(buffer);
    }

    struct DevicePath {
      String narrow;
      std::wstring wide;
    };

    DevicePath getDevicePath(HDEVINFO infoSet, SP_DEVICE_INTERFACE_DATA& interfaceData) {
      DWORD requiredSize = 0;
      SetupDiGetDeviceInterfaceDetailW(infoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);
      DevicePath result;
      if (requiredSize == 0) return result;
      auto buffer = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(malloc(requiredSize));
      if (!buffer) return result;
      buffer->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
      if (SetupDiGetDeviceInterfaceDetailW(infoSet, &interfaceData, buffer, requiredSize, nullptr, nullptr)) {
        result.wide = buffer->DevicePath;
        result.narrow = wideToString(buffer->DevicePath);
      }
      free(buffer);
      return result;
    }

    HID::Backend::CollectionInfo makeCollection(const HIDP_CAPS& caps,
      bool usesInputReportId,
      bool usesOutputReportId,
      bool usesFeatureReportId) {
      HID::Backend::CollectionInfo info;
      info.usagePage = static_cast<uint16_t>(caps.UsagePage);
      info.usage = static_cast<uint16_t>(caps.Usage);
      info.type = "application";

      auto addReport = [&](Vector<HID::Backend::ReportInfo>& list, uint8_t reportId, uint16_t size) {
        HID::Backend::ReportInfo r;
        r.reportId = reportId;
        r.size = size;
        list.push_back(r);
      };

      if (caps.InputReportByteLength) {
        addReport(info.inputReports, 0, static_cast<uint16_t>(caps.InputReportByteLength));
      }
      if (caps.OutputReportByteLength) {
        addReport(info.outputReports, 0, static_cast<uint16_t>(caps.OutputReportByteLength));
      }
      if (caps.FeatureReportByteLength) {
        addReport(info.featureReports, 0, static_cast<uint16_t>(caps.FeatureReportByteLength));
      }

      info.usesInputReportId = usesInputReportId;
      info.usesOutputReportId = usesOutputReportId;
      info.usesFeatureReportId = usesFeatureReportId;
      return info;
    }

    bytes::Buffer slicePayload(const uint8_t* data, size_t length, size_t offset) {
      if (!data || length <= offset) return bytes::Buffer();
      bytes::Buffer payload(length - offset);
      memcpy(payload.data(), data + offset, payload.size());
      return payload;
    }

    bool reportTypeUsesReportId(
      PHIDP_PREPARSED_DATA preparsed,
      HIDP_REPORT_TYPE type,
      USHORT buttonCount,
      USHORT valueCount
    ) {
      if (!preparsed) return false;

      if (buttonCount > 0) {
        Vector<HIDP_BUTTON_CAPS> buttonCaps(buttonCount);
        USHORT count = buttonCount;
        const NTSTATUS status = HidP_GetButtonCaps(type, buttonCaps.data(), &count, preparsed);
        if (status == HIDP_STATUS_SUCCESS) {
          for (USHORT idx = 0; idx < count; ++idx) {
            if (buttonCaps[idx].ReportID != 0) {
              return true;
            }
          }
        }
      }

      if (valueCount > 0) {
        Vector<HIDP_VALUE_CAPS> valueCaps(valueCount);
        USHORT count = valueCount;
        const NTSTATUS status = HidP_GetValueCaps(type, valueCaps.data(), &count, preparsed);
        if (status == HIDP_STATUS_SUCCESS) {
          for (USHORT idx = 0; idx < count; ++idx) {
            if (valueCaps[idx].ReportID != 0) {
              return true;
            }
          }
        }
      }

      return false;
    }

    class WindowsHIDBackend final : public HID::Backend {
      public:
        using Callback = HID::Callback;
        using DeviceSelection = HID::DeviceSelection;

        explicit WindowsHIDBackend(HID& svc)
          : service(svc) {
          this->startNotifications();
        }

        ~WindowsHIDBackend() override {
          this->stopNotifications();
          this->shutdown();
        }

        void getDevices(const String& seq, const Callback cb) override {
          this->enumerateDevices([=, this](const EnumerateResult& result) {
            if (!result.ok) {
              cb(seq, result.error, QueuedResponse{});
              return;
            }
            JSON::Array::Entries devices;
            for (const auto& descriptor : result.devices) {
              if (!descriptor.authorized) continue;
              devices.push_back(this->descriptorToJson(descriptor));
            }
            JSON::Object json = JSON::Object::Entries {{
              {"data", JSON::Object::Entries {{
                {"devices", devices}
              }}}
            }};
            cb(seq, json, QueuedResponse{});
          });
        }

        void requestDevice(const String& seq, const HID::RequestDeviceOptions& options, const Callback cb) override {
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

            JSON::Array::Entries devices;
            for (const auto& descriptor : matches) {
              devices.push_back(this->descriptorToJson(descriptor));
            }
            {
              Lock lock(this->mutex);
              this->pendingRequest = RequestState{ options, matches };
            }
            JSON::Object json = JSON::Object::Entries {{
              {"data", JSON::Object::Entries {{
                {"requiresSelection", true},
                {"devices", devices}
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
            auto it = this->openDevices.find(deviceId);
            if (it != this->openDevices.end()) {
              this->stopReader(it->second);
              this->closeHandleUnlocked(deviceId, it->second);
              this->openDevices.erase(it);
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
              this->stopReader(it->second);
              this->closeHandleUnlocked(deviceId, it->second);
              this->openDevices.erase(it);
            }
            auto devIt = this->devices.find(deviceId);
            if (devIt != this->devices.end()) {
              devIt->second.opened = false;
              devIt->second.descriptor.opened = false;
              descriptor = devIt->second.descriptor;
            }
          }
          cb(seq, this->makeDeviceResponse(descriptor), QueuedResponse{});
        }

        void sendReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) override {
          JSON::Object error;
          if (!this->sendReportInternal(deviceId, reportId, data, false, error)) {
            cb(seq, error, QueuedResponse{});
            return;
          }
          cb(seq, makeOk(), QueuedResponse{});
        }

        void sendFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, const bytes::Buffer& data, const Callback cb) override {
          JSON::Object error;
          if (!this->sendReportInternal(deviceId, reportId, data, true, error)) {
            cb(seq, error, QueuedResponse{});
            return;
          }
          cb(seq, makeOk(), QueuedResponse{});
        }

        void receiveFeatureReport(const String& seq, const String& deviceId, uint8_t reportId, uint16_t length, const Callback cb) override {
          JSON::Object error;
          JSON::Object response = this->receiveFeatureReportInternal(deviceId, reportId, length, error);
          if (error.has("err")) {
            cb(seq, error, QueuedResponse{});
            return;
          }
          cb(seq, response, QueuedResponse{});
        }

        bool authorizeDevice(const String& deviceId) override {
          Lock lock(this->mutex);
          this->authorized.insert(deviceId);
          auto it = this->devices.find(deviceId);
          if (it != this->devices.end()) {
            it->second.authorized = true;
            it->second.descriptor.authorized = true;
          }
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
          HDEVINFO infoSet = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
          if (infoSet == INVALID_HANDLE_VALUE) {
            result.error = makeError("OperationError", "SetupDiGetClassDevs failed");
            completion(result);
            return;
          }

          std::vector<DeviceRecord> records;
          SP_DEVICE_INTERFACE_DATA interfaceData;
          interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

          for (DWORD index = 0; SetupDiEnumDeviceInterfaces(infoSet, nullptr, &GUID_DEVINTERFACE_HID, index, &interfaceData); ++index) {
            DevicePath devicePath = getDevicePath(infoSet, interfaceData);
            if (devicePath.wide.empty()) continue;

            HANDLE handle = CreateFileW(devicePath.wide.c_str(), GENERIC_READ | GENERIC_WRITE,
              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
              handle = CreateFileW(devicePath.wide.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
              if (handle == INVALID_HANDLE_VALUE) continue;
            }

            HIDD_ATTRIBUTES attributes;
            attributes.Size = sizeof(HIDD_ATTRIBUTES);
            if (!HidD_GetAttributes(handle, &attributes)) {
              CloseHandle(handle);
              continue;
            }

            PHIDP_PREPARSED_DATA preparsed = nullptr;
            if (!HidD_GetPreparsedData(handle, &preparsed) || !preparsed) {
              CloseHandle(handle);
              continue;
            }

            HIDP_CAPS caps;
            NTSTATUS status = HidP_GetCaps(preparsed, &caps);
            if (status != HIDP_STATUS_SUCCESS) {
              HidD_FreePreparsedData(preparsed);
              CloseHandle(handle);
              continue;
            }

            const bool usesInputReportId = reportTypeUsesReportId(
              preparsed,
              HidP_Input,
              caps.NumberInputButtonCaps,
              caps.NumberInputValueCaps
            );
            const bool usesOutputReportId = reportTypeUsesReportId(
              preparsed,
              HidP_Output,
              caps.NumberOutputButtonCaps,
              caps.NumberOutputValueCaps
            );
            const bool usesFeatureReportId = reportTypeUsesReportId(
              preparsed,
              HidP_Feature,
              caps.NumberFeatureButtonCaps,
              caps.NumberFeatureValueCaps
            );

            HidD_FreePreparsedData(preparsed);

            DeviceRecord record;
            record.path = devicePath.narrow;
            record.widePath = devicePath.wide;
            record.descriptor.deviceId = devicePath.narrow;
            record.descriptor.vendorId = static_cast<uint16_t>(attributes.VendorID);
            record.descriptor.productId = static_cast<uint16_t>(attributes.ProductID);
            record.descriptor.productName = hidString(handle, 0);
            record.descriptor.manufacturerName = hidString(handle, 1);
            record.descriptor.serialNumber = hidString(handle, 2);
            record.descriptor.collections.clear();
            record.descriptor.collections.push_back(makeCollection(
              caps,
              usesInputReportId,
              usesOutputReportId,
              usesFeatureReportId
            ));
            record.descriptor.authorized = false;
            record.descriptor.opened = false;
            record.authorized = false;
            record.opened = false;
            uint16_t featureLength = static_cast<uint16_t>(caps.FeatureReportByteLength ? caps.FeatureReportByteLength : 0);
            for (const auto& collection : record.descriptor.collections) {
              for (const auto& report : collection.featureReports) {
                if (report.size > featureLength) {
                  featureLength = report.size;
                }
              }
            }
            if (featureLength == 0) {
              featureLength = static_cast<uint16_t>(64);
            }
            record.featureReportSize = featureLength;

            records.push_back(record);
            CloseHandle(handle);
          }

          SetupDiDestroyDeviceInfoList(infoSet);

          std::vector<DeviceRecord> added;
          std::vector<DeviceRecord> removed;
          this->refreshDeviceCache(records, added, removed);

          for (const auto& entry : added) {
            this->dispatchDeviceEvent("hid.deviceconnect", entry.descriptor);
          }
          for (const auto& entry : removed) {
            const String removedId = !entry.descriptor.deviceId.empty() ? entry.descriptor.deviceId : entry.path;
            auto descriptor = entry.descriptor.deviceId.empty()
              ? this->makeRemovedDescriptor(removedId)
              : entry.descriptor;
            this->dispatchDeviceEvent("hid.devicedisconnect", descriptor);
          }

          result.ok = true;
          for (auto& record : records) {
            result.devices.push_back(record.descriptor);
          }
          completion(result);
        }

      private:
        struct RequestState {
          HID::RequestDeviceOptions options;
          Vector<HID::Backend::DeviceDescriptor> pending;
        };

        struct DeviceRecord {
          HID::Backend::DeviceDescriptor descriptor;
          String path;
          std::wstring widePath;
          bool opened = false;
          bool authorized = false;
          uint16_t featureReportSize = 0;
        };

        struct DeviceState {
          HANDLE handle = INVALID_HANDLE_VALUE;
          std::thread reader;
          std::atomic<bool> running { false };
          uint16_t reportSize = 0;
          uint16_t featureReportSize = 0;
          bool usesInputReportId = false;
          bool usesOutputReportId = false;
          bool usesFeatureReportId = false;
        };

        HID& service;
        mutable Mutex mutex;
        Map<String, DeviceRecord> devices;
        Map<String, std::shared_ptr<DeviceState>> openDevices;
        Set<String> authorized;
        std::optional<RequestState> pendingRequest;
        std::thread notificationThread;
        std::atomic<bool> notificationRunning { false };
        std::atomic<DWORD> notificationThreadId { 0 };
        HWND notificationWindow = nullptr;
        HDEVNOTIFY notificationHandle = nullptr;
        std::atomic<bool> refreshPending { false };

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
            JSON::Array::Entries inputs;
            for (const auto& r : collection.inputReports) {
              inputs.push_back(JSON::Object::Entries {{ {"reportId", static_cast<uint32_t>(r.reportId)}, {"size", static_cast<uint32_t>(r.size)} }});
            }
            JSON::Array::Entries outputs;
            for (const auto& r : collection.outputReports) {
              outputs.push_back(JSON::Object::Entries {{ {"reportId", static_cast<uint32_t>(r.reportId)}, {"size", static_cast<uint32_t>(r.size)} }});
            }
            JSON::Array::Entries features;
            for (const auto& r : collection.featureReports) {
              features.push_back(JSON::Object::Entries {{ {"reportId", static_cast<uint32_t>(r.reportId)}, {"size", static_cast<uint32_t>(r.size)} }});
            }
            json.set("inputReports", inputs);
            json.set("outputReports", outputs);
            json.set("featureReports", features);
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

        void startNotifications() {
          bool expected = false;
          if (!this->notificationRunning.compare_exchange_strong(expected, true)) {
            return;
          }
          this->notificationThread = std::thread([this]() {
            this->notificationLoop();
          });
        }

        void stopNotifications() {
          this->notificationRunning.store(false);
          this->refreshPending.store(false);
          HWND window = this->notificationWindow;
          if (window) {
            PostMessageW(window, WM_CLOSE, 0, 0);
          } else {
            DWORD threadId = this->notificationThreadId.load();
            if (threadId != 0) {
              PostThreadMessageW(threadId, WM_QUIT, 0, 0);
            }
          }
          if (this->notificationThread.joinable()) {
            this->notificationThread.join();
          }
          this->notificationThreadId.store(0);
        }

        void notificationLoop() {
          this->notificationThreadId.store(GetCurrentThreadId());
          static const wchar_t* className = L"SocketHIDNotificationWindow";
          static std::once_flag registerFlag;
          std::call_once(registerFlag, []() {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = WindowsHIDBackend::NotificationWindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;
            RegisterClassW(&wc);
          });

          HINSTANCE instance = GetModuleHandleW(nullptr);
          HWND hwnd = CreateWindowExW(
            0,
            className,
            L"",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            instance,
            this
          );

          if (!hwnd) {
            this->notificationRunning.store(false);
            this->notificationThreadId.store(0);
            return;
          }

          this->notificationWindow = hwnd;

          DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
          filter.dbcc_size = sizeof(filter);
          filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
          filter.dbcc_classguid = GUID_DEVINTERFACE_HID;

          this->notificationHandle = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
          if (!this->notificationHandle) {
            DestroyWindow(hwnd);
            this->notificationWindow = nullptr;
            this->notificationRunning.store(false);
            this->notificationThreadId.store(0);
            return;
          }

          MSG msg;
          while (true) {
            if (!this->notificationRunning.load()) {
              break;
            }
            const BOOL rc = GetMessageW(&msg, nullptr, 0, 0);
            if (rc <= 0) {
              break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
          }

          if (this->notificationWindow) {
            DestroyWindow(this->notificationWindow);
            this->notificationWindow = nullptr;
          }

          this->notificationThreadId.store(0);
          this->notificationRunning.store(false);
        }

        void handleDeviceChange(const DEV_BROADCAST_HDR* header) {
          if (!header) return;
          if (header->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) return;
          const auto* iface = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(header);
          if (!iface) return;
          if (iface->dbcc_classguid != GUID_DEVINTERFACE_HID) return;

          bool expected = false;
          if (!this->refreshPending.compare_exchange_strong(expected, true)) {
            return;
          }

          this->enumerateDevices([this](const EnumerateResult&) {
            this->refreshPending.store(false);
          });
        }

        static LRESULT CALLBACK NotificationWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
          if (message == WM_CREATE) {
            auto create = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            auto* backend = static_cast<WindowsHIDBackend*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(backend));
            return 0;
          }

          auto* backend = reinterpret_cast<WindowsHIDBackend*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
          if (!backend) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
          }

          switch (message) {
            case WM_DEVICECHANGE:
              if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
                backend->handleDeviceChange(reinterpret_cast<const DEV_BROADCAST_HDR*>(lParam));
              }
              return 0;
            case WM_CLOSE:
              DestroyWindow(hwnd);
              return 0;
            case WM_DESTROY:
              if (backend->notificationHandle) {
                UnregisterDeviceNotification(backend->notificationHandle);
                backend->notificationHandle = nullptr;
              }
              backend->notificationWindow = nullptr;
              PostQuitMessage(0);
              return 0;
            default:
              break;
          }

          return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        void shutdown() {
          std::vector<std::shared_ptr<DeviceState>> openCopy;
          {
            Lock lock(this->mutex);
            for (auto& entry : this->openDevices) {
              openCopy.push_back(entry.second);
            }
            this->openDevices.clear();
          }
          for (auto& state : openCopy) {
            this->stopReader(state);
            if (state && state->handle != INVALID_HANDLE_VALUE) {
              CancelIoEx(state->handle, nullptr);
              CloseHandle(state->handle);
              state->handle = INVALID_HANDLE_VALUE;
            }
          }
          {
            Lock lock(this->mutex);
            this->devices.clear();
            this->authorized.clear();
            this->pendingRequest.reset();
          }
        }

        bool refreshDeviceCache(std::vector<DeviceRecord>& newRecords, std::vector<DeviceRecord>& added, std::vector<DeviceRecord>& removed) {
          Lock lock(this->mutex);
          Map<String, DeviceRecord> next;
          added.clear();
          removed.clear();

          for (auto& record : newRecords) {
            const String deviceId = record.descriptor.deviceId;
            if (deviceId.empty()) {
              continue;
            }

            if (record.featureReportSize == 0) {
              record.featureReportSize = static_cast<uint16_t>(64);
            }

            if (this->authorized.count(deviceId) > 0) {
              record.authorized = true;
              record.descriptor.authorized = true;
            }

            auto existing = this->devices.find(deviceId);
            if (existing == this->devices.end()) {
              added.push_back(record);
            } else {
              if (existing->second.authorized) {
                record.authorized = true;
                record.descriptor.authorized = true;
              }
              if (existing->second.opened) {
                record.opened = true;
                record.descriptor.opened = true;
              }
              if (record.featureReportSize == 0) {
                record.featureReportSize = existing->second.featureReportSize;
              }
            }

            auto openIt = this->openDevices.find(deviceId);
            if (openIt != this->openDevices.end() && openIt->second) {
              record.opened = true;
              record.descriptor.opened = true;
              if (record.featureReportSize == 0 && openIt->second->featureReportSize != 0) {
                record.featureReportSize = openIt->second->featureReportSize;
              }
            }

            next.emplace(deviceId, record);
          }

          for (auto& entry : this->devices) {
            if (!next.count(entry.first)) {
              removed.push_back(entry.second);
              auto openIt = this->openDevices.find(entry.first);
              if (openIt != this->openDevices.end()) {
                this->stopReader(openIt->second);
                this->closeHandleUnlocked(entry.first, openIt->second);
                this->openDevices.erase(openIt);
              }
            }
          }

          this->devices = std::move(next);
          return true;
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

        HID::Backend::DeviceDescriptor makeRemovedDescriptor(const String& deviceId) const {
          HID::Backend::DeviceDescriptor descriptor;
          descriptor.deviceId = deviceId;
          descriptor.vendorId = 0;
          descriptor.productId = 0;
          return descriptor;
        }

        bool openDevice(const String& deviceId, HID::Backend::DeviceDescriptor& descriptor, JSON::Object& error) {
          DeviceRecord record;
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
            record = it->second;
            if (record.opened) {
              descriptor = record.descriptor;
              descriptor.opened = true;
              return true;
            }
          }

          HANDLE handle = CreateFileW(record.widePath.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
          if (handle == INVALID_HANDLE_VALUE) {
            handle = CreateFileW(record.widePath.c_str(), GENERIC_READ,
              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
              error = makeError("OperationError", "Failed to open HID device");
              return false;
            }
          }

          auto state = std::make_shared<DeviceState>();
          state->running.store(true);

          // refresh descriptor caps after successful open
          PHIDP_PREPARSED_DATA preparsed = nullptr;
          if (!HidD_GetPreparsedData(handle, &preparsed) || !preparsed) {
            CloseHandle(handle);
            error = makeError("OperationError", "HidD_GetPreparsedData failed");
            return false;
          }

          HIDP_CAPS caps;
          NTSTATUS status = HidP_GetCaps(preparsed, &caps);
          if (status != HIDP_STATUS_SUCCESS) {
            HidD_FreePreparsedData(preparsed);
            CloseHandle(handle);
            error = makeError("OperationError", "HidP_GetCaps failed");
            return false;
          }

          const bool usesInputReportId = reportTypeUsesReportId(
            preparsed,
            HidP_Input,
            caps.NumberInputButtonCaps,
            caps.NumberInputValueCaps
          );
          const bool usesOutputReportId = reportTypeUsesReportId(
            preparsed,
            HidP_Output,
            caps.NumberOutputButtonCaps,
            caps.NumberOutputValueCaps
          );
          const bool usesFeatureReportId = reportTypeUsesReportId(
            preparsed,
            HidP_Feature,
            caps.NumberFeatureButtonCaps,
            caps.NumberFeatureValueCaps
          );

          HidD_FreePreparsedData(preparsed);

          state->handle = handle;
          state->reportSize = static_cast<uint16_t>(caps.InputReportByteLength ? caps.InputReportByteLength : 64);
          state->featureReportSize = static_cast<uint16_t>(caps.FeatureReportByteLength ? caps.FeatureReportByteLength
            : (state->reportSize ? state->reportSize : static_cast<uint16_t>(64)));
          state->usesInputReportId = usesInputReportId;
          state->usesOutputReportId = usesOutputReportId;
          state->usesFeatureReportId = usesFeatureReportId;

          {
            Lock lock(this->mutex);
            auto& stored = this->devices[deviceId];
            stored.opened = true;
            stored.descriptor.opened = true;
            stored.descriptor.collections.clear();
            stored.descriptor.collections.push_back(makeCollection(
              caps,
              usesInputReportId,
              usesOutputReportId,
              usesFeatureReportId
            ));
            stored.featureReportSize = state->featureReportSize;
            descriptor = stored.descriptor;
            this->openDevices[deviceId] = state;
          }

          this->startReader(deviceId);
          return true;
        }

        void startReader(const String& deviceId) {
          std::shared_ptr<DeviceState> state;
          {
            Lock lock(this->mutex);
            auto it = this->openDevices.find(deviceId);
            if (it == this->openDevices.end()) return;
            state = it->second;
          }
          if (!state || state->handle == INVALID_HANDLE_VALUE) return;
          if (state->reader.joinable()) return;
          state->running.store(true);

          HANDLE handle = state->handle;
          uint16_t reportSize = state->reportSize;
          bool usesReportId = state->usesInputReportId;
          HID& svc = this->service;

          state->reader = std::thread([handle, reportSize, usesReportId, deviceId, &svc, state]() {
            Vector<uint8_t> buffer(reportSize ? reportSize : 64);
            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent) {
              state->running.store(false);
              return;
            }
            while (true) {
              if (!state->running.load()) break;
              DWORD bytesRead = 0;
              ResetEvent(overlapped.hEvent);
              BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, &overlapped);
              if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                  DWORD waitResult = WaitForSingleObject(overlapped.hEvent, INFINITE);
                  if (waitResult != WAIT_OBJECT_0) {
                    break;
                  }
                  if (!GetOverlappedResult(handle, &overlapped, &bytesRead, FALSE)) {
                    break;
                  }
                } else if (err == ERROR_OPERATION_ABORTED) {
                  break;
                } else {
                  break;
                }
              }

              if (bytesRead == 0) {
                continue;
              }

              uint8_t reportId = 0;
              size_t offset = 0;
              if (usesReportId && bytesRead > 0) {
                reportId = buffer[0];
                offset = 1;
              }

              bytes::Buffer payload = slicePayload(buffer.data(), bytesRead, offset);
              JSON::Object event = JSON::Object::Entries {{
                {"deviceId", deviceId},
                {"reportId", static_cast<uint32_t>(reportId)},
                {"encoding", String("base64")},
                {"data", payload.str(bytes::Buffer::Encoding::BASE64)}
              }};

              const String serialized = event.str();
              svc.dispatcher.dispatch([serialized]() {
                using oro::runtime::app::App;
                auto app = App::sharedApplication();
                if (!app) return;
                for (const auto& win : app->runtime.windowManager.windows) {
                  if (win && win->bridge) win->bridge->emit("hid.inputreport", serialized);
                }
              });
            }

            if (overlapped.hEvent) CloseHandle(overlapped.hEvent);
            state->running.store(false);
          });
        }

        bool isDeviceRunning(const String& deviceId) {
          Lock lock(this->mutex);
          auto it = this->openDevices.find(deviceId);
          if (it == this->openDevices.end()) return false;
          return it->second->running.load();
        }

        void stopReader(const std::shared_ptr<DeviceState>& state) {
          if (!state) return;
          state->running.store(false);
          if (state->handle != INVALID_HANDLE_VALUE) {
            CancelIoEx(state->handle, nullptr);
          }
          if (state->reader.joinable()) {
            state->reader.join();
          }
        }

        void closeHandleUnlocked(const String& deviceId, const std::shared_ptr<DeviceState>& state) {
          if (state) {
            state->running.store(false);
            if (state->handle != INVALID_HANDLE_VALUE) {
              CancelIoEx(state->handle, nullptr);
              CloseHandle(state->handle);
              state->handle = INVALID_HANDLE_VALUE;
            }
          }
          auto it = this->devices.find(deviceId);
          if (it != this->devices.end()) {
            it->second.opened = false;
            it->second.descriptor.opened = false;
          }
        }

        bool sendReportInternal(const String& deviceId, uint8_t reportId, const bytes::Buffer& data, bool feature, JSON::Object& error) {
          HANDLE handle = INVALID_HANDLE_VALUE;
          bool usesReportId = false;
          {
            Lock lock(this->mutex);
            auto it = this->openDevices.find(deviceId);
            if (it == this->openDevices.end() || !it->second || it->second->handle == INVALID_HANDLE_VALUE) {
              error = makeError("InvalidStateError", "HID device is not open");
              return false;
            }
            handle = it->second->handle;
            usesReportId = feature ? it->second->usesFeatureReportId : it->second->usesOutputReportId;
          }

          Vector<uint8_t> payload;
          const bool expectReportIdByte = (reportId != 0) || usesReportId;
          if (expectReportIdByte) {
            payload.resize(1 + data.size());
            payload[0] = reportId;
            if (data.size() > 0) {
              memcpy(payload.data() + 1, data.data(), data.size());
            }
          } else {
            if (data.size() == 0) {
              payload.push_back(0x00);
            } else {
              payload.assign(data.begin(), data.end());
            }
          }

          if (payload.size() > static_cast<size_t>(std::numeric_limits<ULONG>::max())) {
            error = makeError("OperationError", "HID report is too large");
            return false;
          }

          BOOL ok = FALSE;
          if (feature) {
            ok = HidD_SetFeature(handle, payload.data(), static_cast<ULONG>(payload.size()));
          } else {
            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent) {
              error = makeError("OperationError", "Failed to create HID write event");
              return false;
            }

            DWORD bytesWritten = 0;
            ok = WriteFile(
              handle,
              payload.data(),
              static_cast<DWORD>(payload.size()),
              &bytesWritten,
              &overlapped
            );
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
              const auto waitResult = WaitForSingleObject(overlapped.hEvent, 5000);
              if (waitResult == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(handle, &overlapped, &bytesWritten, FALSE);
              } else {
                CancelIoEx(handle, &overlapped);
                WaitForSingleObject(overlapped.hEvent, INFINITE);
                ok = FALSE;
              }
            }
            CloseHandle(overlapped.hEvent);
            ok = ok && bytesWritten == payload.size();
          }

          if (!ok) {
            error = makeError("OperationError", "Failed to send HID report");
            return false;
          }
          return true;
        }

        JSON::Object receiveFeatureReportInternal(const String& deviceId, uint8_t reportId, uint16_t length, JSON::Object& error) {
          HANDLE handle = INVALID_HANDLE_VALUE;
          uint16_t expected = length;
          bool usesReportId = false;
          {
            Lock lock(this->mutex);
            auto it = this->openDevices.find(deviceId);
            if (it == this->openDevices.end() || !it->second || it->second->handle == INVALID_HANDLE_VALUE) {
              error = makeError("InvalidStateError", "HID device is not open");
              return JSON::Object{};
            }
            handle = it->second->handle;
            usesReportId = it->second->usesFeatureReportId;
            if (expected == 0) expected = it->second->featureReportSize;
            if (expected == 0) expected = it->second->reportSize;
          }

          if (expected == 0) expected = 64;
          const bool expectReportId = (reportId != 0) || usesReportId;
          size_t bufferLength = static_cast<size_t>(expected);
          if (bufferLength == 0) bufferLength = 1;
          if (expectReportId && bufferLength < 1) bufferLength = 1;
          Vector<uint8_t> buffer(bufferLength);
          if (!buffer.empty()) {
            buffer[0] = reportId;
          }
          if (!HidD_GetFeature(handle, buffer.data(), static_cast<ULONG>(buffer.size()))) {
            error = makeError("OperationError", "HidD_GetFeature failed");
            return JSON::Object{};
          }

          uint8_t resolvedId = reportId;
          size_t offset = 0;
          if (!buffer.empty() && expectReportId) {
            resolvedId = buffer[0];
            offset = 1;
          }

          bytes::Buffer payload = slicePayload(buffer.data(), buffer.size(), offset);
          return JSON::Object::Entries {{
            {"data", JSON::Object::Entries {{
              {"reportId", static_cast<uint32_t>(resolvedId)},
              {"encoding", String("base64")},
              {"data", payload.str(bytes::Buffer::Encoding::BASE64)}
            }}}
          }};
        }
      };
  }

  std::unique_ptr<HID::Backend> makeWindowsHIDBackend(HID& service) {
    return std::make_unique<WindowsHIDBackend>(service);
  }
}

#endif
