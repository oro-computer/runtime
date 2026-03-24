#ifndef ORO_RUNTIME_CORE_SERVICES_IROH_H
#define ORO_RUNTIME_CORE_SERVICES_IROH_H

#include <optional>

#include "../../core.hh"
#include "../../iroh.hh"

namespace oro::runtime::core::services {
  class Iroh : public core::Service {
    public:
      using ID = uint64_t;
      using Callback = core::Service::Callback;

      struct EndpointOptions {
        std::optional<String> secretKey;
        std::optional<iroh::RelayMode> relayMode;
        std::optional<iroh::DiscoveryConfig> discovery;
        Vector<Vector<uint8_t>> alpns;
      };

      struct BindOptions {
        std::optional<String> ipv4;
        std::optional<String> ipv6;
      };

      struct ConnectOptions {
        String nodeAddr;
        Vector<uint8_t> alpn;
      };

      struct AcceptOptions {
        std::optional<Vector<uint8_t>> expectedAlpn;
      };

      struct DatagramOptions {
        std::optional<uint64_t> timeoutMs;
      };

      struct StreamWriteOptions {
        std::optional<uint64_t> timeoutMs;
      };

      struct StreamReadOptions {
        size_t length = 0;
        std::optional<uint64_t> timeoutMs;
      };

      struct StreamReadToEndOptions {
        size_t sizeLimit = 0;
        uint64_t timeoutMs = 0;
      };

      Iroh (const Options& options);
      ~Iroh () override;

      bool start () override;
      bool stop () override;

      void init (const String& seq, const Callback cb);
      void shutdown (const String& seq, const Callback cb);
      void status (const String& seq, const Callback cb);
      void setLogLevel (const String& seq, iroh::LogLevel level, const Callback cb);
      void pathToKey (
        const String& seq,
        const String& path,
        const std::optional<String>& prefix,
        const std::optional<String>& root,
        const Callback cb
      );
      void keyToPath (
        const String& seq,
        const Vector<uint8_t>& key,
        const std::optional<String>& prefix,
        const std::optional<String>& root,
        const Callback cb
      );

      void createEndpoint (const String& seq, ID id, const EndpointOptions&, const Callback cb);
      void destroyEndpoint (const String& seq, ID id, const Callback cb);
      void bindEndpoint (const String& seq, ID id, const BindOptions&, const Callback cb);
      void getHomeRelay (const String& seq, ID id, const Callback cb);
      void getNodeAddr (const String& seq, ID id, const Callback cb);

      void connect (
        const String& seq,
        ID endpointId,
        ID connectionId,
        const ConnectOptions& options,
        const Callback cb
      );

      void accept (
        const String& seq,
        ID endpointId,
        ID connectionId,
        const AcceptOptions& options,
        const Callback cb
      );

      void acceptAny (
        const String& seq,
        ID endpointId,
        ID connectionId,
        const Callback cb
      );

      void closeEndpoint (const String& seq, ID id, const Callback cb);
      void closeConnection (const String& seq, ID id, const Callback cb);
      void waitConnectionClosed (const String& seq, ID id, const Callback cb);
      void connectionStats (const String& seq, ID id, const Callback cb);

      void writeDatagram (
        const String& seq,
        ID connectionId,
        const Vector<uint8_t>& data,
        const DatagramOptions& options,
        const Callback cb
      );

      void readDatagram (
        const String& seq,
        ID connectionId,
        const DatagramOptions& options,
        const Callback cb
      );

      void watchConnectionType (
        const String& seq,
        ID endpointId,
        const String& nodeId,
        bool enable,
        const Callback cb
      );

      void openBidirectionalStream (
        const String& seq,
        ID connectionId,
        ID sendStreamId,
        ID recvStreamId,
        const Callback cb
      );

      void openUnidirectionalStream (
        const String& seq,
        ID connectionId,
        ID sendStreamId,
        const Callback cb
      );

      void acceptBidirectionalStream (
        const String& seq,
        ID connectionId,
        ID sendStreamId,
        ID recvStreamId,
        const Callback cb
      );

      void acceptUnidirectionalStream (
        const String& seq,
        ID connectionId,
        ID recvStreamId,
        const Callback cb
      );

      void sendStreamWrite (
        const String& seq,
        ID streamId,
        const Vector<uint8_t>& data,
        const StreamWriteOptions& options,
        const Callback cb
      );

      void sendStreamFinish (const String& seq, ID streamId, const Callback cb);

      void recvStreamRead (
        const String& seq,
        ID streamId,
        const StreamReadOptions& options,
        const Callback cb
      );

      void recvStreamReadToEnd (
        const String& seq,
        ID streamId,
        const StreamReadToEndOptions& options,
        const Callback cb
      );

    private:
      struct EndpointEntry;
      struct ConnectionEntry;
      struct SendStreamEntry;
      struct RecvStreamEntry;
      struct ConnectionTypeWatcher;

      using EndpointPtr = SharedPointer<EndpointEntry>;
      using ConnectionPtr = SharedPointer<ConnectionEntry>;
      using SendStreamPtr = SharedPointer<SendStreamEntry>;
      using RecvStreamPtr = SharedPointer<RecvStreamEntry>;
      using WatcherPtr = SharedPointer<ConnectionTypeWatcher>;

      EndpointPtr getEndpoint (ID id) const;
      ConnectionPtr getConnection (ID id) const;
      SendStreamPtr getSendStream (ID id) const;
      RecvStreamPtr getRecvStream (ID id) const;

      void removeConnectionLocked (ID id);
      void removeStreamsForConnectionLocked (ID connectionId);

      void respondError (
        const Callback& cb,
        const String& seq,
        const String& source,
        const String& message,
        const String& code = ""
      );

      JSON::Object::Entries makeStatusData () const;
      JSON::Object::Entries describeConnectionStats (const ConnectionPtr&) const;

      mutable Mutex mutex;
      Map<ID, EndpointPtr> endpoints;
      Map<ID, ConnectionPtr> connections;
      Map<ID, SendStreamPtr> sendStreams;
      Map<ID, RecvStreamPtr> recvStreams;

      const bool allowService;
      std::atomic<int> cachedLogLevel;

      bool hasLibrary () const;
  };
}

#endif
