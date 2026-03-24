#include "server.hh"
#include "../platform.hh"
#include "../tcp.hh"

#include <algorithm>
#include <vector>
#include <cstring>

#if defined(ORO_RUNTIME_TLS_OPENSSL)

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <cstdio>
#include <string>
#include <mutex>

#include "../env.hh"

namespace oro::runtime::tls {
  namespace {
    inline int mapVersion(const types::String& v) {
      if (v == "TLSv1.2" || v == "1.2") return TLS1_2_VERSION;
      if (v == "TLSv1.3" || v == "1.3") return TLS1_3_VERSION;
      return 0;
    }
    inline void configure_versions(SSL_CTX* ctx, const types::String& minV, const types::String& maxV) {
      int minv = mapVersion(minV);
      int maxv = mapVersion(maxV);
      if (minv) SSL_CTX_set_min_proto_version(ctx, minv);
      if (maxv) SSL_CTX_set_max_proto_version(ctx, maxv);
    }
    inline void set_alpn_cb(SSL_CTX* ctx, const types::Vector<types::String>& serverProtos) {
      if (serverProtos.empty()) return;
      // Copy server protocols into a contiguous buffer we can compare against client list
      struct AlpnState { std::vector<unsigned char> protos; };
      auto* state = new AlpnState();
      for (const auto& p : serverProtos) {
        if (p.size() > 255) continue;
        state->protos.push_back((unsigned char)p.size());
        state->protos.insert(state->protos.end(), p.begin(), p.end());
      }
      SSL_CTX_set_alpn_select_cb(ctx,
        [](SSL* ssl, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void* arg) -> int {
          (void)ssl;
          auto* st = static_cast<AlpnState*>(arg);
          const unsigned char* sp = st->protos.data();
          const unsigned char* se = sp + st->protos.size();
          // Iterate server-preferred list; pick first present in client list
          while (sp < se) {
            unsigned char sl = *sp++;
            if (sp + sl > se) break;
            const unsigned char* sname = sp; sp += sl;
            // Scan client's offered list
            const unsigned char* cp = in; const unsigned char* ce = in + inlen;
            while (cp < ce) {
              unsigned char cl = *cp++;
              if (cp + cl > ce) break;
              if (cl == sl && memcmp(cp, sname, sl) == 0) {
                *out = sname; *outlen = sl; return SSL_TLSEXT_ERR_OK;
              }
              cp += cl;
            }
          }
          return SSL_TLSEXT_ERR_NOACK; // no match
        },
        state
      );
    }
    inline void flushEncryptedOut(ssl_st* ssl, types::SharedPointer<::oro::runtime::tcp::Socket> transport) {
      BIO* wbio = SSL_get_wbio(ssl);
      if (!wbio || !transport) return;
      for (;;) {
        size_t pending = (size_t)BIO_ctrl_pending(wbio);
        if (!pending) break;
        const size_t kChunk = 16 * 1024;
        size_t n = std::min(kChunk, pending);
        std::vector<char> buf(n);
        int rd = BIO_read(wbio, buf.data(), (int)n);
        if (rd <= 0) break;
        transport->write(buf.data(), (size_t)rd, [](int){});
      }
    }

    inline int pemPasswordCallback(char* buf, int size, int /*rwflag*/, void* userdata) {
      if (!userdata || size <= 0) return 0;
      auto* pass = static_cast<const std::string*>(userdata);
      if (!pass) return 0;
      if (size == 1) {
        buf[0] = '\0';
        return 0;
      }
      const size_t copyCount = std::min(pass->size(), static_cast<size_t>(size - 1));
      if (copyCount > 0) {
        memcpy(buf, pass->data(), copyCount);
      }
      buf[copyCount] = '\0';
      return static_cast<int>(copyCount);
    }
  }

  struct Server::Impl {
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    BIO* rbio = nullptr;
    BIO* wbio = nullptr;
    bool initialized = false;
    bool handshake_complete = false;
    std::vector<unsigned char> inbuf;
  };

  Server::Server(loop::Loop& l, const Options& o) : loop(l), options(o) {
    this->impl = new Impl();
  }

  Server::~Server() {
    if (!this->impl) return;
    if (this->impl->ssl) SSL_free(this->impl->ssl);
    if (this->impl->ctx) SSL_CTX_free(this->impl->ctx);
    delete this->impl; this->impl = nullptr;
  }

