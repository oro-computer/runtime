#include "libusb_backend.hh"
#include "transport.hh"

#if ORO_RUNTIME_PLATFORM_ANDROID

#include "../../../app.hh"
#include "../../../debug.hh"
#include "../../../json.hh"
#include "../../../runtime.hh"
#include "../../../string.hh"
#include "../../../platform/android.hh"
#include "../../../platform/android/string_wrap.hh"
#include "../../../platform/android/environment.hh"
#include "../../../queued_response.hh"

#include <algorithm>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace oro::runtime::core::services::usb {
  class AndroidBackend;
  static std::weak_ptr<AndroidBackend> activeBackend;
  static Mutex activeBackendMutex;
  static std::atomic<uint64_t> nextPermissionRequestId{1};

  namespace {
    JSON::Object makeErrorObject(const char* type, const String& message) {
      return JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String(type)},
          {"name", String(type)},
          {"message", message}
        }}}
      }};
    }

    JSON::Object makeOkObject() {
      return JSON::Object::Entries {{
        {"data", JSON::Object::Entries {{"ok", true}}}
      }};
    }


  }

  class AndroidBackend final : public TransportBackend {
    public:
      using Callback = USB::Callback;
      using EnumerateResult = USB::Backend::EnumerateResult;
      using DeviceDescriptor = USB::Backend::DeviceDescriptor;
      using InterfaceDescriptor = USB::Backend::InterfaceDescriptor;

      explicit AndroidBackend(USB& svc)
        : TransportBackend(svc) {
        libusb_init_option option{};
        option.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY;
        libusb_init_context(&ctx, &option, 1);
      }

      void disconnectClient (uint64_t client) override {
        TransportBackend::disconnectClient(client);
        Lock lock(this->mutex);
        if (pendingChooser && pendingChooser->client == client) pendingChooser.reset();
      }

      void getDevices(const USB::Request& seq, const Callback cb) override {
        this->enumerateDevices([this, cb, seq](const EnumerateResult& result) {
          if (!result.ok) {
            cb(seq, result.error, oro::runtime::QueuedResponse{});
            return;
          }

          JSON::Array::Entries devices;
          devices.reserve(result.devices.size());
          for (const auto& descriptor : result.devices) {
            if (!descriptor.authorized) continue;
            devices.push_back(describeDevice(descriptor, seq));
          }

          cb(seq, JSON::Object::Entries {{
            {"data", JSON::Object::Entries {{ "devices", devices }}}
          }}, oro::runtime::QueuedResponse{});
        });
      }

      void requestDevice(const USB::Request& seq, const USB::RequestDeviceOptions& options, const Callback cb) override {
        if (!this->service.context.getRuntime()->hasPermission("usb")) {
          cb(seq, makeErrorObject("NotAllowedError", "USB runtime permission disabled"), oro::runtime::QueuedResponse{});
          return;
        }

        this->enumerateDevices([this, seq, cb, options](const EnumerateResult& result) mutable {
          if (!result.ok) {
            cb(seq, result.error, oro::runtime::QueuedResponse{});
            return;
          }

          if (result.devices.empty()) {
            cb(seq, makeErrorObject("NotFoundError", "No USB devices are available"), oro::runtime::QueuedResponse{});
            return;
          }

          Vector<DeviceDescriptor> candidates;
          candidates.reserve(result.devices.size());
          Set<String> seen;

          for (const auto& descriptor : result.devices) {
            bool matches = options.acceptAllDevices || options.filters.empty();
            if (!options.acceptAllDevices) {
              for (const auto& filter : options.filters) {
                if (filterMatches(filter, descriptor)) {
                  matches = true;
                  break;
                }
              }
            }

            if (!matches || std::any_of(options.exclusionFilters.begin(), options.exclusionFilters.end(),
                  [&](const auto& filter) { return filterMatches(filter, descriptor); })) {
              continue;
            }

            if (!seen.insert(descriptor.deviceId).second) {
              continue;
            }

            candidates.push_back(descriptor);
          }

          if (candidates.empty()) {
            cb(seq, makeErrorObject("NotFoundError", "No USB devices matched the requested filters"), oro::runtime::QueuedResponse{});
            return;
          }

          if (std::any_of(candidates.begin(), candidates.end(), [](const auto& device) { return !device.authorized; })) {
            requestPermissionForDevices(seq, cb, candidates);
            return;
          }

          {
            Lock lock(this->mutex);
            if (this->pendingChooser.has_value()) {
              cb(seq, makeErrorObject("InvalidStateError", "Another usb.requestDevice is already pending"), oro::runtime::QueuedResponse{});
              return;
            }

            RequestState state;
            state.client = seq.client;
            state.pending = candidates;
            this->pendingChooser = std::move(state);
          }

          JSON::Array::Entries deviceEntries;
          deviceEntries.reserve(candidates.size());
          for (const auto& descriptor : candidates) {
            deviceEntries.push_back(descriptorToJson(descriptor));
          }

          JSON::Object::Entries data = {
            {"devices", deviceEntries},
            {"requiresSelection", true},
            {"context", String("usb.requestDevice")}
          };

          cb(seq, JSON::Object::Entries {{"data", data}}, oro::runtime::QueuedResponse{});
        });
      }

      void forgetDevice(const USB::Request& seq, const String& deviceId, const Callback cb) override {
        enqueue([this, seq, cb, deviceId] {
          revokeDevice(deviceId);
          sessions.erase(deviceId);
          cb(seq, makeOkObject(), {});
        });
      }

      int openDevice (const String& deviceId, Session& session) override {
        auto app = oro::runtime::app::App::sharedApplication();
        if (!app || !ctx) return LIBUSB_ERROR_NO_DEVICE;
        auto& android = app->runtime.android;
        android::JNIEnvironmentAttachment attachment(android.jvm);
        if (!attachment.env) return LIBUSB_ERROR_OTHER;
        auto env = attachment.env;
        const auto cls = env->GetObjectClass(android.activity);
        const auto method = env->GetMethodID(cls, "openUSBFileDescriptor", "(Ljava/lang/String;)I");
        const auto id = android::StringWrap(env, deviceId).j_str();
        session.fd = method ? env->CallIntMethod(android.activity, method, id) : -1;
        env->DeleteLocalRef(id);
        env->DeleteLocalRef(cls);
        if (env->ExceptionCheck()) {
          env->ExceptionClear();
          return LIBUSB_ERROR_ACCESS;
        }
        if (session.fd < 0) return LIBUSB_ERROR_ACCESS;
        return libusb_wrap_sys_device(ctx, session.fd, &session.handle);
      }

      void enumerateDevices(Function<void(const EnumerateResult&)> completion) override {
        enqueue([this, completion] { enumerateOnWorker(completion); });
      }

      void enumerateOnWorker (Function<void(const EnumerateResult&)> completion) {
        auto app = oro::runtime::app::App::sharedApplication();
        if (!app) {
          completion(EnumerateResult{
            .ok = false,
            .error = makeErrorObject("InvalidStateError", "Application unavailable")
          });
          return;
        }

        auto& android = app->runtime.android;
        android::JNIEnvironmentAttachment attachment(android.jvm);
        if (!attachment.env) {
          completion(EnumerateResult{
            .ok = false,
            .error = makeErrorObject("InvalidStateError", "JNI environment unavailable")
          });
          return;
        }

        jclass cls = attachment.env->GetObjectClass(android.activity);
        if (!cls) {
          attachment.printException();
          completion(EnumerateResult{
            .ok = false,
            .error = makeErrorObject("OperationError", "USBPlatform class not found")
          });
          return;
        }

        jmethodID enumerate = attachment.env->GetMethodID(
          cls,
          "enumerateUSB",
          "()Ljava/lang/String;"
        );

        if (!enumerate) {
          attachment.printException();
          attachment.env->DeleteLocalRef(cls);
          completion(EnumerateResult{
            .ok = false,
            .error = makeErrorObject("OperationError", "USBPlatform.enumerate unavailable")
          });
          return;
        }

        const auto resultString = (jstring) attachment.env->CallObjectMethod(
          android.activity,
          enumerate
        );

        bool hadException = attachment.hasException();
        if (hadException) {
          attachment.printException();
          attachment.env->ExceptionClear();
        }

        String serialized = "[]";
        if (!hadException && resultString) {
          serialized = android::StringWrap(attachment.env, resultString).str();
        }

        if (resultString) {
          attachment.env->DeleteLocalRef(resultString);
        }
        attachment.env->DeleteLocalRef(cls);

        if (hadException) {
          completion(EnumerateResult{
            .ok = false,
            .error = makeErrorObject("OperationError", "Failed to enumerate USB devices")
          });
          return;
        }

        USB::Backend::EnumerateResult output;
        output.ok = true;

        try {
          const auto parsed = JSON::parse(serialized);
          if (parsed.type != JSON::Type::Array) {
            output.ok = false;
            output.error = makeErrorObject("OperationError", "Unexpected enumeration payload");
          } else {
            const auto& array = parsed.as<JSON::Array>();
            output.devices.reserve(array.size());
            for (const auto& entry : array) {
              if (entry.type != JSON::Type::Object) {
                continue;
              }
              const auto& obj = entry.as<JSON::Object>();
              DeviceDescriptor descriptor;
              descriptor.deviceId = obj.get("deviceId").as<JSON::String>().value();
              descriptor.vendorId = static_cast<uint16_t>(obj.get("vendorId").as<JSON::Number>().value());
              descriptor.productId = static_cast<uint16_t>(obj.get("productId").as<JSON::Number>().value());
              descriptor.classCode = static_cast<uint8_t>(obj.get("classCode").as<JSON::Number>().value());
              descriptor.subclassCode = static_cast<uint8_t>(obj.get("subclassCode").as<JSON::Number>().value());
              descriptor.protocolCode = static_cast<uint8_t>(obj.get("protocolCode").as<JSON::Number>().value());
              if (obj.has("authorized")) {
                const auto& authorized = obj.get("authorized");
                if (authorized.type == JSON::Type::Boolean) {
                  descriptor.authorized = authorized.as<JSON::Boolean>().value();
                } else if (authorized.type == JSON::Type::Number) {
                  descriptor.authorized = authorized.as<JSON::Number>().value() != 0;
                } else if (authorized.type == JSON::Type::String) {
                  const auto str = authorized.as<JSON::String>().value();
                  descriptor.authorized = str == "1" || str == "true" || str == "TRUE";
                } else {
                  descriptor.authorized = false;
                }
              } else {
                descriptor.authorized = false;
              }
              descriptor.authorized = descriptor.authorized && isDeviceAuthorized(descriptor.deviceId);
              descriptor.productName = obj.get("productName").as<JSON::String>().value();
              descriptor.manufacturerName = obj.get("manufacturerName").as<JSON::String>().value();
              descriptor.serialNumber = obj.get("serialNumber").as<JSON::String>().value();
              descriptor.deviceVersion = obj.get("deviceVersion").as<JSON::Number>().value();
              descriptor.configurationNames = obj.get("configurationNames").as<JSON::Object>();
              for (const auto& value : obj.get("configurations").as<JSON::Array>()) {
                descriptor.configurations.push_back(value.as<JSON::Number>().value());
              }
              descriptor.busNumber = 0;
              descriptor.deviceAddress = 0;

              if (obj.has("interfaces") && obj.get("interfaces").type == JSON::Type::Array) {
                const auto& ifaceArray = obj.get("interfaces").as<JSON::Array>();
                for (const auto& ifaceAny : ifaceArray) {
                  if (ifaceAny.type != JSON::Type::Object) {
                    continue;
                  }
                  const auto& ifaceObj = ifaceAny.as<JSON::Object>();
                  InterfaceDescriptor iface;
                  iface.interfaceNumber = static_cast<uint8_t>(ifaceObj.get("interfaceNumber").as<JSON::Number>().value());
                  iface.alternateSetting = static_cast<uint8_t>(ifaceObj.get("alternateSetting").as<JSON::Number>().value());
                  iface.classCode = static_cast<uint8_t>(ifaceObj.get("classCode").as<JSON::Number>().value());
                  iface.subclassCode = static_cast<uint8_t>(ifaceObj.get("subclassCode").as<JSON::Number>().value());
                  iface.protocolCode = static_cast<uint8_t>(ifaceObj.get("protocolCode").as<JSON::Number>().value());
                  iface.configurationValue = ifaceObj.get("configurationValue").as<JSON::Number>().value();
                  iface.interfaceName = ifaceObj.get("interfaceName").as<JSON::String>().value();
                  iface.endpoints = ifaceObj.get("endpoints").as<JSON::Array>();
                  descriptor.interfaces.push_back(iface);
                }
              }

              if (!isBlockedUSBDevice(descriptor.vendorId, descriptor.productId)) output.devices.push_back(descriptor);
            }
          }
        } catch (...) {
          output.ok = false;
          output.error = makeErrorObject("OperationError", "Failed to parse USB enumeration result");
        }

        if (output.ok) {
          Vector<DeviceDescriptor> added, removed;
          Map<String, DeviceDescriptor> current;
          for (const auto& device : output.devices) {
            current.emplace(device.deviceId, device);
            if (!knownDevices.contains(device.deviceId)) added.push_back(device);
          }
          for (const auto& [id, device] : knownDevices) {
            if (!current.contains(id) || (device.authorized && !current.at(id).authorized)) {
              removed.push_back(device);
              sessions.erase(id);
              revokeDevice(id);
            }
          }
          knownDevices = std::move(current);
          emitChanges(std::move(added), std::move(removed));
        }
        completion(output);
      }

      bool authorizeDevice(const String& deviceId) override {
        Lock lock(this->mutex);
        this->authorizedDevices.insert(deviceId);
        return true;
      }

      bool isDeviceAuthorized(const String& deviceId) const override {
        UniqueLock lock(this->mutex);
        return this->authorizedDevices.count(deviceId) > 0;
      }

      void revokeDevice(const String& deviceId) override {
        Lock lock(this->mutex);
        this->authorizedDevices.erase(deviceId);
      }

      void chooseDevice(const USB::Request& seq, const USB::DeviceSelection& selection, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingChooser || this->pendingChooser->client != seq.client) {
            cb(seq, makeErrorObject("InvalidStateError", "No pending usb.requestDevice"), oro::runtime::QueuedResponse{});
            return;
          }
          request = *this->pendingChooser;
          this->pendingChooser.reset();
        }

        auto it = std::find_if(request.pending.begin(), request.pending.end(), [&](const DeviceDescriptor& descriptor) {
          return descriptor.deviceId == selection.deviceId;
        });

        if (it == request.pending.end()) {
          cb(seq, makeErrorObject("NotFoundError", String("Unknown device ") + selection.deviceId), oro::runtime::QueuedResponse{});
          return;
        }

        requestPermissionForDevices(seq, cb, {*it});
      }

      void cancelRequest(const USB::Request& seq, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingChooser || this->pendingChooser->client != seq.client) {
            cb(seq, makeErrorObject("InvalidStateError", "No pending usb.requestDevice"), oro::runtime::QueuedResponse{});
            return;
          }
          request = *this->pendingChooser;
          this->pendingChooser.reset();
        }

        cb(seq, makeOkObject(), oro::runtime::QueuedResponse{});
      }

      void handlePermissionResult(uint64_t requestId, const String& deviceId, bool granted) {
        PendingPermission pending;
        bool found = false;
        {
          Lock lock(this->mutex);
          auto it = this->pendingPermissions.find(requestId);
          if (it != this->pendingPermissions.end()) {
            pending = std::move(it->second);
            pendingPermissions.erase(it);
            found = true;
          }
        }

        if (!found) {
          return;
        }

        enqueue([this, pending = std::move(pending), deviceId, granted]() mutable {
          const auto it = std::find_if(pending.devices.begin(), pending.devices.end(),
            [&](const auto& device) { return device.deviceId == deviceId; });
          if (!granted || it == pending.devices.end()) {
            pending.callback(pending.seq, makeErrorObject("NotFoundError", "No USB device selected"), {});
            return;
          }
          auto app = oro::runtime::app::App::sharedApplication();
          const auto window = app ? app->runtime.windowManager.getWindow(pending.seq.window) : nullptr;
          if (!window || !window->bridge || window->bridge->usbDocument.load() != pending.seq.client ||
              !app->runtime.hasPermission("usb")) {
            pending.callback(pending.seq, makeErrorObject("SecurityError", "USB requester has navigated"), {});
            return;
          }
          authorizeDevice(deviceId);
          enumerateOnWorker([&, this](const auto& result) {
            const auto current = std::find_if(result.devices.begin(), result.devices.end(),
              [&](const auto& device) { return device.deviceId == deviceId && device.authorized; });
            if (!result.ok || current == result.devices.end()) {
              revokeDevice(deviceId);
              pending.callback(pending.seq, makeErrorObject("NotFoundError", "Selected device disconnected"), {});
              return;
            }
            pending.callback(pending.seq, JSON::Object::Entries{{"data", JSON::Object::Entries{{"device", descriptorToJson(*current)}}}}, {});
          });
        });
      }

    private:
      struct RequestState {
        uint64_t client = 0;
        Vector<DeviceDescriptor> pending;
      };

      struct PendingPermission {
        USB::Request seq;
        Callback callback;
        Vector<DeviceDescriptor> devices;
      };

      bool requestPermissionForDevices(const USB::Request& seq, const Callback& cb, const Vector<DeviceDescriptor>& candidates) {
        auto app = oro::runtime::app::App::sharedApplication();
        if (!app) {
          cb(seq, makeErrorObject("InvalidStateError", "Application unavailable"), oro::runtime::QueuedResponse{});
          return false;
        }

        auto& android = app->runtime.android;
        android::JNIEnvironmentAttachment attachment(android.jvm);
        if (!attachment.env) {
          cb(seq, makeErrorObject("InvalidStateError", "JNI environment unavailable"), oro::runtime::QueuedResponse{});
          return false;
        }

        jclass cls = attachment.env->GetObjectClass(android.activity);
        if (!cls) {
          attachment.printException();
          cb(seq, makeErrorObject("OperationError", "USBPlatform class not found"), oro::runtime::QueuedResponse{});
          return false;
        }

        jmethodID requestPermission = attachment.env->GetMethodID(
          cls,
          "requestUSBPermission",
          "(J[Ljava/lang/String;)V"
        );

        if (!requestPermission) {
          attachment.printException();
          attachment.env->DeleteLocalRef(cls);
          cb(seq, makeErrorObject("OperationError", "USBPlatform.requestPermission unavailable"), oro::runtime::QueuedResponse{});
          return false;
        }

        jclass stringClass = attachment.env->FindClass("java/lang/String");
        if (!stringClass) {
          attachment.printException();
          attachment.env->DeleteLocalRef(cls);
          cb(seq, makeErrorObject("OperationError", "java.lang.String class not found"), oro::runtime::QueuedResponse{});
          return false;
        }

        jobjectArray array = attachment.env->NewObjectArray(candidates.size(), stringClass, nullptr);
        if (!array) {
          attachment.env->DeleteLocalRef(stringClass);
          attachment.env->DeleteLocalRef(cls);
          cb(seq, makeErrorObject("OperationError", "Unable to allocate permission array"), oro::runtime::QueuedResponse{});
          return false;
        }

        for (size_t i = 0; i < candidates.size(); ++i) {
          auto id = android::StringWrap(attachment.env, candidates[i].deviceId).j_str();
          attachment.env->SetObjectArrayElement(array, i, id);
          attachment.env->DeleteLocalRef(id);
        }

        uint64_t requestId = nextPermissionRequestId.fetch_add(1, std::memory_order_relaxed);
        {
          Lock lock(this->mutex);
          this->pendingPermissions[requestId] = PendingPermission{
            .seq = seq,
            .callback = cb,
            .devices = candidates
          };
        }

        attachment.env->CallVoidMethod(
          android.activity,
          requestPermission,
          static_cast<jlong>(requestId),
          array
        );

        bool ok = !attachment.hasException();
        if (!ok) {
          attachment.printException();
          attachment.env->ExceptionClear();
          Lock lock(this->mutex);
          this->pendingPermissions.erase(requestId);
        }

        attachment.env->DeleteLocalRef(array);
        attachment.env->DeleteLocalRef(stringClass);
        attachment.env->DeleteLocalRef(cls);

        if (!ok) {
          cb(seq, makeErrorObject("OperationError", "Failed to dispatch USB permission request"), oro::runtime::QueuedResponse{});
          return false;
        }

        return true;
      }

      mutable Mutex mutex;
      Set<String> authorizedDevices;
      Map<String, DeviceDescriptor> knownDevices;
      std::optional<RequestState> pendingChooser;
      std::unordered_map<uint64_t, PendingPermission> pendingPermissions;
  };

  extern "C" void ANDROID_EXTERNAL(usb, USBPlatform, onPermissionResult) (
    JNIEnv* env,
    jclass,
    jlong requestId,
    jstring deviceIdString,
    jboolean granted
  ) {
    Lock lock(activeBackendMutex);
    auto backend = activeBackend.lock();
    if (!backend) {
      return;
    }
    const String deviceId = android::StringWrap(env, deviceIdString).str();
    backend->handlePermissionResult(static_cast<uint64_t>(requestId), deviceId, granted == JNI_TRUE);
  }

  std::shared_ptr<USB::Backend> makeUSBBackend(USB& service) {
    auto backend = std::make_shared<AndroidBackend>(service);
    Lock lock(activeBackendMutex);
    activeBackend = backend;
    return backend;
  }
}

