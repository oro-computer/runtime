#if defined(__linux__) && !defined(__ANDROID__)
#include "../../../debug.hh"
#include "../bluetooth.hh"
#include "../../../app.hh"
#include "../../../string.hh"
#include <dbus/dbus.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <utility>
#include <cmath>
#include <mutex>
#include <memory>
#include <atomic>

using oro::runtime::String;
using oro::runtime::Vector;
using oro::runtime::Map;
using oro::runtime::core::services::Bluetooth;
namespace JSON = oro::runtime::JSON;

namespace {
  static inline JSON::Object makeError(const char* type, const char* message) {
    return JSON::Object::Entries {
      {"err", JSON::Object::Entries {
        {"type", type},
        {"message", message}
      }}
    };
  }

  std::once_flag g_dbusThreadsInitFlag;

  class LinuxBackend final : public Bluetooth::Backend {
    Bluetooth& svc;
    bool choosing = false;
    oro::runtime::String pendingSeq;
    oro::runtime::core::Service::Callback pendingCb = nullptr;
    DBusConnection* conn = nullptr;
    bool pumping = false;
    std::thread pump;
    std::string adapterPath;
    std::unordered_map<std::string, std::string> devices; // address -> object path
    std::unordered_map<std::string, std::tuple<std::string, std::string, std::string>> notifyMap; // charPath -> (deviceId, serviceUuid, charUuid)
  static LinuxBackend* g_self;

    struct SeenDevice {
      std::chrono::steady_clock::time_point lastEmit;
      String name;
      int rssi = 0;
    };

    Bluetooth::ParsedRequestDeviceOptions requestFilters;
    std::unordered_map<std::string, SeenDevice> seenDevices;
    int deviceEmitIntervalMs = 1000;
    int deviceEmitRssiDelta = 4;
    std::atomic<bool> discoveryActive {false};
    bool chooserDisabled = false;
    int lastDiscoveryTimeoutMs = 120000;
    std::shared_ptr<std::atomic<bool>> discoveryTimerToken;
    mutable std::mutex timerMutex;

    static inline std::string toLower(const std::string& s) { std::string o; o.resize(s.size()); std::transform(s.begin(), s.end(), o.begin(), [](unsigned char c) { return (char) ::tolower(c); }); return o; }
    static inline std::string to128 (const std::string& in) {
      if (in.size() == 4) {
        char buf[37];
        snprintf(buf, sizeof(buf), "0000%s-0000-1000-8000-00805f9b34fb", in.c_str());
        return toLower(buf);
      }
      return toLower(in);
    }

