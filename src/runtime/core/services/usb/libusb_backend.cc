#include "libusb_backend.hh"

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
  static AndroidBackend* g_backend = nullptr;

  namespace {
    JSON::Object makeErrorObject(const char* type, const String& message) {
      return JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", String(type)},
          {"message", message}
        }}}
      }};
    }

    JSON::Object makeOkObject() {
      return JSON::Object::Entries {{
        {"data", JSON::Object::Entries {{"ok", true}}}
      }};
    }

    JSON::Object descriptorToJson(const USB::Backend::DeviceDescriptor& descriptor) {
      JSON::Object::Entries json = {
        {"deviceId", descriptor.deviceId},
        {"vendorId", static_cast<int64_t>(descriptor.vendorId)},
        {"productId", static_cast<int64_t>(descriptor.productId)},
        {"classCode", static_cast<int64_t>(descriptor.classCode)},
        {"subclassCode", static_cast<int64_t>(descriptor.subclassCode)},
        {"protocolCode", static_cast<int64_t>(descriptor.protocolCode)},
        {"authorized", descriptor.authorized}
      };

      if (!descriptor.interfaces.empty()) {
        JSON::Array::Entries interfaces;
        interfaces.reserve(descriptor.interfaces.size());
        for (const auto& iface : descriptor.interfaces) {
          interfaces.push_back(JSON::Object::Entries {
            {"interfaceNumber", static_cast<int64_t>(iface.interfaceNumber)},
            {"alternateSetting", static_cast<int64_t>(iface.alternateSetting)},
            {"classCode", static_cast<int64_t>(iface.classCode)},
            {"subclassCode", static_cast<int64_t>(iface.subclassCode)},
            {"protocolCode", static_cast<int64_t>(iface.protocolCode)}
          });
        }
        json["interfaces"] = interfaces;
      }

      return json;
    }
  }

  class AndroidBackend final : public USB::Backend {
    public:
      using Callback = USB::Callback;
      using EnumerateResult = USB::Backend::EnumerateResult;
      using DeviceDescriptor = USB::Backend::DeviceDescriptor;
      using InterfaceDescriptor = USB::Backend::InterfaceDescriptor;

      explicit AndroidBackend(USB& svc)
        : service(svc) {
        g_backend = this;
      }

      ~AndroidBackend() override {
        if (g_backend == this) {
          g_backend = nullptr;
        }
      }

      void getDevices(const String& seq, const Callback cb) override {
        this->enumerateDevices([cb, seq](const EnumerateResult& result) {
          if (!result.ok) {
            cb(seq, result.error, oro::runtime::QueuedResponse{});
            return;
          }

          JSON::Array::Entries devices;
          devices.reserve(result.devices.size());
          for (const auto& descriptor : result.devices) {
            devices.push_back(descriptorToJson(descriptor));
          }

          cb(seq, JSON::Object::Entries {{
            {"data", JSON::Object::Entries {{ "devices", devices }}}
          }}, oro::runtime::QueuedResponse{});
        });
      }

      void requestDevice(const String& seq, const USB::RequestDeviceOptions& options, const Callback cb) override {
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
            bool matches = options.acceptAllDevices;
            if (!options.acceptAllDevices) {
              for (const auto& filter : options.filters) {
                if (filterMatches(filter, descriptor)) {
                  matches = true;
                  break;
                }
              }
            }

            if (!matches) {
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

          if (candidates.size() == 1) {
            auto selected = candidates.front();
            if (selected.authorized) {
              this->authorizeDevice(selected.deviceId);
              JSON::Object::Entries data = {{"device", descriptorToJson(selected)}};
              cb(seq, JSON::Object::Entries {{"data", data}}, oro::runtime::QueuedResponse{});
              return;
            }

            if (this->requestPermissionForDevice(seq, cb, selected)) {
              return;
            }

            cb(seq, makeErrorObject("NotAllowedError", "Unable to request USB permission"), oro::runtime::QueuedResponse{});
            return;
          }

          {
            Lock lock(this->mutex);
            if (this->pendingChooser.has_value()) {
              cb(seq, makeErrorObject("InvalidStateError", "Another usb.requestDevice is already pending"), oro::runtime::QueuedResponse{});
              return;
            }

            RequestState state;
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

      void forgetDevice(const String& seq, const String& deviceId, const Callback cb) override {
        {
          Lock lock(this->mutex);
          this->authorizedDevices.erase(deviceId);
        }
        cb(seq, makeOkObject(), oro::runtime::QueuedResponse{});
      }

      void open(const String& seq, const String&, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB transfer APIs are not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void close(const String& seq, const String&, const Callback cb) override {
        cb(seq, makeOkObject(), oro::runtime::QueuedResponse{});
      }

      void selectConfiguration(const String& seq, const String&, uint8_t, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB configuration selection is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void claimInterface(const String& seq, const String&, uint8_t, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB interface claims are not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void releaseInterface(const String& seq, const String&, uint8_t, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB interface release is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void selectAlternateInterface(const String& seq, const String&, uint8_t, uint8_t, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB alternate interface selection is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void controlTransferIn(const String& seq, const String&, const JSON::Any&, uint32_t, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB controlTransferIn is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void controlTransferOut(const String& seq, const String&, const JSON::Any&, const bytes::Buffer&, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB controlTransferOut is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void transferIn(const String& seq, const String&, uint8_t, uint32_t, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB transferIn is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void transferOut(const String& seq, const String&, uint8_t, const bytes::Buffer&, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB transferOut is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void clearHalt(const String& seq, const String&, uint8_t, bool, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB clearHalt is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void reset(const String& seq, const String&, const Callback cb) override {
        cb(seq, makeErrorObject("NotSupportedError", "USB reset is not yet available on Android"), oro::runtime::QueuedResponse{});
      }

      void enumerateDevices(Function<void(const EnumerateResult&)> completion) override {
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

        jclass cls = attachment.env->FindClass("oro/runtime/usb/USBPlatform");
        if (!cls) {
          attachment.printException();
          completion(EnumerateResult{
            .ok = false,
            .error = makeErrorObject("OperationError", "USBPlatform class not found")
          });
          return;
        }

        jmethodID enumerate = attachment.env->GetStaticMethodID(
          cls,
          "enumerate",
          "(Landroid/content/Context;)Ljava/lang/String;"
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

        const auto resultString = (jstring) attachment.env->CallStaticObjectMethod(
          cls,
          enumerate,
          android.activity
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
              descriptor.deviceId = obj.get("deviceId").str();
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
                  const auto str = authorized.str();
                  descriptor.authorized = str == "1" || str == "true" || str == "TRUE";
                } else {
                  descriptor.authorized = false;
                }
              } else {
                descriptor.authorized = false;
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
                  descriptor.interfaces.push_back(iface);
                }
              }

              output.devices.push_back(descriptor);
            }
          }
        } catch (...) {
          output.ok = false;
          output.error = makeErrorObject("OperationError", "Failed to parse USB enumeration result");
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

      void chooseDevice(const String& seq, const USB::DeviceSelection& selection, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingChooser.has_value()) {
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

        auto selected = *it;
        if (selected.authorized) {
          this->authorizeDevice(selected.deviceId);
          JSON::Object::Entries data = {{"device", descriptorToJson(selected)}};
          cb(seq, JSON::Object::Entries {{"data", data}}, oro::runtime::QueuedResponse{});
          return;
        }

        if (!this->requestPermissionForDevice(seq, cb, selected)) {
          cb(seq, makeErrorObject("NotAllowedError", "Unable to request USB permission"), oro::runtime::QueuedResponse{});
        }
      }

      void cancelRequest(const String& seq, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingChooser.has_value()) {
            cb(seq, makeErrorObject("InvalidStateError", "No pending usb.requestDevice"), oro::runtime::QueuedResponse{});
            return;
          }
          request = *this->pendingChooser;
          this->pendingChooser.reset();
        }

        cb(seq, makeErrorObject("AbortError", "USB device request cancelled"), oro::runtime::QueuedResponse{});
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

        this->service.queue.push([this, pending = std::move(pending), granted]() mutable {
          if (granted) {
            this->authorizeDevice(pending.device.deviceId);
            pending.device.authorized = true;
            JSON::Object::Entries data = {{"device", descriptorToJson(pending.device)}};
            pending.callback(pending.seq, JSON::Object::Entries {{"data", data}}, oro::runtime::QueuedResponse{});
          } else {
            pending.callback(pending.seq, makeErrorObject("NotAllowedError", "User denied USB permission"), oro::runtime::QueuedResponse{});
          }
        });
      }

    private:
      struct RequestState {
        Vector<DeviceDescriptor> pending;
      };

      struct PendingPermission {
        String seq;
        Callback callback;
        DeviceDescriptor device;
      };

      bool requestPermissionForDevice(const String& seq, const Callback& cb, const DeviceDescriptor& descriptor) {
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

        jclass cls = attachment.env->FindClass("oro/runtime/usb/USBPlatform");
        if (!cls) {
          attachment.printException();
          cb(seq, makeErrorObject("OperationError", "USBPlatform class not found"), oro::runtime::QueuedResponse{});
          return false;
        }

        jmethodID requestPermission = attachment.env->GetStaticMethodID(
          cls,
          "requestPermission",
          "(Landroid/content/Context;J[Ljava/lang/String;)V"
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

        jobjectArray array = attachment.env->NewObjectArray(1, stringClass, nullptr);
        if (!array) {
          attachment.env->DeleteLocalRef(stringClass);
          attachment.env->DeleteLocalRef(cls);
          cb(seq, makeErrorObject("OperationError", "Unable to allocate permission array"), oro::runtime::QueuedResponse{});
          return false;
        }

        jstring deviceIdString = attachment.env->NewStringUTF(descriptor.deviceId.c_str());
        if (!deviceIdString) {
          attachment.env->DeleteLocalRef(array);
          attachment.env->DeleteLocalRef(stringClass);
          attachment.env->DeleteLocalRef(cls);
          cb(seq, makeErrorObject("OperationError", "Unable to encode device identifier"), oro::runtime::QueuedResponse{});
          return false;
        }

        attachment.env->SetObjectArrayElement(array, 0, deviceIdString);
        attachment.env->DeleteLocalRef(deviceIdString);

        uint64_t requestId = this->nextPermissionRequestId.fetch_add(1, std::memory_order_relaxed);
        {
          Lock lock(this->mutex);
          this->pendingPermissions[requestId] = PendingPermission{
            .seq = seq,
            .callback = cb,
            .device = descriptor
          };
        }

        attachment.env->CallStaticVoidMethod(
          cls,
          requestPermission,
          android.activity,
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

      USB& service;
      mutable Mutex mutex;
      Set<String> authorizedDevices;
      std::optional<RequestState> pendingChooser;
      std::unordered_map<uint64_t, PendingPermission> pendingPermissions;
      std::atomic<uint64_t> nextPermissionRequestId {1};
  };

  extern "C" void ANDROID_EXTERNAL(usb, USBPlatform, onPermissionResult) (
    JNIEnv* env,
    jclass,
    jlong requestId,
    jstring deviceIdString,
    jboolean granted
  ) {
    if (!g_backend) {
      return;
    }
    const String deviceId = android::StringWrap(env, deviceIdString).str();
    g_backend->handlePermissionResult(static_cast<uint64_t>(requestId), deviceId, granted == JNI_TRUE);
  }

  std::unique_ptr<USB::Backend> makeUSBBackend(USB& service) {
    return std::make_unique<AndroidBackend>(service);
  }
}

#else

#include "../../../json.hh"
#include "../../../string.hh"
#include "../../../bytes.hh"
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

    String makeLegacyDeviceId(uint8_t bus, uint8_t address) {
      return String(std::to_string(bus)) + ":" + std::to_string(address);
    }

    String sanitizeDescriptorString(const unsigned char* data, int length) {
      if (length <= 0) {
        return {};
      }

      static const char hexDigits[] = "0123456789abcdef";
      String result;
      result.reserve(static_cast<size_t>(length));

      bool allPrintable = true;
      for (int idx = 0; idx < length; ++idx) {
        const unsigned char ch = data[idx];
        if (ch < 0x20 || ch > 0x7Eu || ch == ':') {
          allPrintable = false;
          break;
        }
      }

      if (allPrintable) {
        for (int idx = 0; idx < length; ++idx) {
          const unsigned char ch = data[idx];
          if (ch == ':') {
            result.push_back('-');
          } else {
            result.push_back(static_cast<char>(ch));
          }
        }
        return result;
      }

      result.reserve(static_cast<size_t>(length) * 2);
      for (int idx = 0; idx < length; ++idx) {
        const unsigned char ch = data[idx];
        result.push_back(hexDigits[(ch >> 4) & 0x0F]);
        result.push_back(hexDigits[ch & 0x0F]);
      }
      return result;
    }

    String readStringDescriptor(libusb_device* device, uint8_t index) {
      if (!device || index == 0) {
        return {};
      }

      libusb_device_handle* handle = nullptr;
      const int openResult = libusb_open(device, &handle);
      if (openResult != LIBUSB_SUCCESS) {
        return {};
      }

      unsigned char buffer[255];
      const int length = libusb_get_string_descriptor_ascii(handle, index, buffer, sizeof(buffer));
      libusb_close(handle);
      if (length <= 0) {
        return {};
      }
      return sanitizeDescriptorString(buffer, length);
    }

    String makeStableDeviceId(libusb_device* device, const libusb_device_descriptor& desc) {
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
        segments.push_back(String("port-") + oro::runtime::string::join(portParts, "."));
      } else {
        const uint8_t bus = libusb_get_bus_number(device);
        const uint8_t address = libusb_get_device_address(device);
        segments.push_back(String("legacy-") + makeLegacyDeviceId(bus, address));
      }

      char vendorBuf[5] = {0};
      char productBuf[5] = {0};
      std::snprintf(vendorBuf, sizeof(vendorBuf), "%04x", desc.idVendor);
      std::snprintf(productBuf, sizeof(productBuf), "%04x", desc.idProduct);
      segments.push_back(String("vid") + vendorBuf);
      segments.push_back(String("pid") + productBuf);

      if (const auto serial = readStringDescriptor(device, desc.iSerialNumber); !serial.empty()) {
        segments.push_back(String("ser") + serial);
      }

      return oro::runtime::string::join(segments, ":");
    }

    JSON::Object makeLibusbError(const String& context, int code) {
      const char* name = libusb_error_name(code);
      const String message = context + ": " + (name ? String(name) : String("UNKNOWN"));
      return makeErrorObject("OperationError", message, code);
    }

    bool parseUint(const JSON::Any& any, uint32_t& out) {
      try {
        switch (any.type) {
          case JSON::Type::Number:
            out = static_cast<uint32_t>(any.as<JSON::Number>().value());
            return true;
          case JSON::Type::String:
            out = static_cast<uint32_t>(std::stoul(any.str(), nullptr, 0));
            return true;
          case JSON::Type::Boolean:
            out = any.as<JSON::Boolean>().value() ? 1u : 0u;
            return true;
          default:
            break;
        }
      } catch (...) {
      }
      return false;
    }

    bool parseUint8(const JSON::Any& any, uint8_t& out) {
      uint32_t tmp = 0;
      if (!parseUint(any, tmp)) {
        return false;
      }
      if (tmp > 0xFFu) {
        return false;
      }
      out = static_cast<uint8_t>(tmp & 0xFFu);
      return true;
    }
  }

  class LibusbBackend final : public USB::Backend {
    public:
      using Callback = USB::Callback;

      explicit LibusbBackend(USB& svc)
        : service(svc) {
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
        UniqueLock lock(this->mutex);
        for (auto& entry : this->devices) {
          this->closeUnlocked(entry.second);
          if (entry.second.device) {
            libusb_unref_device(entry.second.device);
            entry.second.device = nullptr;
          }
        }
        this->devices.clear();
        this->authorizedDevices.clear();
        lock.unlock();

        if (this->ctx != nullptr) {
          libusb_exit(this->ctx);
          this->ctx = nullptr;
        }
      }

      void getDevices(const String& seq, const Callback cb) override {
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
            devicesJson.push_back(descriptorToJson(descriptor));
          }

          JSON::Object::Entries payload = {
            {"data", JSON::Object::Entries {{"devices", devicesJson}}}
          };
          cb(seq, payload, QueuedResponse{});
        });
      }

      void requestDevice(const String& seq, const USB::RequestDeviceOptions& options, const Callback cb) override {
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
            bool matches = options.acceptAllDevices;
            if (!options.acceptAllDevices) {
              for (const auto& filter : options.filters) {
                if (filterMatches(filter, descriptor)) {
                  matches = true;
                  break;
                }
              }
            }

            if (!matches) {
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

          if (candidates.size() == 1) {
            auto selected = candidates.front();
            this->authorizeDevice(selected.deviceId);
            selected.authorized = true;

            JSON::Object::Entries data = {{"device", descriptorToJson(selected)}};
            cb(seq, JSON::Object::Entries {{"data", data}}, QueuedResponse{});
            return;
          }

          {
            Lock lock(this->mutex);
            if (this->pendingRequest.has_value()) {
              cb(seq, makeErrorObject("InvalidStateError", "Another usb.requestDevice is already pending"), QueuedResponse{});
              return;
            }

            RequestState state;
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

      void forgetDevice(const String& seq, const String& deviceId, const Callback cb) override {
        this->service.queue.push([this, seq, cb, deviceId]() {
          bool authorizationChanged = false;
          {
            Lock lock(this->mutex);
            Set<String> identifiers;
            identifiers.insert(deviceId);

            constexpr const char* legacyPrefix = "usb:legacy-";
            constexpr size_t legacyPrefixLength = sizeof("usb:legacy-") - 1;
            String rawLegacyId;
            if (deviceId.rfind(legacyPrefix, 0) == 0 && deviceId.size() > legacyPrefixLength) {
              rawLegacyId = deviceId.substr(legacyPrefixLength);
              identifiers.insert(rawLegacyId);
            }

            auto addKnownLegacyIds = [&](const String& key) {
              const auto known = this->knownDescriptors.find(key);
              if (known != this->knownDescriptors.end()) {
                const String legacy = makeLegacyDeviceId(known->second.busNumber, known->second.deviceAddress);
                if (!legacy.empty()) {
                  identifiers.insert(legacy);
                  identifiers.insert(String(legacyPrefix) + legacy);
                }
              }
            };

            addKnownLegacyIds(deviceId);
            if (!rawLegacyId.empty()) {
              addKnownLegacyIds(rawLegacyId);
            }

            for (const auto& id : identifiers) {
              if (auto it = this->devices.find(id); it != this->devices.end()) {
                this->closeUnlocked(it->second);
                if (it->second.device) {
                  libusb_unref_device(it->second.device);
                  it->second.device = nullptr;
                }
                this->devices.erase(it);
              }
            }

            for (const auto& id : identifiers) {
              authorizationChanged = this->authorizedDevices.erase(id) > 0 || authorizationChanged;
              this->knownDescriptors.erase(id);
            }
          }
          if (authorizationChanged) {
            this->persistAuthorizedToState();
          }
          cb(seq, makeOkObject(), QueuedResponse{});
        });
      }

      void open(const String& seq, const String& deviceId, const Callback cb) override {
        if (!this->service.context.getRuntime()->hasPermission("usb")) {
          cb(seq, makeErrorObject("NotAllowedError", "USB runtime permission disabled"), QueuedResponse{});
          return;
        }

        this->service.queue.push([this, seq, cb, deviceId]() {
          if (!this->ctx) {
            cb(seq, makeErrorObject("NotSupportedError", "libusb context unavailable"), QueuedResponse{});
            return;
          }

          libusb_device* device = nullptr;
          {
            Lock lock(this->mutex);
            auto it = this->devices.find(deviceId);
            if (it == this->devices.end()) {
              cb(seq, makeErrorObject("NotFoundError", String("Unknown device ") + deviceId), QueuedResponse{});
              return;
            }
            if (!this->authorizedDevices.count(deviceId)) {
              cb(seq, makeErrorObject("SecurityError", String("Device ") + deviceId + " not authorized"), QueuedResponse{});
              return;
            }
            if (it->second.handle != nullptr) {
              cb(seq, makeOkObject(), QueuedResponse{});
              return;
            }
            device = it->second.device;
            if (device == nullptr) {
              cb(seq, makeErrorObject("InvalidStateError", "Device pointer missing"), QueuedResponse{});
              return;
            }
            libusb_ref_device(device);
          }

          libusb_device_handle* handle = nullptr;
          const int rc = libusb_open(device, &handle);
          libusb_unref_device(device);
          if (rc != 0) {
            cb(seq, makeLibusbError("libusb_open", rc), QueuedResponse{});
            return;
          }

          {
            Lock lock(this->mutex);
            auto it = this->devices.find(deviceId);
            if (it == this->devices.end()) {
              libusb_close(handle);
              cb(seq, makeErrorObject("NotFoundError", String("Unknown device ") + deviceId), QueuedResponse{});
              return;
            }
            if (it->second.handle) {
              libusb_close(handle);
            } else {
              it->second.handle = handle;
            }
          }

          cb(seq, makeOkObject(), QueuedResponse{});
        });
      }

      void close(const String& seq, const String& deviceId, const Callback cb) override {
        this->service.queue.push([this, seq, cb, deviceId]() {
          Lock lock(this->mutex);
          auto it = this->devices.find(deviceId);
          if (it == this->devices.end()) {
            cb(seq, makeErrorObject("NotFoundError", String("Unknown device ") + deviceId), QueuedResponse{});
            return;
          }
          this->closeUnlocked(it->second);
          cb(seq, makeOkObject(), QueuedResponse{});
        });
      }

      void selectConfiguration(const String& seq, const String& deviceId, uint8_t configurationValue, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          const int rc = libusb_set_configuration(handle, configurationValue);
          if (rc != 0) {
            cb(seq, makeLibusbError("libusb_set_configuration", rc), QueuedResponse{});
          } else {
            cb(seq, makeOkObject(), QueuedResponse{});
          }
        });
      }

      void claimInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          int rc = LIBUSB_SUCCESS;
        #if defined(__linux__)
          if (libusb_kernel_driver_active(handle, interfaceNumber) == 1) {
            rc = libusb_detach_kernel_driver(handle, interfaceNumber);
            if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_NOT_FOUND) {
              cb(seq, makeLibusbError("libusb_detach_kernel_driver", rc), QueuedResponse{});
              return;
            }
          }
        #endif
          rc = libusb_claim_interface(handle, interfaceNumber);
          if (rc != 0) {
            cb(seq, makeLibusbError("libusb_claim_interface", rc), QueuedResponse{});
          } else {
            cb(seq, makeOkObject(), QueuedResponse{});
          }
        });
      }

      void releaseInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          const int rc = libusb_release_interface(handle, interfaceNumber);
          if (rc != 0) {
            cb(seq, makeLibusbError("libusb_release_interface", rc), QueuedResponse{});
          } else {
        #if defined(__linux__)
            libusb_attach_kernel_driver(handle, interfaceNumber);
        #endif
            cb(seq, makeOkObject(), QueuedResponse{});
          }
        });
      }

      void selectAlternateInterface(const String& seq, const String& deviceId, uint8_t interfaceNumber, uint8_t alternateSetting, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          const int rc = libusb_set_interface_alt_setting(handle, interfaceNumber, alternateSetting);
          if (rc != 0) {
            cb(seq, makeLibusbError("libusb_set_interface_alt_setting", rc), QueuedResponse{});
          } else {
            cb(seq, makeOkObject(), QueuedResponse{});
          }
        });
      }

      void controlTransferIn(const String& seq, const String& deviceId, const JSON::Any& setupAny, uint32_t length, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          if (length == 0) {
            cb(seq, makeErrorObject("TypeError", "length must be greater than 0"), QueuedResponse{});
            return;
          }

          if (length > 0xFFFFu) {
            cb(seq, makeErrorObject("TypeError", "length must not exceed 65535 bytes"), QueuedResponse{});
            return;
          }

          if (setupAny.type != JSON::Type::Object) {
            cb(seq, makeErrorObject("TypeError", "setup must be an object"), QueuedResponse{});
            return;
          }

          const auto& setup = setupAny.as<JSON::Object>();
          uint32_t bmRequestType = 0;
          uint32_t bRequest = 0;
          uint32_t wValue = 0;
          uint32_t wIndex = 0;
          uint32_t timeout = 1000;

          if (!setup.has("bmRequestType") || !parseUint(setup.get("bmRequestType"), bmRequestType)) {
            cb(seq, makeErrorObject("TypeError", "setup.bmRequestType must be a number"), QueuedResponse{});
            return;
          }
          if (!setup.has("bRequest") || !parseUint(setup.get("bRequest"), bRequest)) {
            cb(seq, makeErrorObject("TypeError", "setup.bRequest must be a number"), QueuedResponse{});
            return;
          }
          if (setup.has("wValue") && !parseUint(setup.get("wValue"), wValue)) {
            cb(seq, makeErrorObject("TypeError", "setup.wValue must be a number"), QueuedResponse{});
            return;
          }
          if (setup.has("wIndex") && !parseUint(setup.get("wIndex"), wIndex)) {
            cb(seq, makeErrorObject("TypeError", "setup.wIndex must be a number"), QueuedResponse{});
            return;
          }
          if (setup.has("timeout") && !parseUint(setup.get("timeout"), timeout)) {
            cb(seq, makeErrorObject("TypeError", "setup.timeout must be a number"), QueuedResponse{});
            return;
          }

          Vector<uint8_t> buffer(static_cast<size_t>(length));
          const int result = libusb_control_transfer(
            handle,
            static_cast<uint8_t>(bmRequestType & 0xFFu),
            static_cast<uint8_t>(bRequest & 0xFFu),
            static_cast<uint16_t>(wValue & 0xFFFFu),
            static_cast<uint16_t>(wIndex & 0xFFFFu),
            buffer.data(),
            static_cast<uint16_t>(length),
            timeout
          );

          if (result < 0) {
            cb(seq, makeLibusbError("libusb_control_transfer", result), QueuedResponse{});
            return;
          }

          buffer.resize(static_cast<size_t>(result));
          JSON::Object::Entries data = {
            {"status", String("ok")},
            {"transferred", static_cast<uint32_t>(result)},
            {"data", bytes::base64::encode(buffer)}
          };
          cb(seq, JSON::Object::Entries {{"data", data}}, QueuedResponse{});
        });
      }

      void controlTransferOut(const String& seq, const String& deviceId, const JSON::Any& setupAny, const bytes::Buffer& payload, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [seq, cb, payload, setupAny](libusb_device_handle* handle) {
          if (setupAny.type != JSON::Type::Object) {
            cb(seq, makeErrorObject("TypeError", "setup must be an object"), QueuedResponse{});
            return;
          }

          const auto& setup = setupAny.as<JSON::Object>();
          uint32_t bmRequestType = 0;
          uint32_t bRequest = 0;
          uint32_t wValue = 0;
          uint32_t wIndex = 0;
          uint32_t timeout = 1000;

          if (!setup.has("bmRequestType") || !parseUint(setup.get("bmRequestType"), bmRequestType)) {
            cb(seq, makeErrorObject("TypeError", "setup.bmRequestType must be a number"), QueuedResponse{});
            return;
          }
          if (!setup.has("bRequest") || !parseUint(setup.get("bRequest"), bRequest)) {
            cb(seq, makeErrorObject("TypeError", "setup.bRequest must be a number"), QueuedResponse{});
            return;
          }
          if (setup.has("wValue") && !parseUint(setup.get("wValue"), wValue)) {
            cb(seq, makeErrorObject("TypeError", "setup.wValue must be a number"), QueuedResponse{});
            return;
          }
          if (setup.has("wIndex") && !parseUint(setup.get("wIndex"), wIndex)) {
            cb(seq, makeErrorObject("TypeError", "setup.wIndex must be a number"), QueuedResponse{});
            return;
          }
          if (setup.has("timeout") && !parseUint(setup.get("timeout"), timeout)) {
            cb(seq, makeErrorObject("TypeError", "setup.timeout must be a number"), QueuedResponse{});
            return;
          }

          if (payload.size() > 0xFFFFu) {
            cb(seq, makeErrorObject("TypeError", "data length must not exceed 65535 bytes"), QueuedResponse{});
            return;
          }

          const int result = libusb_control_transfer(
            handle,
            static_cast<uint8_t>(bmRequestType & 0xFFu),
            static_cast<uint8_t>(bRequest & 0xFFu),
            static_cast<uint16_t>(wValue & 0xFFFFu),
            static_cast<uint16_t>(wIndex & 0xFFFFu),
            const_cast<unsigned char*>(payload.data()),
            static_cast<uint16_t>(payload.size()),
            timeout
          );

          if (result < 0) {
            cb(seq, makeLibusbError("libusb_control_transfer", result), QueuedResponse{});
            return;
          }

          JSON::Object::Entries data = {
            {"status", String("ok")},
            {"transferred", static_cast<uint32_t>(result)}
          };
          cb(seq, JSON::Object::Entries {{"data", data}}, QueuedResponse{});
        });
      }

      void transferIn(const String& seq, const String& deviceId, uint8_t endpointNumber, uint32_t length, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          if (length == 0) {
            cb(seq, makeErrorObject("TypeError", "length must be greater than 0"), QueuedResponse{});
            return;
          }

          const auto maxLength = static_cast<uint32_t>(std::numeric_limits<int>::max());
          if (length > maxLength) {
            cb(seq, makeErrorObject(
              "TypeError",
              String("length must not exceed ") + std::to_string(static_cast<unsigned long long>(maxLength)) + " bytes"
            ), QueuedResponse{});
            return;
          }

          Vector<uint8_t> buffer(static_cast<size_t>(length));
          const int result = this->bulkOrInterrupt(handle, static_cast<uint8_t>(endpointNumber | LIBUSB_ENDPOINT_IN), buffer.data(), static_cast<int>(length));
          if (result < 0) {
            cb(seq, makeLibusbError("libusb_transfer_in", result), QueuedResponse{});
            return;
          }

          buffer.resize(static_cast<size_t>(result));
          JSON::Object::Entries data = {
            {"status", String("ok")},
            {"transferred", static_cast<uint32_t>(result)},
            {"data", bytes::base64::encode(buffer)}
          };
          cb(seq, JSON::Object::Entries {{"data", data}}, QueuedResponse{});
        });
      }

      void transferOut(const String& seq, const String& deviceId, uint8_t endpointNumber, const bytes::Buffer& payload, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [this, seq, cb, payload, endpointNumber](libusb_device_handle* handle) {
          const auto maxLength = static_cast<size_t>(std::numeric_limits<int>::max());
          if (payload.size() > maxLength) {
            cb(seq, makeErrorObject(
              "TypeError",
              String("data length must not exceed ") + std::to_string(static_cast<unsigned long long>(maxLength)) + " bytes"
            ), QueuedResponse{});
            return;
          }

          const int result = this->bulkOrInterrupt(handle, static_cast<uint8_t>(endpointNumber | LIBUSB_ENDPOINT_OUT), const_cast<unsigned char*>(payload.data()), static_cast<int>(payload.size()));
          if (result < 0) {
            cb(seq, makeLibusbError("libusb_transfer_out", result), QueuedResponse{});
            return;
          }

          JSON::Object::Entries data = {
            {"status", String("ok")},
            {"transferred", static_cast<uint32_t>(result)}
          };
          cb(seq, JSON::Object::Entries {{"data", data}}, QueuedResponse{});
        });
      }

      void clearHalt(const String& seq, const String& deviceId, uint8_t endpointNumber, bool directionIn, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          const uint8_t endpointAddress = static_cast<uint8_t>(endpointNumber | (directionIn ? LIBUSB_ENDPOINT_IN : LIBUSB_ENDPOINT_OUT));
          const int rc = libusb_clear_halt(handle, endpointAddress);
          if (rc != 0) {
            cb(seq, makeLibusbError("libusb_clear_halt", rc), QueuedResponse{});
          } else {
            cb(seq, makeOkObject(), QueuedResponse{});
          }
        });
      }

      void reset(const String& seq, const String& deviceId, const Callback cb) override {
        this->withHandle(seq, deviceId, cb, [=, this](libusb_device_handle* handle) {
          const int rc = libusb_reset_device(handle);
          if (rc != 0) {
            cb(seq, makeLibusbError("libusb_reset_device", rc), QueuedResponse{});
          } else {
            cb(seq, makeOkObject(), QueuedResponse{});
          }
        });
      }

      void enumerateDevices(Function<void(const EnumerateResult&)> completion) override {
        this->service.queue.push([this, completion]() {
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
          bool authorizedModified = false;

          for (ssize_t i = 0; i < count; ++i) {
            auto* dev = list[i];
            if (!dev) continue;

            libusb_device_descriptor desc{};
            if (libusb_get_device_descriptor(dev, &desc) != 0) {
              continue;
            }

            const uint8_t bus = libusb_get_bus_number(dev);
            const uint8_t address = libusb_get_device_address(dev);
            const String legacyId = makeLegacyDeviceId(bus, address);
            const String stableCandidate = makeStableDeviceId(dev, desc);
            const String deviceId = stableCandidate.empty() ? String("usb:legacy-") + legacyId : stableCandidate;
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
            descriptor.opened = false;
            descriptor.authorized = false;

            bool promoteLegacyAuthorization = false;

            {
              Lock lock(this->mutex);
              auto it = this->devices.find(deviceId);
              if (it == this->devices.end()) {
                DeviceState state;
                state.device = dev;
                libusb_ref_device(dev);
                this->devices.emplace(deviceId, state);
              } else {
                libusb_ref_device(dev);
                if (it->second.device != nullptr) {
                  libusb_unref_device(it->second.device);
                }
                it->second.device = dev;
                descriptor.opened = (it->second.handle != nullptr);
              }

              if (this->authorizedDevices.count(deviceId) > 0) {
                descriptor.authorized = true;
              } else if (this->authorizedDevices.count(legacyId) > 0) {
                descriptor.authorized = true;
                promoteLegacyAuthorization = true;
              }
            }

            if (promoteLegacyAuthorization) {
              if (this->replaceAuthorization(legacyId, deviceId)) {
                authorizedModified = true;
              }
            }

            libusb_config_descriptor* config = nullptr;
            int configResult = libusb_get_active_config_descriptor(dev, &config);
            if (configResult != LIBUSB_SUCCESS) {
              configResult = libusb_get_config_descriptor(dev, 0, &config);
            }

            if (config != nullptr) {
              for (uint8_t ifaceIndex = 0; ifaceIndex < config->bNumInterfaces; ++ifaceIndex) {
                const auto& iface = config->interface[ifaceIndex];
                for (int altIndex = 0; altIndex < iface.num_altsetting; ++altIndex) {
                  const auto& alt = iface.altsetting[altIndex];
                  USB::Backend::InterfaceDescriptor ifaceDesc;
                  ifaceDesc.interfaceNumber = alt.bInterfaceNumber;
                  ifaceDesc.alternateSetting = alt.bAlternateSetting;
                  ifaceDesc.classCode = alt.bInterfaceClass;
                  ifaceDesc.subclassCode = alt.bInterfaceSubClass;
                  ifaceDesc.protocolCode = alt.bInterfaceProtocol;
                  descriptor.interfaces.push_back(ifaceDesc);
                }
              }
              libusb_free_config_descriptor(config);
            }

            descriptors.push_back(descriptor);
            {
              Lock lock(this->mutex);
              if (!this->knownDescriptors.count(descriptor.deviceId)) {
                addedDevices.push_back(descriptor);
              }
              this->knownDescriptors[descriptor.deviceId] = descriptor;
              this->knownDescriptors.erase(legacyId);
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
                authorizedModified = authorizedModified || (this->authorizedDevices.erase(it->first) > 0);
                if (it->second.handle) {
                  libusb_close(it->second.handle);
                  it->second.handle = nullptr;
                }
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

          if (authorizedModified) {
            this->persistAuthorizedToState();
          }

          if (!addedDevices.empty() || !removedDevices.empty()) {
            this->service.dispatcher.dispatch([added = std::move(addedDevices), removed = std::move(removedDevices)]() mutable {
              using oro::runtime::app::App;
              auto app = App::sharedApplication();
              if (!app) return;
              for (const auto& win : app->runtime.windowManager.windows) {
                if (!win || !win->bridge) continue;
                for (const auto& descriptor : added) {
                  JSON::Object evt = JSON::Object::Entries {{"device", LibusbBackend::descriptorToJson(descriptor)}};
                  win->bridge->emit("usb.deviceconnect", evt.str());
                }
                for (const auto& descriptor : removed) {
                  JSON::Object evt = JSON::Object::Entries {{"device", LibusbBackend::descriptorToJson(descriptor)}};
                  win->bridge->emit("usb.devicedisconnect", evt.str());
                }
              }
            });
          }

          completion(result);
        });
      }

      bool authorizeDevice(const String& deviceId) override {
        {
          Lock lock(this->mutex);
          this->authorizedDevices.insert(deviceId);
        }
        this->persistAuthorizedToState();
        return true;
      }

      bool isDeviceAuthorized(const String& deviceId) const override {
        UniqueLock lock(this->mutex);
        return this->authorizedDevices.count(deviceId) > 0;
      }

      void revokeDevice(const String& deviceId) override {
        {
          Lock lock(this->mutex);
          this->authorizedDevices.erase(deviceId);
        }
        this->persistAuthorizedToState();
      }

      void chooseDevice(const String& seq, const USB::DeviceSelection& selection, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingRequest.has_value()) {
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

        auto selected = *it;
        this->authorizeDevice(selected.deviceId);
        selected.authorized = true;

        JSON::Object::Entries data = {{"device", descriptorToJson(selected)}};
        cb(seq, JSON::Object::Entries {{"data", data}}, QueuedResponse{});
      }

      void cancelRequest(const String& seq, const Callback cb) override {
        RequestState request;
        {
          Lock lock(this->mutex);
          if (!this->pendingRequest.has_value()) {
            cb(seq, makeErrorObject("InvalidStateError", "No pending usb.requestDevice"), QueuedResponse{});
            return;
          }
          request = *this->pendingRequest;
          this->pendingRequest.reset();
        }

        cb(seq, makeErrorObject("AbortError", "USB device request cancelled"), QueuedResponse{});
      }

    private:
      struct DeviceState {
        libusb_device* device = nullptr;
        libusb_device_handle* handle = nullptr;
      };

      struct RequestState {
        Vector<DeviceDescriptor> pending;
      };

      USB& service;
      libusb_context* ctx = nullptr;
      mutable Mutex mutex;
      Map<String, DeviceState> devices;
      Set<String> authorizedDevices;
      Map<String, DeviceDescriptor> knownDescriptors;
      std::optional<RequestState> pendingRequest;
      String stateScope;
      bool replaceAuthorization(const String& legacyId, const String& stableId);

      void closeUnlocked(DeviceState& state) {
        if (state.handle) {
          libusb_close(state.handle);
          state.handle = nullptr;
        }
      }

      String computeStateScope() const {
        String bundle = "app";
        if (auto* runtime = this->service.context.getRuntime()) {
          const auto it = runtime->userConfig.find("meta_bundle_identifier");
          if (it != runtime->userConfig.end() && !it->second.empty()) {
            bundle = it->second;
          }
        }
        return String("oro://") + bundle;
      }

      void persistAuthorizedToState() {
        if (!this->service.services.state.isReady()) {
          return;
        }
        Vector<String> ids;
        {
          Lock lock(this->mutex);
          ids.reserve(this->authorizedDevices.size());
          for (const auto& id : this->authorizedDevices) {
            ids.push_back(id);
          }
        }
        if (ids.empty()) {
          this->service.services.state.remove(this->stateScope, "authorizedDevices");
          return;
        }
        std::sort(ids.begin(), ids.end());
        const String serialized = oro::runtime::string::join(ids, "\n");
        this->service.services.state.putString(this->stateScope, "authorizedDevices", serialized);
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
          if (!entry.empty()) {
            this->authorizedDevices.insert(entry);
          }
        }
      }

      template <typename Fn>
      void withHandle(const String& seq, const String& deviceId, const Callback& cb, Fn&& fn) {
        this->service.queue.push([this, seq, cb, deviceId, fn = std::forward<Fn>(fn)]() mutable {
          if (!this->ctx) {
            cb(seq, makeErrorObject("NotSupportedError", "libusb context unavailable"), QueuedResponse{});
            return;
          }

          libusb_device_handle* handle = nullptr;
          {
            Lock lock(this->mutex);
            auto it = this->devices.find(deviceId);
            if (it == this->devices.end()) {
              cb(seq, makeErrorObject("NotFoundError", String("Unknown device ") + deviceId), QueuedResponse{});
              return;
            }
            if (!this->authorizedDevices.count(deviceId)) {
              cb(seq, makeErrorObject("SecurityError", String("Device ") + deviceId + " not authorized"), QueuedResponse{});
              return;
            }
            handle = it->second.handle;
          }

          if (!handle) {
            cb(seq, makeErrorObject("InvalidStateError", String("Device ") + deviceId + " not opened"), QueuedResponse{});
            return;
          }

          fn(handle);
        });
      }

      int bulkOrInterrupt(libusb_device_handle* handle, uint8_t endpoint, unsigned char* data, int length, unsigned int timeout = 5000) {
        int actual = 0;
        int rc = libusb_bulk_transfer(handle, endpoint, data, length, &actual, timeout);
        if (rc == LIBUSB_ERROR_PIPE) {
          rc = libusb_interrupt_transfer(handle, endpoint, data, length, &actual, timeout);
        }
        if (rc != LIBUSB_SUCCESS) {
          return rc;
        }
        return actual;
      }

      static JSON::Object descriptorToJson(const DeviceDescriptor& descriptor) {
        JSON::Array::Entries interfacesJson;
        interfacesJson.reserve(descriptor.interfaces.size());
        for (const auto& iface : descriptor.interfaces) {
          interfacesJson.push_back(JSON::Object::Entries {
            {"interfaceNumber", static_cast<uint32_t>(iface.interfaceNumber)},
            {"alternateSetting", static_cast<uint32_t>(iface.alternateSetting)},
            {"classCode", static_cast<uint32_t>(iface.classCode)},
            {"subclassCode", static_cast<uint32_t>(iface.subclassCode)},
            {"protocolCode", static_cast<uint32_t>(iface.protocolCode)}
          });
        }

        JSON::Object::Entries entries = {
          {"deviceId", descriptor.deviceId},
          {"busNumber", static_cast<uint32_t>(descriptor.busNumber)},
          {"deviceAddress", static_cast<uint32_t>(descriptor.deviceAddress)},
          {"vendorId", static_cast<uint32_t>(descriptor.vendorId)},
          {"productId", static_cast<uint32_t>(descriptor.productId)},
          {"classCode", static_cast<uint32_t>(descriptor.classCode)},
          {"subclassCode", static_cast<uint32_t>(descriptor.subclassCode)},
          {"protocolCode", static_cast<uint32_t>(descriptor.protocolCode)},
          {"opened", descriptor.opened},
          {"authorized", descriptor.authorized},
          {"interfaces", interfacesJson}
        };
        return JSON::Object(entries);
      }
    };

  bool LibusbBackend::replaceAuthorization(const String& legacyId, const String& stableId) {
    bool modified = false;
    {
      Lock lock(this->mutex);
      if (this->authorizedDevices.erase(legacyId) > 0) {
        this->authorizedDevices.insert(stableId);
        modified = true;
      }
    }
    return modified;
  }

  std::unique_ptr<USB::Backend> makeUSBBackend(USB& service) {
    return std::make_unique<LibusbBackend>(service);
  }
}

#endif // ORO_RUNTIME_PLATFORM_ANDROID
