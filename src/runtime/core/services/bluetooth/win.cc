#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <combaseapi.h>
#include <bluetoothapis.h>
#include <bluetoothleapis.h>
#include <robuffer.h>
#include <setupapi.h>
#pragma comment(lib, "runtimeobject")
#pragma comment(lib, "windowsapp")
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <roapi.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.devices.bluetooth.advertisement.h>
#include <windows.storage.streams.h>
#include "../../../app.hh"
#include "../../../http.hh"
#include "../../../debug.hh"
#include "../../../runtime.hh"
#include "../bluetooth.hh"
#include "../../../string.hh"
#include "../../../bytes.hh"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstring>

// Dynamic Bluetooth GATT API loader (no static link deps)
namespace {
  static const GUID BLUETOOTH_LE_DEVICE_INTERFACE_GUID = {
    0x781aee18,
    0x7733,
    0x4ce4,
    {0xad, 0xd0, 0x91, 0xf4, 0x1c, 0x67, 0xb5, 0x92}
  };

  struct GattFns {
    HMODULE mod = nullptr;
    decltype(&BluetoothGATTGetServices) GetServices = nullptr;
    decltype(&BluetoothGATTGetCharacteristics) GetCharacteristics = nullptr;
    decltype(&BluetoothGATTGetCharacteristicValue) ReadCharacteristicValue = nullptr;
    decltype(&BluetoothGATTSetCharacteristicValue) WriteCharacteristicValue = nullptr;
    decltype(&BluetoothGATTRegisterEvent) RegisterEvent = nullptr;
    decltype(&BluetoothGATTUnregisterEvent) UnregisterEvent = nullptr;
  };

  static GattFns& gatt() {
    static GattFns fns;
    if (!fns.mod) {
      fns.mod = LoadLibraryA("BluetoothAPIs.dll");
      if (fns.mod) {
        fns.GetServices = reinterpret_cast<decltype(fns.GetServices)>(GetProcAddress(fns.mod, "BluetoothGATTGetServices"));
        fns.GetCharacteristics = reinterpret_cast<decltype(fns.GetCharacteristics)>(GetProcAddress(fns.mod, "BluetoothGATTGetCharacteristics"));
        fns.ReadCharacteristicValue = reinterpret_cast<decltype(fns.ReadCharacteristicValue)>(GetProcAddress(fns.mod, "BluetoothGATTGetCharacteristicValue"));
        fns.WriteCharacteristicValue = reinterpret_cast<decltype(fns.WriteCharacteristicValue)>(GetProcAddress(fns.mod, "BluetoothGATTSetCharacteristicValue"));
        fns.RegisterEvent = reinterpret_cast<decltype(fns.RegisterEvent)>(GetProcAddress(fns.mod, "BluetoothGATTRegisterEvent"));
        fns.UnregisterEvent = reinterpret_cast<decltype(fns.UnregisterEvent)>(GetProcAddress(fns.mod, "BluetoothGATTUnregisterEvent"));
      }
    }
    return fns;
  }

  static inline bool isLoadedGatt() {
    auto &f = gatt();
    return f.mod && f.GetServices && f.GetCharacteristics && f.ReadCharacteristicValue && f.WriteCharacteristicValue && f.RegisterEvent && f.UnregisterEvent;
  }

  static inline std::string guidToString(const GUID& g) {
    char buf[37];
    snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      (unsigned) g.Data1, g.Data2, g.Data3,
      g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    // to lower
    for (int i = 0; i < 36; ++i) buf[i] = (char) ::tolower((unsigned char) buf[i]);
    return std::string(buf);
  }

  static inline GUID bthLeUuidToGuid(const BTH_LE_UUID& uuid) {
    if (!uuid.IsShortUuid) {
      return uuid.Value.LongUuid;
    }

    return GUID {
      static_cast<unsigned long>(uuid.Value.ShortUuid),
      0x0000,
      0x1000,
      {0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}
    };
  }

  static inline std::string bthLeUuidToString(const BTH_LE_UUID& uuid) {
    return guidToString(bthLeUuidToGuid(uuid));
  }

  static inline bool bthLeUuidEqualsGuid(const BTH_LE_UUID& uuid, const GUID& guid) {
    const auto expanded = bthLeUuidToGuid(uuid);
    return std::memcmp(&expanded, &guid, sizeof(GUID)) == 0;
  }

  static inline bool parseHex(
    const std::string& input,
    size_t offset,
    size_t length,
    uint32_t& output
  ) {
    output = 0;
    if (offset + length > input.size()) {
      return false;
    }

    for (size_t i = offset; i < offset + length; ++i) {
      const auto c = input[i];
      uint32_t value = 0;
      if (c >= '0' && c <= '9') {
        value = static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value = static_cast<uint32_t>(10 + c - 'a');
      } else if (c >= 'A' && c <= 'F') {
        value = static_cast<uint32_t>(10 + c - 'A');
      } else {
        return false;
      }
      output = (output << 4) | value;
    }
    return true;
  }

  static inline bool parseGuid(const std::string& in, GUID& out) {
    if (in.size() == 4) {
      uint32_t shortId = 0;
      if (!parseHex(in, 0, 4, shortId)) {
        return false;
      }
      out = GUID {
        static_cast<unsigned long>(shortId),
        0x0000,
        0x1000,
        {0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}
      };
      return true;
    }

    if (
      in.size() != 36 ||
      in[8] != '-' ||
      in[13] != '-' ||
      in[18] != '-' ||
      in[23] != '-'
    ) {
      return false;
    }

    uint32_t segments[11] = {};
    if (
      !parseHex(in, 0, 8, segments[0]) ||
      !parseHex(in, 9, 4, segments[1]) ||
      !parseHex(in, 14, 4, segments[2]) ||
      !parseHex(in, 19, 2, segments[3]) ||
      !parseHex(in, 21, 2, segments[4]) ||
      !parseHex(in, 24, 2, segments[5]) ||
      !parseHex(in, 26, 2, segments[6]) ||
      !parseHex(in, 28, 2, segments[7]) ||
      !parseHex(in, 30, 2, segments[8]) ||
      !parseHex(in, 32, 2, segments[9]) ||
      !parseHex(in, 34, 2, segments[10])
    ) {
      return false;
    }

    out.Data1 = static_cast<unsigned long>(segments[0]);
    out.Data2 = static_cast<unsigned short>(segments[1]);
    out.Data3 = static_cast<unsigned short>(segments[2]);
    for (size_t i = 0; i < 8; ++i) {
      out.Data4[i] = static_cast<unsigned char>(segments[i + 3]);
    }
    return true;
  }
}

using oro::runtime::core::services::Bluetooth;
using oro::runtime::String;
using oro::runtime::Vector;
using oro::runtime::Map;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
using ABI::Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs;
using ABI::Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEAdvertisementWatcher;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEAdvertisementReceivedEventArgs;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEAdvertisement;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEManufacturerData;
using ABI::Windows::Devices::Bluetooth::Advertisement::BluetoothLEScanningMode_Active;
using ABI::Windows::Foundation::ITypedEventHandler;
using ABI::Windows::Foundation::Collections::IVector;
using ABI::Windows::Storage::Streams::IBuffer;
using Windows::Storage::Streams::IBufferByteAccess;
namespace JSON = oro::runtime::JSON;
namespace bytes = oro::runtime::bytes;
namespace http = oro::runtime::http;

