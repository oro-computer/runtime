#if defined(_WIN32)
#include "../../../debug.hh"
#include "../../../runtime.hh"
#include "../bluetooth.hh"
#include "../../../string.hh"
#include "../../../bytes.hh"
#define NOMINMAX
#include <windows.h>
#include <bluetoothleapis.h>
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
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <cstring>

// Dynamic Bluetooth GATT API loader (no static link deps)
namespace {
  struct GattFns {
    HMODULE mod = nullptr;
    HRESULT (WINAPI *GetServices)(HANDLE, USHORT, PBTH_LE_GATT_SERVICE, USHORT*, ULONG) = nullptr;
    HRESULT (WINAPI *GetCharacteristics)(HANDLE, PBTH_LE_GATT_SERVICE, USHORT, PBTH_LE_GATT_CHARACTERISTIC, USHORT*, ULONG) = nullptr;
    HRESULT (WINAPI *ReadCharacteristicValue)(HANDLE, PBTH_LE_GATT_CHARACTERISTIC, USHORT, PBTH_LE_GATT_CHARACTERISTIC_VALUE, USHORT*, ULONG) = nullptr;
    HRESULT (WINAPI *WriteCharacteristicValue)(HANDLE, PBTH_LE_GATT_CHARACTERISTIC, PBTH_LE_GATT_CHARACTERISTIC_VALUE, ULONG) = nullptr;
    HRESULT (WINAPI *RegisterEvent)(HANDLE, BTH_LE_GATT_EVENT_TYPE, PVOID, PFNBLUETOOTH_GATT_EVENT_CALLBACK, PVOID, BLUETOOTH_GATT_EVENT_HANDLE*, ULONG) = nullptr;
    HRESULT (WINAPI *UnregisterEvent)(BLUETOOTH_GATT_EVENT_HANDLE, ULONG) = nullptr;
  };

