#if defined(__ANDROID__)
#include "../../../platform/android/environment.hh"
#include "../../../runtime.hh"
#include "../../../app.hh"
#include "../../../bytes.hh"
#include "../../../debug.hh"
#include "../../../string.hh"
#include "../../../http.hh"
#include "../bluetooth.hh"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tuple>
#include <thread>
#include <chrono>
#include <cstring>

using oro::runtime::String;
using oro::runtime::Vector;
using oro::runtime::Map;
using oro::runtime::core::services::Bluetooth;
using oro::runtime::android::JNIEnvironmentAttachment;
namespace JSON = oro::runtime::JSON;
namespace bytes = oro::runtime::bytes;
namespace http = oro::runtime::http;

namespace {
  class AndroidBackend;
  static AndroidBackend* g_backend = nullptr;

  struct AndroidBackend final : public Bluetooth::Backend {
    Bluetooth& svc;
    bool scanning = false;
    std::string pendingSeq;
    oro::runtime::core::Service::Callback pendingCb = nullptr;
    Bluetooth::ParsedRequestDeviceOptions requestFilters;
    std::unordered_set<std::string> seenDeviceIds;
    struct DeviceInfo {
      String name;
      int rssi = 0;
      Vector<String> services;
      Map<uint16_t, Vector<uint8_t>> manufacturerData;
    };
    std::unordered_map<std::string, DeviceInfo> discovered;
    // Pending GATT connect callbacks by device id
    std::unordered_map<std::string, std::pair<std::string, oro::runtime::core::Service::Callback>> pendingConnect;
    struct PendingServicesEntry {
      std::string seq;
      oro::runtime::core::Service::Callback cb;
      std::string filter;
      bool hasFilter = false;
    };
    std::unordered_map<std::string, PendingServicesEntry> pendingServices;
    std::unordered_map<std::string, std::pair<std::string, oro::runtime::core::Service::Callback>> pendingChars;
    std::unordered_map<std::string, std::pair<std::string, oro::runtime::core::Service::Callback>> pendingRead;
    std::unordered_map<std::string, std::pair<std::string, oro::runtime::core::Service::Callback>> pendingWrite;

   public:
    AndroidBackend(Bluetooth& service) : svc(service) { g_backend = this; }
    ~AndroidBackend() override { if (g_backend == this) g_backend = nullptr; }

    void resetRequestState() {
      seenDeviceIds.clear();
      discovered.clear();
    }