using AdvertisementReceivedHandler = ABI::Windows::Foundation::ITypedEventHandler<
  BluetoothLEAdvertisementWatcher*,
  BluetoothLEAdvertisementReceivedEventArgs*
>;

namespace {
  class WindowsBackend final : public Bluetooth::Backend {
    Bluetooth& svc;
    // Pending chooser state
    bool choosing = false;
    std::string pendingSeq;
    oro::runtime::core::Service::Callback pendingCb = nullptr;
    // Connected device handles by normalized deviceId
    std::unordered_map<std::string, HANDLE> connections;
    Bluetooth::ParsedRequestDeviceOptions requestFilters;
    bool manufacturerFilterActive = false;
    bool watcherStarted = false;
    bool watcherHandlerRegistered = false;
    bool roInitializedByBackend = false;
    std::jthread discoveryTimeoutThread;
    EventRegistrationToken watcherToken{};
    ComPtr<IBluetoothLEAdvertisementWatcher> watcher;
    ComPtr<AdvertisementReceivedHandler> watcherHandler;
    std::unordered_set<std::string> watcherSeen;
    struct DeviceInfo {
      String name;
      int rssi = 0;
      Vector<String> services;
      Map<uint16_t, Vector<uint8_t>> manufacturer;
    };
    std::unordered_map<std::string, DeviceInfo> discovered;

  public:
    WindowsBackend(Bluetooth& service) : svc(service) {}
    ~WindowsBackend() override {
      stopDiscoveryTimeout();
      stopWatcher();

      if (!subscriptions.empty()) {
        auto &f = gatt();
        if (f.UnregisterEvent) {
          for (const auto& entry : subscriptions) {
            if (entry.second.first) {
              f.UnregisterEvent(entry.second.first, BLUETOOTH_GATT_FLAG_NONE);
            }
          }
        }
        subscriptions.clear();
      }

      for (const auto& entry : connections) {
        if (entry.second && entry.second != INVALID_HANDLE_VALUE) {
          CloseHandle(entry.second);
        }
      }
      connections.clear();

      if (roInitializedByBackend) {
        RoUninitialize();
      }
    }

    static std::string bluetoothAddressToString(UINT64 address) {
      char buf[18];
      snprintf(buf, sizeof(buf), "%02llX:%02llX:%02llX:%02llX:%02llX:%02llX",
        (address >> 40) & 0xFF,
        (address >> 32) & 0xFF,
        (address >> 24) & 0xFF,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF);
      return std::string(buf);
    }

    static std::string hstringToUtf8(const HString& hstr) {
      unsigned int len = 0;
      const wchar_t* buffer = hstr.GetRawBuffer(&len);
      if (!buffer || len == 0) return std::string();
      return oro::runtime::string::convertWStringToString(
        oro::runtime::WString(buffer, len)
      );
    }

    HRESULT ensureWatcherStarted() {
      if (watcherStarted) return S_OK;
      if (!roInitializedByBackend) {
        HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;
        roInitializedByBackend = SUCCEEDED(hr);
      }

      HStringReference className(RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementWatcher);
      ComPtr<IActivationFactory> factory;
      HRESULT hr = RoGetActivationFactory(className.Get(), IID_PPV_ARGS(&factory));
      if (FAILED(hr)) return hr;

      ComPtr<IInspectable> inspectable;
      hr = factory->ActivateInstance(&inspectable);
      if (FAILED(hr)) return hr;

      hr = inspectable.As(&watcher);
      if (FAILED(hr)) return hr;

      hr = watcher->put_ScanningMode(BluetoothLEScanningMode_Active);
      if (FAILED(hr)) {
        watcher.Reset();
        return hr;
      }

      watcherHandler = Callback<AdvertisementReceivedHandler>(
        [this](IBluetoothLEAdvertisementWatcher*, IBluetoothLEAdvertisementReceivedEventArgs* args) -> HRESULT {
          this->handleAdvertisement(args);
          return S_OK;
        }
      );
      if (!watcherHandler) {
        watcher.Reset();
        return E_FAIL;
      }

      hr = watcher->add_Received(watcherHandler.Get(), &watcherToken);
      if (FAILED(hr)) {
        watcherHandler.Reset();
        watcher.Reset();
        return hr;
      }
      watcherHandlerRegistered = true;

      watcherSeen.clear();
      hr = watcher->Start();
      if (FAILED(hr)) {
        watcher->remove_Received(watcherToken);
        watcherHandlerRegistered = false;
        watcherHandler.Reset();
        watcher.Reset();
        return hr;
      }
      watcherStarted = true;
      return S_OK;
    }

    void stopWatcher() {
      if (!watcher) return;
      if (watcherStarted) {
        watcher->Stop();
      }
      if (watcherHandlerRegistered) {
        watcher->remove_Received(watcherToken);
      }
      watcher.Reset();
      watcherHandler.Reset();
      watcherStarted = false;
      watcherHandlerRegistered = false;
      watcherSeen.clear();
      manufacturerFilterActive = false;
    }

    void stopDiscoveryTimeout() {
      if (discoveryTimeoutThread.joinable()) {
        discoveryTimeoutThread.request_stop();
        discoveryTimeoutThread.join();
      }
    }

    void handleAdvertisement(IBluetoothLEAdvertisementReceivedEventArgs* args) {
      if (!choosing) return;
      UINT64 address = 0;
      if (FAILED(args->get_BluetoothAddress(&address))) return;

      int16_t rssi = 0;
      args->get_RawSignalStrengthInDBm(&rssi);

      ComPtr<IBluetoothLEAdvertisement> advertisement;
      if (FAILED(args->get_Advertisement(&advertisement)) || !advertisement) return;

      HString localName;
      advertisement->get_LocalName(localName.GetAddressOf());
      std::string name = hstringToUtf8(localName);

      std::vector<std::string> services;
      ComPtr<IVector<GUID>> serviceUuids;
      if (SUCCEEDED(advertisement->get_ServiceUuids(&serviceUuids)) && serviceUuids) {
        unsigned int size = 0;
        serviceUuids->get_Size(&size);
        for (unsigned int i = 0; i < size; ++i) {
          GUID guid{};
          if (SUCCEEDED(serviceUuids->GetAt(i, &guid))) {
            services.push_back(guidToString(guid));
          }
        }
      }

      std::vector<std::pair<uint16_t, std::vector<uint8_t>>> manufacturer;
      ComPtr<IVector<ABI::Windows::Devices::Bluetooth::Advertisement::BluetoothLEManufacturerData*>> manufVector;
      if (SUCCEEDED(advertisement->get_ManufacturerData(&manufVector)) && manufVector) {
        unsigned int count = 0;
        manufVector->get_Size(&count);
        for (unsigned int i = 0; i < count; ++i) {
          ComPtr<IBluetoothLEManufacturerData> manuf;
          if (FAILED(manufVector->GetAt(i, &manuf)) || !manuf) continue;
          uint16_t companyId = 0;
          if (FAILED(manuf->get_CompanyId(&companyId))) continue;
          ComPtr<IBuffer> buffer;
          if (FAILED(manuf->get_Data(&buffer)) || !buffer) {
            manufacturer.emplace_back(companyId, std::vector<uint8_t>{});
            continue;
          }
          UINT32 len = 0;
          buffer->get_Length(&len);
          std::vector<uint8_t> bytes(len);
          if (len > 0) {
            ComPtr<IBufferByteAccess> byteAccess;
            if (SUCCEEDED(buffer.As(&byteAccess)) && byteAccess) {
              BYTE* raw = nullptr;
              if (SUCCEEDED(byteAccess->Buffer(&raw)) && raw) {
                memcpy(bytes.data(), raw, len);
              }
            }
          }
          manufacturer.emplace_back(companyId, std::move(bytes));
        }
      }

      const std::string deviceId = normalizeMac(bluetoothAddressToString(address));
      if (!watcherSeen.insert(deviceId).second && manufacturer.empty()) {
        return;
      }

      svc.loop.dispatch([this, deviceId, name, rssi, services, manufacturer]() {
        this->processDeviceCandidate(deviceId, name, rssi, services, manufacturer);
      });
    }

