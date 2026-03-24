#include "server.hh"
#include "../tcp.hh"

#include <algorithm>
#include <vector>
#include <cstring>

#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
#  include "common_mbedtls.hh"
#  include <mbedtls/ssl.h>
#  include <mbedtls/ctr_drbg.h>
#  include <mbedtls/entropy.h>
#  include <mbedtls/x509_crt.h>
#  include <mbedtls/pk.h>
#  include <mbedtls/net_sockets.h>
#endif

namespace oro::runtime::tls {
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)

  struct Server::Impl {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt srv_cert;
    mbedtls_pk_context srv_key;
    bool initialized = false;
    bool handshake_complete = false;
    std::vector<unsigned char> inbuf;
    std::vector<char*> alpn_ptrs;
  };

  Server::Server(loop::Loop& l, const Options& o) : loop(l), options(o) {
    this->impl = new Impl();
    auto* c = this->impl;
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ctr_drbg_init(&c->ctr_drbg);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_x509_crt_init(&c->cacert);
    mbedtls_x509_crt_init(&c->srv_cert);
    mbedtls_pk_init(&c->srv_key);
  }

  Server::~Server() {
    if (this->impl) {
      for (auto p : this->impl->alpn_ptrs) {
        if (p) {
          delete[] p;
        }
      }

      this->impl->alpn_ptrs.clear();
      mbedtls_x509_crt_free(&this->impl->cacert);
      mbedtls_x509_crt_free(&this->impl->srv_cert);
      mbedtls_pk_free(&this->impl->srv_key);
      mbedtls_ssl_free(&this->impl->ssl);
      mbedtls_ssl_config_free(&this->impl->conf);
      mbedtls_ctr_drbg_free(&this->impl->ctr_drbg);
      mbedtls_entropy_free(&this->impl->entropy);
      delete this->impl;
      this->impl = nullptr;
    }
  }

  int Server::setup(const Options& opts) {
    this->options = opts;
    auto* c = this->impl;
    int rc = 0;
    using namespace oro::runtime::tls::mbedtls_helpers;
    if ((rc = seeded_ctr_drbg(&c->ctr_drbg, &c->entropy)) != 0) return rc;
    if ((rc = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) return rc;
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->ctr_drbg);

    // Load server cert/key
    if (opts.cert.empty() || opts.key.empty()) return -1;
    if ((rc = mbedtls_x509_crt_parse(&c->srv_cert, reinterpret_cast<const unsigned char*>(opts.cert.c_str()), opts.cert.size() + 1)) < 0) return rc;
    const unsigned char* pass = nullptr;
    size_t passlen = 0;
    if (!opts.keyPassphrase.empty()) {
      pass = reinterpret_cast<const unsigned char*>(opts.keyPassphrase.c_str());
      passlen = opts.keyPassphrase.size();
    }
    if ((rc = mbedtls_pk_parse_key(&c->srv_key, reinterpret_cast<const unsigned char*>(opts.key.c_str()), opts.key.size() + 1, pass, passlen, mbedtls_ctr_drbg_random, &c->ctr_drbg)) < 0) return rc;
    if ((rc = mbedtls_ssl_conf_own_cert(&c->conf, &c->srv_cert, &c->srv_key)) != 0) return rc;

    // CA chain for client cert verify (optional)
    if (!opts.ca.empty()) {
      if ((rc = mbedtls_x509_crt_parse(&c->cacert, reinterpret_cast<const unsigned char*>(opts.ca.c_str()), opts.ca.size() + 1)) < 0) return rc;
      mbedtls_ssl_conf_ca_chain(&c->conf, &c->cacert, nullptr);
    }
    mbedtls_ssl_conf_authmode(&c->conf, opts.requestClientCert ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);

  // ALPN
  if (!opts.alpn.empty()) {
      c->alpn_ptrs.clear();
      c->alpn_ptrs.reserve(opts.alpn.size() + 1);

      for (const auto& proto : opts.alpn) {
        char* p = new char[proto.size() + 1];
        memcpy(p, proto.c_str(), proto.size() + 1);
        c->alpn_ptrs.push_back(p);
      }

      c->alpn_ptrs.push_back(nullptr);
      if ((rc = mbedtls_ssl_conf_alpn_protocols(&c->conf, const_cast<const char**>(c->alpn_ptrs.data()))) != 0) return rc;
    }

  // versions/ciphers
  configure_versions(&c->conf, opts.minVersion, opts.maxVersion);
  configure_ciphers(&c->conf, opts.ciphers);

    if ((rc = mbedtls_ssl_setup(&c->ssl, &c->conf)) != 0) {
      return rc;
    }

    c->initialized = true;
    c->handshake_complete = false;
    c->inbuf.reserve(16 * 1024);
    return 0;
  }

  void Server::attachTransport(types::SharedPointer<::oro::runtime::tcp::Socket> sock) {
    this->transport = sock;
  }

  int Server::onEncryptedData(const unsigned char* data, size_t len) {
    auto* c = this->impl;
    c->inbuf.insert(c->inbuf.end(), data, data + len);
    return 0;
  }

  int Server::driveHandshake() {
    auto* c = this->impl;
    if (!c->initialized) {
      return -1;
    }

    // BIO hooks
    mbedtls_ssl_set_bio(
      &c->ssl,
      this,
      [](void* ctx, const unsigned char* buf, size_t len) -> int {
        auto self = static_cast<Server*>(ctx);

        if (!self->transport) {
          return MBEDTLS_ERR_NET_SEND_FAILED;
        }

        int err = self->transport->write(
          reinterpret_cast<const char*>(buf),
          len,
          [self](int /*status*/) {
            self->flushPendingWrites();
          }
        );

        if (err == 0) {
          return static_cast<int>(len);
        }

        return MBEDTLS_ERR_SSL_WANT_WRITE;
      },
      [](void* ctx, unsigned char* buf, size_t len) -> int {
        auto self = static_cast<Server*>(ctx);
        auto* c = self->impl;

        if (c->inbuf.empty()) {
          return MBEDTLS_ERR_SSL_WANT_READ;
        }

        const size_t n = std::min(len, c->inbuf.size());
        memcpy(buf, c->inbuf.data(), n);
        c->inbuf.erase(c->inbuf.begin(), c->inbuf.begin() + n);

        return static_cast<int>(n);
      },
      nullptr
    );

    int rc = mbedtls_ssl_handshake(&c->ssl);

    if (rc == 0) {
      c->handshake_complete = true;
      return 0;
    }

    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
      return rc;
    }

    return rc;
  }

  bool Server::handshakeDone() const {
    return this->impl && this->impl->handshake_complete;
  }

  int Server::writeApp(const unsigned char* data, size_t len) {
    auto* c = this->impl;

    if (!c->handshake_complete) {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }

    return mbedtls_ssl_write(&c->ssl, data, len);
  }

  void Server::enqueueWrite(const unsigned char* data, size_t len) {
    std::vector<unsigned char> copy(len);

    if (len) {
      memcpy(copy.data(), data, len);
    }

    pendingWrites.emplace_back(std::move(copy));
  }

  void Server::flushPendingWrites() {
    auto* c = this->impl;

    if (!c || !c->handshake_complete) {
      return;
    }

    while (!pendingWrites.empty()) {
      auto& front = pendingWrites.front();
      int rc = mbedtls_ssl_write(&c->ssl, front.data(), front.size());

      if (rc > 0) {
        pendingWrites.erase(pendingWrites.begin());
        continue;
      }

      if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
        break;
      }

      pendingWrites.clear();
      break;
    }
  }

  int Server::shutdown() {
    auto* c = this->impl;
    if (!c) {
      return -1;
    }

    int rc = mbedtls_ssl_close_notify(&c->ssl);
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
      return rc;
    }

    return rc;
  }

  int Server::readOnce(unsigned char* out, size_t maxlen) {
    auto* c = this->impl;

    if (!c->handshake_complete) {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }

    return mbedtls_ssl_read(&c->ssl, out, maxlen);
  }

  int Server::getPeerSubject(types::String& subject) {
    auto* c = this->impl;
    const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&c->ssl);

    subject.clear();

    if (!crt) {
      return -1;
    }

    char buf[512] = {0};

    if (mbedtls_x509_dn_gets(buf, sizeof(buf), &crt->subject) <= 0) {
      return -1;
    }

    subject.assign(buf);
    return 0;
  }

  int Server::getSessionInfo(types::String& protocol, types::String& cipher) {
    auto* c = this->impl;

    protocol = mbedtls_ssl_get_version(&c->ssl) ?: "";
    cipher = mbedtls_ssl_get_ciphersuite(&c->ssl) ?: "";

    return 0;
  }

  int Server::getVerifyResult(unsigned long& flags, types::String& info) {
    auto* c = this->impl;

    flags = mbedtls_ssl_get_verify_result(&c->ssl);
    info.clear();

    if (flags != 0) {
      char vr[512] = {0};
      mbedtls_x509_crt_verify_info(vr, sizeof(vr), "", flags);
      info.assign(vr);
    }

    return 0;
  }

  int Server::getPeerSANs(types::Vector<types::String>& sans) {
    sans.clear();

    auto* c = this->impl;
    const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&c->ssl);

    if (!crt) {
      return -1;
    }

    // Extract SANs by parsing mbedtls_x509_crt_info output (string-based).
    char info[4096] = {0};
    if (mbedtls_x509_crt_info(info, sizeof(info), "", crt) <= 0) {
      return -1;
    }

    // Look for lines containing "subject alt name" and extract entries
    const char* p = info;
    while (*p) {
      const char* lineStart = p;
      const char* nl = strchr(p, '\n');

      if (!nl) {
        nl = p + strlen(p);
      }

      std::string line(lineStart, nl - lineStart);
      auto pos = line.find("subject alt name");
      if (pos != std::string::npos) {
        // Extract everything after ':' if present
        auto colon = line.find(':', pos);
        if (colon != std::string::npos && colon + 1 < line.size()) {
          std::string rest = line.substr(colon + 1);
          // split by ',' into tokens
          size_t start = 0;

          while (start <= rest.size()) {
            size_t comma = rest.find(',', start);
            std::string tok = rest.substr(
              start,
              comma == std::string::npos ? std::string::npos : comma - start
            );

            // trim
            auto l = tok.find_first_not_of(" \t");
            auto r = tok.find_last_not_of(" \t");

            if (l != std::string::npos && r != std::string::npos) {
              tok = tok.substr(l, r - l + 1);
            }

            if (!tok.empty()) {
              sans.push_back(tok);
            }

            if (comma == std::string::npos) {
              break;
            }

            start = comma + 1;
          }
        }
      }

      p = (*nl ? nl + 1 : nl);
    }
    return 0;
  }

  int Server::getNegotiatedALPN(types::String& alpn) {
    auto* c = this->impl;
    alpn.clear();
    #if defined(MBEDTLS_SSL_ALPN)
      const char* p = mbedtls_ssl_get_alpn_protocol(&c->ssl);
      if (p) {
        alpn.assign(p);
      }
    #endif
    return 0;
  }

  const ::oro::runtime::types::String& Server::lastErrorMessage() const {
    static ::oro::runtime::types::String empty;
    return empty;
  }

  long Server::lastErrorCode() const {
    return 0;
  }