    void onDeviceFound(
      const std::string& id,
      const std::string& name,
      int rssi,
      const std::vector<std::string>& servicesVec,
      const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& manufacturerVec
    ) {
      if (!scanning) return;

      Vector<String> services; services.reserve(servicesVec.size());
      for (const auto& s : servicesVec) {
        services.push_back(Bluetooth::normalizeUUID(String(s.c_str())));
      }

      Map<uint16_t, Vector<uint8_t>> manufacturerData;
      for (const auto& entry : manufacturerVec) {
        Vector<uint8_t> bytesVec;
        bytesVec.reserve(entry.second.size());
        for (auto b : entry.second) {
          bytesVec.push_back(b);
        }
        manufacturerData[entry.first] = std::move(bytesVec);
      }
      if (!Bluetooth::anyFilterMatches(requestFilters, String(name.c_str()), services, manufacturerData)) {
        return;
      }

      DeviceInfo info;
      info.name = String(name.c_str());
      info.rssi = rssi;
      info.services = services;
      info.manufacturerData = manufacturerData;
      discovered[id] = info;

      if (!seenDeviceIds.insert(id).second) {
        return;
      }

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
          if (win && win->bridge) {
            win->bridge->emit("bluetooth.devicefound", evt.str());
          }
        }
      }
    }

    void getAvailability(const std::string& seq, const oro::runtime::core::Service::Callback cb) const override {
      const auto& rt = *svc.context.getRuntime();
      const bool allowed = rt.hasPermission("bluetooth");
      bool enabled = false;
      JNIEnvironmentAttachment attachment(rt.android.jvm);
      if (attachment.env) {
        // android.bluetooth.BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        jclass cls = attachment.env->FindClass("android/bluetooth/BluetoothAdapter");
        if (cls) {
          jmethodID midGet = attachment.env->GetStaticMethodID(cls, "getDefaultAdapter", "()Landroid/bluetooth/BluetoothAdapter;");
          jobject adapter = (jobject) attachment.env->CallStaticObjectMethod(cls, midGet);
          if (adapter) {
            jmethodID midEnabled = attachment.env->GetMethodID(cls, "isEnabled", "()Z");
            enabled = attachment.env->CallBooleanMethod(adapter, midEnabled);
            attachment.env->DeleteLocalRef(adapter);
          }
          attachment.env->DeleteLocalRef(cls);
        }
      }
    JSON::Object json = JSON::Object::Entries {{
      "data", JSON::Object::Entries {{"available", allowed && enabled}}
    }};
    cb(seq, json, oro::runtime::QueuedResponse{});
  }

    void getDevices(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object json = JSON::Object::Entries {{
        "data", JSON::Object::Entries {{"devices", JSON::Array {}}}
      }};
      cb(seq, json, oro::runtime::QueuedResponse{});
    }

    void requestDevice(const std::string& seq, const JSON::Any& options, const oro::runtime::core::Service::Callback cb) override {
      auto matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::All;
      if (const auto runtime = svc.context.getRuntime()) {
        const auto& cfg = runtime->userConfig;
        if (cfg.contains("web_bluetooth_services_match")) {
          const auto mode = oro::runtime::string::toLowerCase(cfg.at("web_bluetooth_services_match"));
          if (mode == "any") {
            matchMode = Bluetooth::ParsedRequestDeviceOptions::ServiceMatchMode::Any;
          }
        }
      }

      JSON::Object error;
      Bluetooth::ParsedRequestDeviceOptions parsed;
      if (!Bluetooth::parseRequestDeviceOptions(options, parsed, &error, matchMode)) {
        cb(seq, JSON::Object::Entries {{"err", error}}, oro::runtime::QueuedResponse{});
        return;
      }

      requestFilters = std::move(parsed);
      resetRequestState();

      const auto runtimePtr = svc.context.getRuntime();
      if (!runtimePtr) {
        cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "InternalError"}, {"message", "Runtime unavailable"}}}}, oro::runtime::QueuedResponse{});
        return;
      }

      const auto& rt = *runtimePtr;
      auto attachment = JNIEnvironmentAttachment(rt.android.jvm);
      if (!attachment.env) {
        cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "InternalError"}, {"message", "JNI env unavailable"}}}}, oro::runtime::QueuedResponse{});
        return;
      }

      if (rt.android.buildInformation.sdk >= 31) {
        std::vector<std::string> requiredPermissions = {
          "android.permission.BLUETOOTH_SCAN",
          "android.permission.BLUETOOTH_CONNECT"
        };
        std::vector<std::string> missingPermissions;

        for (const auto& perm : requiredPermissions) {
          jstring permString = attachment.env->NewStringUTF(perm.c_str());
          const auto granted = CallClassMethodFromAndroidEnvironment(
            attachment.env,
            Boolean,
            rt.android.activity,
            "checkPermission",
            "(Ljava/lang/String;)Z",
            permString
          );
          attachment.env->DeleteLocalRef(permString);
          if (!granted) {
            missingPermissions.push_back(perm);
          }
        }

        if (!missingPermissions.empty()) {
          jclass stringClass = attachment.env->FindClass("java/lang/String");
          jobjectArray permissionsArray = attachment.env->NewObjectArray(
            (jsize) missingPermissions.size(),
            stringClass,
            nullptr
          );
          for (jsize i = 0; i < (jsize) missingPermissions.size(); ++i) {
            jstring permString = attachment.env->NewStringUTF(missingPermissions[i].c_str());
            attachment.env->SetObjectArrayElement(permissionsArray, i, permString);
            attachment.env->DeleteLocalRef(permString);
          }

          CallVoidClassMethodFromAndroidEnvironment(
            attachment.env,
            rt.android.activity,
            "requestPermissions",
            "([Ljava/lang/String;)V",
            permissionsArray
          );

          attachment.env->DeleteLocalRef(permissionsArray);
          attachment.env->DeleteLocalRef(stringClass);

          JSON::Object err = JSON::Object::Entries {{
            {"type", "NotAllowedError"},
            {"message", "Bluetooth permissions are required"}
          }};
          cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
          return;
        }
      }

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

      // Emit chooser request
      if (auto app = oro::runtime::app::App::sharedApplication()) {
        JSON::Object evt = JSON::Object::Entries {{"reason", "requestDevice"}};
        for (const auto &win : app->runtime.windowManager.windows) { if (win && win->bridge) win->bridge->emit("bluetooth.chooserequest", evt.str()); }
      }

      // Build services array from filters (union of advertised services)
      jobjectArray jServices = nullptr;
      std::unordered_set<std::string> serviceSet;
      for (const auto& filter : requestFilters.filters) {
        for (const auto& s : filter.services) {
          serviceSet.insert(std::string(s.c_str()));
        }
      }
      if (!serviceSet.empty()) {
        jclass jstr = attachment.env->FindClass("java/lang/String");
        jServices = attachment.env->NewObjectArray((jsize) serviceSet.size(), jstr, nullptr);
        jsize index = 0;
        for (const auto& svcId : serviceSet) {
          jstring js = attachment.env->NewStringUTF(svcId.c_str());
          attachment.env->SetObjectArrayElement(jServices, index++, js);
          attachment.env->DeleteLocalRef(js);
        }
        attachment.env->DeleteLocalRef(jstr);
      }

	      // BLE.startScan(context, services[])
	      jclass cls = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
	      if (!cls) {
	        cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE class missing"}}}}, oro::runtime::QueuedResponse{});
	        return;
	      }
	      jmethodID mid = attachment.env->GetStaticMethodID(cls, "startScan", "(Landroid/content/Context;[Ljava/lang/String;)V");
	      if (!mid) {
	        attachment.env->DeleteLocalRef(cls);
	        cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE.startScan missing"}}}}, oro::runtime::QueuedResponse{});
	        return;
	      }
      attachment.env->CallStaticVoidMethod(cls, mid, rt.android.activity, jServices);
      if (jServices) attachment.env->DeleteLocalRef(jServices);
      attachment.env->DeleteLocalRef(cls);

      scanning = true; pendingSeq = seq; pendingCb = cb;

      int timeoutCopy = timeoutMs;
      std::thread([this, timeoutCopy]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutCopy));
        this->svc.loop.dispatch([this]() {
	          if (!scanning) return;
	          scanning = false;
	          seenDeviceIds.clear();
	          JSON::Object err = JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "Timed out"}};
          if (!pendingSeq.empty() && pendingCb) {
            pendingCb(pendingSeq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
            pendingSeq.clear(); pendingCb = nullptr;
          }
          auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
          if (attachment.env) {
            jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
            if (c) {
              jmethodID m = attachment.env->GetStaticMethodID(c, "stopScan", "(Landroid/content/Context;)V");
              if (m) attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity);
              attachment.env->DeleteLocalRef(c);
            }
          }
        });
      }).detach();
    }

    void gattConnect(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      // Call into Kotlin BLE.connect(context, address)
	      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
	      if (!attachment.env) {
	        JSON::Object json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "InternalError"}, {"message", "JNI env unavailable"}}}};
	        return cb(seq, json, oro::runtime::QueuedResponse{});
	      }
	      jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
	      if (!c) {
	        JSON::Object json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE class missing"}}}};
	        return cb(seq, json, oro::runtime::QueuedResponse{});
	      }
	      jmethodID m = attachment.env->GetStaticMethodID(c, "connect", "(Landroid/content/Context;Ljava/lang/String;)V");
	      if (!m) {
	        attachment.env->DeleteLocalRef(c);
	        JSON::Object json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE.connect missing"}}}};
	        return cb(seq, json, oro::runtime::QueuedResponse{});
	      }
      jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
      pendingConnect[deviceId] = { seq, cb };
      attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity, jAddr);
      attachment.env->DeleteLocalRef(jAddr);
      attachment.env->DeleteLocalRef(c);
    }
    void gattDisconnect(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
      if (attachment.env) {
        jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
        if (c) {
          jmethodID m = attachment.env->GetStaticMethodID(c, "disconnect", "(Landroid/content/Context;Ljava/lang/String;)V");
          if (m) {
            jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
            attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity, jAddr);
            attachment.env->DeleteLocalRef(jAddr);
          }
          attachment.env->DeleteLocalRef(c);
        }
      }
      JSON::Object json = JSON::Object::Entries {{"data", JSON::Object {}}};
      cb(seq, json, oro::runtime::QueuedResponse{});
    }
    void gattGetPrimaryService(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& service, const oro::runtime::core::Service::Callback cb) override {
      this->gattGetPrimaryServices(seq, deviceId, service, [cb, service](String s, JSON::Any json, oro::runtime::QueuedResponse qr){
        if (json.type != JSON::Type::Object) {
          JSON::Object err = JSON::Object::Entries {{"type", "InternalError"}, {"message", "Unexpected response"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
          return;
        }

        const auto& obj = json.as<JSON::Object>();
        if (obj.has("err")) {
          cb(s, json, qr);
          return;
        }

        if (!obj.has("data") || obj.get("data").type != JSON::Type::Object) {
          JSON::Object err = JSON::Object::Entries {{"type", "InternalError"}, {"message", "Unexpected response"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
          return;
        }

        const auto& data = obj.get("data").as<JSON::Object>();
        if (!data.has("services") || data.get("services").type != JSON::Type::Array) {
          JSON::Object err = JSON::Object::Entries {{"type", "InternalError"}, {"message", "Unexpected response"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
          return;
        }

        const auto& arr = data.get("services").as<JSON::Array>();
        bool found = false; for (size_t i = 0; i < arr.size(); ++i) { if (arr[i].str() == service) { found = true; break; } }
	        if (!found) {
	          JSON::Object err = JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "Service not found"}};
	          cb(s, JSON::Object::Entries {{"err", err}}, qr);
	        } else {
	          cb(s, JSON::Object::Entries {{"data", JSON::Object::Entries {{"service", service}}}}, qr);
	        }
      });
    }
    void gattGetPrimaryServices(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& serviceFilter, const oro::runtime::core::Service::Callback cb) override {
	      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
	      if (!attachment.env) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "InternalError"}, {"message", "JNI env unavailable"}}}}, oro::runtime::QueuedResponse{}); return; }
	      jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
	      if (!c) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE class missing"}}}}, oro::runtime::QueuedResponse{}); return; }
      jmethodID mGet = attachment.env->GetStaticMethodID(c, "getServices", "(Landroid/content/Context;Ljava/lang/String;)[Ljava/lang/String;");
	      jmethodID mDisc = attachment.env->GetStaticMethodID(c, "discoverServices", "(Landroid/content/Context;Ljava/lang/String;)V");
	      if (!mGet || !mDisc) { attachment.env->DeleteLocalRef(c); cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE.getServices/discoverServices missing"}}}}, oro::runtime::QueuedResponse{}); return; }
      jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
      const bool hasFilter = !serviceFilter.empty();
      String normalizedFilterString;
      if (hasFilter) {
        normalizedFilterString = Bluetooth::normalizeUUID(String(serviceFilter.c_str()));
      }
      jobjectArray arr = (jobjectArray) attachment.env->CallStaticObjectMethod(c, mGet, svc.context.getRuntime()->android.activity, jAddr);
      if (arr) {
        // marshal array
        size_t n = (size_t) attachment.env->GetArrayLength(arr);
        JSON::Array out;
        for (size_t i = 0; i < n; ++i) {
          jstring js = (jstring) attachment.env->GetObjectArrayElement(arr, (jsize) i);
          const char* s = js ? attachment.env->GetStringUTFChars(js, nullptr) : "";
          String normalized = Bluetooth::normalizeUUID(String(s ? s : ""));
          if (!hasFilter || normalized == normalizedFilterString) {
            out.push(normalized);
          }
          if (js) attachment.env->ReleaseStringUTFChars(js, s);
          if (js) attachment.env->DeleteLocalRef(js);
        }
	        if (hasFilter && out.size() == 0) {
	          JSON::Object err = JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "Service not found"}};
	          attachment.env->DeleteLocalRef(arr);
	          attachment.env->DeleteLocalRef(jAddr);
	          attachment.env->DeleteLocalRef(c);
	          cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
	          return;
	        }
	        JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"services", out}}}};
        attachment.env->DeleteLocalRef(arr);
        attachment.env->DeleteLocalRef(jAddr);
        attachment.env->DeleteLocalRef(c);
        cb(seq, json, oro::runtime::QueuedResponse{});
        return;
      }
      // No cached services yet; trigger discovery and respond later
      PendingServicesEntry entry;
      entry.seq = seq;
      entry.cb = cb;
      if (hasFilter) {
        entry.filter = std::string(normalizedFilterString.c_str());
        entry.hasFilter = true;
      }
      pendingServices[deviceId] = std::move(entry);
      attachment.env->CallStaticVoidMethod(c, mDisc, svc.context.getRuntime()->android.activity, jAddr);
      attachment.env->DeleteLocalRef(jAddr);
      attachment.env->DeleteLocalRef(c);
    }
    void serviceGetCharacteristic(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& service, const std::string& characteristic, const oro::runtime::core::Service::Callback cb) override {
      this->serviceGetCharacteristics(seq, deviceId, service, "", [cb, characteristic](String s, JSON::Any json, oro::runtime::QueuedResponse qr){
        if (json.type != JSON::Type::Object) {
          JSON::Object err = JSON::Object::Entries {{"type", "InternalError"}, {"message", "Unexpected response"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
          return;
        }

        const auto& obj = json.as<JSON::Object>();
        if (obj.has("err")) {
          cb(s, json, qr);
          return;
        }

        if (!obj.has("data") || obj.get("data").type != JSON::Type::Object) {
          JSON::Object err = JSON::Object::Entries {{"type", "InternalError"}, {"message", "Unexpected response"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
          return;
        }

        const auto& data = obj.get("data").as<JSON::Object>();
        if (!data.has("characteristics") || data.get("characteristics").type != JSON::Type::Array) {
          JSON::Object err = JSON::Object::Entries {{"type", "InternalError"}, {"message", "Unexpected response"}};
          cb(s, JSON::Object::Entries {{"err", err}}, qr);
          return;
        }

        const auto& chars = data.get("characteristics").as<JSON::Array>();
        bool found = false; for (size_t i = 0; i < chars.size(); ++i) { if (chars[i].str() == characteristic) { found = true; break; } }
	        if (!found) {
	          JSON::Object err = JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "Characteristic not found"}};
	          cb(s, JSON::Object::Entries {{"err", err}}, qr);
	        } else {
	          cb(s, JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristic", characteristic}}}}, qr);
	        }
      });
    }
    void serviceGetCharacteristics(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& service, const std::string&, const oro::runtime::core::Service::Callback cb) override {
	      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
	      if (!attachment.env) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "InternalError"}, {"message", "JNI env unavailable"}}}}, oro::runtime::QueuedResponse{}); return; }
	      jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
	      if (!c) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE class missing"}}}}, oro::runtime::QueuedResponse{}); return; }
      jmethodID mGet = attachment.env->GetStaticMethodID(c, "getCharacteristics", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;");
      jmethodID mGetFlags = attachment.env->GetStaticMethodID(c, "getCharacteristicPropertiesFlags", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)[I");
	      jmethodID mReq = attachment.env->GetStaticMethodID(c, "requestCharacteristics", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V");
	      if (!mGet || !mReq) { attachment.env->DeleteLocalRef(c); cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE.get/requestCharacteristics missing"}}}}, oro::runtime::QueuedResponse{}); return; }
      jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
      jstring jSvc = attachment.env->NewStringUTF(service.c_str());
      jobjectArray arr = (jobjectArray) attachment.env->CallStaticObjectMethod(c, mGet, svc.context.getRuntime()->android.activity, jAddr, jSvc);
      if (arr) {
        size_t n = (size_t) attachment.env->GetArrayLength(arr);
        JSON::Array out;
        // optional flags, aligned with order if available
        std::vector<int> flags;
        if (mGetFlags) {
          jintArray jFlags = (jintArray) attachment.env->CallStaticObjectMethod(c, mGetFlags, svc.context.getRuntime()->android.activity, jAddr, jSvc);
          if (jFlags) {
            jsize fn = attachment.env->GetArrayLength(jFlags);
            flags.resize((size_t) fn);
            attachment.env->GetIntArrayRegion(jFlags, 0, fn, flags.data());
            attachment.env->DeleteLocalRef(jFlags);
          }
        }
        JSON::Object propsMap;
        for (size_t i = 0; i < n; ++i) {
          jstring js = (jstring) attachment.env->GetObjectArrayElement(arr, (jsize) i);
          const char* s = js ? attachment.env->GetStringUTFChars(js, nullptr) : "";
          out.push(String(s ? s : ""));
          if (!flags.empty() && i < flags.size()) {
            int p = flags[i];
            // Android BluetoothGattCharacteristic property bit masks
            const int PROP_BROADCAST = 0x01;
            const int PROP_READ = 0x02;
            const int PROP_WRITE_NO_RESP = 0x04;
            const int PROP_WRITE = 0x08;
            const int PROP_NOTIFY = 0x10;
            const int PROP_INDICATE = 0x20;
            const int PROP_SIGNED_WRITE = 0x40;
            const int PROP_EXTENDED = 0x80;
            JSON::Object props = JSON::Object::Entries {{
              {"broadcast", (bool) (p & PROP_BROADCAST)},
              {"read", (bool) (p & PROP_READ)},
              {"writeWithoutResponse", (bool) (p & PROP_WRITE_NO_RESP)},
              {"write", (bool) (p & PROP_WRITE)},
              {"notify", (bool) (p & PROP_NOTIFY)},
              {"indicate", (bool) (p & PROP_INDICATE)},
              {"authenticatedSignedWrites", (bool) (p & PROP_SIGNED_WRITE)},
              {"reliableWrite", false},
              {"writableAuxiliaries", (bool) (p & PROP_EXTENDED)}
            }};
            propsMap.set(String(s ? s : ""), props);
          }
          if (js) attachment.env->ReleaseStringUTFChars(js, s);
          if (js) attachment.env->DeleteLocalRef(js);
        }
        JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristics", out}, {"propertiesByCharacteristic", propsMap}}}};
        attachment.env->DeleteLocalRef(arr);
        attachment.env->DeleteLocalRef(jAddr);
        attachment.env->DeleteLocalRef(jSvc);
        attachment.env->DeleteLocalRef(c);
        cb(seq, json, oro::runtime::QueuedResponse{});
        return;
      }
      // Not ready, request and pend
      const std::string key = deviceId + "|" + service;
      pendingChars[key] = { seq, cb };
      attachment.env->CallStaticVoidMethod(c, mReq, svc.context.getRuntime()->android.activity, jAddr, jSvc);
      attachment.env->DeleteLocalRef(jAddr);
      attachment.env->DeleteLocalRef(jSvc);
      attachment.env->DeleteLocalRef(c);
    }
    void characteristicReadValue(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& service, const std::string& characteristic, const oro::runtime::core::Service::Callback cb) override {
	      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
	      if (!attachment.env) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "InternalError"}, {"message", "JNI env unavailable"}}}}, oro::runtime::QueuedResponse{}); return; }
	      jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
	      if (!c) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE class missing"}}}}, oro::runtime::QueuedResponse{}); return; }
	      jmethodID m = attachment.env->GetStaticMethodID(c, "readCharacteristic", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	      if (!m) { attachment.env->DeleteLocalRef(c); cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE.readCharacteristic missing"}}}}, oro::runtime::QueuedResponse{}); return; }
      jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
      jstring jSvc = attachment.env->NewStringUTF(service.c_str());
      jstring jChr = attachment.env->NewStringUTF(characteristic.c_str());
      const std::string key = deviceId + "|" + service + "|" + characteristic;
      pendingRead[key] = { seq, cb };
      attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity, jAddr, jSvc, jChr);
      attachment.env->DeleteLocalRef(jAddr);
      attachment.env->DeleteLocalRef(jSvc);
      attachment.env->DeleteLocalRef(jChr);
      attachment.env->DeleteLocalRef(c);
    }
    void characteristicWriteValue(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& service, const std::string& characteristic, const bytes::Buffer& value, const oro::runtime::core::Service::Callback cb) override {
	      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
	      if (!attachment.env) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "InternalError"}, {"message", "JNI env unavailable"}}}}, oro::runtime::QueuedResponse{}); return; }
	      jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
	      if (!c) { cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE class missing"}}}}, oro::runtime::QueuedResponse{}); return; }
	      jmethodID m = attachment.env->GetStaticMethodID(c, "writeCharacteristic", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[B)V");
	      if (!m) { attachment.env->DeleteLocalRef(c); cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "NotSupportedError"}, {"message", "BLE.writeCharacteristic missing"}}}}, oro::runtime::QueuedResponse{}); return; }
      jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
      jstring jSvc = attachment.env->NewStringUTF(service.c_str());
      jstring jChr = attachment.env->NewStringUTF(characteristic.c_str());
      jbyteArray jValue = attachment.env->NewByteArray((jsize) value.size());
      if (jValue && value.size() > 0) attachment.env->SetByteArrayRegion(jValue, 0, (jsize) value.size(), reinterpret_cast<const jbyte*>(value.data()));
      const std::string key = deviceId + "|" + service + "|" + characteristic;
      pendingWrite[key] = { seq, cb };
      attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity, jAddr, jSvc, jChr, jValue);
      if (jValue) attachment.env->DeleteLocalRef(jValue);
      attachment.env->DeleteLocalRef(jAddr);
      attachment.env->DeleteLocalRef(jSvc);
      attachment.env->DeleteLocalRef(jChr);
      attachment.env->DeleteLocalRef(c);
    }
    void characteristicStartNotifications(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& service, const std::string& characteristic, const oro::runtime::core::Service::Callback cb) override {
      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
      if (attachment.env) {
        jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
        if (c) {
          jmethodID m = attachment.env->GetStaticMethodID(c, "startNotifications", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
          if (m) {
            jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
            jstring jSvc = attachment.env->NewStringUTF(service.c_str());
            jstring jChr = attachment.env->NewStringUTF(characteristic.c_str());
            attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity, jAddr, jSvc, jChr);
            attachment.env->DeleteLocalRef(jAddr);
            attachment.env->DeleteLocalRef(jSvc);
            attachment.env->DeleteLocalRef(jChr);
          }
          attachment.env->DeleteLocalRef(c);
        }
      }
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }
    void characteristicStopNotifications(const std::string& seq, const Bluetooth::DeviceID& deviceId, const std::string& service, const std::string& characteristic, const oro::runtime::core::Service::Callback cb) override {
      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
      if (attachment.env) {
        jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
        if (c) {
          jmethodID m = attachment.env->GetStaticMethodID(c, "stopNotifications", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
          if (m) {
            jstring jAddr = attachment.env->NewStringUTF(deviceId.c_str());
            jstring jSvc = attachment.env->NewStringUTF(service.c_str());
            jstring jChr = attachment.env->NewStringUTF(characteristic.c_str());
            attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity, jAddr, jSvc, jChr);
            attachment.env->DeleteLocalRef(jAddr);
            attachment.env->DeleteLocalRef(jSvc);
            attachment.env->DeleteLocalRef(jChr);
          }
          attachment.env->DeleteLocalRef(c);
        }
      }
      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
    }
    void chooseDevice(const std::string& seq, const Bluetooth::DeviceID& deviceId, const oro::runtime::core::Service::Callback cb) override {
      // Stop scan
      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
      if (attachment.env) {
        jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
        if (c) {
          jmethodID m = attachment.env->GetStaticMethodID(c, "stopScan", "(Landroid/content/Context;)V");
          if (m) attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity);
          attachment.env->DeleteLocalRef(c);
        }
      }

      scanning = false;
      auto it = discovered.find(deviceId);
	      if (it == discovered.end()) {
	        JSON::Object err = JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "Device not found"}};
	        cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
	        if (!pendingSeq.empty() && pendingCb) {
	          pendingCb(pendingSeq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
	          pendingSeq.clear(); pendingCb = nullptr;
	        }
	        resetRequestState();
	        return;
	      }

      cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
      if (!pendingSeq.empty() && pendingCb) {
        JSON::Object device = JSON::Object::Entries {{
          {"id", String(deviceId.c_str())},
          {"name", it->second.name}
        }};
        if (!it->second.services.empty()) {
          JSON::Array servicesJson;
          for (const auto& s : it->second.services) servicesJson.push(s);
          device.set("services", servicesJson);
        }
        if (!it->second.manufacturerData.empty()) {
          JSON::Array mdJson;
          for (const auto& entry : it->second.manufacturerData) {
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
        pendingCb(pendingSeq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"device", device}}}}, oro::runtime::QueuedResponse{});
        pendingSeq.clear(); pendingCb = nullptr;
      }
      resetRequestState();
    }
    void cancelRequest(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      auto attachment = JNIEnvironmentAttachment(svc.context.getRuntime()->android.jvm);
      if (attachment.env) {
        jclass c = attachment.env->FindClass("oro/runtime/bluetooth/BLE");
        if (c) {
          jmethodID m = attachment.env->GetStaticMethodID(c, "stopScan", "(Landroid/content/Context;)V");
          if (m) attachment.env->CallStaticVoidMethod(c, m, svc.context.getRuntime()->android.activity);
          attachment.env->DeleteLocalRef(c);
        }
      }
      scanning = false;
	      cb(seq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, oro::runtime::QueuedResponse{});
	      if (!pendingSeq.empty() && pendingCb) {
	        pendingCb(pendingSeq, JSON::Object::Entries {{"err", JSON::Object::Entries {{"type", "AbortError"}, {"message", "Cancelled"}}}}, oro::runtime::QueuedResponse{});
	        pendingSeq.clear(); pendingCb = nullptr;
	      }
      resetRequestState();
    }

    void deviceWatchAdvertisements(const std::string& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
	      JSON::Object err = JSON::Object::Entries {{
	        {"type", "NotSupportedError"},
	        {"message", String("BluetoothDevice.watchAdvertisements is not supported on Android yet")}
	      }};
	      cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
    }

    void deviceForget(const std::string& seq, const Bluetooth::DeviceID&, const oro::runtime::core::Service::Callback cb) override {
	      JSON::Object err = JSON::Object::Entries {{
	        {"type", "NotSupportedError"},
	        {"message", String("BluetoothDevice.forget is not supported on Android yet")}
	      }};
	      cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
    }

    void restartDiscovery(const std::string& seq, const oro::runtime::core::Service::Callback cb) override {
      JSON::Object err = JSON::Object::Entries {{
        {"type", "NotSupportedError"},
        {"message", String("Restarting discovery is not supported on Android")}
      }};
      cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
    }
  };
}