#elif ORO_RUNTIME_PLATFORM_IOS

#include "../../../json.hh"
#include "../../../queued_response.hh"

namespace oro::runtime::core::services::usb {
  namespace {
    JSON::Object makeUnsupportedError() {
      return JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String("NotSupportedError")},
          {"message", String("WebUSB is unavailable on iOS")}
        }}}
      }};
    }

    JSON::Object makeOkObject() {
      return JSON::Object::Entries {{ "data", JSON::Object::Entries {{ "ok", true }} }};
    }
  }

  class IOSBackend final : public USB::Backend {
    public:
      using Callback = USB::Callback;
      using EnumerateResult = USB::Backend::EnumerateResult;

      void getDevices(const USB::Request& seq, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void requestDevice(const USB::Request& seq, const USB::RequestDeviceOptions&, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void forgetDevice(const USB::Request& seq, const String&, const Callback cb) override {
        cb(seq, makeOkObject(), oro::runtime::QueuedResponse{});
      }
      void open(const USB::Request& seq, const String&, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void close(const USB::Request& seq, const String&, const Callback cb) override {
        cb(seq, makeOkObject(), oro::runtime::QueuedResponse{});
      }
      void selectConfiguration(const USB::Request& seq, const String&, uint8_t, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void claimInterface(const USB::Request& seq, const String&, uint8_t, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void releaseInterface(const USB::Request& seq, const String&, uint8_t, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void selectAlternateInterface(const USB::Request& seq, const String&, uint8_t, uint8_t, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void controlTransferIn(const USB::Request& seq, const String&, const JSON::Any&, uint32_t, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void controlTransferOut(const USB::Request& seq, const String&, const JSON::Any&, const bytes::Buffer&, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void transferIn(const USB::Request& seq, const String&, uint8_t, uint32_t, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void transferOut(const USB::Request& seq, const String&, uint8_t, const bytes::Buffer&, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void clearHalt(const USB::Request& seq, const String&, uint8_t, bool, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void reset(const USB::Request& seq, const String&, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void enumerateDevices(Function<void(const EnumerateResult&)> completion) override {
        completion(EnumerateResult{.ok = true});
      }
      bool authorizeDevice(const String&) override {
        return false;
      }
      bool isDeviceAuthorized(const String&) const override {
        return false;
      }
      void revokeDevice(const String&) override {
      }
      void chooseDevice(const USB::Request& seq, const USB::DeviceSelection&, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
      void cancelRequest(const USB::Request& seq, const Callback cb) override {
        cb(seq, makeUnsupportedError(), oro::runtime::QueuedResponse{});
      }
  };

  std::shared_ptr<USB::Backend> makeUSBBackend(USB&) {
    return std::make_shared<IOSBackend>();
  }
}

#else

#include "../../../json.hh"
#include "../../../string.hh"
#include "../../../bytes.hh"
#include "../../../native/unicode.hh"
#include "../../../debug.hh"
#include "../../../app.hh"
#include "../../services.hh"

#if __has_include(<libusb-1.0/libusb.h>)
#  include <libusb-1.0/libusb.h>
#else
#  include <libusb.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

namespace oro::runtime::core::services::usb {
  namespace {
    JSON::Object makeErrorObject(const char* type, const String& message, int code = 0) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String(type)},
          {"name", String(type)},
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

    constexpr const char* endpointTypes[] = {"control", "isochronous", "bulk", "interrupt"};

    String makeAddressDeviceId(uint8_t bus, uint8_t address) {
      return String(std::to_string(bus)) + ":" + std::to_string(address);
    }

    struct DescriptorStrings {
      std::unique_ptr<libusb_device_handle, decltype(&libusb_close)> handle{nullptr, libusb_close};
      uint16_t language = 0;

      explicit DescriptorStrings (libusb_device* device) {
        libusb_device_handle* raw = nullptr;
        if (libusb_open(device, &raw) != LIBUSB_SUCCESS) return;
        handle.reset(raw);
        unsigned char languages[256]{};
        if (libusb_get_string_descriptor(raw, 0, 0, languages, sizeof(languages)) >= 4) {
          language = languages[2] | (languages[3] << 8);
        }
      }

      String read (uint8_t index) const {
        if (!index || !language) return {};
        unsigned char buffer[256]{};
        const int length = libusb_get_string_descriptor(handle.get(), index, language, buffer, sizeof(buffer));
        if (length < 2 || buffer[1] != LIBUSB_DT_STRING || buffer[0] > length || buffer[0] % 2) return {};
        std::u16string text;
        for (unsigned i = 2; i < buffer[0]; i += 2) text.push_back(buffer[i] | (buffer[i + 1] << 8));
        try {
          return native::nativeUtf8(text);
        } catch (const std::exception&) {
          return {};
        }
      }
    };

    String makeStableDeviceId (libusb_device* device, const libusb_device_descriptor& desc, const String& serial) {
      Vector<String> segments;
      segments.reserve(5);
      segments.push_back("usb");

      std::array<uint8_t, 8> ports{};
      const int portCount = libusb_get_port_numbers(device, ports.data(), static_cast<int>(ports.size()));
      if (portCount > 0) {
        Vector<String> portParts;
        portParts.reserve(static_cast<size_t>(portCount));
        for (int idx = 0; idx < portCount; ++idx) {
          portParts.push_back(String(std::to_string(static_cast<unsigned>(ports[idx]))));
        }
        segments.push_back(std::to_string(libusb_get_bus_number(device)));
        segments.push_back(String("port-") + oro::runtime::string::join(portParts, "."));
      } else {
        const uint8_t bus = libusb_get_bus_number(device);
        const uint8_t address = libusb_get_device_address(device);
        segments.push_back(String("address-") + makeAddressDeviceId(bus, address));
      }

      char vendorBuf[5] = {0};
      char productBuf[5] = {0};
      std::snprintf(vendorBuf, sizeof(vendorBuf), "%04x", desc.idVendor);
      std::snprintf(productBuf, sizeof(productBuf), "%04x", desc.idProduct);
      segments.push_back(String("vid") + vendorBuf);
      segments.push_back(String("pid") + productBuf);

      if (!serial.empty()) {
        return String("usb:vid") + vendorBuf + ":pid" + productBuf + ":ser" + bytes::encodeHexString(serial);
      }

      segments.push_back(std::to_string(crypto::rand64()));
      return oro::runtime::string::join(segments, ":");
    }

    JSON::Object makeLibusbError(const String& context, int code) {
      const char* name = libusb_error_name(code);
      const String message = context + ": " + (name ? String(name) : String("UNKNOWN"));
      return makeErrorObject("OperationError", message, code);
    }


  }

  class LibusbBackend final : public TransportBackend {
    public:
      using Callback = USB::Callback;

      explicit LibusbBackend(USB& svc)
        : TransportBackend(svc) {
        const int rc = libusb_init(&this->ctx);
        if (rc != 0) {
          this->ctx = nullptr;
          debug("libusb_init failed: %s", libusb_error_name(rc));
        } else {
          libusb_set_option(this->ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
        }

        this->stateScope = this->computeStateScope();
        this->loadAuthorizedFromState();
      }

      ~LibusbBackend() override {
        Lock lock(this->mutex);
        for (auto& entry : this->devices) {
          if (entry.second.device) {
            libusb_unref_device(entry.second.device);
            entry.second.device = nullptr;
          }
        }
        this->devices.clear();
        this->authorizedDevices.clear();
      }

      void disconnectClient (uint64_t client) override {
        TransportBackend::disconnectClient(client);
        Lock lock(this->mutex);
        if (pendingRequest && pendingRequest->client == client) pendingRequest.reset();
      }

      void getDevices(const USB::Request& seq, const Callback cb) override {
        this->enumerateDevices([this, seq, cb](const EnumerateResult& result) {
          if (!result.ok) {
            cb(seq, result.error, QueuedResponse{});
            return;
          }

          JSON::Array::Entries devicesJson;
          devicesJson.reserve(result.devices.size());
          for (const auto& descriptor : result.devices) {
            if (!descriptor.authorized) {
              continue;
            }
            devicesJson.push_back(describeDevice(descriptor, seq));
          }

          JSON::Object::Entries payload = {
            {"data", JSON::Object::Entries {{"devices", devicesJson}}}
          };
          cb(seq, payload, QueuedResponse{});
        });
      }

      void requestDevice(const USB::Request& seq, const USB::RequestDeviceOptions& options, const Callback cb) override {
        if (!this->service.context.getRuntime()->hasPermission("usb")) {
          cb(seq, makeErrorObject("NotAllowedError", "USB runtime permission disabled"), QueuedResponse{});
          return;
        }

        this->enumerateDevices([this, seq, cb, options](const EnumerateResult& result) mutable {
          if (!result.ok) {
            cb(seq, result.error, QueuedResponse{});
            return;
          }

          if (result.devices.empty()) {
            cb(seq, makeErrorObject("NotFoundError", "No USB devices are available"), QueuedResponse{});
            return;
          }

          Vector<DeviceDescriptor> candidates;
          candidates.reserve(result.devices.size());
          Set<String> seen;

          for (const auto& descriptor : result.devices) {
            bool matches = options.acceptAllDevices || options.filters.empty();
            if (!options.acceptAllDevices) {
              for (const auto& filter : options.filters) {
                if (filterMatches(filter, descriptor)) {
                  matches = true;
                  break;
                }
              }
            }

            if (!matches || std::any_of(options.exclusionFilters.begin(), options.exclusionFilters.end(),
                  [&](const auto& filter) { return filterMatches(filter, descriptor); })) {
              continue;
            }

            if (!seen.insert(descriptor.deviceId).second) {
              continue;
            }

            candidates.push_back(descriptor);
          }

          if (candidates.empty()) {
            cb(seq, makeErrorObject("NotFoundError", "No USB devices matched the requested filters"), QueuedResponse{});
            return;
          }

          if (std::any_of(candidates.begin(), candidates.end(), [](const auto& device) { return !device.authorized; })) {
            presentDevices(seq, cb, candidates);
            return;
          }

          {
            Lock lock(this->mutex);
            if (this->pendingRequest.has_value()) {
              cb(seq, makeErrorObject("InvalidStateError", "Another usb.requestDevice is already pending"), QueuedResponse{});
              return;
            }

            RequestState state;
            state.client = seq.client;
            state.pending = candidates;
            this->pendingRequest = std::move(state);
          }

          JSON::Array::Entries deviceEntries;
          deviceEntries.reserve(candidates.size());
          for (const auto& descriptor : candidates) {
            deviceEntries.push_back(descriptorToJson(descriptor));
          }

          JSON::Object::Entries data = {
            {"devices", deviceEntries},
            {"requiresSelection", true},
            {"context", String("usb.requestDevice")}
          };

          cb(seq, JSON::Object::Entries {{"data", data}}, QueuedResponse{});
        });
      }

      void forgetDevice(const USB::Request& seq, const String& deviceId, const Callback cb) override {
        enqueue([this, seq, cb, deviceId]() {
          if (!this->setAuthorization(deviceId, false)) {
            return cb(seq, makeErrorObject("OperationError", "Could not remove the saved USB device permission"), {});
          }
          {
            Lock lock(this->mutex);
            if (auto it = this->devices.find(deviceId); it != this->devices.end()) {
              sessions.erase(deviceId);
              if (it->second.device) {
                libusb_unref_device(it->second.device);
                it->second.device = nullptr;
              }
              this->devices.erase(it);
            }

            this->knownDescriptors.erase(deviceId);
          }
          cb(seq, makeOkObject(), QueuedResponse{});
        });
      }

      int openDevice (const String& id, Session& session) override {
        Lock lock(this->mutex);
        const auto it = devices.find(id);
        return it == devices.end() ? LIBUSB_ERROR_NO_DEVICE : libusb_open(it->second.device, &session.handle);
      }

      void enumerateDevices(Function<void(const EnumerateResult&)> completion) override {
        enqueue([this, completion]() {
          EnumerateResult result;

          if (!this->ctx) {
            result.ok = false;
            result.error = makeErrorObject("NotSupportedError", "libusb context unavailable");
            completion(result);
            return;
          }

          libusb_device** list = nullptr;
          const ssize_t count = libusb_get_device_list(this->ctx, &list);
          if (count < 0) {
            result.ok = false;
            result.error = makeLibusbError("libusb_get_device_list", static_cast<int>(count));
            completion(result);
            return;
          }

          Vector<DeviceDescriptor> descriptors;
          descriptors.reserve(static_cast<size_t>(count));
          Set<String> seen;
          Vector<DeviceDescriptor> addedDevices;
          Vector<DeviceDescriptor> removedDevices;


          for (ssize_t i = 0; i < count; ++i) {
            auto* dev = list[i];
            if (!dev) continue;

            {
              Lock lock(mutex);
              const auto cached = std::find_if(devices.begin(), devices.end(),
                [dev](const auto& entry) { return entry.second.device == dev; });
              if (cached != devices.end() && knownDescriptors.contains(cached->first)) {
                auto& descriptor = knownDescriptors.at(cached->first);
                descriptor.authorized = authorizedDevices.contains(cached->first);
                seen.insert(cached->first);
                descriptors.push_back(descriptor);
                continue;
              }
            }
            libusb_device_descriptor desc{};
            if (libusb_get_device_descriptor(dev, &desc) != 0) {
              continue;
            }

            if (isBlockedUSBDevice(desc.idVendor, desc.idProduct)) continue;
            const uint8_t bus = libusb_get_bus_number(dev);
            const uint8_t address = libusb_get_device_address(dev);
            const DescriptorStrings strings(dev);
            const String serial = strings.read(desc.iSerialNumber);
            const String deviceId = makeStableDeviceId(dev, desc, serial);
            seen.insert(deviceId);

            DeviceDescriptor descriptor;
            descriptor.deviceId = deviceId;
            descriptor.busNumber = bus;
            descriptor.deviceAddress = address;
            descriptor.vendorId = desc.idVendor;
            descriptor.productId = desc.idProduct;
            descriptor.classCode = desc.bDeviceClass;
            descriptor.subclassCode = desc.bDeviceSubClass;
            descriptor.protocolCode = desc.bDeviceProtocol;
            descriptor.authorized = false;

            {
              Lock lock(this->mutex);
              auto it = this->devices.find(deviceId);
              if (it == this->devices.end()) {
                DeviceState state;
                state.device = dev;
                libusb_ref_device(dev);
                this->devices.emplace(deviceId, state);
              } else {
                if (knownDescriptors.contains(deviceId)) removedDevices.push_back(knownDescriptors.at(deviceId));
                knownDescriptors.erase(deviceId);
                sessions.erase(deviceId);
                if (deviceId.find(":ser") == String::npos) authorizedDevices.erase(deviceId);
                libusb_ref_device(dev);
                if (it->second.device != nullptr) {
                  libusb_unref_device(it->second.device);
                }
                it->second.device = dev;
              }

              if (this->authorizedDevices.count(deviceId) > 0) {
                descriptor.authorized = true;
              }
            }

            descriptor.manufacturerName = strings.read(desc.iManufacturer);
            descriptor.productName = strings.read(desc.iProduct);
            descriptor.serialNumber = serial;
            descriptor.usbVersion = desc.bcdUSB;
            descriptor.deviceVersion = desc.bcdDevice;
            libusb_config_descriptor* active = nullptr;
            if (libusb_get_active_config_descriptor(dev, &active) == LIBUSB_SUCCESS) {
              descriptor.activeConfiguration = active->bConfigurationValue;
              libusb_free_config_descriptor(active);
            }
            for (uint8_t n = 0; n < desc.bNumConfigurations; ++n) {
              libusb_config_descriptor* raw = nullptr;
              if (libusb_get_config_descriptor(dev, n, &raw) != LIBUSB_SUCCESS) continue;
              std::unique_ptr<libusb_config_descriptor, decltype(&libusb_free_config_descriptor)> config(raw, libusb_free_config_descriptor);
              descriptor.configurations.push_back(config->bConfigurationValue);
              descriptor.configurationNames.set(std::to_string(config->bConfigurationValue), strings.read(config->iConfiguration));
              for (int i = 0; i < config->bNumInterfaces; ++i) {
                const auto& iface = config->interface[i];
                for (int j = 0; j < iface.num_altsetting; ++j) {
                  const auto& alt = iface.altsetting[j];
                  InterfaceDescriptor record;
                  record.configurationValue = config->bConfigurationValue;
                  record.interfaceNumber = alt.bInterfaceNumber;
                  record.alternateSetting = alt.bAlternateSetting;
                  record.interfaceName = strings.read(alt.iInterface);
                  record.classCode = alt.bInterfaceClass;
                  record.subclassCode = alt.bInterfaceSubClass;
                  record.protocolCode = alt.bInterfaceProtocol;
                  for (int k = 0; k < alt.bNumEndpoints; ++k) {
                    const auto& ep = alt.endpoint[k];
                    record.endpoints.push(JSON::Object::Entries{
                      {"endpointNumber", ep.bEndpointAddress & 15}, {"direction", ep.bEndpointAddress & 128 ? "in" : "out"},
                      {"type", endpointTypes[ep.bmAttributes & 3]}, {"packetSize", ep.wMaxPacketSize & 2047}
                    });
                  }
                  descriptor.interfaces.push_back(std::move(record));
                }
              }
            }

            descriptors.push_back(descriptor);
            {
              Lock lock(this->mutex);
              if (!this->knownDescriptors.count(descriptor.deviceId)) {
                addedDevices.push_back(descriptor);
              }
              this->knownDescriptors[descriptor.deviceId] = descriptor;
            }
          }

          libusb_free_device_list(list, 1);

          {
            Lock lock(this->mutex);
            for (auto it = this->devices.begin(); it != this->devices.end();) {
              if (!seen.count(it->first)) {
                auto knownIt = this->knownDescriptors.find(it->first);
                if (knownIt != this->knownDescriptors.end()) {
                  removedDevices.push_back(knownIt->second);
                  this->knownDescriptors.erase(knownIt);
                } else {
                  DeviceDescriptor desc;
                  desc.deviceId = it->first;
                  removedDevices.push_back(desc);
                }
                if (it->first.find(":ser") == String::npos) this->authorizedDevices.erase(it->first);
                sessions.erase(it->first);
                if (it->second.device) {
                  libusb_unref_device(it->second.device);
                  it->second.device = nullptr;
                }
                it = this->devices.erase(it);
              } else {
                ++it;
              }
            }
          }

          result.ok = true;
          result.devices = std::move(descriptors);


          emitChanges(std::move(addedDevices), std::move(removedDevices));

          completion(result);
        });
      }

      bool authorizeDevice(const String& deviceId) override {
        return this->setAuthorization(deviceId, true);
      }

      bool isDeviceAuthorized(const String& deviceId) const override {
        UniqueLock lock(this->mutex);
        return this->authorizedDevices.count(deviceId) > 0;
      }

      void revokeDevice(const String& deviceId) override {
        this->setAuthorization(deviceId, false);
      }

      void chooseDevice(const USB::Request& seq, const USB::DeviceSelection& selection, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingRequest || this->pendingRequest->client != seq.client) {
            cb(seq, makeErrorObject("InvalidStateError", "No pending usb.requestDevice"), QueuedResponse{});
            return;
          }
          request = *this->pendingRequest;
          this->pendingRequest.reset();
        }

        auto it = std::find_if(request.pending.begin(), request.pending.end(), [&](const DeviceDescriptor& descriptor) {
          return descriptor.deviceId == selection.deviceId;
        });

        if (it == request.pending.end()) {
          cb(seq, makeErrorObject("NotFoundError", String("Unknown device ") + selection.deviceId), QueuedResponse{});
          return;
        }

        presentDevices(seq, cb, {*it});
      }

      void presentDevices (const Request& seq, const Callback& cb, const Vector<DeviceDescriptor>& candidates) {
        const auto weak = weak_from_this();
        this->service.dispatcher.dispatch([weak, seq, cb, candidates] {
          auto app = oro::runtime::app::App::sharedApplication();
          const auto window = app ? app->runtime.windowManager.getWindow(seq.window) : nullptr;
          const auto eligible = [&] {
            return window && window->bridge && window->bridge->usbDocument.load() == seq.client &&
              app->runtime.hasPermission("usb");
          };
          Vector<String> labels;
          for (const auto& device : candidates) {
            char ids[12];
            std::snprintf(ids, sizeof(ids), "%04x:%04x", device.vendorId, device.productId);
            labels.push_back(device.productName + " (" + ids + ")");
          }
          const int choice = eligible() ? window->dialog.chooseUSB(labels) : -1;
          if (choice < 0 || static_cast<size_t>(choice) >= candidates.size() || !eligible()) {
            cb(seq, makeErrorObject("NotFoundError", "No USB device selected"), {});
            return;
          }
          const auto selected = candidates[choice];
          auto base = weak.lock();
          if (!base) return cb(seq, makeErrorObject("InvalidStateError", "USB service stopped"), {});
          auto backend = std::static_pointer_cast<LibusbBackend>(base);
          backend->enumerateDevices([backend, selected, window, seq, cb](const EnumerateResult& result) {
            if (!window->bridge || window->bridge->usbDocument.load() != seq.client) {
              return cb(seq, makeErrorObject("AbortError", "USB document changed"), {});
            }
            const auto found = std::find_if(result.devices.begin(), result.devices.end(),
              [&](const auto& current) { return current.deviceId == selected.deviceId; });
            if (!result.ok || found == result.devices.end()) return cb(seq, makeErrorObject("NotFoundError", "Selected device disconnected"), {});
            if (!backend->authorizeDevice(selected.deviceId)) {
              return cb(seq, makeErrorObject("OperationError", "Could not save the USB device permission"), {});
            }
            auto device = *found;
            device.authorized = true;
            cb(seq, JSON::Object::Entries{{"data", JSON::Object::Entries{{"device", descriptorToJson(device)}}}}, {});
          });
        });
      }

      void cancelRequest(const USB::Request& seq, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingRequest || this->pendingRequest->client != seq.client) {
            cb(seq, makeErrorObject("InvalidStateError", "No pending usb.requestDevice"), QueuedResponse{});
            return;
          }
          request = *this->pendingRequest;
          this->pendingRequest.reset();
        }

        cb(seq, makeOkObject(), QueuedResponse{});
      }

    private:
      struct DeviceState {
        libusb_device* device = nullptr;
      };

      struct RequestState {
        uint64_t client = 0;
        Vector<DeviceDescriptor> pending;
      };

      mutable Mutex mutex;
      Map<String, DeviceState> devices;
      Set<String> authorizedDevices;
      Map<String, DeviceDescriptor> knownDescriptors;
      std::optional<RequestState> pendingRequest;
      String stateScope;
      String computeStateScope() const {
        String bundle = "app";
        if (auto* runtime = this->service.context.getRuntime()) {
          const auto it = runtime->userConfig.find("meta_bundle_identifier");
          if (it != runtime->userConfig.end() && !it->second.empty()) {
            bundle = it->second;
          }
        }
        return String("webusb-grants://") + bundle;
      }

      bool setAuthorization (const String& deviceId, bool authorized) {
        Lock lock(this->mutex);
        if (this->authorizedDevices.contains(deviceId) == authorized) return true;
        if (deviceId.find(":ser") != String::npos) {
          auto& state = this->service.services.state;
          if (!state.isReady()) return false;
          Vector<String> ids;
          for (const auto& id : this->authorizedDevices) {
            if (id != deviceId && id.find(":ser") != String::npos) ids.push_back(id);
          }
          if (authorized) ids.push_back(deviceId);
          std::sort(ids.begin(), ids.end());
          if (ids.empty()) {
            if (!state.remove(this->stateScope, "authorizedDevices")) return false;
          } else if (!state.putString(this->stateScope, "authorizedDevices", oro::runtime::string::join(ids, "\n"))) {
            return false;
          }
        }
        if (authorized) {
          this->authorizedDevices.insert(deviceId);
        } else {
          this->authorizedDevices.erase(deviceId);
        }
        if (auto it = this->knownDescriptors.find(deviceId); it != this->knownDescriptors.end()) {
          it->second.authorized = authorized;
        }
        return true;
      }

      void loadAuthorizedFromState() {
        if (!this->service.services.state.isReady()) {
          return;
        }
        auto stored = this->service.services.state.getString(this->stateScope, "authorizedDevices");
        if (!stored) {
          return;
        }
        auto entries = oro::runtime::string::split(*stored, "\n");
        Lock lock(this->mutex);
        for (const auto& entry : entries) {
          if (!entry.empty() && entry.find(":ser") != String::npos) {
            this->authorizedDevices.insert(entry);
          }
        }
      }
    };

  std::shared_ptr<USB::Backend> makeUSBBackend(USB& service) {
    return std::make_shared<LibusbBackend>(service);
  }
}

#endif // ORO_RUNTIME_PLATFORM_ANDROID