  static GattFns& gatt() {
    static GattFns fns;
    if (!fns.mod) {
      fns.mod = LoadLibraryA("BluetoothAPIs.dll");
      if (fns.mod) {
        fns.GetServices = reinterpret_cast<decltype(fns.GetServices)>(GetProcAddress(fns.mod, "BluetoothGATTGetServices"));
        fns.GetCharacteristics = reinterpret_cast<decltype(fns.GetCharacteristics)>(GetProcAddress(fns.mod, "BluetoothGATTGetCharacteristics"));
        fns.ReadCharacteristicValue = reinterpret_cast<decltype(fns.ReadCharacteristicValue)>(GetProcAddress(fns.mod, "BluetoothGATTReadCharacteristicValue"));
        fns.WriteCharacteristicValue = reinterpret_cast<decltype(fns.WriteCharacteristicValue)>(GetProcAddress(fns.mod, "BluetoothGATTWriteCharacteristicValue"));
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

  static inline bool parseHex16(const std::string& s, unsigned short& out) {
    try {
      out = (unsigned short) std::stoul(s, nullptr, 16);
      return true;
    } catch (...) {
      return false;
    }
  }

  static inline bool parseGuid(const std::string& in, GUID& out) {
    // Accept full 36-char GUID, or 16-bit short UUID (hex)
    if (in.size() == 36 && in[8] == '-' && in[13] == '-' && in[18] == '-' && in[23] == '-') {
      const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
      };
      auto parse32 = [&](int pos) -> unsigned {
        unsigned v = 0;
        for (int i = 0; i < 8; i++) {
          int h = hex(in[pos + i]);
          if (h < 0) return 0;
          v = (v << 4) | h;
        }
        return v;
      };
      auto parse16 = [&](int pos) -> unsigned short {
        unsigned short v = 0;
        for (int i = 0; i < 4; i++) {
          int h = hex(in[pos + i]);
          if (h < 0) return 0;
          v = (unsigned short) ((v << 4) | h);
        }
        return v;
      };
      auto parse8 = [&](int pos) -> unsigned char {
        int hi = hex(in[pos]);
        int lo = hex(in[pos + 1]);
        if (hi < 0 || lo < 0) return 0;
        return (unsigned char) ((hi << 4) | lo);
      };
      out.Data1 = parse32(0);
      out.Data2 = parse16(9);
      out.Data3 = parse16(14);
      out.Data4[0] = parse8(19);
      out.Data4[1] = parse8(21);
      out.Data4[2] = parse8(24);
      out.Data4[3] = parse8(26);
      out.Data4[4] = parse8(28);
      out.Data4[5] = parse8(30);
      out.Data4[6] = parse8(32);
      out.Data4[7] = parse8(34);
      return true;
    }
    // Short UUID: format to 128-bit base UUID
    unsigned short shortId = 0;
    if (!parseHex16(in, shortId)) return false;
    out.Data1 = 0x0000 | ((unsigned) shortId);
    out.Data2 = 0x0000;
    out.Data3 = 0x1000;
    out.Data4[0] = 0x80;
    out.Data4[1] = 0x00;
    out.Data4[2] = 0x00;
    out.Data4[3] = 0x80;
    out.Data4[4] = 0x5f;
    out.Data4[5] = 0x9b;
    out.Data4[6] = 0x34;
    out.Data4[7] = 0xfb;
    return true;
  }
}
#include <string>
#include <vector>
#include <unordered_map>

// Minimal SetupAPI declarations to avoid static headers/deps
typedef PVOID HDEVINFO;
typedef struct _SP_DEVICE_INTERFACE_DATA {
  DWORD cbSize;
  GUID InterfaceClassGuid;
  DWORD Flags;
  ULONG_PTR Reserved;
} SP_DEVICE_INTERFACE_DATA, *PSP_DEVICE_INTERFACE_DATA;

typedef struct _SP_DEVICE_INTERFACE_DETAIL_DATA_W {
  DWORD cbSize;
  WCHAR DevicePath[1];
} SP_DEVICE_INTERFACE_DETAIL_DATA_W, *PSP_DEVICE_INTERFACE_DETAIL_DATA_W;

#ifndef DIGCF_PRESENT
#define DIGCF_PRESENT 0x00000002
#endif
#ifndef DIGCF_DEVICEINTERFACE
#define DIGCF_DEVICEINTERFACE 0x00000010
#endif

// BLE Device Interface GUID {781AEE18-7733-4CE4-ADD0-91F41C67B592}
static const GUID GUID_BLUETOOTHLE_DEVICE_INTERFACE = { 0x781aee18, 0x7733, 0x4ce4, { 0xad, 0xd0, 0x91, 0xf4, 0x1c, 0x67, 0xb5, 0x92 } };

// SetupAPI function pointer types
using PFN_SetupDiGetClassDevsW = HDEVINFO (WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
using PFN_SetupDiEnumDeviceInterfaces = BOOL (WINAPI*)(HDEVINFO, PVOID, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
using PFN_SetupDiGetDeviceInterfaceDetailW = BOOL (WINAPI*)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD, PVOID);
using PFN_SetupDiDestroyDeviceInfoList = BOOL (WINAPI*)(HDEVINFO);

using oro::runtime::core::services::Bluetooth;
using oro::runtime::String;
using oro::runtime::Vector;
using oro::runtime::Map;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
using Microsoft::WRL::Wrappers::RoInitializeWrapper;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEAdvertisementWatcher;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEAdvertisementReceivedEventArgs;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEAdvertisement;
using ABI::Windows::Devices::Bluetooth::Advertisement::IBluetoothLEManufacturerData;
using ABI::Windows::Devices::Bluetooth::Advertisement::BluetoothLEScanningMode_Active;
using ABI::Windows::Foundation::ITypedEventHandler;
using ABI::Windows::Foundation::Collections::IVector;
using ABI::Windows::Foundation::Collections::IVectorView;
using ABI::Windows::Storage::Streams::IBuffer;
using ABI::Windows::Storage::Streams::IBufferByteAccess;

namespace {
  class WindowsBackend final : public Bluetooth::Backend {
    oro::runtime::core::Service& svc;
    // Pending chooser state
    bool choosing = false;
    std::string pendingSeq;
    oro::runtime::core::Service::Callback pendingCb = nullptr;
    // Connected device handles by normalized deviceId
    std::unordered_map<std::string, HANDLE> connections;
    Bluetooth::ParsedRequestDeviceOptions requestFilters;
    bool manufacturerFilterActive = false;
    bool watcherStarted = false;
    EventRegistrationToken watcherToken{};
    ComPtr<IBluetoothLEAdvertisementWatcher> watcher;
    ComPtr<ITypedEventHandler<IBluetoothLEAdvertisementWatcher*, IBluetoothLEAdvertisementReceivedEventArgs*>> watcherHandler;
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
    ~WindowsBackend() override { stopWatcher(); }

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
      int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, len, nullptr, 0, nullptr, nullptr);
      if (needed <= 0) return std::string();
      std::string out;
      out.resize((size_t) needed);
      WideCharToMultiByte(CP_UTF8, 0, buffer, len, out.data(), needed, nullptr, nullptr);
      return out;
    }

    HRESULT ensureWatcherStarted() {
      if (watcherStarted) return S_OK;
      static bool roInitialized = false;
      if (!roInitialized) {
        HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr;
        roInitialized = true;
      }

      HStringReference className(RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementWatcher);
      ComPtr<IActivationFactory> factory;
      HRESULT hr = RoGetActivationFactory(className.GetHSTRING(), IID_PPV_ARGS(&factory));
      if (FAILED(hr)) return hr;

      ComPtr<IInspectable> inspectable;
      hr = factory->ActivateInstance(&inspectable);
      if (FAILED(hr)) return hr;

      hr = inspectable.As(&watcher);
      if (FAILED(hr)) return hr;

      watcher->put_ScanningMode(BluetoothLEScanningMode_Active);

      watcherHandler = Callback<ITypedEventHandler<IBluetoothLEAdvertisementWatcher*, IBluetoothLEAdvertisementReceivedEventArgs*>>(
        [this](IBluetoothLEAdvertisementWatcher*, IBluetoothLEAdvertisementReceivedEventArgs* args) -> HRESULT {
          this->handleAdvertisement(args);
          return S_OK;
        }
      );
      if (!watcherHandler) return E_FAIL;

      hr = watcher->add_Received(watcherHandler.Get(), &watcherToken);
      if (FAILED(hr)) return hr;

      watcherSeen.clear();
      watcher->Start();
      watcherStarted = true;
      return S_OK;
    }

    void stopWatcher() {
      if (!watcherStarted || !watcher) return;
      watcher->Stop();
      watcher->remove_Received(watcherToken);
      watcher.Reset();
      watcherHandler.Reset();
      watcherStarted = false;
      watcherSeen.clear();
      manufacturerFilterActive = false;
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

      // Dynamically probe classic Bluetooth radio presence using Bthprops.cpl exports.
      // Lightweight probe that avoids additional link dependencies (may miss some cases).
      HMODULE bth = LoadLibraryA("Bthprops.cpl");
      if (bth) {
        struct BLUETOOTH_FIND_RADIO_PARAMS { DWORD dwSize; } params{ sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
        using HBLUETOOTH_RADIO_FIND = HANDLE; // treat as HANDLE for our purposes
        using PFN_BluetoothFindFirstRadio = HBLUETOOTH_RADIO_FIND (WINAPI*)(const BLUETOOTH_FIND_RADIO_PARAMS*, HANDLE*);
        using PFN_BluetoothFindNextRadio = BOOL (WINAPI*)(HBLUETOOTH_RADIO_FIND, HANDLE*);
        using PFN_BluetoothFindRadioClose = BOOL (WINAPI*)(HBLUETOOTH_RADIO_FIND);

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

      const int timeoutCopy = timeoutMs;
      std::thread([this, timeoutCopy]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutCopy));
        svc.loop.dispatch([this]() {
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
      }).detach();

      // Enumerate radios
      HMODULE bth = LoadLibraryA("Bthprops.cpl");
      if (!bth) return; // silent failure; no devices to emit

      struct BLUETOOTH_FIND_RADIO_PARAMS { DWORD dwSize; } rparams{ sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
      using HBLUETOOTH_RADIO_FIND = HANDLE;
      using PFN_BluetoothFindFirstRadio = HBLUETOOTH_RADIO_FIND (WINAPI*)(const BLUETOOTH_FIND_RADIO_PARAMS*, HANDLE*);
      using PFN_BluetoothFindNextRadio = BOOL (WINAPI*)(HBLUETOOTH_RADIO_FIND, HANDLE*);
      using PFN_BluetoothFindRadioClose = BOOL (WINAPI*)(HBLUETOOTH_RADIO_FIND);
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
          auto uuid = guidToString(svc.ServiceUuid);
          auto normalized = Bluetooth::normalizeUUID(String(uuid.c_str()));
          deviceServices.insert(std::string(normalized.c_str()));
        }
        CloseHandle(h);
        servicesLoadOk = true;
        return true;
      };

      // Device enumeration APIs
      typedef struct _BLUETOOTH_ADDRESS { ULONGLONG ullLong; } BLUETOOTH_ADDRESS;
      typedef struct _SYSTEMTIME_ { WORD wYear; WORD wMonth; WORD wDayOfWeek; WORD wDay; WORD wHour; WORD wMinute; WORD wSecond; WORD wMilliseconds; } SYSTEMTIME_;
      const size_t BLUETOOTH_MAX_NAME_SIZE = 248;
      typedef struct _BLUETOOTH_DEVICE_INFO_V1 {
        DWORD dwSize;
        BLUETOOTH_ADDRESS Address;
        ULONG ulClassofDevice;
        BOOL fConnected;
        BOOL fRemembered;
        BOOL fAuthenticated;
        SYSTEMTIME_ stLastSeen;
        SYSTEMTIME_ stLastUsed;
        WCHAR szName[248];
      } BLUETOOTH_DEVICE_INFO;
      typedef BLUETOOTH_DEVICE_INFO* PBLUETOOTH_DEVICE_INFO;
      typedef struct _BLUETOOTH_DEVICE_SEARCH_PARAMS {
        DWORD dwSize;
        BOOL fReturnAuthenticated;
        BOOL fReturnRemembered;
        BOOL fReturnUnknown;
        BOOL fReturnConnected;
        BOOL fIssueInquiry;
        UCHAR cTimeoutMultiplier;
        HANDLE hRadio;
      } BLUETOOTH_DEVICE_SEARCH_PARAMS;
      using HBLUETOOTH_DEVICE_FIND = HANDLE;
      using PFN_BluetoothFindFirstDevice = HBLUETOOTH_DEVICE_FIND (WINAPI*)(const BLUETOOTH_DEVICE_SEARCH_PARAMS*, PBLUETOOTH_DEVICE_INFO);
      using PFN_BluetoothFindNextDevice = BOOL (WINAPI*)(HBLUETOOTH_DEVICE_FIND, PBLUETOOTH_DEVICE_INFO);
      using PFN_BluetoothFindDeviceClose = BOOL (WINAPI*)(HBLUETOOTH_DEVICE_FIND);
      using PFN_BluetoothGetDeviceInfo = DWORD (WINAPI*)(HANDLE, PBLUETOOTH_DEVICE_INFO);

      auto pFirstDev = reinterpret_cast<PFN_BluetoothFindFirstDevice>(GetProcAddress(bth, "BluetoothFindFirstDevice"));
      auto pNextDev = reinterpret_cast<PFN_BluetoothFindNextDevice>(GetProcAddress(bth, "BluetoothFindNextDevice"));
      auto pCloseDev = reinterpret_cast<PFN_BluetoothFindDeviceClose>(GetProcAddress(bth, "BluetoothFindDeviceClose"));
      auto pGetInfo = reinterpret_cast<PFN_BluetoothGetDeviceInfo>(GetProcAddress(bth, "BluetoothGetDeviceInfoW"));

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
          // Convert UTF-16 name to UTF-8
          int len = WideCharToMultiByte(CP_UTF8, 0, info.szName, -1, nullptr, 0, nullptr, nullptr);
          std::string name; name.resize(len > 0 ? (size_t)len : 0);
          if (len > 0) {
            WideCharToMultiByte(CP_UTF8, 0, info.szName, -1, name.data(), len, nullptr, nullptr);
            if (!name.empty() && name.back() == '\0') name.pop_back();
          }
          const std::string deviceId(idbuf);
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

          const std::string deviceId = normalizeMac(idbuf);
          std::vector<std::string> serviceList;
          serviceList.reserve(deviceServices.size());
          for (const auto& svcName : deviceServices) serviceList.push_back(svcName);
          std::vector<std::pair<uint16_t, std::vector<uint8_t>>> emptyManufacturer;
          processDeviceCandidate(deviceId, name, rssi, serviceList, emptyManufacturer);

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
      }

      FreeLibrary(bth);
    }
    static inline std::string normalizeMac(const std::string& mac) {
      std::string s; s.reserve(mac.size());
      for (auto c : mac) { if (c != ':' && c != '-') s.push_back((char) ::toupper((unsigned char)c)); }
      return s;
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
      auto pGetClass = reinterpret_cast<PFN_SetupDiGetClassDevsW>(GetProcAddress(setup, "SetupDiGetClassDevsW"));
      auto pEnum = reinterpret_cast<PFN_SetupDiEnumDeviceInterfaces>(GetProcAddress(setup, "SetupDiEnumDeviceInterfaces"));
      auto pDetail = reinterpret_cast<PFN_SetupDiGetDeviceInterfaceDetailW>(GetProcAddress(setup, "SetupDiGetDeviceInterfaceDetailW"));
      auto pDestroy = reinterpret_cast<PFN_SetupDiDestroyDeviceInfoList>(GetProcAddress(setup, "SetupDiDestroyDeviceInfoList"));
      if (!pGetClass || !pEnum || !pDetail || !pDestroy) { FreeLibrary(setup); return L""; }

      HDEVINFO devs = pGetClass(&GUID_BLUETOOTHLE_DEVICE_INTERFACE, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
      if (!devs) { FreeLibrary(setup); return L""; }

      std::wstring found;
      for (DWORD i = 0; ; ++i) {
        SP_DEVICE_INTERFACE_DATA ifdata{}; ifdata.cbSize = sizeof(ifdata);
        if (!pEnum(devs, nullptr, &GUID_BLUETOOTHLE_DEVICE_INTERFACE, i, &ifdata)) break;
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
      connections[normalizeMac(deviceId)] = h;
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }

    void gattDisconnect(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      const auto key = normalizeMac(deviceId);
      // Unregister any subscriptions for this device
      for (auto it = subscriptions.begin(); it != subscriptions.end(); ) {
        if (it->first.rfind(deviceId + "|", 0) == 0) {
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
      this->gattGetPrimaryServices(seq, deviceId, serviceFilter, [cb, serviceFilter](auto s, auto json, auto qr) {
        if (json.has("err")) return cb(s, json, qr);
        auto services = json.get("data").get("services");
        bool found = false;
        for (size_t i = 0; i < services.size(); ++i) {
          if (services[i].str() == serviceFilter) {
            found = true;
            break;
          }
        }
        if (!found) {
          JSON::Object err = JSON::Object::Entries {{
            {"type", "NotFoundError"},
            {"message", "Service not found"}
          }};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
        } else {
          cb(s, JSON::Object::Entries {{
            "data",
            JSON::Object::Entries {{
              {"service", serviceFilter}
            }}
          }}, qr);
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
        const auto u = guidToString(s.ServiceUuid);
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
      this->serviceGetCharacteristics(seq, deviceId, serviceUuid, "", [cb, charUuid](auto s, auto json, auto qr) {
        if (json.has("err")) return cb(s, json, qr);
        auto chars = json.get("data").get("characteristics");
        bool found = false;
        for (size_t i = 0; i < chars.size(); ++i) {
          if (chars[i].str() == charUuid) {
            found = true;
            break;
          }
        }
        if (!found) {
          JSON::Object err = JSON::Object::Entries {{
            {"type", "NotFoundError"},
            {"message", "Characteristic not found"}
          }};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
        } else {
          cb(s, JSON::Object::Entries {{
            "data",
            JSON::Object::Entries {{
              {"characteristic", charUuid}
            }}
          }}, qr);
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
        if (memcmp(&s.ServiceUuid, &svcGuid, sizeof(GUID)) == 0) {
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
        const auto u = guidToString(c.CharacteristicUuid.Value.LongUuid);
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
        if (memcmp(&s.ServiceUuid, &svcGuid, sizeof(GUID)) == 0) {
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
        if (memcmp(&c.CharacteristicUuid.Value.LongUuid, &chrGuid, sizeof(GUID)) == 0) {
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
        if (memcmp(&s.ServiceUuid, &svcGuid, sizeof(GUID)) == 0) {
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
        if (memcmp(&c.CharacteristicUuid.Value.LongUuid, &chrGuid, sizeof(GUID)) == 0) {
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
      HRESULT hr = f.WriteCharacteristicValue(it->second, chr, v, BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE);
      if (FAILED(hr)) {
        // Try with response
        hr = f.WriteCharacteristicValue(it->second, chr, v, BLUETOOTH_GATT_FLAG_NONE);
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
      auto* param = reinterpret_cast<PBTH_LE_GATT_EVENT_PARAMETER>(EventOutParameter);
      if (!param || param->EventType != CharacteristicValueChangedEvent) return;
      auto* cv = param->Parameters.CharValueChangedEvent.ChangedCharacteristicValue;
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
        if (memcmp(&s.ServiceUuid, &svcGuid, sizeof(GUID)) == 0) {
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
        if (memcmp(&c.CharacteristicUuid.Value.LongUuid, &chrGuid, sizeof(GUID)) == 0) {
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

      BTH_LE_GATT_VALUE_CHANGED_EVENT_REGISTRATION reg{};
      reg.NumCharacteristics = 1;
      BTH_LE_GATT_CHARACTERISTIC chArr[1];
      chArr[0] = *chr;
      reg.Characteristics = chArr;
      auto ctx = std::make_unique<NotifyContext>();
      ctx->self = this;
      ctx->deviceId = deviceId;
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
      subscriptions[deviceId + "|" + serviceUuid + "|" + charUuid] = { handle, std::move(ctx) };
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }

    void characteristicStopNotifications(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceUuid, const std::string& charUuid, const oro::runtime::core::Service::Callback cb) override {
      const std::string key = deviceId + "|" + serviceUuid + "|" + charUuid;
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
        // Finish the pending requestDevice with chosen id
        JSON::Object device = JSON::Object::Entries {{
          {"id", String(deviceId.c_str())},
          {"name", String("")}
        }};
        const auto it = discovered.find(deviceId);
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
