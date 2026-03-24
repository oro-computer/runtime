#ifndef ORO_RUNTIME_CORE_SERVICES_DBUS_H
#define ORO_RUNTIME_CORE_SERVICES_DBUS_H

#include "../../core.hh"
#include "../../ipc.hh"
#include "../../../include/oro/dbus.h"

namespace oro::runtime::core::services {
  class DBus : public core::Service {
    public:
      using ConnectionID = uint64_t;
      using MatchID = uint64_t;
      using ExportID = uint64_t;
      using CallID = uint64_t;

      struct ConnectOptions {
        enum class BusType {
          Session,
          System,
          Starter,
          Address
        };

        BusType bus = BusType::Session;
        String address;
        bool allowPeerAuthentication = false;
        bool privateConnection = false;
      };

      struct CallOptions {
        ConnectionID connectionId = 0;
        String destination;
        String path;
        String interface;
        String member;
        String signature;
        JSON::Any body;
        int timeoutMs = -1;
        bool noReply = false;
      };

      struct SignalOptions {
        ConnectionID connectionId = 0;
        String path;
        String interface;
        String name;
        String signature;
        JSON::Any body;
      };

      struct MatchRule {
        ConnectionID connectionId = 0;
        String rule;
      };

      struct ExportOptions {
        ConnectionID connectionId = 0;
        String path;
        String interface;
      };

      explicit DBus(const Options& options);
      ~DBus() override;

      bool start() override;
      bool stop() override;

      void connect(const String& seq, const JSON::Any& options, const Callback cb);
      void disconnect(const String& seq, ConnectionID id, const Callback cb);

      void requestName(const String& seq, ConnectionID id, const String& name, uint32_t flags, const Callback cb);
      void releaseName(const String& seq, ConnectionID id, const String& name, const Callback cb);

      void call(const String& seq, const CallOptions& options, const Callback cb);
      void emitSignal(const String& seq, const SignalOptions& options, const Callback cb);

      void addMatch(const String& seq, const MatchRule& rule, const Callback cb);
      void removeMatch(const String& seq, MatchID id, const Callback cb);

      void exportObject(const String& seq, const ExportOptions& options, const JSON::Any& definition, const Callback cb);
      void unexportObject(const String& seq, ExportID id, const Callback cb);

      void respond(const String& seq, CallID id, bool isError, const String& name, const String& signature, const JSON::Any& body, const Callback cb);

      JSON::Object availability() const;

    private:
      struct Implementation;
      UniquePointer<Implementation> impl;
  };
}

#endif