    void processDeviceCandidate(
      const std::string& id,
      const std::string& name,
      int rssi,
      const std::vector<std::string>& servicesIn,
      const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& manufacturerVec
    ) {
      if (!choosing) return;

      Vector<String> services;
      services.reserve(servicesIn.size());
      for (const auto& s : servicesIn) {
        services.push_back(Bluetooth::normalizeUUID(String(s.c_str())));
      }

      Map<uint16_t, Vector<uint8_t>> manufacturer;
      for (const auto& entry : manufacturerVec) {
        Vector<uint8_t> bytes;
        bytes.reserve(entry.second.size());
        for (auto b : entry.second) bytes.push_back(b);
        manufacturer[entry.first] = std::move(bytes);
      }

      if (!Bluetooth::anyFilterMatches(requestFilters, String(name.c_str()), services, manufacturer)) {
        return;
      }

      DeviceInfo info;
      info.name = String(name.c_str());
      info.rssi = rssi;
      info.services = services;
      info.manufacturer = manufacturer;
      discovered[id] = info;

      JSON::Object evt = JSON::Object::Entries {{
        {"id", String(id.c_str())},
        {"name", info.name},
        {"rssi", (int64_t) rssi}
      }};
      if (!services.empty()) {
        JSON::Array servicesJson;
        for (const auto& s : services) servicesJson.push(s);
        evt.set("services", servicesJson);
      }
      if (!manufacturerVec.empty()) {
        JSON::Array mdJson;
        for (const auto& entry : manufacturerVec) {
          JSON::Object md = JSON::Object::Entries {{
            {"companyId", (int64_t) entry.first},
            {"encoding", String("base64")}
          }};
          if (!entry.second.empty()) {
            bytes::Buffer buf(entry.second.size());
            memcpy(buf.data(), entry.second.data(), entry.second.size());
            md.set("data", buf.str(bytes::Buffer::Encoding::BASE64));
          } else {
            md.set("data", String(""));
          }
          mdJson.push(md);
        }
        evt.set("manufacturerData", mdJson);
      }

      if (auto app = oro::runtime::app::App::sharedApplication()) {
        for (const auto &win : app->runtime.windowManager.windows) {
          if (win && win->bridge) win->bridge->emit("bluetooth.devicefound", evt.str());
        }
      }
    }

