#include "provider_registry.hh"
#include "client.hh"
#include "server.hh"

#include <memory>

namespace oro::runtime::tls {
  // Constructing the registry retains this archive member and registers providers
  // once, before instance() exposes the registry to any caller.
  ProviderRegistry::ProviderRegistry () {
  #if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
    this->registerStatic(
      Provider::mbedTLS,
      [](loop::Loop& loop, const Options& opts) {
        auto copy = opts;
        copy.provider = Provider::mbedTLS;
        return std::make_shared<Client>(loop, copy);
      },
      [](loop::Loop& loop, const Server::Options& opts) {
        auto copy = opts;
        copy.provider = Provider::mbedTLS;
        return std::make_shared<Server>(loop, copy);
      }
    );
  #endif

  #if defined(ORO_RUNTIME_TLS_OPENSSL)
    this->registerStatic(
      Provider::OpenSSL,
      [](loop::Loop& loop, const Options& opts) {
        auto copy = opts;
        copy.provider = Provider::OpenSSL;
        return std::make_shared<Client>(loop, copy);
      },
      [](loop::Loop& loop, const Server::Options& opts) {
        auto copy = opts;
        copy.provider = Provider::OpenSSL;
        return std::make_shared<Server>(loop, copy);
      }
    );
  #endif

  #if defined(ORO_RUNTIME_TLS_GNUTLS)
    this->registerStatic(
      Provider::GnuTLS,
      [](loop::Loop& loop, const Options& opts) {
        auto copy = opts;
        copy.provider = Provider::GnuTLS;
        return std::make_shared<Client>(loop, copy);
      },
      [](loop::Loop& loop, const Server::Options& opts) {
        auto copy = opts;
        copy.provider = Provider::GnuTLS;
        return std::make_shared<Server>(loop, copy);
      }
    );
  #endif

  #if defined(ORO_RUNTIME_TLS_SCHANNEL)
    this->registerStatic(
      Provider::Schannel,
      [](loop::Loop& loop, const Options& opts) {
        auto copy = opts;
        copy.provider = Provider::Schannel;
        return std::make_shared<Client>(loop, copy);
      },
      [](loop::Loop& loop, const Server::Options& opts) {
        auto copy = opts;
        copy.provider = Provider::Schannel;
        return std::make_shared<Server>(loop, copy);
      }
    );
  #endif
  }
}
