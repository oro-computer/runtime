#ifndef ORO_RUNTIME_TLS_CLIENT_H
#define ORO_RUNTIME_TLS_CLIENT_H

#include "../loop.hh"
#include "../tls.hh"
#include "../platform/types.hh"

namespace oro::runtime { namespace tcp { class Socket; } }

namespace oro::runtime::tls {
  struct ClientOptions {
    String host;
    int port = 0;
    bool rejectUnauthorized = true;
    String servername;
    String ca;    // PEM
    String cert;  // PEM
    String key;   // PEM
    String keyPassphrase;
    Vector<String> alpn;
    String minVersion;
    String maxVersion;
    Vector<String> ciphers; // numeric IDs as hex strings
  };

  class Client {
    public:
      Client(loop::Loop& loop, const Options& options);
      ~Client();

      int connect(const ClientOptions& options);
      int write(const char* data, size_t len);
      int readStart();
      int readStop();
      int shutdown();
      void close();

      void attachTransport(types::SharedPointer<oro::runtime::tcp::Socket> socket);
      int onEncryptedData(const unsigned char* data, size_t len);
      int driveHandshake();
      bool handshakeDone() const;
      int writeApp(const unsigned char* data, size_t len);
      int readOnce(unsigned char* out, size_t maxlen);
      int getVerifyResult(unsigned long& flags, types::String& info);
      int getPeerSANs(types::Vector<types::String>& sans);
      int getSessionInfo(types::String& protocol, types::String& cipher);
      int getPeerSubject(types::String& subject);
      int getPeerCertificateSha256(types::Vector<uint8_t>& digest);
      int getNegotiatedALPN(types::String& alpn);
      void enqueueWrite(const unsigned char* data, size_t len);
      void flushPendingWrites();
      const String& lastErrorMessage() const;
      long lastErrorCode() const;

    private:
      loop::Loop& loop;
      Options options;
      struct Impl; // provider-specific impl (e.g., mbedTLS)
      Impl* impl = nullptr;
      types::SharedPointer<oro::runtime::tcp::Socket> transport;
      std::vector<std::vector<unsigned char>> pendingWrites;
  };
}

#endif
