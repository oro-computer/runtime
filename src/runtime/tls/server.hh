#ifndef ORO_RUNTIME_TLS_SERVER_H
#define ORO_RUNTIME_TLS_SERVER_H

#include "../loop.hh"
#include "../tls.hh"
#include "../platform/types.hh"

namespace oro::runtime { namespace tcp { class Socket; } }

namespace oro::runtime::tls {
  class Server {
    public:
      struct Options {
        types::String cert;
        types::String key;
        types::String ca;
        types::String keyPassphrase;
        bool requestClientCert = false;
        types::Vector<types::String> alpn;
        types::String minVersion;
        types::String maxVersion;
        types::Vector<types::String> ciphers;
        Provider provider = Provider::Auto;
      };

      Server(loop::Loop& loop, const Options& options);
      ~Server();

      int setup(const Options& options);
      void attachTransport(types::SharedPointer<oro::runtime::tcp::Socket> socket);
      int onEncryptedData(const unsigned char* data, size_t len);
      int driveHandshake();
      bool handshakeDone() const;
      int writeApp(const unsigned char* data, size_t len);
      int readOnce(unsigned char* out, size_t maxlen);
      int getPeerSubject(types::String& subject);
      int getSessionInfo(types::String& protocol, types::String& cipher);
      int getVerifyResult(unsigned long& flags, types::String& info);
      int getPeerSANs(types::Vector<types::String>& sans);
      int getNegotiatedALPN(types::String& alpn);
      void enqueueWrite(const unsigned char* data, size_t len);
      void flushPendingWrites();
      int shutdown();
      const types::String& lastErrorMessage() const;
      long lastErrorCode() const;

    private:
      void close();
      loop::Loop& loop;
      Options options;
      struct Impl; Impl* impl = nullptr;
      types::SharedPointer<oro::runtime::tcp::Socket> transport;
      std::vector<std::vector<unsigned char>> pendingWrites;
  };
}

#endif