#elif !defined(ORO_RUNTIME_TLS_OPENSSL) && !defined(ORO_RUNTIME_TLS_SCHANNEL)
  Server::Server(loop::Loop& l, const Options& o) : loop(l), options(o) {}
  Server::~Server() {}
  int Server::setup(const Options&) { return -1; }
  void Server::attachTransport(types::SharedPointer<::oro::runtime::tcp::Socket>) {}
  int Server::onEncryptedData(const unsigned char*, size_t) { return -1; }
  int Server::driveHandshake() { return -1; }
  bool Server::handshakeDone() const { return false; }
  int Server::writeApp(const unsigned char*, size_t) { return -1; }
  void Server::enqueueWrite(const unsigned char*, size_t) {}
  void Server::flushPendingWrites() {}
  int Server::readOnce(unsigned char*, size_t) { return -1; }
  int Server::getPeerSubject(types::String&) { return -1; }
  int Server::getSessionInfo(types::String&, types::String&) { return -1; }
  int Server::getVerifyResult(unsigned long& /*flags*/, types::String& /*info*/) { return -1; }
  int Server::getPeerSANs(types::Vector<types::String>& /*sans*/) { return -1; }
  int Server::getNegotiatedALPN(types::String& /*alpn*/) { return -1; }
  int Server::shutdown() { return -1; }
    const ::oro::runtime::types::String& Server::lastErrorMessage() const { static ::oro::runtime::types::String empty; return empty; }
  long Server::lastErrorCode() const { return 0; }
#endif
}
