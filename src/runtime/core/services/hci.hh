#ifndef ORO_RUNTIME_CORE_SERVICES_HCI_H
#define ORO_RUNTIME_CORE_SERVICES_HCI_H

#include "../../core.hh"

namespace oro::runtime::core::services {
  class HCI : public core::Service {
    public:
      using ID = uint64_t;
      using Callback = core::Service::Callback;

      struct OpenOptions {
        uint16_t devId = 0;
        bool bringDown = true;
      };

      HCI (const Options& options)
        : core::Service(options)
      {}

      void listAdapters (const String& seq, const Callback cb);
      void getAdapter (const String& seq, uint16_t devId, const Callback cb);
      void setAdapterState (const String& seq, uint16_t devId, bool up, const Callback cb);

      void open (const String& seq, ID id, uint16_t devId, const Callback cb);
      void close (const String& seq, ID id, const Callback cb);
      void write (const String& seq, ID id, SharedPointer<unsigned char[]>, size_t, const Callback cb);
      void readStart (const String& seq, ID id, const Callback cb);
      void readStop (const String& seq, ID id, const Callback cb);

    private:
#if defined(__linux__) && !defined(__ANDROID__)
      struct Handle;
      void onPoll (Handle&, int status, int events);
      SharedPointer<Handle> getHandle (ID id);
      void removeHandle (ID id);
      Map<ID, SharedPointer<Handle>> handles;
      Mutex handlesMutex;
#endif
  };
}

#endif