extern "C" void oro_bluetooth_android_onDeviceFound(
  const char* id,
  const char* name,
  int rssi,
  const char** services,
  size_t serviceCount,
  const uint16_t* manufacturerIds,
  const unsigned char* const* manufacturerData,
  const size_t* manufacturerLengths,
  size_t manufacturerCount
) {
  if (!g_backend) return;
  std::vector<std::string> svc;
  if (services && serviceCount) {
    svc.reserve(serviceCount);
    for (size_t i = 0; i < serviceCount; ++i) {
      svc.emplace_back(services[i] ? services[i] : "");
    }
  }
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> manufacturer;
  if (manufacturerIds && manufacturerData && manufacturerLengths && manufacturerCount > 0) {
    manufacturer.reserve(manufacturerCount);
    for (size_t i = 0; i < manufacturerCount; ++i) {
      std::vector<uint8_t> bytes;
      const size_t len = manufacturerLengths[i];
      if (len > 0 && manufacturerData[i]) {
        bytes.assign(manufacturerData[i], manufacturerData[i] + len);
      }
      manufacturer.emplace_back(manufacturerIds[i], std::move(bytes));
    }
  }
  g_backend->onDeviceFound(id ? id : "", name ? name : "", rssi, svc, manufacturer);
}

extern "C" void oro_bluetooth_android_onGattConnected(const char* id, int ok, const char* message) {
	  if (!g_backend) return;
	  const std::string deviceId = id ? id : "";
	  auto it = g_backend->pendingConnect.find(deviceId);
	  if (it == g_backend->pendingConnect.end()) return;
	  const auto seq = it->second.first;
	  const auto cb = it->second.second;
	  g_backend->pendingConnect.erase(it);
		  if (ok) {
		    cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
		  } else {
		    JSON::Object err = JSON::Object::Entries {{"type", "NetworkError"}, {"message", String(message ? message : "Connection failed")}};
		    cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
		  }
	}