  int Server::setup(const Options& opts) {
    this->options = opts;
    auto* c = this->impl;
    const SSL_METHOD* method = TLS_server_method();
    c->ctx = SSL_CTX_new(method);
    if (!c->ctx) return -1;
    // Optional NSS key log file for debugging (Wireshark)
    {
      static std::once_flag once;
      static std::string keylog_path;
      std::call_once(once, [](){
        keylog_path = ::oro::runtime::env::get("ORO_TLS_KEYLOG");
      });
      if (!keylog_path.empty()) {
        SSL_CTX_set_keylog_callback(c->ctx, [](const SSL* /*ssl*/, const char* line){
          if (!line) return;
          static std::mutex mu;
          static FILE* fp = nullptr;
          std::lock_guard<std::mutex> lk(mu);
          if (!fp) {
            auto path = ::oro::runtime::env::get("ORO_TLS_KEYLOG");
            if (path.empty()) return;
            fp = fopen(path.c_str(), "a");
            if (!fp) return;
          }
          fputs(line, fp);
          size_t L = strlen(line);
          if (L == 0 || line[L-1] != '\n') fputc('\n', fp);
          fflush(fp);
        });
      }
    }
    SSL_CTX_set_default_verify_paths(c->ctx);
    if (!opts.ca.empty()) {
      SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
      BIO* cab = BIO_new_mem_buf(opts.ca.data(), (int)opts.ca.size());
      if (!cab) return -1;
      X509_STORE* store = SSL_CTX_get_cert_store(c->ctx);
      int loaded = 0;
      for (;;) {
        X509* x = PEM_read_bio_X509(cab, nullptr, 0, nullptr);
        if (!x) break;
        if (X509_STORE_add_cert(store, x) == 1) loaded++;
        X509_free(x);
      }
      BIO_free(cab);
      if (loaded == 0) return -1;
    }
    configure_versions(c->ctx, opts.minVersion, opts.maxVersion);

    if (!opts.cert.empty() && !opts.key.empty()) {
      BIO* cb = BIO_new_mem_buf(opts.cert.data(), (int)opts.cert.size());
      BIO* kb = BIO_new_mem_buf(opts.key.data(), (int)opts.key.size());
      if (!cb || !kb) return -1;
      X509* x = PEM_read_bio_X509(cb, nullptr, 0, nullptr);
      std::string keyPass = opts.keyPassphrase;
      pem_password_cb* passCb = nullptr;
      void* passArg = nullptr;
      if (!keyPass.empty()) {
        passCb = pemPasswordCallback;
        passArg = &keyPass;
      }
      EVP_PKEY* pkey = PEM_read_bio_PrivateKey(kb, nullptr, passCb, passArg);
      BIO_free(cb); BIO_free(kb);
      if (!x || !pkey) { if (x) X509_free(x); if (pkey) EVP_PKEY_free(pkey); return -1; }
      if (SSL_CTX_use_certificate(c->ctx, x) != 1) { X509_free(x); EVP_PKEY_free(pkey); return -1; }
      if (SSL_CTX_use_PrivateKey(c->ctx, pkey) != 1) { X509_free(x); EVP_PKEY_free(pkey); return -1; }
      X509_free(x); EVP_PKEY_free(pkey);
    } else {
      return -1;
    }

    c->ssl = SSL_new(c->ctx);
    if (!c->ssl) return -1;
    c->rbio = BIO_new(BIO_s_mem());
    c->wbio = BIO_new(BIO_s_mem());
    if (!c->rbio || !c->wbio) return -1;
    SSL_set_bio(c->ssl, c->rbio, c->wbio);
    SSL_set_accept_state(c->ssl);
    set_alpn_cb(c->ctx, opts.alpn);
    c->initialized = true; c->handshake_complete = false;
    c->inbuf.reserve(16 * 1024);
    return 0;
  }

  void Server::attachTransport(types::SharedPointer<::oro::runtime::tcp::Socket> sock) { this->transport = sock; }

  int Server::onEncryptedData(const unsigned char* data, size_t len) {
    auto* c = this->impl;
    if (!c || !c->ssl) return -1;
    BIO_write(c->rbio, data, (int)len);
    return 0;
  }

  int Server::driveHandshake() {
    auto* c = this->impl;
    if (!c || !c->ssl) return -1;
    int rc = SSL_do_handshake(c->ssl);
    if (rc == 1) { c->handshake_complete = true; flushEncryptedOut(c->ssl, this->transport); return 0; }
    int err = SSL_get_error(c->ssl, rc);
    flushEncryptedOut(c->ssl, this->transport);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return err;
    return -1;
  }

  bool Server::handshakeDone() const { return this->impl && this->impl->handshake_complete; }