    void getAvailability(const std::string& seq, const oro::runtime::core::Service::Callback cb) const override {
      const bool allowed = svc.context.getRuntime()->hasPermission("bluetooth");
      bool available = false;

      // Dynamically probe classic Bluetooth radio presence without a static
      // BluetoothApis import-library dependency.
      HMODULE bth = LoadLibraryA("BluetoothApis.dll");
      if (bth) {
        BLUETOOTH_FIND_RADIO_PARAMS params{ sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
        using PFN_BluetoothFindFirstRadio = decltype(&BluetoothFindFirstRadio);
        using PFN_BluetoothFindNextRadio = decltype(&BluetoothFindNextRadio);
        using PFN_BluetoothFindRadioClose = decltype(&BluetoothFindRadioClose);

        auto pFirst = reinterpret_cast<PFN_BluetoothFindFirstRadio>(GetProcAddress(bth, "BluetoothFindFirstRadio"));
        auto pNext = reinterpret_cast<PFN_BluetoothFindNextRadio>(GetProcAddress(bth, "BluetoothFindNextRadio"));
        auto pClose = reinterpret_cast<PFN_BluetoothFindRadioClose>(GetProcAddress(bth, "BluetoothFindRadioClose"));
        if (pFirst && pNext && pClose) {
          HANDLE hRadio = nullptr; HBLUETOOTH_RADIO_FIND hFind = pFirst(&params, &hRadio);
          if (hFind) {
            // Found at least one radio
            available = true;
            if (hRadio) CloseHandle(hRadio);
            // Close search handle
            pClose(hFind);
          }
        }
        FreeLibrary(bth);
      }

    JSON::Object json = JSON::Object::Entries {{
      "data", JSON::Object::Entries {{"available", allowed && available}}
    }};
    cb(seq, json, oro::runtime::QueuedResponse{});
  }

    void getDevices(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object json = JSON::Object::Entries {{
        "data", JSON::Object::Entries {{"devices", JSON::Array {}}}
      }};
      cb(seq, json, oro::runtime::QueuedResponse{});
    }

    // Minimal requestDevice: enumerate remembered/connected/authenticated devices via classic APIs and emit chooser events.
    void requestDevice(const std::string& seq, const JSON::Any& options, const oro::runtime::core::Service::Callback cb) override {
      auto matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::All;
      if (auto runtime = svc.context.getRuntime()) {
        const auto& cfg = runtime->userConfig;
        if (cfg.contains("web_bluetooth_services_match")) {
          auto mode = oro::runtime::string::toLowerCase(cfg.at("web_bluetooth_services_match"));
          if (mode == "any") {
            matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::Any;
          }
        }
      }

      JSON::Object error;
      if (!Bluetooth::parseRequestDeviceOptions(options, requestFilters, &error, matchMode)) {
        cb(seq, JSON::Object::Entries {{"err", error}}, oro::runtime::QueuedResponse{});
        return;
      }
      if (!svc.context.getRuntime()->hasPermission("bluetooth")) {
        JSON::Object json = JSON::Object::Entries {{
          "err",
          JSON::Object::Entries {{
            {"type", "NotAllowedError"},
            {"message", "Bluetooth permission is disabled by runtime configuration"}
          }}
        }};
        return cb(seq, json, oro::runtime::QueuedResponse{});
      }

      discovered.clear();
      watcherSeen.clear();

      int timeoutMs = requestFilters.timeoutMs;
      if (timeoutMs <= 0) {
        if (auto runtime = svc.context.getRuntime()) {
          const auto& cfg = runtime->userConfig;
          if (cfg.contains("web_bluetooth_timeout_ms")) {
            try { timeoutMs = std::stoi(cfg.at("web_bluetooth_timeout_ms")); }
            catch (...) { timeoutMs = -1; }
          }
        }
      }
      if (timeoutMs <= 0) timeoutMs = 15000;

      choosing = true; pendingSeq = seq; pendingCb = cb;

      // Inform renderer to present chooser
      if (auto app = oro::runtime::app::App::sharedApplication()) {
        JSON::Object evt = JSON::Object::Entries {{"reason", "requestDevice"}};
        for (const auto &win : app->runtime.windowManager.windows) {
          if (win && win->bridge) win->bridge->emit("bluetooth.chooserequest", evt.str());
        }
      }

      stopDiscoveryTimeout();
      discoveryTimeoutThread = std::jthread([this, timeoutMs](std::stop_token token) {
        const auto deadline = std::chrono::steady_clock::now() +
          std::chrono::milliseconds(timeoutMs);
        while (!token.stop_requested() && std::chrono::steady_clock::now() < deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (token.stop_requested()) {
          return;
        }

        svc.loop.dispatch([this, token]() {
          if (token.stop_requested()) return;
          if (!choosing) return;
          choosing = false;
          if (!pendingSeq.empty() && pendingCb) {
            JSON::Object err = JSON::Object::Entries {{
              {"type", "NotFoundError"},
              {"message", "Timed out"}
            }};
            pendingCb(pendingSeq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
            pendingSeq.clear();
            pendingCb = nullptr;
          }
          stopWatcher();
          discovered.clear();
        });
      });

      // Enumerate radios
      HMODULE bth = LoadLibraryA("BluetoothApis.dll");
      if (!bth) return; // silent failure; no devices to emit

      BLUETOOTH_FIND_RADIO_PARAMS rparams{ sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
      using PFN_BluetoothFindFirstRadio = decltype(&BluetoothFindFirstRadio);
      using PFN_BluetoothFindNextRadio = decltype(&BluetoothFindNextRadio);
      using PFN_BluetoothFindRadioClose = decltype(&BluetoothFindRadioClose);
      auto pFirstRadio = reinterpret_cast<PFN_BluetoothFindFirstRadio>(GetProcAddress(bth, "BluetoothFindFirstRadio"));
      auto pNextRadio = reinterpret_cast<PFN_BluetoothFindNextRadio>(GetProcAddress(bth, "BluetoothFindNextRadio"));
      auto pCloseRadio = reinterpret_cast<PFN_BluetoothFindRadioClose>(GetProcAddress(bth, "BluetoothFindRadioClose"));

      if (!pFirstRadio || !pNextRadio || !pCloseRadio) { FreeLibrary(bth); return; }

      const bool hasFilters = !requestFilters.acceptAllDevices && !requestFilters.filters.empty();
      manufacturerFilterActive = false;
      if (hasFilters) {
        for (const auto& filter : requestFilters.filters) {
          if (!filter.manufacturerData.empty()) { manufacturerFilterActive = true; break; }
        }
      }
      if (manufacturerFilterActive) {
        if (FAILED(ensureWatcherStarted())) {
          manufacturerFilterActive = false;
        }
      } else {
        stopWatcher();
      }
      bool servicesLoaded = false;
      bool servicesLoadOk = false;
      std::unordered_set<std::string> deviceServices;
      bool manufacturerFilterPresent = false;

      auto loadServices = [&](const std::string& deviceId) -> bool {
        if (servicesLoaded) return servicesLoadOk;
        servicesLoaded = true;
        servicesLoadOk = false;
        if (!isLoadedGatt()) return false;
        const std::wstring path = findBleDevicePath(deviceId);
        if (path.empty()) return false;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE || h == nullptr) {
          return false;
        }
        auto &f = gatt();
        USHORT count = 0;
        HRESULT hr = f.GetServices(h, 0, nullptr, &count, BLUETOOTH_GATT_FLAG_NONE);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
          CloseHandle(h);
          return false;
        }
        std::vector<BTH_LE_GATT_SERVICE> svcs; svcs.resize(count);
        if (count > 0) {
          hr = f.GetServices(h, count, svcs.data(), &count, BLUETOOTH_GATT_FLAG_NONE);
          if (FAILED(hr)) {
            CloseHandle(h);
            return false;
          }
        }
        deviceServices.clear();
        for (auto &svc : svcs) {
          auto uuid = bthLeUuidToString(svc.ServiceUuid);
          auto normalized = Bluetooth::normalizeUUID(String(uuid.c_str()));
          deviceServices.insert(std::string(normalized.c_str()));
        }
        CloseHandle(h);
        servicesLoadOk = true;
        return true;
      };

      using PFN_BluetoothFindFirstDevice = decltype(&BluetoothFindFirstDevice);
      using PFN_BluetoothFindNextDevice = decltype(&BluetoothFindNextDevice);
      using PFN_BluetoothFindDeviceClose = decltype(&BluetoothFindDeviceClose);
      using PFN_BluetoothGetDeviceInfo = decltype(&BluetoothGetDeviceInfo);

      auto pFirstDev = reinterpret_cast<PFN_BluetoothFindFirstDevice>(GetProcAddress(bth, "BluetoothFindFirstDevice"));
      auto pNextDev = reinterpret_cast<PFN_BluetoothFindNextDevice>(GetProcAddress(bth, "BluetoothFindNextDevice"));
      auto pCloseDev = reinterpret_cast<PFN_BluetoothFindDeviceClose>(GetProcAddress(bth, "BluetoothFindDeviceClose"));
      auto pGetInfo = reinterpret_cast<PFN_BluetoothGetDeviceInfo>(GetProcAddress(bth, "BluetoothGetDeviceInfo"));

      HANDLE hRadio = nullptr; HBLUETOOTH_RADIO_FIND hFind = pFirstRadio(&rparams, &hRadio);
      while (hFind && hRadio) {
        BLUETOOTH_DEVICE_SEARCH_PARAMS sparams{};
        sparams.dwSize = sizeof(sparams);
        sparams.fReturnAuthenticated = TRUE;
        sparams.fReturnRemembered = TRUE;
        sparams.fReturnUnknown = FALSE;
        sparams.fReturnConnected = TRUE;
        sparams.fIssueInquiry = FALSE; // no active inquiry for speed
        sparams.cTimeoutMultiplier = 0;
        sparams.hRadio = hRadio;

        BLUETOOTH_DEVICE_INFO info{}; info.dwSize = sizeof(info);
        HBLUETOOTH_DEVICE_FIND hDev = pFirstDev ? pFirstDev(&sparams, &info) : nullptr;
        while (hDev) {
          servicesLoaded = false;
          servicesLoadOk = false;
          deviceServices.clear();

          // Ensure full info
          if (pGetInfo) pGetInfo(hRadio, &info);
          // Build device id string from address (big-endian hex pairs)
          const ULONGLONG a = info.Address.ullLong;
          char idbuf[18];
          snprintf(idbuf, sizeof(idbuf), "%02llX:%02llX:%02llX:%02llX:%02llX:%02llX",
            (a >> 40) & 0xFF, (a >> 32) & 0xFF, (a >> 24) & 0xFF,
            (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
          // Convert the bounded UTF-16 device name to UTF-8.
          size_t nameLength = 0;
          while (
            nameLength < ARRAYSIZE(info.szName) &&
            info.szName[nameLength] != L'\0'
          ) {
            nameLength++;
          }
          const auto name = oro::runtime::string::convertWStringToString(
            oro::runtime::WString(info.szName, nameLength)
          );
          const std::string deviceId = normalizeMac(idbuf);
          bool show = requestFilters.acceptAllDevices;
          if (!show && hasFilters) {
            for (const auto& filter : requestFilters.filters) {
              bool matches = true;
              if (filter.hasName && name != filter.name.c_str()) matches = false;
              if (matches && filter.hasNamePrefix) {
                const std::string prefix = filter.namePrefix.c_str();
                if (name.rfind(prefix, 0) != 0) matches = false;
              }
              if (matches && !filter.manufacturerData.empty()) {
                manufacturerFilterPresent = true;
                matches = false; // manufacturer filtering unsupported on Windows
              }
              if (matches && !filter.services.empty()) {
                if (!loadServices(deviceId) || deviceServices.empty()) {
                  matches = false;
                } else if (requestFilters.servicesMatch == Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::All) {
                  for (const auto& svc : filter.services) {
                    auto normalized = Bluetooth::normalizeUUID(svc);
                    if (!deviceServices.count(std::string(normalized.c_str()))) {
                      matches = false;
                      break;
                    }
                  }
                } else {
                  bool anyPresent = false;
                  for (const auto& svc : filter.services) {
                    auto normalized = Bluetooth::normalizeUUID(svc);
                    if (deviceServices.count(std::string(normalized.c_str()))) {
                      anyPresent = true;
                      break;
                    }
                  }
                  matches = anyPresent;
                }
              }
              if (matches) {
                show = true;
                break;
              }
            }
          }

          if (!show) {
            BLUETOOTH_DEVICE_INFO next{};
            next.dwSize = sizeof(next);
            if (!pNextDev(hDev, &next)) {
              pCloseDev(hDev);
              hDev = nullptr;
            } else {
              info = next;
            }
            continue;
          }

          std::vector<std::string> serviceList;
          serviceList.reserve(deviceServices.size());
          for (const auto& svcName : deviceServices) serviceList.push_back(svcName);
          std::vector<std::pair<uint16_t, std::vector<uint8_t>>> emptyManufacturer;
          processDeviceCandidate(deviceId, name, 0, serviceList, emptyManufacturer);

          BLUETOOTH_DEVICE_INFO next{}; next.dwSize = sizeof(next);
          if (!pNextDev(hDev, &next)) { pCloseDev(hDev); hDev = nullptr; }
          else info = next;
        }

        CloseHandle(hRadio);
        HANDLE nextRadio = nullptr;
        if (!pNextRadio(hFind, &nextRadio)) { pCloseRadio(hFind); hFind = nullptr; }
        hRadio = nextRadio;
      }

      if (manufacturerFilterPresent && !manufacturerFilterActive && choosing && pendingCb) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", "Bluetooth manufacturerData filters are not supported on Windows"}
        }};
        pendingCb(pendingSeq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        pendingSeq.clear(); pendingCb = nullptr;
        choosing = false;
        stopDiscoveryTimeout();
      }

      FreeLibrary(bth);
    }
    static inline std::string normalizeMac(const std::string& mac) {
      std::string s; s.reserve(mac.size());
      for (auto c : mac) { if (c != ':' && c != '-') s.push_back((char) ::toupper((unsigned char)c)); }
      return s;
    }

    static inline std::string subscriptionKey(
      const std::string& deviceId,
      const std::string& serviceUuid,
      const std::string& characteristicUuid
    ) {
      return normalizeMac(deviceId) + "|" +
        Bluetooth::normalizeUUID(serviceUuid) + "|" +
        Bluetooth::normalizeUUID(characteristicUuid);
    }

    static inline std::wstring toLower(const std::wstring& in) {
      std::wstring out; out.resize(in.size());
      for (size_t i = 0; i < in.size(); ++i) out[i] = (WCHAR) ::towlower(in[i]);
      return out;
    }

    static std::wstring findBleDevicePath(const std::string& macRaw) {
      const std::string mac = normalizeMac(macRaw);
      HMODULE setup = LoadLibraryA("Setupapi.dll");
      if (!setup) {
        return L"";
      }
      using PFN_SetupDiGetClassDevsW = decltype(&SetupDiGetClassDevsW);
      using PFN_SetupDiEnumDeviceInterfaces = decltype(&SetupDiEnumDeviceInterfaces);
      using PFN_SetupDiGetDeviceInterfaceDetailW = decltype(&SetupDiGetDeviceInterfaceDetailW);
      using PFN_SetupDiDestroyDeviceInfoList = decltype(&SetupDiDestroyDeviceInfoList);
      auto pGetClass = reinterpret_cast<PFN_SetupDiGetClassDevsW>(GetProcAddress(setup, "SetupDiGetClassDevsW"));
      auto pEnum = reinterpret_cast<PFN_SetupDiEnumDeviceInterfaces>(GetProcAddress(setup, "SetupDiEnumDeviceInterfaces"));
      auto pDetail = reinterpret_cast<PFN_SetupDiGetDeviceInterfaceDetailW>(GetProcAddress(setup, "SetupDiGetDeviceInterfaceDetailW"));
      auto pDestroy = reinterpret_cast<PFN_SetupDiDestroyDeviceInfoList>(GetProcAddress(setup, "SetupDiDestroyDeviceInfoList"));
      if (!pGetClass || !pEnum || !pDetail || !pDestroy) { FreeLibrary(setup); return L""; }

      HDEVINFO devs = pGetClass(&BLUETOOTH_LE_DEVICE_INTERFACE_GUID, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
      if (devs == INVALID_HANDLE_VALUE) { FreeLibrary(setup); return L""; }

      std::wstring found;
      for (DWORD i = 0; ; ++i) {
        SP_DEVICE_INTERFACE_DATA ifdata{}; ifdata.cbSize = sizeof(ifdata);
        if (!pEnum(devs, nullptr, &BLUETOOTH_LE_DEVICE_INTERFACE_GUID, i, &ifdata)) break;
        DWORD required = 0;
        pDetail(devs, &ifdata, nullptr, 0, &required, nullptr);
        if (required == 0) continue;
        std::vector<unsigned char> buf; buf.resize(required);
        auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!pDetail(devs, &ifdata, detail, required, nullptr, nullptr)) continue;
        std::wstring path(detail->DevicePath);
        std::wstring low = toLower(path);
        std::string macLower = mac; for (auto& c : macLower) c = (char) ::tolower((unsigned char) c);
        std::wstring wmac(macLower.begin(), macLower.end());
        if (low.find(wmac) != std::wstring::npos) { found = path; break; }
      }

      pDestroy(devs);
      FreeLibrary(setup);
      return found;
    }

    void gattConnect(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      const std::wstring path = findBleDevicePath(deviceId);
      if (path.empty()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Device path not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE || h == nullptr) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NetworkError"},
          {"message", "Failed to open device"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      const auto key = normalizeMac(deviceId);
      const auto existing = connections.find(key);
      if (existing != connections.end()) {
        if (existing->second && existing->second != INVALID_HANDLE_VALUE) {
          CloseHandle(existing->second);
        }
        connections.erase(existing);
      }
      connections[key] = h;
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }

    void gattDisconnect(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      const auto key = normalizeMac(deviceId);
      // Unregister any subscriptions for this device
      for (auto it = subscriptions.begin(); it != subscriptions.end(); ) {
        if (it->first.rfind(key + "|", 0) == 0) {
          auto &f = gatt();
          if (f.UnregisterEvent && it->second.first) {
            f.UnregisterEvent(it->second.first, BLUETOOTH_GATT_FLAG_NONE);
          }
          it = subscriptions.erase(it);
        } else {
          ++it;
        }
      }
      auto it = connections.find(key);
      if (it != connections.end()) {
        if (it->second && it->second != INVALID_HANDLE_VALUE) CloseHandle(it->second);
        connections.erase(it);
      }
      // Emit gattserverdisconnected to all windows
      if (auto app = oro::runtime::app::App::sharedApplication()) {
        oro::runtime::JSON::Object evt = oro::runtime::JSON::Object::Entries {{
          {"deviceId", oro::runtime::String(deviceId.c_str())},
          {"reason", oro::runtime::String("")}
        }};
        for (const auto &win : app->runtime.windowManager.windows) {
          if (win && win->bridge) win->bridge->emit("bluetooth.gattserverdisconnected", evt.str());
        }
      }
      JSON::Object j = JSON::Object::Entries {{"data", JSON::Object {}}};
      cb(seq, j, oro::runtime::QueuedResponse{});
    }
    void gattGetPrimaryService(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceFilter, const oro::runtime::core::Service::Callback cb) override {
      this->gattGetPrimaryServices(seq, deviceId, serviceFilter, [cb, serviceFilter](
        std::string responseSeq,
        JSON::Any json,
        oro::runtime::QueuedResponse queuedResponse
      ) {
        auto& response = json.as<JSON::Object>();
        if (response.has("err")) return cb(responseSeq, json, queuedResponse);
        auto& data = response.get("data").as<JSON::Object>();
        auto& services = data.get("services").as<JSON::Array>();
        const auto normalizedFilter = Bluetooth::normalizeUUID(serviceFilter);
        bool found = false;
        for (size_t i = 0; i < services.size(); ++i) {
          if (Bluetooth::normalizeUUID(services[i].str()) == normalizedFilter) {
            found = true;
            break;
          }
        }
        if (!found) {
          JSON::Object err = JSON::Object::Entries {{
            {"type", "NotFoundError"},
            {"message", "Service not found"}
          }};
          cb(responseSeq, JSON::Object::Entries {{"err", err}}, queuedResponse);
        } else {
          cb(responseSeq, JSON::Object::Entries {{
            "data",
            JSON::Object::Entries {{
              {"service", normalizedFilter}
            }}
          }}, queuedResponse);
        }
      });
    }

    void gattGetPrimaryServices(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceFilter, const oro::runtime::core::Service::Callback cb) override {
      auto it = connections.find(normalizeMac(deviceId));
      if (it == connections.end()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Device not connected"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      if (!isLoadedGatt()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", "Bluetooth GATT API unavailable"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      auto &f = gatt();
      USHORT count = 0;
      HRESULT hr = f.GetServices(it->second, 0, nullptr, &count, BLUETOOTH_GATT_FLAG_NONE);
      if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "OperationError"},
          {"message", "GetServices failed"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      std::vector<BTH_LE_GATT_SERVICE> svcs;
      svcs.resize(count);
      if (count > 0) {
        hr = f.GetServices(it->second, count, svcs.data(), &count, BLUETOOTH_GATT_FLAG_NONE);
        if (FAILED(hr)) {
          JSON::Object err = JSON::Object::Entries {{
            {"type", "OperationError"},
            {"message", "GetServices failed"}
          }};
          return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        }
      }
      JSON::Array arr;
      GUID filterGuid{};
      bool hasFilter = serviceFilter.size() > 0 && parseGuid(serviceFilter, filterGuid);
      for (auto &s : svcs) {
        const auto u = bthLeUuidToString(s.ServiceUuid);
        if (!hasFilter || _stricmp(u.c_str(), guidToString(filterGuid).c_str()) == 0) {
          arr.push(String(u.c_str()));
        }
      }
      if (hasFilter && arr.size() == 0) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Service not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      cb(seq, JSON::Object::Entries {{
        "data",
        JSON::Object::Entries {{
          {"services", arr}
        }}
      }}, oro::runtime::QueuedResponse{});
    }

    void serviceGetCharacteristic(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      this->serviceGetCharacteristics(seq, deviceId, serviceUuid, "", [cb, charUuid](
        std::string responseSeq,
        JSON::Any json,
        oro::runtime::QueuedResponse queuedResponse
      ) {
        auto& response = json.as<JSON::Object>();
        if (response.has("err")) return cb(responseSeq, json, queuedResponse);
        auto& data = response.get("data").as<JSON::Object>();
        auto& characteristics = data.get("characteristics").as<JSON::Array>();
        const auto normalizedUuid = Bluetooth::normalizeUUID(charUuid);
        bool found = false;
        for (size_t i = 0; i < characteristics.size(); ++i) {
          if (Bluetooth::normalizeUUID(characteristics[i].str()) == normalizedUuid) {
            found = true;
            break;
          }
        }
        if (!found) {
          JSON::Object err = JSON::Object::Entries {{
            {"type", "NotFoundError"},
            {"message", "Characteristic not found"}
          }};
          cb(responseSeq, JSON::Object::Entries {{"err", err}}, queuedResponse);
        } else {
          cb(responseSeq, JSON::Object::Entries {{
            "data",
            JSON::Object::Entries {{
              {"characteristic", normalizedUuid}
            }}
          }}, queuedResponse);
        }
      });
    }

    void serviceGetCharacteristics(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string&, const oro::runtime::core::Service::Callback cb) override {
      auto it = connections.find(normalizeMac(deviceId));
      if (it == connections.end()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Device not connected"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      if (!isLoadedGatt()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", "Bluetooth GATT API unavailable"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      auto &f = gatt();
      // Find the requested service
      USHORT count = 0;
      HRESULT hr = f.GetServices(it->second, 0, nullptr, &count, BLUETOOTH_GATT_FLAG_NONE);
      if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "OperationError"},
          {"message", "GetServices failed"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      std::vector<BTH_LE_GATT_SERVICE> svcs;
      svcs.resize(count);
      if (count > 0) {
        hr = f.GetServices(it->second, count, svcs.data(), &count, BLUETOOTH_GATT_FLAG_NONE);
        if (FAILED(hr)) {
          JSON::Object err = JSON::Object::Entries {{
            {"type", "OperationError"},
            {"message", "GetServices failed"}
          }};
          return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        }
      }
      GUID svcGuid{};
      if (!parseGuid(serviceUuid, svcGuid)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "Invalid service UUID"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      BTH_LE_GATT_SERVICE* target = nullptr;
      for (auto &s : svcs) {
        if (bthLeUuidEqualsGuid(s.ServiceUuid, svcGuid)) {
          target = &s;
          break;
        }
      }
      if (!target) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Service not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      // Get characteristics for target service
      USHORT cc = 0;
      hr = f.GetCharacteristics(it->second, target, 0, nullptr, &cc, BLUETOOTH_GATT_FLAG_NONE);
      if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "OperationError"},
          {"message", "GetCharacteristics failed"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      std::vector<BTH_LE_GATT_CHARACTERISTIC> clist;
      clist.resize(cc);
      if (cc > 0) {
        hr = f.GetCharacteristics(it->second, target, cc, clist.data(), &cc, BLUETOOTH_GATT_FLAG_NONE);
        if (FAILED(hr)) {
          JSON::Object err = JSON::Object::Entries {{
            {"type", "OperationError"},
            {"message", "GetCharacteristics failed"}
          }};
          return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
        }
      }
      JSON::Array arr;
      JSON::Object propsMap;
      for (auto &c : clist) {
        const auto u = bthLeUuidToString(c.CharacteristicUuid);
        arr.push(String(u.c_str()));
        JSON::Object props = JSON::Object::Entries {{
          {"broadcast", (bool) c.IsBroadcastable},
          {"read", (bool) c.IsReadable},
          {"writeWithoutResponse", (bool) c.IsWritableWithoutResponse},
          {"write", (bool) c.IsWritable},
          {"notify", (bool) c.IsNotifiable},
          {"indicate", (bool) c.IsIndicatable},
          {"authenticatedSignedWrites", (bool) c.IsSignedWritable},
          {"reliableWrite", false},
          {"writableAuxiliaries", (bool) c.HasExtendedProperties}
        }};
        propsMap.set(String(u.c_str()), props);
      }
      cb(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristics", arr}, {"propertiesByCharacteristic", propsMap}}}}, oro::runtime::QueuedResponse{});
    }

    void characteristicReadValue(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      auto it = connections.find(normalizeMac(deviceId));
      if (it == connections.end()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Device not connected"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      if (!isLoadedGatt()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", "Bluetooth GATT API unavailable"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      auto &f = gatt();
      // Find service and characteristic
      USHORT sc = 0;
      f.GetServices(it->second, 0, nullptr, &sc, BLUETOOTH_GATT_FLAG_NONE);
      std::vector<BTH_LE_GATT_SERVICE> svcs;
      svcs.resize(sc);
      if (sc > 0) {
        f.GetServices(it->second, sc, svcs.data(), &sc, BLUETOOTH_GATT_FLAG_NONE);
      }
      GUID svcGuid{};
      if (!parseGuid(serviceUuid, svcGuid)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "Invalid service UUID"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      BTH_LE_GATT_SERVICE* target = nullptr;
      for (auto &s : svcs) {
        if (bthLeUuidEqualsGuid(s.ServiceUuid, svcGuid)) {
          target = &s;
          break;
        }
      }
      if (!target) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Service not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      USHORT cc = 0;
      f.GetCharacteristics(it->second, target, 0, nullptr, &cc, BLUETOOTH_GATT_FLAG_NONE);
      std::vector<BTH_LE_GATT_CHARACTERISTIC> clist;
      clist.resize(cc);
      if (cc > 0) {
        f.GetCharacteristics(it->second, target, cc, clist.data(), &cc, BLUETOOTH_GATT_FLAG_NONE);
      }
      GUID chrGuid{};
      if (!parseGuid(charUuid, chrGuid)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "Invalid characteristic UUID"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      BTH_LE_GATT_CHARACTERISTIC* chr = nullptr;
      for (auto &c : clist) {
        if (bthLeUuidEqualsGuid(c.CharacteristicUuid, chrGuid)) {
          chr = &c;
          break;
        }
      }
      if (!chr) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Characteristic not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      USHORT needed = 0;
      HRESULT hr = f.ReadCharacteristicValue(it->second, chr, 0, nullptr, &needed, BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE);
      if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "OperationError"},
          {"message", "Read failed"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      std::vector<unsigned char> buf;
      buf.resize(needed);
      auto* val = reinterpret_cast<PBTH_LE_GATT_CHARACTERISTIC_VALUE>(buf.data());
      hr = f.ReadCharacteristicValue(it->second, chr, needed, val, &needed, BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE);
      if (FAILED(hr)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "OperationError"},
          {"message", "Read failed"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      const ULONG n = val->DataSize;
      bytes::Buffer data(n);
      if (n > 0) {
        memcpy(data.data(), val->Data, n);
      }
      http::Headers hdr;
      hdr.set("content-type", "application/octet-stream");
      hdr.set("content-length", (uint64_t) n);
      cb(seq, JSON::Object {}, oro::runtime::QueuedResponse{ oro::runtime::crypto::rand64(), 0, data.shared(), data.size(), hdr.str() });
    }

    void characteristicWriteValue(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const bytes::Buffer& value, const oro::runtime::core::Service::Callback cb) override {
      auto it = connections.find(normalizeMac(deviceId));
      if (it == connections.end()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Device not connected"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      if (!isLoadedGatt()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", "Bluetooth GATT API unavailable"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      auto &f = gatt();
      USHORT sc = 0;
      f.GetServices(it->second, 0, nullptr, &sc, BLUETOOTH_GATT_FLAG_NONE);
      std::vector<BTH_LE_GATT_SERVICE> svcs;
      svcs.resize(sc);
      if (sc > 0) {
        f.GetServices(it->second, sc, svcs.data(), &sc, BLUETOOTH_GATT_FLAG_NONE);
      }
      GUID svcGuid{};
      if (!parseGuid(serviceUuid, svcGuid)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "Invalid service UUID"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      BTH_LE_GATT_SERVICE* target = nullptr;
      for (auto &s : svcs) {
        if (bthLeUuidEqualsGuid(s.ServiceUuid, svcGuid)) {
          target = &s;
          break;
        }
      }
      if (!target) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Service not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      USHORT cc = 0;
      f.GetCharacteristics(it->second, target, 0, nullptr, &cc, BLUETOOTH_GATT_FLAG_NONE);
      std::vector<BTH_LE_GATT_CHARACTERISTIC> clist;
      clist.resize(cc);
      if (cc > 0) {
        f.GetCharacteristics(it->second, target, cc, clist.data(), &cc, BLUETOOTH_GATT_FLAG_NONE);
      }
      GUID chrGuid{};
      if (!parseGuid(charUuid, chrGuid)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "Invalid characteristic UUID"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      BTH_LE_GATT_CHARACTERISTIC* chr = nullptr;
      for (auto &c : clist) {
        if (bthLeUuidEqualsGuid(c.CharacteristicUuid, chrGuid)) {
          chr = &c;
          break;
        }
      }
      if (!chr) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Characteristic not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      const size_t n = value.size();
      std::vector<unsigned char> buf;
      buf.resize(sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + n);
      auto* v = reinterpret_cast<PBTH_LE_GATT_CHARACTERISTIC_VALUE>(buf.data());
      memset(v, 0, buf.size());
      v->DataSize = (ULONG) n;
      if (n) {
        memcpy(v->Data, value.data(), n);
      }
      HRESULT hr = f.WriteCharacteristicValue(
        it->second,
        chr,
        v,
        0,
        BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE
      );
      if (FAILED(hr)) {
        // Try with response
        hr = f.WriteCharacteristicValue(
          it->second,
          chr,
          v,
          0,
          BLUETOOTH_GATT_FLAG_NONE
        );
      }
      if (FAILED(hr)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "OperationError"},
          {"message", "Write failed"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }

    struct NotifyContext { WindowsBackend* self; std::string deviceId; std::string service; std::string characteristic; };
    static VOID WINAPI onGattEvent(BTH_LE_GATT_EVENT_TYPE EventType, PVOID EventOutParameter, PVOID Context) {
      auto* ctx = reinterpret_cast<NotifyContext*>(Context);
      if (!ctx || !ctx->self) return;
      if (EventType != CharacteristicValueChangedEvent) return;
      auto* event = reinterpret_cast<PBLUETOOTH_GATT_VALUE_CHANGED_EVENT>(EventOutParameter);
      if (!event) return;
      auto* cv = event->CharacteristicValue;
      if (!cv) return;
      const ULONG n = cv->DataSize;
      oro::runtime::bytes::Buffer bd(n);
      if (n) memcpy(bd.data(), cv->Data, n);
      ctx->self->svc.notifyCharacteristicValue(ctx->deviceId, ctx->service, ctx->characteristic, bd);
    }
    std::unordered_map<std::string, std::pair<BLUETOOTH_GATT_EVENT_HANDLE, std::unique_ptr<NotifyContext>>> subscriptions;

    void characteristicStartNotifications(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      auto it = connections.find(normalizeMac(deviceId));
      if (it == connections.end()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Device not connected"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      if (!isLoadedGatt()) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", "Bluetooth GATT API unavailable"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      auto &f = gatt();
      USHORT sc = 0;
      f.GetServices(it->second, 0, nullptr, &sc, BLUETOOTH_GATT_FLAG_NONE);
      std::vector<BTH_LE_GATT_SERVICE> svcs;
      svcs.resize(sc);
      if (sc > 0) {
        f.GetServices(it->second, sc, svcs.data(), &sc, BLUETOOTH_GATT_FLAG_NONE);
      }
      GUID svcGuid{};
      if (!parseGuid(serviceUuid, svcGuid)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "Invalid service UUID"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      BTH_LE_GATT_SERVICE* svcPtr = nullptr;
      for (auto &s : svcs) {
        if (bthLeUuidEqualsGuid(s.ServiceUuid, svcGuid)) {
          svcPtr = &s;
          break;
        }
      }
      if (!svcPtr) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Service not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      USHORT cc = 0;
      f.GetCharacteristics(it->second, svcPtr, 0, nullptr, &cc, BLUETOOTH_GATT_FLAG_NONE);
      std::vector<BTH_LE_GATT_CHARACTERISTIC> clist;
      clist.resize(cc);
      if (cc > 0) {
        f.GetCharacteristics(it->second, svcPtr, cc, clist.data(), &cc, BLUETOOTH_GATT_FLAG_NONE);
      }
      GUID chrGuid{};
      if (!parseGuid(charUuid, chrGuid)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "Invalid characteristic UUID"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      BTH_LE_GATT_CHARACTERISTIC* chr = nullptr;
      for (auto &c : clist) {
        if (bthLeUuidEqualsGuid(c.CharacteristicUuid, chrGuid)) {
          chr = &c;
          break;
        }
      }
      if (!chr) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotFoundError"},
          {"message", "Characteristic not found"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }

      BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION reg{};
      reg.NumCharacteristics = 1;
      reg.Characteristics[0] = *chr;
      auto ctx = std::make_unique<NotifyContext>();
      ctx->self = this;
      ctx->deviceId = normalizeMac(deviceId);
      ctx->service = guidToString(svcGuid);
      ctx->characteristic = guidToString(chrGuid);
      BLUETOOTH_GATT_EVENT_HANDLE handle = nullptr;
      HRESULT hr = f.RegisterEvent(it->second, CharacteristicValueChangedEvent, &reg, &onGattEvent, ctx.get(), &handle, BLUETOOTH_GATT_FLAG_NONE);
      if (FAILED(hr)) {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "OperationError"},
          {"message", "RegisterEvent failed"}
        }};
        return cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
      const auto key = subscriptionKey(deviceId, serviceUuid, charUuid);
      const auto existing = subscriptions.find(key);
      if (existing != subscriptions.end()) {
        if (f.UnregisterEvent && existing->second.first) {
          f.UnregisterEvent(existing->second.first, BLUETOOTH_GATT_FLAG_NONE);
        }
        subscriptions.erase(existing);
      }
      subscriptions[key] = { handle, std::move(ctx) };
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }

    void characteristicStopNotifications(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      const auto key = subscriptionKey(deviceId, serviceUuid, charUuid);
      auto it = subscriptions.find(key);
      if (it != subscriptions.end()) {
        auto &f = gatt();
        if (f.UnregisterEvent && it->second.first) {
          f.UnregisterEvent(it->second.first, BLUETOOTH_GATT_FLAG_NONE);
        }
        subscriptions.erase(it);
      }
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }
    void chooseDevice(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      // Resolve the IPC call immediately
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      if (choosing && pendingCb) {
        const auto normalizedDeviceId = normalizeMac(deviceId);
        // Finish the pending requestDevice with chosen id
        JSON::Object device = JSON::Object::Entries {{
          {"id", String(normalizedDeviceId.c_str())},
          {"name", String("")}
        }};
        const auto it = discovered.find(normalizedDeviceId);
        if (it != discovered.end()) {
          device.set("name", it->second.name);
          if (!it->second.services.empty()) {
            JSON::Array servicesJson;
            for (const auto& s : it->second.services) servicesJson.push(s);
            device.set("services", servicesJson);
          }
          if (!it->second.manufacturer.empty()) {
            JSON::Array mdJson;
            for (const auto& entry : it->second.manufacturer) {
              JSON::Object md = JSON::Object::Entries {{
                {"companyId", (int64_t) entry.first},
                {"encoding", String("base64")}
              }};
              if (!entry.second.empty()) {
                bytes::Buffer buf(entry.second.size());
                memcpy(buf.data(), entry.second.data(), entry.second.size());
                md.set("data", buf.str(bytes::Buffer::Encoding::BASE64));
              } else {
                md.set("data", String(""));
              }
              mdJson.push(md);
            }
            device.set("manufacturerData", mdJson);
          }
        }
        pendingCb(pendingSeq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"device", device}}}}, oro::runtime::QueuedResponse{});
        choosing = false;
        pendingSeq.clear();
        pendingCb = nullptr;
        stopDiscoveryTimeout();
        stopWatcher();
        discovered.clear();
      }
    }
    void cancelRequest(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, oro::runtime::QueuedResponse{});
      if (choosing && pendingCb) {
        pendingCb(pendingSeq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, oro::runtime::QueuedResponse{});
        choosing = false;
        pendingSeq.clear();
        pendingCb = nullptr;
        stopDiscoveryTimeout();
        stopWatcher();
        discovered.clear();
      }
    }

    void restartDiscovery(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object json = JSON::Object::Entries {{
        "err",
        JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", "Restarting discovery is not supported on Windows yet"}
        }}
      }};
      cb(seq, json, oro::runtime::QueuedResponse{});
    }

    void deviceWatchAdvertisements(const std::string& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object err = JSON::Object::Entries {{
        {"type", "NotSupportedError"},
        {"message", String("BluetoothDevice.watchAdvertisements is not supported on Windows yet")}
      }};
      cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
    }

    void deviceForget(const std::string& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object err = JSON::Object::Entries {{
        {"type", "NotSupportedError"},
        {"message", String("BluetoothDevice.forget is not supported on Windows yet")}
      }};
      cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
    }
  };
}

namespace oro::runtime::core::services {
  std::unique_ptr<Bluetooth::Backend> makeBluetoothBackend(Bluetooth& svc) {
    return std::unique_ptr<Bluetooth::Backend>(new WindowsBackend(svc));
  }
}

#endif // _WIN32