extern "C" void oro_bluetooth_android_onGattDisconnected(const char* id, const char* reason) {
  using oro::runtime::app::App;
	  auto app = App::sharedApplication();
	  if (!app) return;
	  JSON::Object evt = JSON::Object::Entries {{
	    {"deviceId", String(id ? id : "")},
	    {"reason", String(reason ? reason : "")}
	  }};
	  for (const auto& win : app->runtime.windowManager.windows) {
	    if (win && win->bridge) win->bridge->emit("bluetooth.gattserverdisconnected", evt.str());
	  }
}

extern "C" void oro_bluetooth_android_onServicesDiscovered(const char* id, const char** services, size_t n) {
  if (!g_backend) return;
  std::string deviceId = id ? id : "";
  auto it = g_backend->pendingServices.find(deviceId);
  if (it == g_backend->pendingServices.end()) return;
  auto entry = it->second;
  g_backend->pendingServices.erase(it);
  String filterStr;
  if (entry.hasFilter) {
    filterStr = String(entry.filter.c_str());
  }
  JSON::Array arr;
  for (size_t i = 0; i < n; ++i) {
    const char* raw = services[i] ? services[i] : "";
    String normalized = Bluetooth::normalizeUUID(String(raw));
    if (entry.hasFilter && normalized != filterStr) {
      continue;
    }
    arr.push(normalized);
  }
	  if (entry.hasFilter && arr.size() == 0) {
	    JSON::Object err = JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "Service not found"}};
	    entry.cb(entry.seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
	    return;
	  }
  JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"services", arr}}}};
  entry.cb(entry.seq, json, oro::runtime::QueuedResponse{});
}

