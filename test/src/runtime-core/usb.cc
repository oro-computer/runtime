#include "src/runtime/ipc/usb_routes.hh"
#include "src/runtime/core/services/usb/libusb_backend.hh"
#include "src/runtime/runtime.hh"
#include "tests.hh"

namespace oro::Tests {
  namespace {
    using USB = runtime::core::services::USB;

    struct LoopbackDevice {
      runtime::bytes::Buffer written;
      unsigned calls = 0;
      void controlTransferOut (const USB::Request& request, const String&, const runtime::JSON::Any& setup,
          const runtime::bytes::Buffer& data, const USB::Callback cb) {
        ++calls;
        if (setup.type == runtime::JSON::Type::Object) {
          const auto& fields = setup.as<runtime::JSON::Object>();
          if (fields.get("bRequest").str() == "7" && fields.get("bmRequestType").str() == "64") written = data;
        }
        cb(request, runtime::JSON::Object::Entries{{"data", runtime::JSON::Object{}}}, {});
      }
      void controlTransferIn (const USB::Request& request, const String&, const runtime::JSON::Any& setup,
          uint32_t length, const USB::Callback cb) {
        ++calls;
        const bool ok = setup.type == runtime::JSON::Type::Object &&
          setup.as<runtime::JSON::Object>().get("bmRequestType").str() == "192" && length == written.size();
        cb(request, runtime::JSON::Object::Entries{{"data", ok ? written.str(runtime::bytes::Buffer::Encoding::BASE64) : String()}}, {});
      }
    };
  }

  void usb (Harness& t) {
#if !ORO_RUNTIME_PLATFORM_ANDROID && !ORO_RUNTIME_PLATFORM_IOS
    t.test("Desktop WebUSB remembers serial grants and honors revocation after restart", [](auto t) {
      runtime::Runtime::Options options;
      options.autoStart = false;
      options.userConfig["meta_bundle_identifier"] = "test.webusb.grants";
      runtime::Runtime instance(options);
      const String serial = "usb:vidcafe:pid0001:ser01";
      const String attachment = "usb:vidcafe:pid0001:123";
      auto backend = runtime::core::services::usb::makeUSBBackend(instance.services.usb);
      t.assert(!backend->authorizeDevice(serial), "failed storage write does not grant access");
      t.assert(!backend->isDeviceAuthorized(serial), "failed grant leaves the device unauthorized");
      t.assert(instance.services.state.init(), "open the test state store");
      t.assert(backend->authorizeDevice(serial), "save the serial grant");
      t.assert(backend->authorizeDevice(attachment), "grant the current attachment");
      backend = runtime::core::services::usb::makeUSBBackend(instance.services.usb);
      t.assert(backend->isDeviceAuthorized(serial), "a fresh backend loads the serial grant");
      t.assert(!backend->isDeviceAuthorized(attachment), "a fresh backend drops the attachment grant");
      backend->revokeDevice(serial);
      backend = runtime::core::services::usb::makeUSBBackend(instance.services.usb);
      t.assert(!backend->isDeviceAuthorized(serial), "revocation survives another backend restart");
    });
#endif

    t.test("WebUSB only authorizes the bundled origin", [](auto t) {
      const runtime::types::Map<String, String> config{{"meta_bundle_identifier", "example.test"}};
      t.assert(USB::allowsOrigin(runtime::URL("oro://example.test/index.html"), config), "application origin");
      t.assert(!USB::allowsOrigin(runtime::URL("oro://example.test:9000/"), config), "unconfigured port");
      t.assert(!USB::allowsOrigin(runtime::URL("oro://third-party.example/"), config), "external origin");
    });

    t.test("WebUSB control routes parse setup and preserve the binary body", [](auto t) {
      LoopbackDevice backend;
      USB::Request caller{"1", 7, 0};
      const String fields = "%2C%22bRequest%22%3A7%2C%22wValue%22%3A0%2C%22wIndex%22%3A0%7D";
      const String setup = "?deviceId=fixture&setup=%7B%22bmRequestType%22%3A";
      runtime::ipc::Message out("ipc://usb.transfer.controlOut" + setup + "64" + fields);
      out.buffer = runtime::bytes::Buffer::from(runtime::types::Vector<uint8_t>{0, 128, 255});
      runtime::ipc::dispatchUSBControlTransfer(backend, caller, out, false, [](auto) {});
      t.assert(runtime::bytes::Buffer::equals(backend.written, out.buffer), "native backend receives the setup object and OUT data");
      runtime::ipc::Message in("ipc://usb.transfer.controlIn" + setup + "192" + fields + "&length=3");
      runtime::ipc::dispatchUSBControlTransfer(backend, caller, in, true, [&](const auto result) {
        const auto& data = result.value.template as<runtime::JSON::Object>().get("data");
        t.equals(data.template as<runtime::JSON::String>().value(), String("AID/"), "IN returns the saved bytes");
      });
      runtime::ipc::Message malformed("ipc://usb.transfer.controlIn?deviceId=fixture&setup=%5B%5D&length=3");
      runtime::ipc::dispatchUSBControlTransfer(backend, caller, malformed, true, [&](const auto result) {
        t.assert(!result.err.isNull(), "rejects malformed setup before backend dispatch");
      });
      runtime::ipc::Message overflow("ipc://usb.transfer.controlIn" + setup + "192" + fields + "&length=4294967299");
      runtime::ipc::dispatchUSBControlTransfer(backend, caller, overflow, true, [&](const auto result) {
        t.assert(!result.err.isNull(), "rejects overflowing lengths before narrowing");
      });
      t.equals(static_cast<size_t>(backend.calls), size_t(2), "only valid messages reach the fake device");
      t.assert(runtime::core::services::isProtectedUSBClass(9), "hub interfaces are protected");
      t.assert(runtime::core::services::isBlockedUSBDevice(0x1050, 0x0407), "blocklisted devices are filtered");
    });
  }
}
