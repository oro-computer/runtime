#ifndef ORO_RUNTIME_CORE_SERVICES_IPFS_H
#define ORO_RUNTIME_CORE_SERVICES_IPFS_H

#include "../../core.hh"
#include "../../ipc.hh"
#include "../../json.hh"

namespace oro::runtime::core::services {
  class IPFS : public core::Service {
    public:
      struct StartOptions {
        String repoPath;
        int port = 0;
      };

      IPFS (const Options& options);
      ~IPFS () override;

      bool start () override;
      bool stop () override;

      void nodeStart (const ipc::Message::Seq&, const StartOptions&, const Callback);
      void nodeStop (const ipc::Message::Seq&, const Callback);
      void nodeStatus (const ipc::Message::Seq&, const Callback);
      void addFile (const ipc::Message::Seq&, const String&, const Callback);
      void fetch (const ipc::Message::Seq&, const String&, const String&, bool, const Callback);
      void pin (const ipc::Message::Seq&, const String&, const Callback);
      void unpin (const ipc::Message::Seq&, const String&, const Callback);
      void garbageCollect (const ipc::Message::Seq&, const Callback);
      void peerId (const ipc::Message::Seq&, const Callback);
      void addPeer (const ipc::Message::Seq&, const String&, const Callback);
      void removePeer (const ipc::Message::Seq&, const String&, const Callback);

    private:
      bool ensureLibraryAvailable (const ipc::Message::Seq&, const String&, const Callback) const;
      void dispatchUnavailable (const ipc::Message::Seq&, const String&, const Callback) const;
      void dispatchError (const ipc::Message::Seq&, const String&, const String&, const Callback) const;
      void dispatchError (const ipc::Message::Seq&, const String&, const String&, const String&, const Callback) const;
      void dispatchData (const ipc::Message::Seq&, const String&, const JSON::Any&, const Callback) const;

      bool hasLibrary = false;
      bool nodeStarted = false;
      String repoPath;
      int port = 0;
      String lastPeerId;
      mutable Mutex mutex;
  };
}
#endif