extern "C" void oro_bluetooth_android_onCharacteristicsDiscovered(const char* id, const char* service, const char** chars, size_t n) {
  if (!g_backend) return;
  const std::string key = std::string(id ? id : "") + "|" + std::string(service ? service : "");
  auto it = g_backend->pendingChars.find(key);
  if (it == g_backend->pendingChars.end()) return;
  auto seq = it->second.first; auto cb = it->second.second;
  g_backend->pendingChars.erase(it);
  JSON::Array arr; for (size_t i = 0; i < n; ++i) { arr.push(String(chars[i] ? chars[i] : "")); }
  JSON::Object json = JSON::Object::Entries {{"data", JSON::Object::Entries {{"characteristics", arr}}}};
  cb(seq, json, oro::runtime::QueuedResponse{});
}

extern "C" void oro_bluetooth_android_onCharacteristicRead(const char* id, const char* service, const char* characteristic, const unsigned char* data, size_t n) {
  if (!g_backend) return;
  const std::string key = std::string(id ? id : "") + "|" + std::string(service ? service : "") + "|" + std::string(characteristic ? characteristic : "");
  auto it = g_backend->pendingRead.find(key);
  if (it == g_backend->pendingRead.end()) return;
  auto seq = it->second.first; auto cb = it->second.second;
  g_backend->pendingRead.erase(it);
  bytes::Buffer buf(n);
  if (n && data) memcpy(buf.data(), data, n);
  http::Headers hdr; hdr.set("content-type", "application/octet-stream"); hdr.set("content-length", (uint64_t) n);
  cb(seq, JSON::Object {}, oro::runtime::QueuedResponse { oro::runtime::crypto::rand64(), 0, buf.shared(), buf.size(), hdr.str() });
}

