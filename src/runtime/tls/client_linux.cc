#include "client.hh"
#include "../platform.hh"
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
#  include <mbedtls/version.h>
#  include <mbedtls/sha256.h>
#endif

namespace oro::runtime::tls {
  // Linux TLS client using mbedTLS. Other providers implement Client in their
  // own translation units (e.g., client_openssl_linux.cc).

  #if defined(ORO_RUNTIME_ENABLE_MBEDTLS)

    struct Client::Impl {
      mbedtls_ssl_context ssl;
      mbedtls_ssl_config conf;
      mbedtls_ctr_drbg_context ctr_drbg;
      mbedtls_entropy_context entropy;
      mbedtls_x509_crt cacert;
      mbedtls_x509_crt client_cert;
      mbedtls_pk_context client_key;
      bool initialized = false;
      bool handshake_complete = false;
      std::vector<unsigned char> inbuf;
      std::vector<char*> alpn_ptrs;
    };

    Client::Client(loop::Loop& l, const Options& opts) : loop(l), options(opts) {
      this->impl = new Impl();
      auto* c = this->impl;
      mbedtls_ssl_init(&c->ssl);
      mbedtls_ssl_config_init(&c->conf);
      mbedtls_ctr_drbg_init(&c->ctr_drbg);
      mbedtls_entropy_init(&c->entropy);
      mbedtls_x509_crt_init(&c->cacert);
      c->inbuf.reserve(16 * 1024);
    }

    Client::~Client() {
      this->close();
    }

    using namespace oro::runtime::tls::mbedtls_helpers;

