#ifndef ORO_RUNTIME_CORE_SERVICES_TCP_H
#define ORO_RUNTIME_CORE_SERVICES_TCP_H

#include "../../core.hh"
#include "../../tcp.hh"

namespace oro::runtime::core::services {
  class TCP : public core::Service {
    public:
      using ID = uint64_t;
      using Callback = core::Service::Callback;

      tcp::SocketManager manager;
      // Track server -> client ids for coordinated shutdown
      Map<ID, Vector<ID>> serverClients;
      Mutex mutex;

      TCP (const Options& options)
        : core::Service(options), manager(this->loop)
      {}

      bool start () override;

      bool stop () override {
        if (this->loop.isOnLoopThread()) {
          this->manager.pauseOnLoopThread();
          return true;
        }
        return this->loop.dispatch([this]() {
          this->manager.pauseOnLoopThread();
        });
      }

      // API surface
      void create (const String& seq, ID id, const Callback cb);
      void close (const String& seq, ID id, const Callback cb);
      void bind (const String& seq, ID id, const String& address, int port, const Callback cb);
      void listen (const String& seq, ID id, int backlog, const Callback cb);
      void accept (const String& seq, ID serverId, ID clientId, const Callback cb);
      void connect (const String& seq, ID id, const String& address, int port, const Callback cb);
      void write (const String& seq, ID id, SharedPointer<unsigned char[]>, size_t, const Callback cb);
      void readStart (const String& seq, ID id, const Callback cb);
      void readStop (const String& seq, ID id, const Callback cb);
      void shutdown (const String& seq, ID id, const Callback cb);
      void setNoDelay (const String& seq, ID id, bool on, const Callback cb);
      void setKeepAlive (const String& seq, ID id, bool on, unsigned int delaySec, const Callback cb);
      void getSockName (const String& seq, ID id, const Callback cb);
      void getPeerName (const String& seq, ID id, const Callback cb);

      // Internal: maintenance
      void registerClient (ID serverId, ID clientId);
      void unregisterClient (ID serverId, ID clientId);
  };
}

#endif