extern "C" void oro_bluetooth_android_onCharacteristicWrite(const char* id, const char* service, const char* characteristic, int ok, const char* message) {
  if (!g_backend) return;
  const std::string key = std::string(id ? id : "") + "|" + std::string(service ? service : "") + "|" + std::string(characteristic ? characteristic : "");
  auto it = g_backend->pendingWrite.find(key);
	  if (it == g_backend->pendingWrite.end()) return;
	  auto seq = it->second.first; auto cb = it->second.second;
	  g_backend->pendingWrite.erase(it);
	  if (ok) {
	    cb(seq, JSON::Object::Entries {{"data", JSON::Object {}}}, oro::runtime::QueuedResponse{});
	  } else {
	    JSON::Object err = JSON::Object::Entries {{"type", "OperationError"}, {"message", String(message ? message : "Write failed")}};
	    cb(seq, JSON::Object::Entries {{"err", err}}, oro::runtime::QueuedResponse{});
	  }
}

extern "C" void oro_bluetooth_android_onCharacteristicChanged(const char* id, const char* service, const char* characteristic, const unsigned char* data, size_t n) {
  if (!g_backend) return;
  bytes::Buffer buf(n);
  if (n && data) memcpy(buf.data(), data, n);
  g_backend->svc.notifyCharacteristicValue(String(id ? id : ""), String(service ? service : ""), String(characteristic ? characteristic : ""), buf);
}

namespace oro::runtime::core::services {
  std::unique_ptr<Bluetooth::Backend> makeBluetoothBackend(Bluetooth& svc) {
    return std::unique_ptr<Bluetooth::Backend>(new AndroidBackend(svc));
  }
}

#endif // __ANDROID__
