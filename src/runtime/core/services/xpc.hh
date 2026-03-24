#ifndef ORO_RUNTIME_CORE_SERVICES_XPC_H
#define ORO_RUNTIME_CORE_SERVICES_XPC_H

#include "../../core.hh"
#include "../../json.hh"

namespace oro::runtime::core::services {
  class XPC : public core::Service {
    public:
      using ConnectionID = uint64_t;
      using MessageID = uint64_t;

      struct Backend {
        virtual ~Backend() = default;
        virtual JSON::Object availability() const = 0;
        virtual void connect(const String& seq, const JSON::Any& options, const Callback cb) = 0;
        virtual void disconnect(const String& seq, ConnectionID id, const Callback cb) = 0;
        virtual void send(
          const String& seq,
          ConnectionID id,
          const JSON::Any& message,
          bool expectReply,
          uint64_t timeoutMs,
          const Callback cb
        ) = 0;
        virtual void respond(
          const String& seq,
          MessageID id,
          bool isError,
          const JSON::Any& message,
          const Callback cb
        ) = 0;
        virtual void suspend(const String& seq, ConnectionID id, const Callback cb) = 0;
        virtual void resume(const String& seq, ConnectionID id, const Callback cb) = 0;
      };

      XPC (const Options& options);
      ~XPC () override;

      JSON::Object availability() const;

      void connect(const String& seq, const JSON::Any& options, const Callback cb);
      void disconnect(const String& seq, ConnectionID id, const Callback cb);
      void send(
        const String& seq,
        ConnectionID id,
        const JSON::Any& message,
        bool expectReply,
        uint64_t timeoutMs,
        const Callback cb
      );
      void respond(
        const String& seq,
        MessageID id,
        bool isError,
        const JSON::Any& message,
        const Callback cb
      );
      void suspend(const String& seq, ConnectionID id, const Callback cb);
      void resume(const String& seq, ConnectionID id, const Callback cb);

    private:
      Backend* ensureBackend();
      SharedPointer<Backend> backend;
  };
}

#endif
