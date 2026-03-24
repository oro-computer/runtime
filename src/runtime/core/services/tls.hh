#ifndef ORO_RUNTIME_CORE_SERVICES_TLS_H
#define ORO_RUNTIME_CORE_SERVICES_TLS_H

#include "../../core.hh"
#include "../../tls.hh"
#include "../../tcp.hh"
#include "../../webview/tls_pins.hh"

namespace oro::runtime::core::services {
  class TLS : public core::Service {
    public:
      using ID = uint64_t;
      using Callback = core::Service::Callback;

      enum class PinsMode {
        Append,
        Replace
      };

      struct ConnectOptions {
        String host;
        int port = 0;
        bool rejectUnauthorized = true;
        String servername;
        String ca;
        String cert;
        String key;
        String keyPassphrase;
        Vector<String> alpn;
        String minVersion; // e.g., "TLSv1.2", "TLSv1.3"
        String maxVersion; // e.g., "TLSv1.2", "TLSv1.3"
        Vector<String> ciphers; // numeric IDs as hex strings, e.g., "0x1301"
        String pins; // newline-delimited '<host> sha256/<base64>' entries
        PinsMode pinsMode = PinsMode::Append;
        tls::Provider provider = tls::Provider::Auto;
      };

      struct Handle {
        ID id;
        oro::runtime::types::SharedPointer<tcp::Socket> tcp;
        oro::runtime::types::SharedPointer<tls::Client> client;
        oro::runtime::types::SharedPointer<tls::Server> server;
        bool reading = false;
        bool handshakeEmitted = false;
      };

      using Map = oro::runtime::types::Map<ID, oro::runtime::types::SharedPointer<Handle>>;
      struct ServerOptions {
        String cert;
        String key;
        String ca;
        String keyPassphrase;
        bool requestClientCert = false;
        Vector<String> alpn;
        String minVersion;
        String maxVersion;
        Vector<String> ciphers;
        tls::Provider provider = tls::Provider::Auto;
      };
      struct ServerHandle {
        ID id;
        oro::runtime::types::SharedPointer<tcp::Socket> tcp;
        ServerOptions options;
        bool handshakeEmitted = false;
      };
      using ServerMap = oro::runtime::types::Map<ID, oro::runtime::types::SharedPointer<ServerHandle>>;

      Mutex mutex;
      Map handles;
      ServerMap servers;
      tls::Provider provider = tls::Provider::Auto;
      String pinsValue;
      oro::runtime::webview::TlsPinMap pins;

      TLS (const Options& options);

      // Client APIs (stubs)
      void connect (const String& seq, ID id, const ConnectOptions& options, const Callback cb);
      void write (const String& seq, ID id, SharedPointer<unsigned char[]>, size_t, const Callback cb);
      void readStart (const String& seq, ID id, const Callback cb);
      void readStop (const String& seq, ID id, const Callback cb);
      void shutdown (const String& seq, ID id, const Callback cb);
      void close (const String& seq, ID id, const Callback cb);

      // Runtime TLS pin configuration
      void getPins (const String& seq, const Callback cb);
      void setPins (const String& seq, const String& value, const Callback cb);
      void getProvider (const String& seq, const Callback cb);

      // Server API (stubs)
      void serverCreate (const String& seq, ID id, const ServerOptions&, const Callback cb);
      void serverBind (const String& seq, ID id, const String& address, int port, const Callback cb);
      void serverListen (const String& seq, ID id, int backlog, const Callback cb);
      void serverAccept (const String& seq, ID serverId, ID clientId, const Callback cb);
      void serverReadStart (const String& seq, ID id, const Callback cb);
      void serverReadStop (const String& seq, ID id, const Callback cb);
      void serverWrite (const String& seq, ID id, SharedPointer<unsigned char[]>, size_t, const Callback cb);
      void serverShutdown (const String& seq, ID id, const Callback cb);
      void serverClose (const String& seq, ID id, const Callback cb);

      oro::runtime::types::SharedPointer<ServerHandle> getServer (ID id) {
        oro::runtime::types::Lock lock(this->mutex);
        if (!servers.contains(id)) return nullptr;
        return servers.at(id);
      }
      void setServer (ID id, oro::runtime::types::SharedPointer<ServerHandle> h) {
        oro::runtime::types::Lock lock(this->mutex);
        servers[id] = h;
      }
      void removeServer (ID id) {
        oro::runtime::types::Lock lock(this->mutex);
        servers.erase(id);
      }

      oro::runtime::types::SharedPointer<Handle> getHandle (ID id) {
        oro::runtime::types::Lock lock(this->mutex);
        if (!handles.contains(id)) return nullptr;
        return handles.at(id);
      }
      void setHandle (ID id, oro::runtime::types::SharedPointer<Handle> h) {
        oro::runtime::types::Lock lock(this->mutex);
        handles[id] = h;
      }
      void removeHandle (ID id) {
        oro::runtime::types::Lock lock(this->mutex);
        handles.erase(id);
      }
  };
}

#endif