    static DBusHandlerResult onSignal (DBusConnection* c, DBusMessage* m, void* u) {
      (void) c;
      (void) u;
      auto* self = g_self;
      if (!self) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
      }
      const char* ifaceProps = "org.freedesktop.DBus.Properties";
      const char* ifaceOM = "org.freedesktop.DBus.ObjectManager";
      if (dbus_message_is_signal(m, ifaceOM, "InterfacesAdded")) {
        // Optional: live devicefound while scanning
        const char* path = dbus_message_get_path(m);
        (void) path;
        // We reuse enumerateAndEmit() for simplicity on next tick
        self->svc.dispatcher.dispatch([self]() { self->enumerateAndEmit(); });
        return DBUS_HANDLER_RESULT_HANDLED;
      }
      if (dbus_message_is_signal(m, ifaceProps, "PropertiesChanged")) {
        const char* objPath = dbus_message_get_path(m);
        const char* iface = nullptr;
        DBusMessageIter it;
        if (!dbus_message_iter_init(m, &it)) {
          return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) {
          dbus_message_iter_get_basic(&it, &iface);
          dbus_message_iter_next(&it);
        }
        if (!iface) {
          return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) {
          return DBUS_HANDLER_RESULT_HANDLED;
        }
        // Handle characteristic value updates
        if (std::string(iface) == "org.bluez.GattCharacteristic1") {
          DBusMessageIter a;
          dbus_message_iter_recurse(&it, &a);
          while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter de;
            dbus_message_iter_recurse(&a, &de);
            const char* key = nullptr;
            if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_STRING) {
              dbus_message_iter_get_basic(&de, &key);
            }
            dbus_message_iter_next(&de);
            if (!key || std::string(key) != "Value") {
              dbus_message_iter_next(&a);
              continue;
            }
            if (dbus_message_iter_get_arg_type(&de) != DBUS_TYPE_VARIANT) {
              dbus_message_iter_next(&a);
              continue;
            }
            DBusMessageIter var;
            dbus_message_iter_recurse(&de, &var);
            if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_ARRAY) {
              dbus_message_iter_next(&a);
              continue;
            }
            DBusMessageIter bytes;
            dbus_message_iter_recurse(&var, &bytes);
            std::vector<unsigned char> buf;
            while (dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE) {
              unsigned char b = 0;
              dbus_message_iter_get_basic(&bytes, &b);
              buf.push_back(b);
              dbus_message_iter_next(&bytes);
            }
            // Map char path to ids
            auto itn = self->notifyMap.find(objPath ? objPath : "");
            if (itn != self->notifyMap.end()) {
              const auto& [dev, svcUuid, chrUuid] = itn->second;
              oro::runtime::bytes::Buffer val(buf.size());
              if (!buf.empty()) {
                memcpy(val.data(), buf.data(), buf.size());
              }
              self->svc.notifyCharacteristicValue(dev, svcUuid, chrUuid, val);
            }
            break;
          }
          return DBUS_HANDLER_RESULT_HANDLED;
        }
        // Handle device connection state changes and emit gattserverdisconnected
        if (std::string(iface) == "org.bluez.Device1") {
          DBusMessageIter a;
          dbus_message_iter_recurse(&it, &a);
          while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter de;
            dbus_message_iter_recurse(&a, &de);
            const char* key = nullptr;
            if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_STRING) {
              dbus_message_iter_get_basic(&de, &key);
            }
            dbus_message_iter_next(&de);
            if (!key || std::string(key) != "Connected") {
              dbus_message_iter_next(&a);
              continue;
            }
            if (dbus_message_iter_get_arg_type(&de) != DBUS_TYPE_VARIANT) {
              dbus_message_iter_next(&a);
              continue;
            }
            DBusMessageIter var;
            dbus_message_iter_recurse(&de, &var);
            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BOOLEAN) {
              dbus_bool_t connected = 0;
              dbus_message_iter_get_basic(&var, &connected);
              if (!connected) {
                // reverse lookup address by object path
                std::string addr;
                for (const auto& kv : self->devices) {
                  if (kv.second == (objPath ? objPath : "")) {
                    addr = kv.first;
                    break;
                  }
                }
                if (!addr.empty()) {
                  self->svc.dispatcher.dispatch([addr]() {
                    using oro::runtime::app::App;
                    auto app = App::sharedApplication();
                    if (!app) {
                      return;
                    }
                    oro::runtime::JSON::Object evt = oro::runtime::JSON::Object::Entries {{
                      {"deviceId", oro::runtime::String(addr.c_str())},
                      {"reason", oro::runtime::String("")}
                    }};
                    for (const auto& win : app->runtime.windowManager.windows) {
                      if (win && win->bridge) {
                        win->bridge->emit("bluetooth.gattserverdisconnected", evt.str());
                      }
                    }
                  });
                }
              }
            }
            break;
          }
          return DBUS_HANDLER_RESULT_HANDLED;
        }
        return DBUS_HANDLER_RESULT_HANDLED;
      }
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    void startPump () {
      if (!conn || pumping) return;
      pumping = true;
      pump = std::thread([this]() {
        while (pumping) {
          dbus_connection_read_write_dispatch(conn, 100);
        }
      });
      pump.detach();
    }

    bool ensureAdapter () {
      if (!conn) {
        return false;
      }
      if (!adapterPath.empty()) {
        return true;
      }

      DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects"
      );
      if (!msg) {
        return false;
      }

      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, nullptr);
      dbus_message_unref(msg);
      if (!reply) {
        return false;
      }

      DBusMessageIter it;
      dbus_message_iter_init(reply, &it);
      if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter arr;
        dbus_message_iter_recurse(&it, &arr);
        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
          DBusMessageIter de;
          dbus_message_iter_recurse(&arr, &de);
          const char* path = nullptr;
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&de, &path);
          }
          dbus_message_iter_next(&de);
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_ARRAY) {
            DBusMessageIter ifs;
            dbus_message_iter_recurse(&de, &ifs);
            while (dbus_message_iter_get_arg_type(&ifs) == DBUS_TYPE_DICT_ENTRY) {
              DBusMessageIter ide;
              dbus_message_iter_recurse(&ifs, &ide);
              const char* iface = nullptr;
              if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&ide, &iface);
              }
              if (iface && std::string(iface) == "org.bluez.Adapter1") {
                adapterPath = path ? path : "";
                break;
              }
              dbus_message_iter_next(&ifs);
            }
          }
          if (!adapterPath.empty()) {
            break;
          }
          dbus_message_iter_next(&arr);
        }
      }

      dbus_message_unref(reply);
      return !adapterPath.empty();
    }

    bool setDiscoveryFilter (const Bluetooth::ParsedRequestDeviceOptions& filters) {
      if (!ensureAdapter()) return false;
      DBusMessage* msg = dbus_message_new_method_call("org.bluez", adapterPath.c_str(), "org.bluez.Adapter1", "SetDiscoveryFilter");
      if (!msg) return false;
      DBusMessageIter it; dbus_message_iter_init_append(msg, &it);
      DBusMessageIter dict; dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
      // Transport: le
      {
        DBusMessageIter entry; dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char* key = "Transport"; dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        DBusMessageIter var; dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        const char* le = "le"; dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &le);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&dict, &entry);
      }
      // UUIDs from filters
      std::vector<std::string> uuids;
	      std::unordered_set<std::string> seenServices;
	      for (const auto& filter : filters.filters) {
	        for (const auto& service : filter.services) {
	          std::string normalized = service.c_str();
	          if (seenServices.insert(normalized).second) {
	            uuids.push_back(normalized);
	          }
	        }
	      }
      if (!uuids.empty()) {
        DBusMessageIter entry; dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char* key = "UUIDs"; dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        DBusMessageIter var; dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
        DBusMessageIter arr; dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
        for (auto &u : uuids) { const char* s = u.c_str(); dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s); }
        dbus_message_iter_close_container(&var, &arr);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&dict, &entry);
      }
      // Pattern from a single-name filter (name or namePrefix)
      if (!filters.acceptAllDevices && filters.filters.size() == 1) {
        const auto& filter = filters.filters.front();
        std::string pattern;
        if (filter.hasName) {
          pattern = filter.name.c_str();
        } else if (filter.hasNamePrefix) {
          pattern = filter.namePrefix.c_str();
        }
        if (!pattern.empty()) {
          DBusMessageIter entry; dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
          const char* key = "Pattern"; dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
          DBusMessageIter var; dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
          const char* p = pattern.c_str(); dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &p);
          dbus_message_iter_close_container(&entry, &var);
          dbus_message_iter_close_container(&dict, &entry);
        }
      }
      dbus_message_iter_close_container(&it, &dict);
      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 1500, nullptr);
      dbus_message_unref(msg);
      const bool ok = (reply != nullptr);
      if (reply) dbus_message_unref(reply);
      return ok;
    }

    bool startDiscovery () {
      if (!ensureAdapter()) return false;
      DBusMessage* msg = dbus_message_new_method_call("org.bluez", adapterPath.c_str(), "org.bluez.Adapter1", "StartDiscovery");
      if (!msg) return false;
      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 1500, nullptr);
      dbus_message_unref(msg);
      const bool ok = (reply != nullptr);
      if (reply) dbus_message_unref(reply);
      return ok;
    }
    void stopDiscovery () {
      if (!ensureAdapter()) {
        return;
      }

      DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        adapterPath.c_str(),
        "org.bluez.Adapter1",
        "StopDiscovery"
      );
      if (!msg) {
        return;
      }

      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 1500, nullptr);
      dbus_message_unref(msg);
      if (reply) {
        dbus_message_unref(reply);
      }
    }

    void cancelDiscoveryTimer () {
      std::lock_guard<std::mutex> lock(timerMutex);
      if (discoveryTimerToken) {
        discoveryTimerToken->store(true);
        discoveryTimerToken.reset();
      }
    }

    void notifyAbort (const char* type, const char* message) {
      cancelDiscoveryTimer();
      this->svc.loop.dispatch([this, type, message]() {
        stopDiscovery();
        if (this->choosing && this->pendingCb) {
          this->pendingCb(this->pendingSeq, makeError(type, message), oro::runtime::QueuedResponse{});
        }
        this->choosing = false;
        this->pendingSeq.clear();
        this->pendingCb = nullptr;
        this->seenDevices.clear();
        this->discoveryActive.store(false, std::memory_order_release);
      });
    }

    void resolveWithDevice (const std::string& deviceId) {
      cancelDiscoveryTimer();
      this->svc.dispatcher.dispatch([this, deviceId]() {
        if (!this->choosing || !this->pendingCb) return;
        stopDiscovery();
        JSON::Object::Entries deviceEntries;
        deviceEntries.insert_or_assign("id", oro::runtime::String(deviceId.c_str()));
        if (auto it = this->seenDevices.find(deviceId); it != this->seenDevices.end()) {
          if (it->second.name.size() > 0) {
            deviceEntries.insert_or_assign("name", it->second.name);
          }
        }
        JSON::Object device(deviceEntries);
        this->pendingCb(this->pendingSeq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"device", device}}}}, oro::runtime::QueuedResponse{});
        this->choosing = false;
        this->pendingSeq.clear();
        this->pendingCb = nullptr;
        this->seenDevices.clear();
        this->discoveryActive.store(false, std::memory_order_release);
      });
    }

    bool startDiscoveryWithTimer (int timeoutMs) {
      if (!this->setDiscoveryFilter(this->requestFilters)) {
        notifyAbort("OperationError", "Failed to set discovery filter");
        return false;
      }
      if (!this->startDiscovery()) {
        notifyAbort("OperationError", "Failed to start discovery");
        return false;
      }
      this->discoveryActive.store(true, std::memory_order_release);
      this->enumerateAndEmit();

      auto token = std::make_shared<std::atomic<bool>>(false);
      {
        std::lock_guard<std::mutex> lock(timerMutex);
        discoveryTimerToken = token;
        lastDiscoveryTimeoutMs = timeoutMs;
      }

	      std::thread([this, timeoutMs, token]() {
	        const bool autoSelect = this->chooserDisabled;
	        const auto start = std::chrono::steady_clock::now();
	        while (!token->load()) {
	          if (autoSelect) {
	            std::string firstId;
	            if (!this->seenDevices.empty()) {
	              firstId = this->seenDevices.begin()->first;
	            }
	            if (!firstId.empty()) {
	              token->store(true);
	              resolveWithDevice(firstId);
	              return;
	            }
	          }
	          if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(timeoutMs)) {
	            break;
	          }
	          std::this_thread::sleep_for(std::chrono::milliseconds(200));
	        }
	        if (token->load()) {
	          return;
	        }
	        token->store(true);
	        notifyAbort("AbortError", "Timed out");
	      }).detach();

      return true;
    }

    void restartDiscoveryInternal () {
      if (!this->choosing) return;
      cancelDiscoveryTimer();
      stopDiscovery();
      this->seenDevices.clear();
      if (!startDiscoveryWithTimer(this->lastDiscoveryTimeoutMs > 0 ? this->lastDiscoveryTimeoutMs : 120000)) {
        return;
      }
      if (!this->chooserDisabled) {
        this->svc.dispatcher.dispatch([]() {
          using oro::runtime::app::App;
          auto app = App::sharedApplication();
          if (!app) {
            return;
          }
          JSON::Object evt = JSON::Object::Entries {{"reason", "requestDevice"}};
          const auto payload = evt.str();
          for (const auto& win : app->runtime.windowManager.windows) {
            if (win && win->bridge) {
              win->bridge->emit("bluetooth.discoverystarted", payload);
            }
          }
        });
      }
    }

    // Resolve device object path by Bluetooth address (cache-only)
    std::string devicePathFor (const std::string& addr) const {
      auto it = devices.find(addr);
      return it == devices.end() ? std::string("") : it->second;
    }

    // Query BlueZ for all managed objects and refresh caches (devices map)
    void refreshObjects () {
      if (!conn) {
        return;
      }

      DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects"
      );
      if (!msg) {
        return;
      }

      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, nullptr);
      dbus_message_unref(msg);
      if (!reply) {
        return;
      }

      devices.clear();
      DBusMessageIter it;
      dbus_message_iter_init(reply, &it);
      if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter arr;
        dbus_message_iter_recurse(&it, &arr);
        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
          DBusMessageIter de;
          dbus_message_iter_recurse(&arr, &de);
          const char* path = nullptr;
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&de, &path);
          }
          dbus_message_iter_next(&de);
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_ARRAY) {
            DBusMessageIter ifs;
            dbus_message_iter_recurse(&de, &ifs);
            while (dbus_message_iter_get_arg_type(&ifs) == DBUS_TYPE_DICT_ENTRY) {
              DBusMessageIter ide;
              dbus_message_iter_recurse(&ifs, &ide);
              const char* iface = nullptr;
              if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&ide, &iface);
              }
              dbus_message_iter_next(&ide);
              if (iface && std::string(iface) == "org.bluez.Device1") {
                if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_ARRAY) {
                  DBusMessageIter props;
                  dbus_message_iter_recurse(&ide, &props);
                  while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter pde;
                    dbus_message_iter_recurse(&props, &pde);
                    const char* key = nullptr;
                    if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_STRING) {
                      dbus_message_iter_get_basic(&pde, &key);
                    }
                    dbus_message_iter_next(&pde);
                    if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_VARIANT) {
                      DBusMessageIter var;
                      dbus_message_iter_recurse(&pde, &var);
                      if (key && std::string(key) == "Address" && dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                        const char* s;
                        dbus_message_iter_get_basic(&var, &s);
                        if (s && path) {
                          devices[s] = path;
                        }
                      }
                    }
                    dbus_message_iter_next(&props);
                  }
                }
              }
              dbus_message_iter_next(&ifs);
            }
          }
          dbus_message_iter_next(&arr);
        }
      }

      dbus_message_unref(reply);
    }

    // Find service object path by UUID on a connected device
    bool findServicePath (const std::string& devicePath, const std::string& serviceUuid, std::string& outPath) {
      if (!conn) {
        return false;
      }
      outPath.clear();

      DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects"
      );
      if (!msg) {
        return false;
      }

      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, nullptr);
      dbus_message_unref(msg);
      if (!reply) {
        return false;
      }

      const std::string want = to128(serviceUuid);
      DBusMessageIter it;
      dbus_message_iter_init(reply, &it);
      if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter arr;
        dbus_message_iter_recurse(&it, &arr);
        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
          DBusMessageIter de;
          dbus_message_iter_recurse(&arr, &de);
          const char* path = nullptr;
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&de, &path);
          }
          dbus_message_iter_next(&de);
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_ARRAY) {
            DBusMessageIter ifs;
            dbus_message_iter_recurse(&de, &ifs);
            bool isSvc = false;
            std::string uuid;
            std::string dev;
            while (dbus_message_iter_get_arg_type(&ifs) == DBUS_TYPE_DICT_ENTRY) {
              DBusMessageIter ide;
              dbus_message_iter_recurse(&ifs, &ide);
              const char* iface = nullptr;
              if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&ide, &iface);
              }
              dbus_message_iter_next(&ide);
              if (iface && std::string(iface) == "org.bluez.GattService1") {
                isSvc = true;
                if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_ARRAY) {
                  DBusMessageIter props;
                  dbus_message_iter_recurse(&ide, &props);
                  while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter pde;
                    dbus_message_iter_recurse(&props, &pde);
                    const char* key = nullptr;
                    if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_STRING) {
                      dbus_message_iter_get_basic(&pde, &key);
                    }
                    dbus_message_iter_next(&pde);
                    if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_VARIANT) {
                      DBusMessageIter var;
                      dbus_message_iter_recurse(&pde, &var);
                      int t = dbus_message_iter_get_arg_type(&var);
                      if (key && std::string(key) == "UUID" && t == DBUS_TYPE_STRING) {
                        const char* s;
                        dbus_message_iter_get_basic(&var, &s);
                        if (s) {
                          uuid = toLower(s);
                        }
                      } else if (key && std::string(key) == "Device" && t == DBUS_TYPE_OBJECT_PATH) {
                        const char* s;
                        dbus_message_iter_get_basic(&var, &s);
                        if (s) {
                          dev = s;
                        }
                      }
                    }
                    dbus_message_iter_next(&props);
                  }
                }
              }
              dbus_message_iter_next(&ifs);
            }
            if (isSvc && dev == devicePath && uuid == want) {
              outPath = path ? path : "";
              break;
            }
          }
          if (!outPath.empty()) {
            break;
          }
          dbus_message_iter_next(&arr);
        }
      }

      dbus_message_unref(reply);
      return !outPath.empty();
    }

    // Find characteristic object path by UUID and its parent service path
    bool findCharPath (const std::string& servicePath, const std::string& charUuid, std::string& outCharPath) {
      if (!conn) {
        return false;
      }
      outCharPath.clear();

      DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects"
      );
      if (!msg) {
        return false;
      }

      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, nullptr);
      dbus_message_unref(msg);
      if (!reply) {
        return false;
      }

      const std::string want = to128(charUuid);
      DBusMessageIter it;
      dbus_message_iter_init(reply, &it);
      if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        DBusMessageIter arr;
        dbus_message_iter_recurse(&it, &arr);
        while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
          DBusMessageIter de;
          dbus_message_iter_recurse(&arr, &de);
          const char* path = nullptr;
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&de, &path);
          }
          dbus_message_iter_next(&de);
          if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_ARRAY) {
            DBusMessageIter ifs;
            dbus_message_iter_recurse(&de, &ifs);
            bool isChar = false;
            std::string uuid;
            std::string svc;
            while (dbus_message_iter_get_arg_type(&ifs) == DBUS_TYPE_DICT_ENTRY) {
              DBusMessageIter ide;
              dbus_message_iter_recurse(&ifs, &ide);
              const char* iface = nullptr;
              if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&ide, &iface);
              }
              dbus_message_iter_next(&ide);
              if (iface && std::string(iface) == "org.bluez.GattCharacteristic1") {
                isChar = true;
                if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_ARRAY) {
                  DBusMessageIter props;
                  dbus_message_iter_recurse(&ide, &props);
                  while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter pde;
                    dbus_message_iter_recurse(&props, &pde);
                    const char* key = nullptr;
                    if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_STRING) {
                      dbus_message_iter_get_basic(&pde, &key);
                    }
                    dbus_message_iter_next(&pde);
                    if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_VARIANT) {
                      DBusMessageIter var;
                      dbus_message_iter_recurse(&pde, &var);
                      int t = dbus_message_iter_get_arg_type(&var);
                      if (key && std::string(key) == "UUID" && t == DBUS_TYPE_STRING) {
                        const char* s;
                        dbus_message_iter_get_basic(&var, &s);
                        if (s) {
                          uuid = toLower(s);
                        }
                      } else if (key && std::string(key) == "Service" && t == DBUS_TYPE_OBJECT_PATH) {
                        const char* s;
                        dbus_message_iter_get_basic(&var, &s);
                        if (s) {
                          svc = s;
                        }
                      }
                    }
                    dbus_message_iter_next(&props);
                  }
                }
              }
              dbus_message_iter_next(&ifs);
            }
            if (isChar && svc == servicePath && uuid == want) {
              outCharPath = path ? path : "";
              break;
            }
          }
          if (!outCharPath.empty()) {
            break;
          }
          dbus_message_iter_next(&arr);
        }
      }

      dbus_message_unref(reply);
      return !outCharPath.empty();
    }

    // Utility to call method with string signature
	    bool callNoReply (const char* bus, const char* path, const char* iface, const char* method) {
	      if (!conn) {
	        return false;
	      }
	      DBusMessage* msg = dbus_message_new_method_call(bus, path, iface, method);
	      if (!msg) {
	        return false;
	      }
	      dbus_connection_send(conn, msg, nullptr);
	      dbus_connection_flush(conn);
	      dbus_message_unref(msg);
	      return true;
    }

    bool matchesFilters(const std::string& devName, const std::vector<std::string>& devUUIDs, const std::unordered_map<uint16_t, std::vector<uint8_t>>& devMfgData) const {
      Vector<String> services;
      services.reserve(devUUIDs.size());
      for (const auto& uuid : devUUIDs) {
        services.push_back(String(uuid.c_str()));
      }

      Map<uint16_t, Vector<uint8_t>> manufacturerData;
      for (const auto& entry : devMfgData) {
        Vector<uint8_t> bytes;
        bytes.reserve(entry.second.size());
        for (auto b : entry.second) bytes.push_back(b);
        manufacturerData[entry.first] = bytes;
      }

      return Bluetooth::anyFilterMatches(
        requestFilters,
        String(devName.c_str()),
        services,
        manufacturerData
      );
    }

    void enumerateAndEmit () {
      DBusError err; dbus_error_init(&err);
	      DBusConnection* conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	      if (!conn) {
	        return;
	      }

      DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects"
      );
	      if (!msg) {
	        dbus_connection_unref(conn);
	        return;
	      }
      DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);
      dbus_message_unref(msg);
	      if (!reply) {
	        dbus_connection_unref(conn);
	        return;
	      }

      DBusMessageIter it; dbus_message_iter_init(reply, &it);
	      if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) {
	        dbus_message_unref(reply);
	        dbus_connection_unref(conn);
	        return;
	      }
      DBusMessageIter arr; dbus_message_iter_recurse(&it, &arr);
      while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter de; dbus_message_iter_recurse(&arr, &de);
        const char* objPath = nullptr;
        if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_OBJECT_PATH) {
          dbus_message_iter_get_basic(&de, &objPath);
          dbus_message_iter_next(&de);
        }
        if (!objPath || dbus_message_iter_get_arg_type(&de) != DBUS_TYPE_ARRAY) {
          dbus_message_iter_next(&arr);
          continue;
        }
        DBusMessageIter ifs; dbus_message_iter_recurse(&de, &ifs);
        while (dbus_message_iter_get_arg_type(&ifs) == DBUS_TYPE_DICT_ENTRY) {
          DBusMessageIter ide;
          dbus_message_iter_recurse(&ifs, &ide);
          const char* iface = nullptr;
          if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&ide, &iface);
          }
          dbus_message_iter_next(&ide);
          if (iface && std::string(iface) == "org.bluez.Device1") {
            std::string address;
            std::string name;
            int rssi = 0;
            bool haveRssi = false;
            std::vector<std::string> uuids;
            std::unordered_map<uint16_t, std::vector<uint8_t>> mfgData;
            if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_ARRAY) {
              DBusMessageIter props; dbus_message_iter_recurse(&ide, &props);
              while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter pde;
                dbus_message_iter_recurse(&props, &pde);
                const char* key = nullptr;
                if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_STRING) {
                  dbus_message_iter_get_basic(&pde, &key);
                }
                dbus_message_iter_next(&pde);
                if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_VARIANT) {
                  DBusMessageIter var;
                  dbus_message_iter_recurse(&pde, &var);
                  int t = dbus_message_iter_get_arg_type(&var);
                  if (key && std::string(key) == "Address" && t == DBUS_TYPE_STRING) {
                    const char* s;
                    dbus_message_iter_get_basic(&var, &s);
                    if (s) {
                      address = s;
                    }
                  } else if (key && std::string(key) == "Name" && t == DBUS_TYPE_STRING) {
                    const char* s;
                    dbus_message_iter_get_basic(&var, &s);
                    if (s) {
                      name = s;
                    }
                  } else if (key && std::string(key) == "UUIDs" && t == DBUS_TYPE_ARRAY) {
                    DBusMessageIter uarr;
                    dbus_message_iter_recurse(&var, &uarr);
                    while (dbus_message_iter_get_arg_type(&uarr) == DBUS_TYPE_STRING) {
                      const char* s = nullptr;
                      dbus_message_iter_get_basic(&uarr, &s);
                      if (s) {
                        uuids.push_back(to128(s));
                      }
                      dbus_message_iter_next(&uarr);
                    }
                  } else if (key && std::string(key) == "ManufacturerData" && t == DBUS_TYPE_ARRAY) {
                    // a{qv} → capture data bytes for each company id
                    DBusMessageIter mdarr;
                    dbus_message_iter_recurse(&var, &mdarr);
                    while (dbus_message_iter_get_arg_type(&mdarr) == DBUS_TYPE_DICT_ENTRY) {
                      DBusMessageIter mde;
                      dbus_message_iter_recurse(&mdarr, &mde);
                      uint16_t cid = 0;
                      if (dbus_message_iter_get_arg_type(&mde) == DBUS_TYPE_UINT16) {
                        dbus_message_iter_get_basic(&mde, &cid);
                      }
                      if (dbus_message_iter_get_arg_type(&mde) != DBUS_TYPE_UINT16) {
                        dbus_message_iter_next(&mdarr);
                        continue;
                      }
                      dbus_message_iter_next(&mde);
                      if (dbus_message_iter_get_arg_type(&mde) == DBUS_TYPE_VARIANT) {
                        DBusMessageIter v;
                        dbus_message_iter_recurse(&mde, &v);
                        if (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_ARRAY) {
                          DBusMessageIter by;
                          dbus_message_iter_recurse(&v, &by);
                          std::vector<uint8_t> bytes;
                          while (dbus_message_iter_get_arg_type(&by) == DBUS_TYPE_BYTE) {
                            unsigned char b = 0;
                            dbus_message_iter_get_basic(&by, &b);
                            bytes.push_back(b);
                            dbus_message_iter_next(&by);
                          }
                          mfgData[cid] = std::move(bytes);
                        }
                      }
                      dbus_message_iter_next(&mdarr);
                    }
                  } else if (key && std::string(key) == "RSSI" && (t == DBUS_TYPE_INT16 || t == DBUS_TYPE_INT32)) {
                    dbus_int32_t v = 0;
                    dbus_message_iter_get_basic(&var, &v);
                    rssi = (int) v;
                    haveRssi = true;
                  }
                }
                dbus_message_iter_next(&props);
              }
            }
            if (!address.empty() && matchesFilters(name, uuids, mfgData)) {
              const auto now = std::chrono::steady_clock::now();
              const String currentName(name.c_str());
              bool shouldEmit = false;
              auto itSeen = this->seenDevices.find(address);
              if (itSeen == this->seenDevices.end()) {
                shouldEmit = true;
              } else {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - itSeen->second.lastEmit).count();
                const bool nameChanged = itSeen->second.name != currentName;
                bool rssiChanged = false;
                if (haveRssi) {
                  rssiChanged = std::abs(itSeen->second.rssi - rssi) >= this->deviceEmitRssiDelta;
                }
                if (elapsed >= this->deviceEmitIntervalMs || nameChanged || rssiChanged) {
                  shouldEmit = true;
                }
              }

              if (shouldEmit) {
                int emitRssi = 0;
                if (haveRssi) {
                  emitRssi = rssi;
                } else if (itSeen != this->seenDevices.end()) {
                  emitRssi = itSeen->second.rssi;
                }

                this->seenDevices[address] = SeenDevice {
                  .lastEmit = now,
                  .name = currentName,
                  .rssi = emitRssi
                };

                auto evt = JSON::Object::Entries {
                  {"id", oro::runtime::String(address.c_str())},
                  {"name", currentName},
                  {"rssi", (int64_t) emitRssi}
                };
                svc.dispatcher.dispatch([evt]() {
                  using oro::runtime::app::App;
                  auto app = App::sharedApplication();
                  if (!app) {
                    return;
                  }
                  for (const auto& win : app->runtime.windowManager.windows) {
                    if (win && win->bridge) {
                      win->bridge->emit("bluetooth.devicefound", JSON::Object(evt).str());
                    }
                  }
                });
              }
            }
          }
          dbus_message_iter_next(&ifs);
        }
        dbus_message_iter_next(&arr);
      }
      dbus_message_unref(reply);
      dbus_connection_unref(conn);
    }

    public:
      explicit LinuxBackend (Bluetooth& s) : svc(s) {
        DBusError err; dbus_error_init(&err);
        conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
        if (conn) {
          // Add matches for BlueZ signals
          dbus_bus_add_match(conn, "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'", &err);
          dbus_bus_add_match(conn, "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'", &err);
          g_self = this;
          dbus_connection_add_filter(conn, &onSignal, this, nullptr);
          startPump();
        }
      }
      ~LinuxBackend () override {
        choosing = false; pendingSeq.clear(); pendingCb = nullptr;
        pumping = false;
        if (conn) {
          dbus_connection_remove_filter(conn, &onSignal, this);
          dbus_connection_unref(conn);
          conn = nullptr;
        }
      }

      void getAvailability (const oro::runtime::String& seq, const oro::runtime::core::Service::Callback cb) const override {
        const auto runtime = svc.context.getRuntime();
        const bool allowed = runtime ? runtime->hasPermission("bluetooth") : true;
        auto* self = const_cast<LinuxBackend*>(this);

        std::call_once(g_dbusThreadsInitFlag, []() {
          if (!dbus_threads_init_default()) {
            debug("[bluetooth][linux] warning: dbus_threads_init_default failed; multi-threaded DBus may be unreliable");
          }
        });

        struct AvailabilityResult {
          oro::runtime::String seq;
          bool allowed = false;
          bool rawAvailable = false;
          JSON::Object json;
        };

        auto result = std::make_shared<AvailabilityResult>();
        result->seq = seq;
        result->allowed = allowed;
        auto cbCopy = cb;

        self->svc.queue.push(
          [self, result]() {
            bool available = false;
            bool sawPoweredProperty = false;
            DBusError err;
            dbus_error_init(&err);

            debug("[bluetooth][linux] getAvailability seq=%s allowed=%d", result->seq.c_str(), result->allowed ? 1 : 0);
            DBusConnection* system = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
            if (system != nullptr) {
              dbus_connection_set_exit_on_disconnect(system, FALSE);
              debug("[bluetooth][linux] system bus connection acquired");
              DBusMessage* msg = dbus_message_new_method_call("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
              if (msg != nullptr) {
                DBusMessage* reply = dbus_connection_send_with_reply_and_block(system, msg, 1200, &err);
                dbus_message_unref(msg);
                if (reply != nullptr) {
                  debug("[bluetooth][linux] GetManagedObjects reply received");
                  DBusMessageIter it;
                  if (dbus_message_iter_init(reply, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
                    DBusMessageIter objects;
                    dbus_message_iter_recurse(&it, &objects);
                    while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY && !available) {
                      DBusMessageIter dictEntry;
                      dbus_message_iter_recurse(&objects, &dictEntry);
                      const char* path = nullptr;
                      if (dbus_message_iter_get_arg_type(&dictEntry) == DBUS_TYPE_OBJECT_PATH) {
                        dbus_message_iter_get_basic(&dictEntry, &path);
                        dbus_message_iter_next(&dictEntry);
                        if (dbus_message_iter_get_arg_type(&dictEntry) == DBUS_TYPE_ARRAY) {
                          DBusMessageIter ifaceArray;
                          dbus_message_iter_recurse(&dictEntry, &ifaceArray);
                          while (dbus_message_iter_get_arg_type(&ifaceArray) == DBUS_TYPE_DICT_ENTRY && !available) {
                            DBusMessageIter ifaceEntry;
                            dbus_message_iter_recurse(&ifaceArray, &ifaceEntry);
                            const char* ifaceName = nullptr;
                            if (dbus_message_iter_get_arg_type(&ifaceEntry) == DBUS_TYPE_STRING) {
                              dbus_message_iter_get_basic(&ifaceEntry, &ifaceName);
                            }
                            dbus_message_iter_next(&ifaceEntry);
                            if (ifaceName && strcmp(ifaceName, "org.bluez.Adapter1") == 0 && dbus_message_iter_get_arg_type(&ifaceEntry) == DBUS_TYPE_ARRAY) {
                              DBusMessageIter props;
                              dbus_message_iter_recurse(&ifaceEntry, &props);
                              while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY && !available) {
                                DBusMessageIter propEntry;
                                dbus_message_iter_recurse(&props, &propEntry);
                                const char* propName = nullptr;
                                if (dbus_message_iter_get_arg_type(&propEntry) == DBUS_TYPE_STRING) {
                                  dbus_message_iter_get_basic(&propEntry, &propName);
                                }
                                dbus_message_iter_next(&propEntry);
                                if (propName && strcmp(propName, "Powered") == 0 && dbus_message_iter_get_arg_type(&propEntry) == DBUS_TYPE_VARIANT) {
                                  sawPoweredProperty = true;
                                  DBusMessageIter variant;
                                  dbus_message_iter_recurse(&propEntry, &variant);
                                  if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN) {
                                    dbus_bool_t powered = 0;
                                    dbus_message_iter_get_basic(&variant, &powered);
                                    debug("[bluetooth][linux] adapter %s Powered=%d", path ? path : "(unknown)", powered ? 1 : 0);
                                    if (powered) {
                                      available = true;
                                      break;
                                    }
                                  }
                                }
                                dbus_message_iter_next(&props);
                              }
                            }
                            if (!available) {
                              dbus_message_iter_next(&ifaceArray);
                            }
                          }
                        }
                      }
                      if (!available) {
                        dbus_message_iter_next(&objects);
                      }
                    }
                  }
                  dbus_message_unref(reply);
                } else if (dbus_error_is_set(&err)) {
                  debug("[bluetooth][linux] GetManagedObjects call failed: %s (%s)", err.name ? err.name : "(none)", err.message ? err.message : "(none)");
                }
              }
              if (!available && !sawPoweredProperty) {
                if (dbus_error_is_set(&err)) {
                  debug("[bluetooth][linux] dbus error while enumerating adapters: %s (%s)", err.name ? err.name : "(none)", err.message ? err.message : "(none)");
                  dbus_error_free(&err);
                  dbus_error_init(&err);
                }
                dbus_bool_t has = dbus_bus_name_has_owner(system, "org.bluez", &err);
                debug("[bluetooth][linux] fallback has org.bluez owner=%d", has ? 1 : 0);
                available = has ? true : false;
              }
              dbus_connection_close(system);
              dbus_connection_unref(system);
            } else if (dbus_error_is_set(&err)) {
              debug("[bluetooth][linux] failed to connect to system bus: %s (%s)", err.name ? err.name : "(none)", err.message ? err.message : "(none)");
            }
            if (dbus_error_is_set(&err)) {
              dbus_error_free(&err);
            }
            result->rawAvailable = available;
            result->json = JSON::Object::Entries {
              {"data", JSON::Object::Entries {{"available", result->allowed && available}}}
            };
            debug("[bluetooth][linux] getAvailability resolved available=%d rawAvailable=%d allowed=%d",
              (result->allowed && available) ? 1 : 0,
              available ? 1 : 0,
              result->allowed ? 1 : 0
            );
          },
          [self, result, cb = std::move(cbCopy)]() mutable {
            if (!self->svc.loop.dispatch([result, cb = std::move(cb)]() mutable {
              cb(result->seq, result->json, oro::runtime::QueuedResponse{});
            })) {
              debug("[bluetooth][linux] failed to dispatch availability result to runtime loop");
            }
          }
        );
      }

      void getDevices (const oro::runtime::String& seq, const oro::runtime::core::Service::Callback cb) override {
        debug("[bluetooth][linux] getDevices seq=%s", seq.c_str());
        JSON::Object json = JSON::Object::Entries {{
          "data", JSON::Object::Entries {{
            {"devices", JSON::Array {}}
          }}
        }};
        cb(seq, json, oro::runtime::QueuedResponse{});
      }

      void requestDevice (const oro::runtime::String& seq, const JSON::Any& options, const oro::runtime::core::Service::Callback cb) override {
      auto matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::All;
      this->deviceEmitIntervalMs = 1000;
      this->deviceEmitRssiDelta = 4;
      bool disableChooser = false;
      if (auto runtime = svc.context.getRuntime()) {
        const auto& cfg = runtime->userConfig;
        if (cfg.contains("web_bluetooth_services_match")) {
          const auto mode = oro::runtime::string::toLowerCase(cfg.at("web_bluetooth_services_match"));
          if (mode == "any") {
            matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::Any;
          }
        }
        if (cfg.contains("web_bluetooth_emit_interval_ms")) {
          try {
            this->deviceEmitIntervalMs = std::max(100, std::stoi(cfg.at("web_bluetooth_emit_interval_ms")));
          } catch (...) {
            this->deviceEmitIntervalMs = 1000;
          }
        }
        if (cfg.contains("web_bluetooth_emit_rssi_delta")) {
          try {
            this->deviceEmitRssiDelta = std::max(1, std::stoi(cfg.at("web_bluetooth_emit_rssi_delta")));
          } catch (...) {
            this->deviceEmitRssiDelta = 4;
          }
        }
        if (cfg.contains("web_bluetooth_disable_chooser")) {
          const auto flag = oro::runtime::string::toLowerCase(cfg.at("web_bluetooth_disable_chooser"));
          disableChooser = (flag == "1" || flag == "true" || flag == "yes");
        }
      }

      JSON::Object error;
      Bluetooth::ParsedRequestDeviceOptions parsed;
      if (!Bluetooth::parseRequestDeviceOptions(options, parsed, &error, matchMode)) {
        cb(seq, JSON::Object::Entries {{"err", error}}, oro::runtime::QueuedResponse{});
        return;
      }

      this->requestFilters = parsed;
      this->chooserDisabled = disableChooser;

      choosing = true;
      this->seenDevices.clear();
      pendingSeq = seq;
      pendingCb = cb;

      svc.dispatcher.dispatch([disableChooser]() {
        using oro::runtime::app::App;
        auto app = App::sharedApplication();
        if (!app) {
          return;
        }
        JSON::Object evt = JSON::Object::Entries {{"reason", "requestDevice"}};
        const auto payload = evt.str();
        for (const auto& win : app->runtime.windowManager.windows) {
          if (!disableChooser && win && win->bridge) {
            win->bridge->emit("bluetooth.chooserequest", payload);
            win->bridge->emit("bluetooth.discoverystarted", payload);
          }
        }
      });

      int timeoutMs = this->requestFilters.timeoutMs;
      if (timeoutMs <= 0) {
        auto rt = svc.context.getRuntime();
        if (rt && rt->userConfig.contains("web_bluetooth_timeout_ms")) {
          try {
            timeoutMs = std::stoi(rt->userConfig.at("web_bluetooth_timeout_ms"));
          } catch (...) {
            timeoutMs = -1;
          }
        }
      }
      if (timeoutMs <= 0) {
        timeoutMs = 120000;
      }
      this->lastDiscoveryTimeoutMs = timeoutMs;
      svc.queue.push([this, timeoutMs]() mutable {
        this->startDiscoveryWithTimer(timeoutMs);
      });
      // requestDevice resolves when a device is chosen via chooseDevice
      }

      void gattConnect (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string path = devicePathFor(deviceId);
        if (path.empty()) { cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{}); return; }
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", path.c_str(), "org.bluez.Device1", "Connect");
        if (!msg) { cb(seq, makeError("OperationError", "Connect allocation failed"), oro::runtime::QueuedResponse{}); return; }
        DBusError err; dbus_error_init(&err);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 4000, &err);
        dbus_message_unref(msg);
        if (!reply) { cb(seq, makeError("NetworkError", err.message ? err.message : "Connect failed"), oro::runtime::QueuedResponse{}); dbus_error_free(&err); return; }
        dbus_message_unref(reply);
        cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      }

      void gattDisconnect (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string path = devicePathFor(deviceId);
        if (path.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", path.c_str(), "org.bluez.Device1", "Disconnect");
        if (!msg) { cb(seq, makeError("OperationError", "Disconnect allocation failed"), oro::runtime::QueuedResponse{}); return; }
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 4000, nullptr);
        dbus_message_unref(msg);
        if (reply) {
          dbus_message_unref(reply);
        }
        cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      }

      void gattGetPrimaryService (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& serviceUuid, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string svcPath; if (!findServicePath(devPath, serviceUuid, svcPath)) { cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{}); return; }
        cb(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"service", to128(serviceUuid)}}}}, oro::runtime::QueuedResponse{});
      }

      void gattGetPrimaryServices (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& filter, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects(); const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) { cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{}); return; }
        const bool hasFilter = filter.size() > 0;
        String normalizedFilter;
        if (hasFilter) {
          normalizedFilter = Bluetooth::normalizeUUID(filter);
        }
        // Scan services
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        if (!msg) {
          cb(seq, makeError("OperationError", "Allocation failed"), oro::runtime::QueuedResponse{});
          return;
        }
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, nullptr);
        dbus_message_unref(msg);
        if (!reply) {
          cb(seq, makeError("OperationError", "Query failed"), oro::runtime::QueuedResponse{});
          return;
        }
        JSON::Array arr;
        DBusMessageIter it;
        dbus_message_iter_init(reply, &it);
        if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
          DBusMessageIter a;
          dbus_message_iter_recurse(&it, &a);
          while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter de;
            dbus_message_iter_recurse(&a, &de);
            const char* path = nullptr;
            if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_OBJECT_PATH) {
              dbus_message_iter_get_basic(&de, &path);
            }
            dbus_message_iter_next(&de);
            if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_ARRAY) {
              DBusMessageIter ifs;
              dbus_message_iter_recurse(&de, &ifs);
              bool isSvc = false;
              std::string uuid;
              std::string dev;
              bool primary = false;
              while (dbus_message_iter_get_arg_type(&ifs) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter ide;
                dbus_message_iter_recurse(&ifs, &ide);
                const char* iface = nullptr;
                if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_STRING) {
                  dbus_message_iter_get_basic(&ide, &iface);
                }
                dbus_message_iter_next(&ide);
                if (iface && std::string(iface) == "org.bluez.GattService1") {
                  isSvc = true;
                  if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_ARRAY) {
                    DBusMessageIter props;
                    dbus_message_iter_recurse(&ide, &props);
                    while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                      DBusMessageIter pde;
                      dbus_message_iter_recurse(&props, &pde);
                      const char* key = nullptr;
                      if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&pde, &key);
                      }
                      dbus_message_iter_next(&pde);
                      if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_VARIANT) {
                        DBusMessageIter var;
                        dbus_message_iter_recurse(&pde, &var);
                        int t = dbus_message_iter_get_arg_type(&var);
                        if (key && std::string(key) == "UUID" && t == DBUS_TYPE_STRING) {
                          const char* s;
                          dbus_message_iter_get_basic(&var, &s);
                          if (s) {
                            uuid = toLower(s);
                          }
                        } else if (key && std::string(key) == "Primary" && t == DBUS_TYPE_BOOLEAN) {
                          dbus_bool_t b;
                          dbus_message_iter_get_basic(&var, &b);
                          primary = b;
                        } else if (key && std::string(key) == "Device" && t == DBUS_TYPE_OBJECT_PATH) {
                          const char* s;
                          dbus_message_iter_get_basic(&var, &s);
                          if (s) {
                            dev = s;
                          }
                        }
                      }
                      dbus_message_iter_next(&props);
                    }
                  }
                }
                dbus_message_iter_next(&ifs);
              }
              if (isSvc && primary && dev == devPath) {
                String normalized = Bluetooth::normalizeUUID(String(uuid.c_str()));
                if (!hasFilter || normalized == normalizedFilter) {
                  arr.push(normalized);
                }
              }
            }
            dbus_message_iter_next(&a);
          }
        }
        dbus_message_unref(reply);
        if (hasFilter && arr.size() == 0) {
          cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{});
          return;
        }
        cb(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"services", arr}}}}, oro::runtime::QueuedResponse{});
      }

      void serviceGetCharacteristic (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& serviceUuid, const oro::runtime::String& charUuid, const oro::runtime::core::Service::Callback cb) override {
        // Just validate existence
        refreshObjects();
        const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string svcPath;
        if (!findServicePath(devPath, serviceUuid, svcPath)) {
          cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string chrPath;
        if (!findCharPath(svcPath, charUuid, chrPath)) {
          cb(seq, makeError("NotFoundError", "Characteristic not found"), oro::runtime::QueuedResponse{});
          return;
        }
        cb(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristic", to128(charUuid)}}}}, oro::runtime::QueuedResponse{});
      }

      void serviceGetCharacteristics (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& serviceUuid, const oro::runtime::String& /*characteristic*/, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string svcPath;
        if (!findServicePath(devPath, serviceUuid, svcPath)) {
          cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{});
          return;
        }
        // Enumerate characteristics beneath service
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        if (!msg) {
          cb(seq, makeError("OperationError", "Allocation failed"), oro::runtime::QueuedResponse{});
          return;
        }
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, nullptr);
        dbus_message_unref(msg);
        if (!reply) {
          cb(seq, makeError("OperationError", "Query failed"), oro::runtime::QueuedResponse{});
          return;
        }
        JSON::Array list;
        JSON::Object propsBy;
        DBusMessageIter it;
        dbus_message_iter_init(reply, &it);
        if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
          DBusMessageIter a;
          dbus_message_iter_recurse(&it, &a);
          while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter de;
            dbus_message_iter_recurse(&a, &de);
            const char* path = nullptr;
            if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_OBJECT_PATH) {
              dbus_message_iter_get_basic(&de, &path);
            }
            dbus_message_iter_next(&de);
            if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_ARRAY) {
              DBusMessageIter ifs;
              dbus_message_iter_recurse(&de, &ifs);
              bool isChar = false;
              std::string uuid;
              std::string svc;
              std::vector<std::string> flags;
              while (dbus_message_iter_get_arg_type(&ifs) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter ide;
                dbus_message_iter_recurse(&ifs, &ide);
                const char* iface = nullptr;
                if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_STRING) {
                  dbus_message_iter_get_basic(&ide, &iface);
                }
                dbus_message_iter_next(&ide);
                if (iface && std::string(iface) == "org.bluez.GattCharacteristic1") {
                  isChar = true;
                  if (dbus_message_iter_get_arg_type(&ide) == DBUS_TYPE_ARRAY) {
                    DBusMessageIter props;
                    dbus_message_iter_recurse(&ide, &props);
                    while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                      DBusMessageIter pde;
                      dbus_message_iter_recurse(&props, &pde);
                      const char* key = nullptr;
                      if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&pde, &key);
                      }
                      dbus_message_iter_next(&pde);
                      if (dbus_message_iter_get_arg_type(&pde) == DBUS_TYPE_VARIANT) {
                        DBusMessageIter var;
                        dbus_message_iter_recurse(&pde, &var);
                        int t = dbus_message_iter_get_arg_type(&var);
                        if (key && std::string(key) == "UUID" && t == DBUS_TYPE_STRING) {
                          const char* s;
                          dbus_message_iter_get_basic(&var, &s);
                          if (s) {
                            uuid = toLower(s);
                          }
                        } else if (key && std::string(key) == "Service" && t == DBUS_TYPE_OBJECT_PATH) {
                          const char* s;
                          dbus_message_iter_get_basic(&var, &s);
                          if (s) {
                            svc = s;
                          }
                        } else if (key && std::string(key) == "Flags" && t == DBUS_TYPE_ARRAY) {
                          DBusMessageIter farr;
                          dbus_message_iter_recurse(&var, &farr);
                          while (dbus_message_iter_get_arg_type(&farr) == DBUS_TYPE_STRING) {
                            const char* s = nullptr;
                            dbus_message_iter_get_basic(&farr, &s);
                            if (s) {
                              flags.push_back(toLower(s));
                            }
                            dbus_message_iter_next(&farr);
                          }
                        }
                      }
                      dbus_message_iter_next(&props);
                    }
                  }
                }
                dbus_message_iter_next(&ifs);
              }
              if (isChar && svc == svcPath) {
                list.push(oro::runtime::String(uuid.c_str()));
                JSON::Object p = JSON::Object::Entries {{
                  {"broadcast", false},
                  {"read", std::find(flags.begin(), flags.end(), "read") != flags.end()},
                  {"writeWithoutResponse", std::find(flags.begin(), flags.end(), "write-without-response") != flags.end()},
                  {"write", std::find(flags.begin(), flags.end(), "write") != flags.end()},
                  {"notify", std::find(flags.begin(), flags.end(), "notify") != flags.end()},
                  {"indicate", std::find(flags.begin(), flags.end(), "indicate") != flags.end()},
                  {"authenticatedSignedWrites", false},
                  {"reliableWrite", false},
                  {"writableAuxiliaries", false}
                }};
                propsBy.set(oro::runtime::String(uuid.c_str()), p);
              }
            }
            dbus_message_iter_next(&a);
          }
        }
        dbus_message_unref(reply);
        cb(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristics", list}, {"propertiesByCharacteristic", propsBy}}}}, oro::runtime::QueuedResponse{});
      }

      void characteristicReadValue (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& serviceUuid, const oro::runtime::String& charUuid, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string svcPath;
        if (!findServicePath(devPath, serviceUuid, svcPath)) {
          cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string chrPath; if (!findCharPath(svcPath, charUuid, chrPath)) { cb(seq, makeError("NotFoundError", "Characteristic not found"), oro::runtime::QueuedResponse{}); return; }
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", chrPath.c_str(), "org.bluez.GattCharacteristic1", "ReadValue");
        if (!msg) { cb(seq, makeError("OperationError", "Allocation failed"), oro::runtime::QueuedResponse{}); return; }
        // Append empty dict options
        DBusMessageIter it; dbus_message_iter_init_append(msg, &it);
        DBusMessageIter opt; dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opt);
        dbus_message_iter_close_container(&it, &opt);
        DBusError err; dbus_error_init(&err);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 4000, &err);
        dbus_message_unref(msg);
        if (!reply) {
          cb(seq, makeError("OperationError", err.message ? err.message : "ReadValue failed"), oro::runtime::QueuedResponse{});
          dbus_error_free(&err);
          return;
        }
        // Parse ay
        DBusMessageIter rit;
        dbus_message_iter_init(reply, &rit);
        if (dbus_message_iter_get_arg_type(&rit) != DBUS_TYPE_ARRAY) {
          dbus_message_unref(reply);
          cb(seq, makeError("TypeError", "Invalid reply"), oro::runtime::QueuedResponse{});
          return;
        }
        DBusMessageIter bytes;
        dbus_message_iter_recurse(&rit, &bytes);
        std::vector<unsigned char> buf;
        while (dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE) {
          unsigned char b = 0;
          dbus_message_iter_get_basic(&bytes, &b);
          buf.push_back(b);
          dbus_message_iter_next(&bytes);
        }
        dbus_message_unref(reply);
        oro::runtime::bytes::Buffer data(buf.size());
        if (!buf.empty()) {
          memcpy(data.data(), buf.data(), buf.size());
        }
        oro::runtime::http::Headers hdr;
        hdr.set("content-type", "application/octet-stream");
        hdr.set("content-length", (uint64_t) data.size());
        cb(seq, JSON::Object {}, oro::runtime::QueuedResponse{ oro::runtime::crypto::rand64(), 0, data.shared(), data.size(), hdr });
      }

      void characteristicWriteValue (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& serviceUuid, const oro::runtime::String& charUuid, const oro::runtime::bytes::Buffer& value, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string svcPath;
        if (!findServicePath(devPath, serviceUuid, svcPath)) {
          cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string chrPath;
        if (!findCharPath(svcPath, charUuid, chrPath)) {
          cb(seq, makeError("NotFoundError", "Characteristic not found"), oro::runtime::QueuedResponse{});
          return;
        }
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", chrPath.c_str(), "org.bluez.GattCharacteristic1", "WriteValue");
        if (!msg) { cb(seq, makeError("OperationError", "Allocation failed"), oro::runtime::QueuedResponse{}); return; }
        DBusMessageIter it;
        dbus_message_iter_init_append(msg, &it);
        // arg0: ay value
        DBusMessageIter barr;
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "y", &barr);
        for (size_t i = 0; i < value.size(); ++i) {
          unsigned char b = value.data()[i];
          dbus_message_iter_append_basic(&barr, DBUS_TYPE_BYTE, &b);
        }
        dbus_message_iter_close_container(&it, &barr);
        // arg1: a{sv} options (empty)
        DBusMessageIter opt;
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opt);
        dbus_message_iter_close_container(&it, &opt);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 4000, nullptr);
        dbus_message_unref(msg);
        if (reply) {
          dbus_message_unref(reply);
        }
        cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      }

      void characteristicStartNotifications (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& serviceUuid, const oro::runtime::String& charUuid, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string svcPath;
        if (!findServicePath(devPath, serviceUuid, svcPath)) {
          cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string chrPath;
        if (!findCharPath(svcPath, charUuid, chrPath)) {
          cb(seq, makeError("NotFoundError", "Characteristic not found"), oro::runtime::QueuedResponse{});
          return;
        }
        // StartNotify
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", chrPath.c_str(), "org.bluez.GattCharacteristic1", "StartNotify");
        if (msg) { DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 4000, nullptr); dbus_message_unref(msg); if (reply) dbus_message_unref(reply); }
        notifyMap[chrPath] = std::make_tuple(std::string(deviceId), to128(serviceUuid), to128(charUuid));
        cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      }

      void characteristicStopNotifications (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::String& serviceUuid, const oro::runtime::String& charUuid, const oro::runtime::core::Service::Callback cb) override {
        refreshObjects();
        const std::string devPath = devicePathFor(deviceId);
        if (devPath.empty()) {
          cb(seq, makeError("NotFoundError", "Device not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string svcPath;
        if (!findServicePath(devPath, serviceUuid, svcPath)) {
          cb(seq, makeError("NotFoundError", "Service not found"), oro::runtime::QueuedResponse{});
          return;
        }
        std::string chrPath;
        if (!findCharPath(svcPath, charUuid, chrPath)) {
          cb(seq, makeError("NotFoundError", "Characteristic not found"), oro::runtime::QueuedResponse{});
          return;
        }
        DBusMessage* msg = dbus_message_new_method_call("org.bluez", chrPath.c_str(), "org.bluez.GattCharacteristic1", "StopNotify");
        if (msg) {
          DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 4000, nullptr);
          dbus_message_unref(msg);
          if (reply) {
            dbus_message_unref(reply);
          }
        }
        notifyMap.erase(chrPath);
        cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      }

      void chooseDevice (const oro::runtime::String& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
        debug("[bluetooth][linux] chooseDevice seq=%s deviceId=%s", seq.c_str(), deviceId.c_str());
        cancelDiscoveryTimer();
        // Resolve the IPC call
        cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
        if (choosing && pendingCb) {
          stopDiscovery();
          JSON::Object::Entries deviceEntries;
          deviceEntries.insert_or_assign("id", deviceId);
          if (auto it = seenDevices.find(deviceId); it != seenDevices.end()) {
            if (it->second.name.size() > 0) {
              deviceEntries.insert_or_assign("name", it->second.name);
            }
          }
          JSON::Object device(deviceEntries);
          pendingCb(pendingSeq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"device", device}}}}, oro::runtime::QueuedResponse{});
          choosing = false; pendingSeq.clear(); pendingCb = nullptr;
          seenDevices.clear();
          discoveryActive.store(false, std::memory_order_release);
        }
      }

      void cancelRequest (const oro::runtime::String& seq, const oro::runtime::core::Service::Callback cb) override {
        cancelDiscoveryTimer();
        stopDiscovery();
        cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, oro::runtime::QueuedResponse{});
        if (choosing && pendingCb) {
          pendingCb(pendingSeq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, oro::runtime::QueuedResponse{});
          choosing = false; pendingSeq.clear(); pendingCb = nullptr;
          seenDevices.clear();
          discoveryActive.store(false, std::memory_order_release);
        }
      }

      void restartDiscovery (const oro::runtime::String& seq, const oro::runtime::core::Service::Callback cb) override {
        bool scheduled = false;
        if (choosing) {
          svc.queue.push([this]() mutable {
            this->restartDiscoveryInternal();
          });
          scheduled = true;
        }
        JSON::Object json = JSON::Object::Entries {{
          "data", JSON::Object::Entries {{"scheduled", scheduled}}
        }};
        cb(seq, json, oro::runtime::QueuedResponse{});
      }

      void deviceWatchAdvertisements(const oro::runtime::String& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", oro::runtime::String("BluetoothDevice.watchAdvertisements is not supported on Linux yet")}
        }};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }

      void deviceForget(const oro::runtime::String& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
        JSON::Object err = JSON::Object::Entries {{
          {"type", "NotSupportedError"},
          {"message", oro::runtime::String("BluetoothDevice.forget is not supported on Linux yet")}
        }};
        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
      }
  };
  LinuxBackend* LinuxBackend::g_self = nullptr;
}

namespace oro::runtime::core::services {
  std::unique_ptr<Bluetooth::Backend> makeBluetoothBackend (Bluetooth& svc) {
    return std::unique_ptr<Bluetooth::Backend>(new LinuxBackend(svc));
  }

  bool oro_runtime_bluetooth_linux_link_anchor () {
    return true;
  }
}

#endif // linux && !android