    int Client::connect(const ClientOptions& opts) {
      auto* c = this->impl;

      int rc = 0;
      // Seed PRNG
      if ((rc = seeded_ctr_drbg(&c->ctr_drbg, &c->entropy)) != 0) {
        return rc;
      }

      if ((rc = mbedtls_ssl_config_defaults(&c->conf,
              MBEDTLS_SSL_IS_CLIENT,
              MBEDTLS_SSL_TRANSPORT_STREAM,
              MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        return rc;
      }

      mbedtls_ssl_conf_authmode(&c->conf, opts.rejectUnauthorized ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
      mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->ctr_drbg);

      // Load CA bundle if provided
      if (!opts.ca.empty()) {
        if ((rc = mbedtls_x509_crt_parse(&c->cacert, reinterpret_cast<const unsigned char*>(opts.ca.c_str()), opts.ca.size() + 1)) < 0) {
          return rc;
        }
        mbedtls_ssl_conf_ca_chain(&c->conf, &c->cacert, nullptr);
      } else if (opts.rejectUnauthorized) {
        // Optional system CA fallback (Linux typical paths)
        // These calls may fail harmlessly if paths don't exist.
        (void) mbedtls_x509_crt_parse_path(&c->cacert, "/etc/ssl/certs");
        (void) mbedtls_x509_crt_parse_file(&c->cacert, "/etc/ssl/cert.pem");
        (void) mbedtls_x509_crt_parse_file(&c->cacert, "/etc/ssl/certs/ca-certificates.crt");
        if (c->cacert.raw.p) {
          mbedtls_ssl_conf_ca_chain(&c->conf, &c->cacert, nullptr);
        }
      }

      // Load client certificate/key if provided
      if (!opts.cert.empty() && !opts.key.empty()) {
        mbedtls_x509_crt_init(&c->client_cert);
        mbedtls_pk_init(&c->client_key);
        if ((rc = mbedtls_x509_crt_parse(&c->client_cert, reinterpret_cast<const unsigned char*>(opts.cert.c_str()), opts.cert.size() + 1)) < 0) {
          return rc;
        }
        const unsigned char* pass = nullptr;
        size_t passlen = 0;
        if (!opts.keyPassphrase.empty()) {
          pass = reinterpret_cast<const unsigned char*>(opts.keyPassphrase.c_str());
          passlen = opts.keyPassphrase.size();
        }
        if ((rc = mbedtls_pk_parse_key(&c->client_key, reinterpret_cast<const unsigned char*>(opts.key.c_str()), opts.key.size() + 1, pass, passlen, mbedtls_ctr_drbg_random, &c->ctr_drbg)) < 0) {
          return rc;
        }
        if ((rc = mbedtls_ssl_conf_own_cert(&c->conf, &c->client_cert, &c->client_key)) < 0) {
          return rc;
        }
      }

      if ((rc = mbedtls_ssl_setup(&c->ssl, &c->conf)) != 0) {
        return rc;
      }

      // Set SNI/hostname
      // Hostname for SNI/verification
      if (!opts.servername.empty()) {
        mbedtls_ssl_set_hostname(&c->ssl, opts.servername.c_str());
      } else if (!opts.host.empty()) {
        mbedtls_ssl_set_hostname(&c->ssl, opts.host.c_str());
      }

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
        if ((rc = mbedtls_ssl_conf_alpn_protocols(&c->conf, const_cast<const char**>(c->alpn_ptrs.data()))) != 0) {
          return rc;
        }
      }

      // TLS versions and ciphers (optional)
      configure_versions(&c->conf, opts.minVersion, opts.maxVersion);
      configure_ciphers(&c->conf, opts.ciphers);

      // BIO hooks to tcp::Socket go here (non-blocking send/recv callbacks)
      // Note: we set callbacks to instance methods via a static trampoline
      mbedtls_ssl_set_bio(&c->ssl, this,
        [](void* ctx, const unsigned char* buf, size_t len) -> int {
          auto self = static_cast<Client*>(ctx);
          if (!self->transport) return MBEDTLS_ERR_NET_SEND_FAILED;
          int err = self->write(reinterpret_cast<const char*>(buf), len);
          if (err == 0) return (int) len;
          return MBEDTLS_ERR_SSL_WANT_WRITE;
        },
        [](void* ctx, unsigned char* buf, size_t len) -> int {
          auto self = static_cast<Client*>(ctx);
          auto* c = self->impl;
          if (c->inbuf.empty()) return MBEDTLS_ERR_SSL_WANT_READ;
          const size_t n = std::min(len, c->inbuf.size());
          memcpy(buf, c->inbuf.data(), n);
          c->inbuf.erase(c->inbuf.begin(), c->inbuf.begin() + n);
          return (int) n;
        },
        nullptr
      );

      c->initialized = true;
      // Perform non-blocking handshake in event loop (drive via read/writable events)
      // For now, return success indicating configured; actual handshake to be performed incrementally.
      return 0;
    }

    int Client::write(const char* data, size_t len) {
      // Write encrypted TLS records via underlying TCP; this assumes data are records from mbedTLS send path.
      // On completion, attempt to flush any pending TLS writes.
      if (!this->transport) return -1;
      int err = this->transport->write(data, len, [this](int /*status*/){ this->flushPendingWrites(); });
      return err;
    }

    int Client::readStart() { return 0; }
    int Client::readStop() { return 0; }
    int Client::shutdown() {
      auto* c = this->impl;
      if (!c) return -1;
      int rc = mbedtls_ssl_close_notify(&c->ssl);
      if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return rc;
      return rc;
    }
    void Client::close() {
      if (this->impl) {
        for (auto p : this->impl->alpn_ptrs) { if (p) delete[] p; }
        this->impl->alpn_ptrs.clear();
        mbedtls_x509_crt_free(&this->impl->cacert);
        mbedtls_x509_crt_free(&this->impl->client_cert);
        mbedtls_pk_free(&this->impl->client_key);
        mbedtls_ssl_free(&this->impl->ssl);
        mbedtls_ssl_config_free(&this->impl->conf);
        mbedtls_ctr_drbg_free(&this->impl->ctr_drbg);
        mbedtls_entropy_free(&this->impl->entropy);
        delete this->impl; this->impl = nullptr;
      }
    }

    void Client::attachTransport(types::SharedPointer<::oro::runtime::tcp::Socket> socket) {
      this->transport = socket;
    }

    int Client::onEncryptedData(const unsigned char* data, size_t len) {
      auto* c = this->impl;
      c->inbuf.insert(c->inbuf.end(), data, data + len);
      return 0;
    }

    int Client::driveHandshake() {
      auto* c = this->impl;
      if (!c->initialized) return -1;
      int rc = 0;
      rc = mbedtls_ssl_handshake(&c->ssl);
      if (rc == 0) { c->handshake_complete = true; return 0; }
      if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return rc;
      return rc;
    }

    bool Client::handshakeDone() const { return this->impl && this->impl->handshake_complete; }

    int Client::writeApp(const unsigned char* data, size_t len) {
      auto* c = this->impl;
      if (!c->handshake_complete) return MBEDTLS_ERR_SSL_WANT_READ;
      int rc = mbedtls_ssl_write(&c->ssl, data, len);
      if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
        enqueueWrite(data, len);
        return 0;
      }
      return rc;
    }

    int Client::readOnce(unsigned char* out, size_t maxlen) {
      auto* c = this->impl;
      if (!c->handshake_complete) return MBEDTLS_ERR_SSL_WANT_READ;
      int rc = mbedtls_ssl_read(&c->ssl, out, maxlen);
      return rc;
    }

    int Client::getVerifyResult(unsigned long& flags, types::String& info) {
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

    int Client::getSessionInfo(types::String& protocol, types::String& cipher) {
      auto* c = this->impl;
      const char* p = mbedtls_ssl_get_version(&c->ssl);
      const char* cs = mbedtls_ssl_get_ciphersuite(&c->ssl);
      protocol = p ? p : "";
      cipher = cs ? cs : "";
      return 0;
    }

    int Client::getNegotiatedALPN(types::String& alpn) {
      auto* c = this->impl;
      alpn.clear();
      #if defined(MBEDTLS_SSL_ALPN)
        const char* p = mbedtls_ssl_get_alpn_protocol(&c->ssl);
        if (p) alpn.assign(p);
      #endif
      return 0;
    }

    int Client::getPeerSubject(types::String& subject) {
      auto* c = this->impl;
      const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&c->ssl);
      subject.clear();
      if (!crt) return -1;
      char buf[512] = {0};
      if (mbedtls_x509_dn_gets(buf, sizeof(buf), &crt->subject) <= 0) return -1;
      subject.assign(buf);
      return 0;
    }