  int Server::writeApp(const unsigned char* data, size_t len) {
    auto* c = this->impl;
    if (!c || !c->handshake_complete) return -2;
    int rc = SSL_write(c->ssl, data, (int)len);
    if (rc > 0) { flushEncryptedOut(c->ssl, this->transport); return rc; }
    int err = SSL_get_error(c->ssl, rc);
    flushEncryptedOut(c->ssl, this->transport);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) { enqueueWrite(data, len); return 0; }
    return -1;
  }

  void Server::enqueueWrite(const unsigned char* data, size_t len) {
    std::vector<unsigned char> copy(len);
    if (len) memcpy(copy.data(), data, len);
    pendingWrites.emplace_back(std::move(copy));
  }

  void Server::flushPendingWrites() {
    auto* c = this->impl;
    if (!c || !c->handshake_complete) return;
    while (!pendingWrites.empty()) {
      auto& front = pendingWrites.front();
      int rc = SSL_write(c->ssl, front.data(), (int)front.size());
      flushEncryptedOut(c->ssl, this->transport);
      if (rc > 0) { pendingWrites.erase(pendingWrites.begin()); continue; }
      int err = SSL_get_error(c->ssl, rc);
      if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) break;
      pendingWrites.clear();
      break;
    }
  }

  int Server::shutdown() {
    auto* c = this->impl;
    if (!c || !c->ssl) return -1;
    int rc = SSL_shutdown(c->ssl);
    flushEncryptedOut(c->ssl, this->transport);
    if (rc == 1 || rc == 0) return 0;
    int err = SSL_get_error(c->ssl, rc);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
    return -1;
  }

  int Server::readOnce(unsigned char* out, size_t maxlen) {
    auto* c = this->impl;
    if (!c || !c->handshake_complete) return -2;
    int rc = SSL_read(c->ssl, out, (int)maxlen);
    if (rc > 0) return rc;
    int err = SSL_get_error(c->ssl, rc);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return -2;
    if (err == SSL_ERROR_ZERO_RETURN) return 0;
    return -1;
  }

  const ::oro::runtime::types::String& Server::lastErrorMessage() const {
    static ::oro::runtime::types::String empty;
    return empty;
  }

  long Server::lastErrorCode() const {
    return 0;
  }

  int Server::getPeerSubject(types::String& subject) {
    auto* c = this->impl;
    subject.clear();
    if (!c || !c->ssl) return -1;
    X509* crt = SSL_get_peer_certificate(c->ssl);
    if (!crt) return -1;
    char buf[512] = {0};
    X509_NAME_oneline(X509_get_subject_name(crt), buf, sizeof(buf));
    subject.assign(buf);
    X509_free(crt);
    return 0;
  }

  int Server::getSessionInfo(types::String& protocol, types::String& cipher) {
    auto* c = this->impl;
    if (!c || !c->ssl) return -1;
    protocol = SSL_get_version(c->ssl);
    cipher = SSL_get_cipher(c->ssl);
    return 0;
  }

  int Server::getNegotiatedALPN(types::String& alpn) {
    alpn.clear();
    auto* c = this->impl;
    if (!c || !c->ssl) {
      return -1;
    }
    const unsigned char* p = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(c->ssl, &p, &len);
    if (p && len) {
      alpn.assign(
        reinterpret_cast<const char*>(p),
        reinterpret_cast<const char*>(p) + len
      );
    }
    return 0;
  }

  int Server::getVerifyResult(unsigned long& flags, types::String& info) {
    flags = 0; info.clear();
    auto* c = this->impl;
    if (!c || !c->ssl) return -1;
    long r = SSL_get_verify_result(c->ssl);
    flags = (unsigned long) r;
    const char* s = X509_verify_cert_error_string(r);
    if (s) info.assign(s);
    return 0;
  }

  int Server::getPeerSANs(types::Vector<types::String>& sans) {
    sans.clear();
    auto* c = this->impl;
    if (!c || !c->ssl) return -1;
    X509* crt = SSL_get_peer_certificate(c->ssl);
    if (!crt) return -1;
    STACK_OF(GENERAL_NAME)* names = (STACK_OF(GENERAL_NAME)*)X509_get_ext_d2i(crt, NID_subject_alt_name, nullptr, nullptr);
    if (names) {
      int n = sk_GENERAL_NAME_num(names);
      for (int i = 0; i < n; i++) {
        const GENERAL_NAME* gn = sk_GENERAL_NAME_value(names, i);
        if (gn->type == GEN_DNS && gn->d.dNSName) {
          const unsigned char* d = ASN1_STRING_get0_data(gn->d.dNSName);
          if (d) sans.push_back(reinterpret_cast<const char*>(d));
        }
      }
      sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
    }
    X509_free(crt);
    return 0;
  }
}

#endif
