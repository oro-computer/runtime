#ifndef ORO_RUNTIME_USB_TRANSPORT_H
#define ORO_RUNTIME_USB_TRANSPORT_H

#include "../usb.hh"

#if !ORO_RUNTIME_PLATFORM_IOS
#if __has_include(<libusb-1.0/libusb.h>)
#include <libusb-1.0/libusb.h>
#else
#include <libusb.h>
#endif

namespace oro::runtime::core::services::usb {
  // All handles, claimed interfaces and alternate settings belong to the USB worker queue.
  class TransportBackend : public USB::Backend {
    public:
      using Request = USB::Request;
      using Callback = USB::Callback;
      explicit TransportBackend (USB& service) : service(service) {}
      ~TransportBackend () override;
      void open (const Request&, const String&, const Callback) override;
      void close (const Request&, const String&, const Callback) override;
      void selectConfiguration (const Request&, const String&, uint8_t, const Callback) override;
      void claimInterface (const Request&, const String&, uint8_t, const Callback) override;
      void releaseInterface (const Request&, const String&, uint8_t, const Callback) override;
      void selectAlternateInterface (const Request&, const String&, uint8_t, uint8_t, const Callback) override;
      void controlTransferIn (const Request&, const String&, const JSON::Any&, uint32_t, const Callback) override;
      void controlTransferOut (const Request&, const String&, const JSON::Any&, const bytes::Buffer&, const Callback) override;
      void transferIn (const Request&, const String&, uint8_t, uint32_t, const Callback) override;
      void transferOut (const Request&, const String&, uint8_t, const bytes::Buffer&, const Callback) override;
      void clearHalt (const Request&, const String&, uint8_t, bool, const Callback) override;
      void reset (const Request&, const String&, const Callback) override;
      void isochronous (const Request&, const String&, uint8_t, bool, const Vector<uint32_t>&, const bytes::Buffer&, const Callback) override;
      void disconnectClient (uint64_t) override;

    protected:
      struct Session {
        libusb_device_handle* handle = nullptr;
        int fd = -1;
        uint64_t owner = 0;
        int configuration = 0;
        Map<uint8_t, uint8_t> claimed;
        ~Session ();
        int endpointType (uint8_t address) const;
      };

      template <class Work>
      void enqueue (Work work) {
        service.queue.push([self = shared_from_this(), work = std::move(work)]() mutable { work(); });
      }
      USB& service;
      libusb_context* ctx = nullptr;
      Map<String, UniquePointer<Session>> sessions;
      JSON::Object describeDevice (const DeviceDescriptor&, const Request&) const;
      void emitChanges (Vector<DeviceDescriptor>, Vector<DeviceDescriptor>);
      virtual int openDevice (const String&, Session&) = 0;
      using Operation = Function<JSON::Object(Session&)>;
      void operate (const Request&, const String&, const Callback&, Operation);
      void transfer (const Request&, const String&, uint8_t, bool, uint32_t, const bytes::Buffer&, const Callback&);
      void control (const Request&, const String&, bool, const JSON::Any&, uint32_t, const bytes::Buffer&, const Callback&);
  };
}
#endif
#endif