    int Client::getPeerCertificateSha256(types::Vector<uint8_t>& digest) {
      digest.clear();
      auto* c = this->impl;
      if (!c || !c->handshake_complete) return -1;
      const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&c->ssl);
      if (!crt || !crt->raw.p || crt->raw.len == 0) return -1;
      unsigned char output[32] = {0};
      int rc = 0;
    #if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER < 0x03000000
      rc = mbedtls_sha256_ret(crt->raw.p, crt->raw.len, output, 0);
    #else
      rc = mbedtls_sha256(crt->raw.p, crt->raw.len, output, 0);
    #endif
      if (rc != 0) return rc;
      digest.assign(output, output + sizeof(output));
      return 0;
    }

    void Client::enqueueWrite(const unsigned char* data, size_t len) {
      std::vector<unsigned char> copy(len);
      if (len) memcpy(copy.data(), data, len);
      pendingWrites.emplace_back(std::move(copy));
    }

    void Client::flushPendingWrites() {
      auto* c = this->impl;
      if (!c || !c->handshake_complete) return;
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
        // On hard error, drop pending writes
        pendingWrites.clear();
        break;
      }
  }

  int Client::getPeerSANs(types::Vector<types::String>& sans) {
    sans.clear();
    auto* c = this->impl;
    const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&c->ssl);
    if (!crt) {
      return -1;
    }

    char info[4096] = {0};
    if (mbedtls_x509_crt_info(info, sizeof(info), "", crt) <= 0) {
      return -1;
    }

    const char* p = info;
    while (*p) {
      const char* ls = p;
      const char* nl = strchr(p, '\n');
      if (!nl) {
        nl = p + strlen(p);
      }

      std::string line(ls, nl - ls);
      auto pos = line.find("subject alt name");
      if (pos != std::string::npos) {
        auto colon = line.find(':', pos);
        if (colon != std::string::npos && colon + 1 < line.size()) {
          std::string rest = line.substr(colon + 1);
          size_t start = 0;
          while (start <= rest.size()) {
            size_t comma = rest.find(',', start);
            std::string tok = rest.substr(
              start,
              comma == std::string::npos ? std::string::npos : comma - start
            );
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

  const ::oro::runtime::types::String& Client::lastErrorMessage() const {
    static ::oro::runtime::types::String empty;
    return empty;
  }

  long Client::lastErrorCode() const {
    return 0;
  }
  #elif !defined(ORO_RUNTIME_TLS_OPENSSL) && !defined(ORO_RUNTIME_TLS_SCHANNEL)
    struct Client::Impl {};
    Client::Client(loop::Loop& l, const Options& opts) : loop(l), options(opts) { this->impl = new Impl(); }
    Client::~Client() { close(); }
    int Client::connect(const ClientOptions& /*opts*/) { return -1; }
    int Client::write(const char* /*data*/, size_t /*len*/) { return -1; }
    int Client::readStart() { return -1; }
    int Client::readStop() { return 0; }
    int Client::shutdown() { return 0; }
    void Client::close() {}
    void Client::attachTransport(types::SharedPointer<::oro::runtime::tcp::Socket> /*socket*/) {}
    int Client::onEncryptedData(const unsigned char* /*data*/, size_t /*len*/) { return -1; }
    int Client::driveHandshake() { return -1; }
    bool Client::handshakeDone() const { return false; }
    int Client::writeApp(const unsigned char* /*data*/, size_t /*len*/) { return -1; }
    void Client::enqueueWrite(const unsigned char* /*data*/, size_t /*len*/) {}
    void Client::flushPendingWrites() {}
    int Client::readOnce(unsigned char* /*out*/, size_t /*maxlen*/) { return -1; }
    int Client::getVerifyResult(unsigned long& /*flags*/, types::String& /*info*/) { return -1; }
    int Client::getSessionInfo(types::String& /*protocol*/, types::String& /*cipher*/) { return -1; }
    int Client::getPeerSubject(types::String& /*subject*/) { return -1; }
    int Client::getPeerCertificateSha256(types::Vector<uint8_t>& /*digest*/) { return -1; }
    int Client::getPeerSANs(types::Vector<types::String>& /*sans*/) { return -1; }
    int Client::getNegotiatedALPN(types::String& /*alpn*/) { return -1; }
    const ::oro::runtime::types::String& Client::lastErrorMessage() const { static ::oro::runtime::types::String empty; return empty; }
    long Client::lastErrorCode() const { return 0; }
  #endif
}
