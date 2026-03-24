#ifndef ORO_RUNTIME_TLS_H
#define ORO_RUNTIME_TLS_H

#include "platform.hh"
#include "loop.hh"

// TLS provider backends are implemented as mutually-exclusive translation units
// under src/runtime/tls/*.cc. Compiling more than one provider at a time will
// result in duplicate symbols (ODR violations) and/or mismatched provider
// behavior. Keep this as an early, explicit compile-time guard.
#if defined(ORO_RUNTIME_TLS_GNUTLS)
  #error "ORO_RUNTIME_TLS_GNUTLS is not implemented in src/runtime/tls yet"
#endif
#if defined(ORO_RUNTIME_TLS_SECURETRANSPORT)
  #error "ORO_RUNTIME_TLS_SECURETRANSPORT is not implemented in src/runtime/tls yet"
#endif
#if defined(ORO_RUNTIME_TLS_PLATFORM_ANDROID)
  #error "ORO_RUNTIME_TLS_PLATFORM_ANDROID is not implemented in src/runtime/tls yet"
#endif
#if (defined(ORO_RUNTIME_ENABLE_MBEDTLS) + defined(ORO_RUNTIME_TLS_OPENSSL) + defined(ORO_RUNTIME_TLS_SCHANNEL) + defined(ORO_RUNTIME_TLS_GNUTLS) + defined(ORO_RUNTIME_TLS_SECURETRANSPORT) + defined(ORO_RUNTIME_TLS_PLATFORM_ANDROID)) > 1
  #error "Multiple TLS providers enabled: select exactly one provider at build time"
#endif

namespace oro::runtime::tls {
  enum class Provider {
    Auto,
    mbedTLS,
    OpenSSL,
    GnuTLS,
    SecureTransport,
    Schannel,
    Android
  };

  // Forward declarations for TLS client/server so headers can refer to them
  // without pulling in implementation-specific backends here.
  class Client;
  class Server;

  struct Options {
    bool rejectUnauthorized = true;
    Provider provider = Provider::Auto;
  };
}

#endif
