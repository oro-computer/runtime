#include "tls.hh"
#include "../services.hh"           // concrete Services definition
#include "../../platform.hh"
#include "../../runtime.hh"
#include "../../http.hh"
#include "../../debug.hh"
#include "../../env.hh"
#include "../../crypto.hh"
#include "../../bytes.hh"
#include "../../string.hh"
// Always include the TLS client/server interfaces; platform-specific
// implementations are provided in their respective .cc files.
#include "../../tls/client.hh"
#include "../../tls/server.hh"
#include "../../tls/provider_registry.hh"

#include <algorithm>
#include <cctype>
#include <cstdio>

#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
#include <mbedtls/ssl.h>
#endif

#if defined(ORO_RUNTIME_TLS_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#endif
#if defined(ORO_RUNTIME_TLS_SCHANNEL) && ORO_RUNTIME_PLATFORM_WINDOWS
#include <wincrypt.h>
#ifndef SSL_ERROR_WANT_READ
#define SSL_ERROR_WANT_READ 2
#endif
#ifndef SSL_ERROR_WANT_WRITE
#define SSL_ERROR_WANT_WRITE 3
#endif
#endif

namespace {
  using oro::runtime::tls::Provider;

  static std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return value;
  }

  static Provider providerFromString(const std::string& input) {
    auto value = toLower(input);
    if (value == "openssl") return Provider::OpenSSL;
    if (value == "mbedtls") return Provider::mbedTLS;
    if (value == "gnutls") return Provider::GnuTLS;
    if (value == "securetransport" || value == "security") return Provider::SecureTransport;
    if (value == "schannel" || value == "windows") return Provider::Schannel;
    if (value == "android" || value == "platform") return Provider::Android;
    return Provider::Auto;
  }

  static const char* providerToString(Provider provider) {
    switch (provider) {
      case Provider::mbedTLS:
        return "mbedtls";
      case Provider::OpenSSL:
        return "openssl";
      case Provider::GnuTLS:
        return "gnutls";
      case Provider::SecureTransport:
        return "securetransport";
      case Provider::Schannel:
        return "schannel";
      case Provider::Android:
        return "android";
      case Provider::Auto:
        break;
    }
    return "auto";
  }

  static bool providerEnabled(Provider provider) {
    switch (provider) {
      case Provider::mbedTLS:
      #if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
        return true;
      #else
        break;
      #endif
      case Provider::OpenSSL:
      #if defined(ORO_RUNTIME_TLS_OPENSSL)
        return true;
      #else
        break;
      #endif
      case Provider::GnuTLS:
      #if defined(ORO_RUNTIME_TLS_GNUTLS)
        return true;
      #else
        break;
      #endif
      case Provider::SecureTransport:
      #if defined(ORO_RUNTIME_TLS_SECURETRANSPORT)
        return true;
      #else
        break;
      #endif
      case Provider::Schannel:
      #if defined(ORO_RUNTIME_TLS_SCHANNEL)
        return true;
      #else
        break;
      #endif
      case Provider::Android:
      #if defined(ORO_RUNTIME_TLS_PLATFORM_ANDROID)
        return true;
      #else
        break;
      #endif
      case Provider::Auto:
        break;
    }
    if (provider == Provider::Auto) return false;
    auto& registry = oro::runtime::tls::ProviderRegistry::instance();
    return registry.ensure(provider);
  }

  static Provider compileTimeDefaultProvider() {
#if defined(ORO_RUNTIME_TLS_SECURETRANSPORT)
    return Provider::SecureTransport;
#elif defined(ORO_RUNTIME_TLS_SCHANNEL)
    return Provider::Schannel;
#elif defined(ORO_RUNTIME_TLS_PLATFORM_ANDROID)
    return Provider::Android;
#elif defined(ORO_RUNTIME_TLS_OPENSSL)
    return Provider::OpenSSL;
#elif defined(ORO_RUNTIME_TLS_GNUTLS)
    return Provider::GnuTLS;
#elif defined(ORO_RUNTIME_ENABLE_MBEDTLS)
    return Provider::mbedTLS;
#else
    return Provider::Auto;
#endif
  }

  static Provider resolveProviderFromEnvironment() {
    const auto providerRaw = oro::runtime::env::get("ORO_TLS_PROVIDER");
    if (!providerRaw.empty()) {
      const auto requested = providerFromString(providerRaw);
      if (requested != Provider::Auto) {
        return requested;
      }
    }

    const auto runtimeProviderRaw = oro::runtime::env::get("ORO_TLS_RUNTIME_PROVIDER");
    if (!runtimeProviderRaw.empty()) {
      const auto requested = providerFromString(runtimeProviderRaw);
      if (requested != Provider::Auto) {
        return requested;
      }
    }

    // Legacy env toggles
    if (oro::runtime::env::get("ORO_ENABLE_MBEDTLS") == "1") {
      return Provider::mbedTLS;
    }

    if (oro::runtime::env::get("ORO_ENABLE_TLS") == "1") {
      auto provider = providerFromString(oro::runtime::env::get("ORO_TLS_PROVIDER"));
      if (provider != Provider::Auto) {
        return provider;
      }
    }

    auto compiled = compileTimeDefaultProvider();
    if (providerEnabled(compiled)) {
      return compiled;
    }

    // Fallback preference order
    if (providerEnabled(Provider::OpenSSL)) return Provider::OpenSSL;
    if (providerEnabled(Provider::GnuTLS)) return Provider::GnuTLS;
    if (providerEnabled(Provider::mbedTLS)) return Provider::mbedTLS;
    if (providerEnabled(Provider::SecureTransport)) return Provider::SecureTransport;
    if (providerEnabled(Provider::Schannel)) return Provider::Schannel;
    if (providerEnabled(Provider::Android)) return Provider::Android;

    return Provider::Auto;
  }
}

namespace oro::runtime::core::services {
  namespace {
    inline JSON::Array::Entries toJsonArray (const types::Vector<types::String>& values) {
      JSON::Array::Entries entries;
      entries.reserve(values.size());
      for (const auto& value : values) {
        entries.emplace_back(value);
      }
      return entries;
    }
  }

  TLS::TLS (const Options& options)
    : core::Service(options) {
    this->provider = resolveProviderFromEnvironment();
    const auto& userConfig = static_cast<runtime::Runtime&>(this->context).userConfig;
    auto it = userConfig.find("tls_pins");
    if (it == userConfig.end() || it->second.empty()) {
      it = userConfig.find("tls.pins");
    }
    if (it != userConfig.end() && !it->second.empty()) {
      this->pinsValue = it->second;
    }
    this->pins = webview::parseTlsPinConfig(this->pinsValue, true);
  }

  static inline JSON::Object notImplemented (const char* source) {
    return JSON::Object::Entries{{"source", source},{"err", JSON::Object::Entries{{"code","NOT_IMPLEMENTED"},{"message","TLS is not implemented yet"}}}};
  }

  void TLS::getPins (const String& seq, const Callback cb) {
    this->loop.dispatch([=, this]{
      const auto json = JSON::Object::Entries{
        {"source", "tls.getPins"},
        {"data", JSON::Object::Entries{
          {"value", this->pinsValue}
        }}
      };
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::setPins (const String& seq, const String& value, const Callback cb) {
    this->loop.dispatch([=, this]{
      this->pinsValue = value;
      this->pins = webview::parseTlsPinConfig(this->pinsValue, true);
      const auto json = JSON::Object::Entries{
        {"source", "tls.setPins"},
        {"data", JSON::Object::Entries{
          {"ok", true},
          {"value", this->pinsValue}
        }}
      };
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::getProvider (const String& seq, const Callback cb) {
    this->loop.dispatch([=, this]{
      const auto json = JSON::Object::Entries{
        {"source", "tls.getProvider"},
        {"data", JSON::Object::Entries{
          {"provider", providerToString(this->provider)}
        }}
      };
      cb(seq, json, QueuedResponse{});
    });
  }

  static inline bool TLS_DEBUG_ENABLED () {
    return runtime::env::get("ORO_TLS_DEBUG").size() > 0;
  }

  #if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
  static inline const char* mapVerifyFlagsToCode (unsigned long flags) {
    // Map common mbedTLS verification flags to canonical error codes
    #ifdef MBEDTLS_X509_BADCERT_CN_MISMATCH
    if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) return "HOSTNAME_MISMATCH";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_NOT_TRUSTED
    if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) return "CERT_UNTRUSTED";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_EXPIRED
    if (flags & MBEDTLS_X509_BADCERT_EXPIRED) return "CERT_EXPIRED";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_REVOKED
    if (flags & MBEDTLS_X509_BADCERT_REVOKED) return "CERT_REVOKED";
    #endif
    #ifdef MBEDTLS_X509_BADCRL_EXPIRED
    if (flags & MBEDTLS_X509_BADCRL_EXPIRED) return "CRL_EXPIRED";
    #endif
    #ifdef MBEDTLS_X509_BADCRL_NOT_TRUSTED
    if (flags & MBEDTLS_X509_BADCRL_NOT_TRUSTED) return "CRL_UNTRUSTED";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_KEY_USAGE
    if (flags & MBEDTLS_X509_BADCERT_KEY_USAGE) return "CERT_NOT_PERMITTED";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_EXT_KEY_USAGE
    if (flags & MBEDTLS_X509_BADCERT_EXT_KEY_USAGE) return "CERT_NOT_PERMITTED";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_BAD_MD
    if (flags & MBEDTLS_X509_BADCERT_BAD_MD) return "CERT_BAD_SIGNATURE_ALG";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_BAD_PK
    if (flags & MBEDTLS_X509_BADCERT_BAD_PK) return "CERT_BAD_PUBLIC_KEY";
    #endif
    #ifdef MBEDTLS_X509_BADCERT_BAD_KEY
    if (flags & MBEDTLS_X509_BADCERT_BAD_KEY) return "CERT_BAD_KEY";
    #endif
    return "CERTIFICATE_VERIFY_FAILED";
  }
  #endif
  #if defined(ORO_RUNTIME_TLS_OPENSSL)
  static inline const char* mapOpenSSLVerifyToCode (unsigned long r) {
    if (r == X509_V_OK) return "OK";

    switch (r) {
    #ifdef X509_V_ERR_HOSTNAME_MISMATCH
      case X509_V_ERR_HOSTNAME_MISMATCH:
        return "HOSTNAME_MISMATCH";
    #endif
    #ifdef X509_V_ERR_IP_ADDRESS_MISMATCH
      case X509_V_ERR_IP_ADDRESS_MISMATCH:
        return "HOSTNAME_MISMATCH";
    #endif
    #ifdef X509_V_ERR_CERT_HAS_EXPIRED
      case X509_V_ERR_CERT_HAS_EXPIRED:
        return "CERT_EXPIRED";
    #endif
    #ifdef X509_V_ERR_CERT_NOT_YET_VALID
      case X509_V_ERR_CERT_NOT_YET_VALID:
        return "CERT_NOT_TIME_VALID";
    #endif
    #ifdef X509_V_ERR_CRL_HAS_EXPIRED
      case X509_V_ERR_CRL_HAS_EXPIRED:
        return "CRL_EXPIRED";
    #endif
    #ifdef X509_V_ERR_CRL_NOT_YET_VALID
      case X509_V_ERR_CRL_NOT_YET_VALID:
        return "CRL_NOT_TIME_VALID";
    #endif
    #ifdef X509_V_ERR_CERT_REVOKED
      case X509_V_ERR_CERT_REVOKED:
        return "CERT_REVOKED";
    #endif
    #ifdef X509_V_ERR_INVALID_PURPOSE
      case X509_V_ERR_INVALID_PURPOSE:
        return "CERT_NOT_PERMITTED";
    #endif
    #ifdef X509_V_ERR_UNSUPPORTED_SIGNATURE_ALGORITHM
      case X509_V_ERR_UNSUPPORTED_SIGNATURE_ALGORITHM:
        return "CERT_BAD_SIGNATURE_ALG";
    #endif
    #ifdef X509_V_ERR_SIGNATURE_ALGORITHM_MISMATCH
      case X509_V_ERR_SIGNATURE_ALGORITHM_MISMATCH:
        return "CERT_BAD_SIGNATURE_ALG";
    #endif
    #ifdef X509_V_ERR_CERT_SIGNATURE_FAILURE
      case X509_V_ERR_CERT_SIGNATURE_FAILURE:
        return "CERT_BAD_SIGNATURE";
    #endif
    #ifdef X509_V_ERR_CRL_SIGNATURE_FAILURE
      case X509_V_ERR_CRL_SIGNATURE_FAILURE:
        return "CERT_BAD_SIGNATURE";
    #endif
    #ifdef X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE
      case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
        return "CERT_BAD_SIGNATURE";
    #endif
    #ifdef X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE
      case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
        return "CERT_BAD_SIGNATURE";
    #endif
    #ifdef X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT
      case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY
      case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT
      case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN
      case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE
      case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_UNABLE_TO_GET_CRL
      case X509_V_ERR_UNABLE_TO_GET_CRL:
        return "CRL_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_CERT_UNTRUSTED
      case X509_V_ERR_CERT_UNTRUSTED:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_INVALID_CA
      case X509_V_ERR_INVALID_CA:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_PATH_LENGTH_EXCEEDED
      case X509_V_ERR_PATH_LENGTH_EXCEEDED:
        return "CERT_UNTRUSTED";
    #endif
    #ifdef X509_V_ERR_EE_KEY_TOO_SMALL
      case X509_V_ERR_EE_KEY_TOO_SMALL:
        return "CERT_BAD_KEY";
    #endif
    #ifdef X509_V_ERR_CA_KEY_TOO_SMALL
      case X509_V_ERR_CA_KEY_TOO_SMALL:
        return "CERT_BAD_KEY";
    #endif
      default:
        break;
    }

    return "CERTIFICATE_VERIFY_FAILED";
  }
  #endif
  #if defined(ORO_RUNTIME_TLS_SCHANNEL)
  static inline const char* mapSchannelVerifyToCode (unsigned long flags) {
    if (flags == CERT_TRUST_NO_ERROR) return "OK";
    if (flags & CERT_TRUST_IS_NOT_TIME_VALID) return "CERT_NOT_TIME_VALID";
    if (flags & CERT_TRUST_IS_NOT_SIGNATURE_VALID) return "CERT_BAD_SIGNATURE";
    if (flags & CERT_TRUST_IS_UNTRUSTED_ROOT) return "CERT_UNTRUSTED_ROOT";
    if (flags & CERT_TRUST_REVOCATION_STATUS_UNKNOWN) return "CERT_REVOCATION_UNKNOWN";
    if (flags & CERT_TRUST_IS_REVOKED) return "CERT_REVOKED";
    if (flags & CERT_TRUST_IS_NOT_VALID_FOR_USAGE) return "CERT_NOT_VALID_FOR_USAGE";
    if (flags & CERT_TRUST_IS_PARTIAL_CHAIN) return "CERT_PARTIAL_CHAIN";
    return "CERTIFICATE_VERIFY_FAILED";
  }
  #endif

  static inline const char* mapVerifyCodeForProvider (tls::Provider provider, unsigned long flags) {
  #if defined(ORO_RUNTIME_TLS_SCHANNEL)
    if (provider == tls::Provider::Schannel) return mapSchannelVerifyToCode(flags);
  #endif
  #if defined(ORO_RUNTIME_TLS_OPENSSL)
    if (provider == tls::Provider::OpenSSL) return mapOpenSSLVerifyToCode(flags);
  #endif
  #if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
    if (provider == tls::Provider::mbedTLS) return mapVerifyFlagsToCode(flags);
  #endif
    return "CERTIFICATE_VERIFY_FAILED";
  }

  void TLS::connect (const String& seq, ID id, const ConnectOptions& options, const Callback cb) {
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) || defined(ORO_RUNTIME_TLS_OPENSSL) || defined(ORO_RUNTIME_TLS_SCHANNEL)
    this->loop.dispatch([=, this]{
      auto& registry = tls::ProviderRegistry::instance();
      if (!registry.ensure(this->provider)) {
        cb(seq, notImplemented("tls.connect"), QueuedResponse{});
        return;
      }

      auto tcpId = oro::runtime::crypto::rand64();
      int tcpErr = 0;
      auto tcpSock = this->services.tcp.manager.create(tcpId, tcpErr);
      if (tcpErr || !tcpSock) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(tcpErr))}}}};
        return cb(seq, json, QueuedResponse{});
      }

      auto handle = std::make_shared<Handle>();
      handle->id = id;
      handle->tcp = tcpSock;
      tls::Options tlsOpts{};
      tlsOpts.rejectUnauthorized = options.rejectUnauthorized;
      tlsOpts.provider = this->provider;

      auto clientFactory = registry.clientFactory(this->provider);
      if (!clientFactory) {
        const auto json = notImplemented("tls.connect");
        cb(seq, json, QueuedResponse{});
        return;
      }

      handle->client = clientFactory(this->loop, tlsOpts);
      if (!handle->client) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS client create failed"}}}};
        cb(seq, json, QueuedResponse{});
        return;
      }

      handle->client->attachTransport(tcpSock);
      this->setHandle(id, handle);

      const auto hostname = options.servername.empty() ? options.host : options.servername;
      auto hostKey = webview::normaliseTlsPinHost(hostname);
      if (hostKey.empty() && !options.host.empty()) {
        hostKey = webview::normaliseTlsPinHost(options.host);
      }

      String endpointKey = hostKey;
      if (!hostKey.empty() && options.port > 0) {
        if (hostKey.find(':') != String::npos) {
          endpointKey = "[" + hostKey + "]:" + std::to_string(options.port);
        } else {
          endpointKey = hostKey + ":" + std::to_string(options.port);
        }
      }
      endpointKey = webview::normaliseTlsPinEndpoint(endpointKey);
      const auto connectionPins = webview::parseTlsPinConfig(options.pins, true);
      const bool connectionPinsProvided = !oro::runtime::string::trim(options.pins).empty();
      const bool replacePins = options.pinsMode == PinsMode::Replace && connectionPinsProvided;

      struct PinExpectation {
        bool configured = false;
        bool hasHostEntry = false;
        types::Vector<types::String> expectedPins;
      };

      const auto computeExpectedPins = [=, this]() {
        PinExpectation result;
        if (hostKey.empty() && endpointKey.empty()) {
          result.configured = replacePins;
          return result;
        }

        auto globalIt = this->pins.find(endpointKey);
        if (globalIt == this->pins.end() && !hostKey.empty()) {
          globalIt = this->pins.find(hostKey);
        }

        auto connectionIt = connectionPins.find(endpointKey);
        if (connectionIt == connectionPins.end() && !hostKey.empty()) {
          connectionIt = connectionPins.find(hostKey);
        }

        const bool globalConfigured = globalIt != this->pins.end();
        const bool connectionConfigured = connectionIt != connectionPins.end();

        if (replacePins) {
          result.configured = connectionPinsProvided;
          result.hasHostEntry = connectionConfigured;
        } else {
          result.configured = globalConfigured || connectionConfigured;
          result.hasHostEntry = result.configured;
        }
        if (!result.configured) {
          return result;
        }

        const auto appendPins = [&](const webview::TlsPinList& list) {
          for (const auto& pin : list) {
            if (pin.algorithm != "sha256" || pin.value.empty()) continue;
            result.expectedPins.push_back(pin.value);
          }
        };

        if (!replacePins && globalConfigured) {
          appendPins(globalIt->second);
        }

        if (connectionConfigured) {
          appendPins(connectionIt->second);
        }

        return result;
      };

      const auto cleanupFailedConnection = [=, this]() {
        handle->reading = false;
        if (handle->tcp) {
          const auto tcpId = handle->tcp->id;
          handle->tcp->readStop();
          handle->tcp->close(nullptr);
          this->services.tcp.manager.remove(tcpId);
        }
        if (handle->client) {
          handle->client->close();
        }
        this->removeHandle(id);
      };

      const auto drainAppData = [=]() {
        if (!handle->reading || !handle->client->handshakeDone()) return;
        const size_t kChunk = 16 * 1024;
        while (handle->reading && handle->client->handshakeDone()) {
          auto out = reinterpret_cast<unsigned char*>(new char[kChunk]);
          int rn = handle->client->readOnce(out, kChunk);
          if (rn > 0) {
            http::Headers headers{{{"content-type","application/octet-stream"},{"content-length",rn}}};
            QueuedResponse post;
            post.id = oro::runtime::crypto::rand64();
            post.body.reset(out);
            post.length = rn;
            post.headers = headers.str();
            const auto j = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",rn}}}};
            cb("-1", j, post);
            continue;
          }

          delete[] reinterpret_cast<char*>(out);
          if (rn == 0) {
            const auto j = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"EOF",true}}}};
            cb("-1", j, QueuedResponse{});
            break;
          }
          if (
  #if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
            rn == MBEDTLS_ERR_SSL_WANT_READ || rn == MBEDTLS_ERR_SSL_WANT_WRITE
  #elif defined(ORO_RUNTIME_TLS_OPENSSL)
            rn == -2
  #elif defined(ORO_RUNTIME_TLS_SCHANNEL)
            rn == -2
  #else
            false
  #endif
          ) break;

          const auto j = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS read error"}}}};
          cb("-1", j, QueuedResponse{});
          break;
        }
      };

      const auto emitHandshakeSuccess = [=, this]() {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        types::String protocol, cipher, subject, alpn;
        (void) handle->client->getSessionInfo(protocol, cipher);
        (void) handle->client->getPeerSubject(subject);
        (void) handle->client->getNegotiatedALPN(alpn);
        types::Vector<types::String> sans;
        (void) handle->client->getPeerSANs(sans);
        JSON::Object::Entries data{
          {"id", std::to_string(id)},
          {"hostname", hostname},
          {"provider", providerToString(this->provider)},
          {"protocol", protocol},
          {"cipher", cipher},
          {"alpn", alpn},
          {"subject", subject},
          {"sans", toJsonArray(sans)}
        };

        types::Vector<uint8_t> digest;
        if (handle->client->getPeerCertificateSha256(digest) == 0 && !digest.empty()) {
          const auto peerPinValue = oro::runtime::bytes::base64::encode(digest);
          data["peerPin"] = String("sha256/") + peerPinValue;
        }

        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"data", data}};
        cb("-1", json, QueuedResponse{});
      };

      const auto emitVerifyFailure = [=, this](unsigned long vflags, const types::String& vinfo) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        const auto code = mapVerifyCodeForProvider(this->provider, vflags);
        types::String subject; (void) handle->client->getPeerSubject(subject);
        types::Vector<types::String> sans; (void) handle->client->getPeerSANs(sans);
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"code",code},{"message",vinfo},{"hostname",hostname},{"provider",providerToString(this->provider)},{"subject",subject},{"sans",toJsonArray(sans)}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto emitHandshakeFailure = [=, this](const char* message) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"provider",providerToString(this->provider)},{"message",message ? String(message) : String("TLS handshake failed")}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto emitPinFailure = [=, this](
        const char* code,
        const types::String& message,
        const types::String& peerPinValue,
        const types::Vector<types::String>& expectedPins
      ) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;

        types::String subject;
        types::Vector<types::String> sans;
        (void) handle->client->getPeerSubject(subject);
        (void) handle->client->getPeerSANs(sans);

        JSON::Array::Entries expected;
        expected.reserve(expectedPins.size());
        for (const auto& pin : expectedPins) {
          expected.emplace_back(String("sha256/") + pin);
        }

        JSON::Object::Entries errEntries{
          {"id", std::to_string(id)},
          {"code", code},
          {"message", message},
          {"hostname", hostname},
          {"provider", providerToString(this->provider)},
          {"subject", subject},
          {"sans", toJsonArray(sans)},
          {"expectedPins", expected}
        };

        if (!peerPinValue.empty()) {
          errEntries["peerPin"] = String("sha256/") + peerPinValue;
        }

        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err", errEntries}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto advanceHandshake = [=](int rc) {
        if (handle->handshakeEmitted) return;
        if (rc == 0 && handle->client->handshakeDone()) {
          unsigned long vflags = 0;
          types::String vinfo;
          (void) handle->client->getVerifyResult(vflags, vinfo);
          if (options.rejectUnauthorized && vflags != 0) {
            emitVerifyFailure(vflags, vinfo);
          } else {
            const auto expectation = computeExpectedPins();
            if (expectation.configured) {
              types::Vector<uint8_t> digest;
              const int prc = handle->client->getPeerCertificateSha256(digest);
              if (prc != 0 || digest.empty()) {
                emitPinFailure(
                  "PIN_UNAVAILABLE",
                  "Unable to obtain peer certificate digest for pinning",
                  "",
                  expectation.expectedPins
                );
                return;
              }

              const auto peerPinValue = oro::runtime::bytes::base64::encode(digest);

              if (expectation.expectedPins.empty()) {
                emitPinFailure(
                  "PIN_MISCONFIGURED",
                  expectation.hasHostEntry
                    ? "TLS pins are configured for this host but no valid pins were provided"
                    : "TLS pins were provided but none apply to this host",
                  peerPinValue,
                  expectation.expectedPins
                );
                return;
              }

              bool match = false;
              for (const auto& pin : expectation.expectedPins) {
                if (pin == peerPinValue) {
                  match = true;
                  break;
                }
              }

              if (!match) {
                emitPinFailure(
                  "PIN_MISMATCH",
                  "TLS pin mismatch",
                  peerPinValue,
                  expectation.expectedPins
                );
                return;
              }
            }

            emitHandshakeSuccess();
            if (handle->reading) drainAppData();
          }
        } else {
  #if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
          if (rc != 0 && rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            emitHandshakeFailure("TLS handshake failed");
          }
  #elif defined(ORO_RUNTIME_TLS_OPENSSL)
          if (rc != 0 && rc != SSL_ERROR_WANT_READ && rc != SSL_ERROR_WANT_WRITE) {
            emitHandshakeFailure("TLS handshake failed");
          }
  #elif defined(ORO_RUNTIME_TLS_SCHANNEL)
          if (rc != 0 && rc != SSL_ERROR_WANT_READ && rc != SSL_ERROR_WANT_WRITE) {
            emitHandshakeFailure("TLS handshake failed");
          }
  #else
          if (rc != 0) {
            emitHandshakeFailure("TLS handshake failed");
          }
  #endif
        }
      };

      const int err = tcpSock->connect(options.host, options.port, [=](int status) {
        if (status) {
          const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(status))}}}};
          cb("-1", json, QueuedResponse{});
          cleanupFailedConnection();
          return;
        }

        tls::ClientOptions copts{};
        copts.host = options.host;
        copts.port = options.port;
        copts.rejectUnauthorized = options.rejectUnauthorized;
        copts.servername = options.servername.empty() ? options.host : options.servername;
        copts.ca = options.ca;
        copts.cert = options.cert;
        copts.key = options.key;
        copts.keyPassphrase = options.keyPassphrase;
        copts.alpn = options.alpn;
        copts.minVersion = options.minVersion;
        copts.maxVersion = options.maxVersion;
        copts.ciphers = options.ciphers;

        const int rc = handle->client->connect(copts);
        if (rc) {
          auto messageText = handle->client->lastErrorMessage();
          if (messageText.empty()) messageText = "TLS setup failed";
          JSON::Object::Entries errEntries{{"id", std::to_string(id)}, {"message", messageText}};
          if (auto code = handle->client->lastErrorCode()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(code));
            errEntries["code"] = String(buf);
          }
          const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",errEntries}};
          cb("-1", json, QueuedResponse{});
          cleanupFailedConnection();
          return;
        }

        int hrc = handle->client->driveHandshake();
        handle->client->flushPendingWrites();
        advanceHandshake(hrc);

        handle->tcp->readStart([=](ssize_t nread, const uv_buf_t* buf){
          if (nread > 0 && buf && buf->base) {
            handle->client->onEncryptedData(reinterpret_cast<unsigned char*>(buf->base), static_cast<size_t>(nread));
            int rc = handle->client->driveHandshake();
            handle->client->flushPendingWrites();
            advanceHandshake(rc);
            drainAppData();
            delete[] buf->base;
            return;
          }

          if (nread == 0 && buf == nullptr) {
            const auto json = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"EOF",true}}}};
            cb("-1", json, QueuedResponse{});
            return;
          }

          if (nread < 0) {
            const auto json = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror((int)nread))}}}};
            cb("-1", json, QueuedResponse{});
          }

          if (buf && buf->base) delete[] buf->base;
        });
      });

      cb(seq, JSON::Object::Entries{{"source","tls.connect"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}}, QueuedResponse{});
      if (err) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      }
    });
#elif defined(ORO_RUNTIME_TLS_OPENSSL)
    this->loop.dispatch([=, this]{
      auto& registry = tls::ProviderRegistry::instance();
      if (!registry.ensure(this->provider)) {
        cb(seq, notImplemented("tls.connect"), QueuedResponse{});
        return;
      }

      auto tcpId = oro::runtime::crypto::rand64();
      int tcpErr = 0;
      auto tcpSock = this->services.tcp.manager.create(tcpId, tcpErr);
      if (tcpErr || !tcpSock) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(tcpErr))}}}};
        return cb(seq, json, QueuedResponse{});
      }

      auto handle = std::make_shared<Handle>();
      handle->id = id;
      handle->tcp = tcpSock;
      tls::Options tlsOpts{};
      tlsOpts.rejectUnauthorized = options.rejectUnauthorized;
      tlsOpts.provider = this->provider;

      auto clientFactory = registry.clientFactory(this->provider);
      if (!clientFactory) {
        cb(seq, notImplemented("tls.connect"), QueuedResponse{});
        return;
      }

      handle->client = clientFactory(this->loop, tlsOpts);
      if (!handle->client) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS client create failed"}}}};
        cb(seq, json, QueuedResponse{});
        return;
      }

      handle->client->attachTransport(tcpSock);
      this->setHandle(id, handle);

      const auto hostname = options.servername.empty() ? options.host : options.servername;
      auto hostKey = webview::normaliseTlsPinHost(hostname);
      if (hostKey.empty() && !options.host.empty()) {
        hostKey = webview::normaliseTlsPinHost(options.host);
      }

      String endpointKey = hostKey;
      if (!hostKey.empty() && options.port > 0) {
        if (hostKey.find(':') != String::npos) {
          endpointKey = "[" + hostKey + "]:" + std::to_string(options.port);
        } else {
          endpointKey = hostKey + ":" + std::to_string(options.port);
        }
      }
      endpointKey = webview::normaliseTlsPinEndpoint(endpointKey);
      const auto connectionPins = webview::parseTlsPinConfig(options.pins, true);
      const bool connectionPinsProvided = !oro::runtime::string::trim(options.pins).empty();
      const bool replacePins = options.pinsMode == PinsMode::Replace && connectionPinsProvided;

      struct PinExpectation {
        bool configured = false;
        bool hasHostEntry = false;
        types::Vector<types::String> expectedPins;
      };

      const auto computeExpectedPins = [=, this]() {
        PinExpectation result;
        if (hostKey.empty() && endpointKey.empty()) {
          result.configured = replacePins;
          return result;
        }

        auto globalIt = this->pins.find(endpointKey);
        if (globalIt == this->pins.end() && !hostKey.empty()) {
          globalIt = this->pins.find(hostKey);
        }

        auto connectionIt = connectionPins.find(endpointKey);
        if (connectionIt == connectionPins.end() && !hostKey.empty()) {
          connectionIt = connectionPins.find(hostKey);
        }

        const bool globalConfigured = globalIt != this->pins.end();
        const bool connectionConfigured = connectionIt != connectionPins.end();

        if (replacePins) {
          result.configured = connectionPinsProvided;
          result.hasHostEntry = connectionConfigured;
        } else {
          result.configured = globalConfigured || connectionConfigured;
          result.hasHostEntry = result.configured;
        }
        if (!result.configured) {
          return result;
        }

        const auto appendPins = [&](const webview::TlsPinList& list) {
          for (const auto& pin : list) {
            if (pin.algorithm != "sha256" || pin.value.empty()) continue;
            result.expectedPins.push_back(pin.value);
          }
        };

        if (!replacePins && globalConfigured) {
          appendPins(globalIt->second);
        }

        if (connectionConfigured) {
          appendPins(connectionIt->second);
        }

        return result;
      };

      const auto cleanupFailedConnection = [=, this]() {
        handle->reading = false;
        if (handle->tcp) {
          const auto tcpId = handle->tcp->id;
          handle->tcp->readStop();
          handle->tcp->close(nullptr);
          this->services.tcp.manager.remove(tcpId);
        }
        if (handle->client) {
          handle->client->close();
        }
        this->removeHandle(id);
      };

      const auto drainAppData = [=]() {
        if (!handle->reading || !handle->client->handshakeDone()) return;
        const size_t kChunk = 16 * 1024;
        while (handle->reading && handle->client->handshakeDone()) {
          auto out = reinterpret_cast<unsigned char*>(new char[kChunk]);
          int rn = handle->client->readOnce(out, kChunk);
          if (rn > 0) {
            http::Headers headers{{{"content-type","application/octet-stream"},{"content-length",rn}}};
            QueuedResponse post; post.id = oro::runtime::crypto::rand64(); post.body.reset(out); post.length = rn; post.headers = headers.str();
            const auto j = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",rn}}}};
            cb("-1", j, post);
            continue;
          }

          delete[] reinterpret_cast<char*>(out);
          if (rn == 0) {
            const auto j = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"EOF",true}}}};
            cb("-1", j, QueuedResponse{});
            break;
          }
          if (rn == -2) break;

          const auto j = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS read error"}}}};
          cb("-1", j, QueuedResponse{});
          break;
        }
      };

      const auto emitHandshakeSuccess = [=, this]() {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        types::String protocol, cipher, subject;
        (void) handle->client->getSessionInfo(protocol, cipher);
        (void) handle->client->getPeerSubject(subject);
        types::String alpn; (void) handle->client->getNegotiatedALPN(alpn);
        types::Vector<types::String> sans; (void) handle->client->getPeerSANs(sans);
        JSON::Object::Entries data{
          {"id", std::to_string(id)},
          {"hostname", hostname},
          {"provider", providerToString(this->provider)},
          {"protocol", protocol},
          {"cipher", cipher},
          {"alpn", alpn},
          {"subject", subject},
          {"sans", toJsonArray(sans)}
        };

        types::Vector<uint8_t> digest;
        if (handle->client->getPeerCertificateSha256(digest) == 0 && !digest.empty()) {
          const auto peerPinValue = oro::runtime::bytes::base64::encode(digest);
          data["peerPin"] = String("sha256/") + peerPinValue;
        }

        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"data", data}};
        cb("-1", json, QueuedResponse{});
      };

      const auto emitVerifyFailure = [=, this](unsigned long vflags, const types::String& vinfo) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        const auto code = mapVerifyCodeForProvider(this->provider, vflags);
        types::String subject; (void) handle->client->getPeerSubject(subject);
        types::Vector<types::String> sans; (void) handle->client->getPeerSANs(sans);
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"code",code},{"message",vinfo},{"hostname",hostname},{"provider",providerToString(this->provider)},{"subject",subject},{"sans",toJsonArray(sans)}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto emitHandshakeFailure = [=, this](const char* message) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"provider",providerToString(this->provider)},{"message",message ? String(message) : String("TLS handshake failed")}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto emitPinFailure = [=, this](
        const char* code,
        const types::String& message,
        const types::String& peerPinValue,
        const types::Vector<types::String>& expectedPins
      ) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;

        types::String subject;
        types::Vector<types::String> sans;
        (void) handle->client->getPeerSubject(subject);
        (void) handle->client->getPeerSANs(sans);

        JSON::Array::Entries expected;
        expected.reserve(expectedPins.size());
        for (const auto& pin : expectedPins) {
          expected.emplace_back(String("sha256/") + pin);
        }

        JSON::Object::Entries errEntries{
          {"id", std::to_string(id)},
          {"code", code},
          {"message", message},
          {"hostname", hostname},
          {"provider", providerToString(this->provider)},
          {"subject", subject},
          {"sans", toJsonArray(sans)},
          {"expectedPins", expected}
        };

        if (!peerPinValue.empty()) {
          errEntries["peerPin"] = String("sha256/") + peerPinValue;
        }

        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err", errEntries}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto advanceHandshake = [=](int rc) {
        if (handle->handshakeEmitted) return;
        if (rc == 0 && handle->client->handshakeDone()) {
          unsigned long vflags = 0;
          types::String vinfo;
          (void) handle->client->getVerifyResult(vflags, vinfo);
          if (options.rejectUnauthorized && vflags != 0) {
            emitVerifyFailure(vflags, vinfo);
          } else {
            const auto expectation = computeExpectedPins();
            if (expectation.configured) {
              types::Vector<uint8_t> digest;
              const int prc = handle->client->getPeerCertificateSha256(digest);
              if (prc != 0 || digest.empty()) {
                emitPinFailure(
                  "PIN_UNAVAILABLE",
                  "Unable to obtain peer certificate digest for pinning",
                  "",
                  expectation.expectedPins
                );
                return;
              }

              const auto peerPinValue = oro::runtime::bytes::base64::encode(digest);

              if (expectation.expectedPins.empty()) {
                emitPinFailure(
                  "PIN_MISCONFIGURED",
                  expectation.hasHostEntry
                    ? "TLS pins are configured for this host but no valid pins were provided"
                    : "TLS pins were provided but none apply to this host",
                  peerPinValue,
                  expectation.expectedPins
                );
                return;
              }

              bool match = false;
              for (const auto& pin : expectation.expectedPins) {
                if (pin == peerPinValue) {
                  match = true;
                  break;
                }
              }

              if (!match) {
                emitPinFailure(
                  "PIN_MISMATCH",
                  "TLS pin mismatch",
                  peerPinValue,
                  expectation.expectedPins
                );
                return;
              }
            }

            emitHandshakeSuccess();
            if (handle->reading) drainAppData();
          }
        } else if (rc != 0 && rc != SSL_ERROR_WANT_READ && rc != SSL_ERROR_WANT_WRITE) {
          emitHandshakeFailure("TLS handshake failed");
        }
      };

      const int err = tcpSock->connect(options.host, options.port, [=](int status) {
        if (status) {
          const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(status))}}}};
          cb("-1", json, QueuedResponse{});
          cleanupFailedConnection();
          return;
        }

        tls::ClientOptions copts{};
        copts.host = options.host;
        copts.port = options.port;
        copts.rejectUnauthorized = options.rejectUnauthorized;
        copts.servername = options.servername.empty() ? options.host : options.servername;
        copts.ca = options.ca;
        copts.cert = options.cert;
        copts.key = options.key;
        copts.keyPassphrase = options.keyPassphrase;
        copts.alpn = options.alpn;
        copts.minVersion = options.minVersion;
        copts.maxVersion = options.maxVersion;
        copts.ciphers = options.ciphers;

        const int rc = handle->client->connect(copts);
        if (rc) {
          const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS setup failed"}}}};
          cb("-1", json, QueuedResponse{});
          cleanupFailedConnection();
          return;
        }

        int hrc = handle->client->driveHandshake();
        handle->client->flushPendingWrites();
        advanceHandshake(hrc);

        handle->tcp->readStart([=](ssize_t nread, const uv_buf_t* buf){
          if (nread > 0 && buf && buf->base) {
            handle->client->onEncryptedData(reinterpret_cast<unsigned char*>(buf->base), static_cast<size_t>(nread));
            int rc = handle->client->driveHandshake();
            handle->client->flushPendingWrites();
            advanceHandshake(rc);
            drainAppData();
            delete[] buf->base;
            return;
          }

          if (nread == 0 && buf == nullptr) {
            const auto json = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"EOF",true}}}};
            cb("-1", json, QueuedResponse{});
            return;
          }

          if (nread < 0) {
            const auto json = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror((int)nread))}}}};
            cb("-1", json, QueuedResponse{});
          }

          if (buf && buf->base) delete[] buf->base;
        });
      });

      cb(seq, JSON::Object::Entries{{"source","tls.connect"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}}, QueuedResponse{});
      if (err) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      }
    });
#elif defined(ORO_RUNTIME_TLS_SCHANNEL)
    this->loop.dispatch([=, this]{
      auto& registry = tls::ProviderRegistry::instance();
      if (!registry.ensure(this->provider)) {
        cb(seq, notImplemented("tls.connect"), QueuedResponse{});
        return;
      }

      auto tcpId = oro::runtime::crypto::rand64();
      int tcpErr = 0;
      auto tcpSock = this->services.tcp.manager.create(tcpId, tcpErr);
      if (tcpErr || !tcpSock) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(tcpErr))}}}};
        return cb(seq, json, QueuedResponse{});
      }

      auto handle = std::make_shared<Handle>();
      handle->id = id;
      handle->tcp = tcpSock;
      tls::Options tlsOpts{};
      tlsOpts.rejectUnauthorized = options.rejectUnauthorized;
      tlsOpts.provider = this->provider;

      auto clientFactory = registry.clientFactory(this->provider);
      if (!clientFactory) {
        cb(seq, notImplemented("tls.connect"), QueuedResponse{});
        return;
      }

      handle->client = clientFactory(this->loop, tlsOpts);
      if (!handle->client) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS client create failed"}}}};
        cb(seq, json, QueuedResponse{});
        return;
      }

      handle->client->attachTransport(tcpSock);
      this->setHandle(id, handle);

      const auto hostname = options.servername.empty() ? options.host : options.servername;
      auto hostKey = webview::normaliseTlsPinHost(hostname);
      if (hostKey.empty() && !options.host.empty()) {
        hostKey = webview::normaliseTlsPinHost(options.host);
      }

      String endpointKey = hostKey;
      if (!hostKey.empty() && options.port > 0) {
        if (hostKey.find(':') != String::npos) {
          endpointKey = "[" + hostKey + "]:" + std::to_string(options.port);
        } else {
          endpointKey = hostKey + ":" + std::to_string(options.port);
        }
      }
      endpointKey = webview::normaliseTlsPinEndpoint(endpointKey);
      const auto connectionPins = webview::parseTlsPinConfig(options.pins, true);
      const bool connectionPinsProvided = !oro::runtime::string::trim(options.pins).empty();
      const bool replacePins = options.pinsMode == PinsMode::Replace && connectionPinsProvided;

      struct PinExpectation {
        bool configured = false;
        bool hasHostEntry = false;
        types::Vector<types::String> expectedPins;
      };

      const auto computeExpectedPins = [=, this]() {
        PinExpectation result;
        if (hostKey.empty() && endpointKey.empty()) {
          result.configured = replacePins;
          return result;
        }

        auto globalIt = this->pins.find(endpointKey);
        if (globalIt == this->pins.end() && !hostKey.empty()) {
          globalIt = this->pins.find(hostKey);
        }

        auto connectionIt = connectionPins.find(endpointKey);
        if (connectionIt == connectionPins.end() && !hostKey.empty()) {
          connectionIt = connectionPins.find(hostKey);
        }

        const bool globalConfigured = globalIt != this->pins.end();
        const bool connectionConfigured = connectionIt != connectionPins.end();

        if (replacePins) {
          result.configured = connectionPinsProvided;
          result.hasHostEntry = connectionConfigured;
        } else {
          result.configured = globalConfigured || connectionConfigured;
          result.hasHostEntry = result.configured;
        }
        if (!result.configured) {
          return result;
        }

        const auto appendPins = [&](const webview::TlsPinList& list) {
          for (const auto& pin : list) {
            if (pin.algorithm != "sha256" || pin.value.empty()) continue;
            result.expectedPins.push_back(pin.value);
          }
        };

        if (!replacePins && globalConfigured) {
          appendPins(globalIt->second);
        }

        if (connectionConfigured) {
          appendPins(connectionIt->second);
        }

        return result;
      };

      const auto cleanupFailedConnection = [=, this]() {
        handle->reading = false;
        if (handle->tcp) {
          const auto tcpId = handle->tcp->id;
          handle->tcp->readStop();
          handle->tcp->close(nullptr);
          this->services.tcp.manager.remove(tcpId);
        }
        if (handle->client) {
          handle->client->close();
        }
        this->removeHandle(id);
      };

      const auto drainAppData = [=]() {
        if (!handle->reading || !handle->client->handshakeDone()) return;
        const size_t kChunk = 16 * 1024;
        while (handle->reading && handle->client->handshakeDone()) {
          auto out = reinterpret_cast<unsigned char*>(new char[kChunk]);
          int rn = handle->client->readOnce(out, kChunk);
          if (rn > 0) {
            http::Headers headers{{{"content-type","application/octet-stream"},{"content-length",rn}}};
            QueuedResponse post; post.id = oro::runtime::crypto::rand64(); post.body.reset(out); post.length = rn; post.headers = headers.str();
            const auto j = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",rn}}}};
            cb("-1", j, post);
            continue;
          }

          delete[] reinterpret_cast<char*>(out);
          if (rn == 0) {
            const auto j = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"EOF",true}}}};
            cb("-1", j, QueuedResponse{});
            break;
          }
          if (rn == SSL_ERROR_WANT_READ || rn == SSL_ERROR_WANT_WRITE) break;

          const auto j = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS read error"}}}};
          cb("-1", j, QueuedResponse{});
          break;
        }
      };

      const auto emitHandshakeSuccess = [=, this]() {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        types::String protocol, cipher, subject;
        (void) handle->client->getSessionInfo(protocol, cipher);
        (void) handle->client->getPeerSubject(subject);
        types::String alpn; (void) handle->client->getNegotiatedALPN(alpn);
        types::Vector<types::String> sans; (void) handle->client->getPeerSANs(sans);
        JSON::Object::Entries data{
          {"id", std::to_string(id)},
          {"hostname", hostname},
          {"provider", providerToString(this->provider)},
          {"protocol", protocol},
          {"cipher", cipher},
          {"alpn", alpn},
          {"subject", subject},
          {"sans", toJsonArray(sans)}
        };

        types::Vector<uint8_t> digest;
        if (handle->client->getPeerCertificateSha256(digest) == 0 && !digest.empty()) {
          const auto peerPinValue = oro::runtime::bytes::base64::encode(digest);
          data["peerPin"] = String("sha256/") + peerPinValue;
        }

        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"data", data}};
        cb("-1", json, QueuedResponse{});
      };

      const auto emitVerifyFailure = [=, this](unsigned long vflags, const types::String& vinfo) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        const auto code = mapVerifyCodeForProvider(this->provider, vflags);
        types::String subject; (void) handle->client->getPeerSubject(subject);
        types::Vector<types::String> sans; (void) handle->client->getPeerSANs(sans);
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"code",code},{"message",vinfo},{"hostname",hostname},{"provider",providerToString(this->provider)},{"subject",subject},{"sans",toJsonArray(sans)}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto emitHandshakeFailure = [=, this](const char* message) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"provider",providerToString(this->provider)},{"message",message ? String(message) : String("TLS handshake failed")}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto emitPinFailure = [=, this](
        const char* code,
        const types::String& message,
        const types::String& peerPinValue,
        const types::Vector<types::String>& expectedPins
      ) {
        if (handle->handshakeEmitted) return;
        handle->handshakeEmitted = true;

        types::String subject;
        types::Vector<types::String> sans;
        (void) handle->client->getPeerSubject(subject);
        (void) handle->client->getPeerSANs(sans);

        JSON::Array::Entries expected;
        expected.reserve(expectedPins.size());
        for (const auto& pin : expectedPins) {
          expected.emplace_back(String("sha256/") + pin);
        }

        JSON::Object::Entries errEntries{
          {"id", std::to_string(id)},
          {"code", code},
          {"message", message},
          {"hostname", hostname},
          {"provider", providerToString(this->provider)},
          {"subject", subject},
          {"sans", toJsonArray(sans)},
          {"expectedPins", expected}
        };

        if (!peerPinValue.empty()) {
          errEntries["peerPin"] = String("sha256/") + peerPinValue;
        }

        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err", errEntries}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      };

      const auto advanceHandshake = [=](int rc) {
        if (handle->handshakeEmitted) return;
        if (rc == 0 && handle->client->handshakeDone()) {
          unsigned long vflags = 0;
          types::String vinfo;
          (void) handle->client->getVerifyResult(vflags, vinfo);
          if (options.rejectUnauthorized && vflags != 0) {
            emitVerifyFailure(vflags, vinfo);
          } else {
            const auto expectation = computeExpectedPins();
            if (expectation.configured) {
              types::Vector<uint8_t> digest;
              const int prc = handle->client->getPeerCertificateSha256(digest);
              if (prc != 0 || digest.empty()) {
                emitPinFailure(
                  "PIN_UNAVAILABLE",
                  "Unable to obtain peer certificate digest for pinning",
                  "",
                  expectation.expectedPins
                );
                return;
              }

              const auto peerPinValue = oro::runtime::bytes::base64::encode(digest);

              if (expectation.expectedPins.empty()) {
                emitPinFailure(
                  "PIN_MISCONFIGURED",
                  expectation.hasHostEntry
                    ? "TLS pins are configured for this host but no valid pins were provided"
                    : "TLS pins were provided but none apply to this host",
                  peerPinValue,
                  expectation.expectedPins
                );
                return;
              }

              bool match = false;
              for (const auto& pin : expectation.expectedPins) {
                if (pin == peerPinValue) {
                  match = true;
                  break;
                }
              }

              if (!match) {
                emitPinFailure(
                  "PIN_MISMATCH",
                  "TLS pin mismatch",
                  peerPinValue,
                  expectation.expectedPins
                );
                return;
              }
            }

            emitHandshakeSuccess();
            if (handle->reading) drainAppData();
          }
        } else if (rc != 0 && rc != SSL_ERROR_WANT_READ && rc != SSL_ERROR_WANT_WRITE) {
          emitHandshakeFailure("TLS handshake failed");
        }
      };

      const int err = tcpSock->connect(options.host, options.port, [=](int status) {
        if (status) {
          const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(status))}}}};
          cb("-1", json, QueuedResponse{});
          cleanupFailedConnection();
          return;
        }

        tls::ClientOptions copts{};
        copts.host = options.host;
        copts.port = options.port;
        copts.rejectUnauthorized = options.rejectUnauthorized;
        copts.servername = options.servername.empty() ? options.host : options.servername;
        copts.ca = options.ca;
        copts.cert = options.cert;
        copts.key = options.key;
        copts.keyPassphrase = options.keyPassphrase;
        copts.alpn = options.alpn;
        copts.minVersion = options.minVersion;
        copts.maxVersion = options.maxVersion;
        copts.ciphers = options.ciphers;

        const int rc = handle->client->connect(copts);
        if (rc) {
          auto messageText = handle->client->lastErrorMessage();
          if (messageText.empty()) messageText = "TLS setup failed";
          JSON::Object::Entries errEntries{{"id", std::to_string(id)}, {"message", messageText}};
          if (auto code = handle->client->lastErrorCode()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(code));
            errEntries["code"] = String(buf);
          }
          const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",errEntries}};
          cb("-1", json, QueuedResponse{});
          cleanupFailedConnection();
          return;
        }

        int hrc = handle->client->driveHandshake();
        handle->client->flushPendingWrites();
        advanceHandshake(hrc);

        handle->tcp->readStart([=](ssize_t nread, const uv_buf_t* buf){
          if (nread > 0 && buf && buf->base) {
            handle->client->onEncryptedData(reinterpret_cast<unsigned char*>(buf->base), static_cast<size_t>(nread));
            int rc = handle->client->driveHandshake();
            handle->client->flushPendingWrites();
            advanceHandshake(rc);
            drainAppData();
            delete[] buf->base;
            return;
          }

          if (nread == 0 && buf == nullptr) {
            const auto json = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"EOF",true}}}};
            cb("-1", json, QueuedResponse{});
            return;
          }

          if (nread < 0) {
            const auto json = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror((int)nread))}}}};
            cb("-1", json, QueuedResponse{});
          }

          if (buf && buf->base) delete[] buf->base;
        });
      });

      cb(seq, JSON::Object::Entries{{"source","tls.connect"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}}, QueuedResponse{});
      if (err) {
        const auto json = JSON::Object::Entries{{"source","tls.connect"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        cb("-1", json, QueuedResponse{});
        cleanupFailedConnection();
      }
    });
#else
    cb(seq, notImplemented("tls.connect"), QueuedResponse{});
#endif
  }

  void TLS::write (const String& seq, ID id, SharedPointer<unsigned char[]> bytes, size_t size, const Callback cb) {
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      int rc = 0;
      if (h->client && h->client->handshakeDone()) {
        rc = h->client->writeApp(bytes.get(), size);
      } else if (h->server && h->server->handshakeDone()) {
        rc = h->server->writeApp(bytes.get(), size);
      } else {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","HandshakeNotComplete"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      if (rc > 0) {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",rc}}}};
        cb(seq, json, QueuedResponse{});
      } else if (rc == 0) {
        if (h->client && h->client->handshakeDone()) {
          h->client->flushPendingWrites();
        }
        if (h->server && h->server->handshakeDone()) {
          h->server->flushPendingWrites();
        }
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",0}}}};
        cb(seq, json, QueuedResponse{});
      } else if (rc == MBEDTLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_WANT_READ) {
        if (h->client && h->client->handshakeDone()) { h->client->enqueueWrite(bytes.get(), size); h->client->flushPendingWrites(); }
        if (h->server && h->server->handshakeDone()) { h->server->enqueueWrite(bytes.get(), size); h->server->flushPendingWrites(); }
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",0}}}};
        cb(seq, json, QueuedResponse{});
      } else {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS write error"}}}};
        cb(seq, json, QueuedResponse{});
      }
    });
#elif defined(ORO_RUNTIME_TLS_OPENSSL)
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      int rc = 0;
      if (h->client && h->client->handshakeDone()) {
        rc = h->client->writeApp(bytes.get(), size);
      } else if (h->server && h->server->handshakeDone()) {
        rc = h->server->writeApp(bytes.get(), size);
      } else {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","HandshakeNotComplete"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      if (rc > 0) {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",rc}}}};
        cb(seq, json, QueuedResponse{});
      } else if (rc == 0) {
        if (h->client && h->client->handshakeDone()) { h->client->flushPendingWrites(); }
        if (h->server && h->server->handshakeDone()) { h->server->flushPendingWrites(); }
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",0}}}};
        cb(seq, json, QueuedResponse{});
      } else {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS write error"}}}};
        cb(seq, json, QueuedResponse{});
      }
    });
#elif defined(ORO_RUNTIME_TLS_SCHANNEL)
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      int rc = 0;
      if (h->client && h->client->handshakeDone()) {
        rc = h->client->writeApp(bytes.get(), size);
      } else if (h->server && h->server->handshakeDone()) {
        rc = h->server->writeApp(bytes.get(), size);
      } else {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","HandshakeNotComplete"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      if (rc > 0) {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",rc}}}};
        cb(seq, json, QueuedResponse{});
      } else if (rc == 0) {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"data",JSON::Object::Entries{{"id",std::to_string(id)},{"bytes",0}}}};
        cb(seq, json, QueuedResponse{});
      } else {
        const auto json = JSON::Object::Entries{{"source","tls.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS write error"}}}};
        cb(seq, json, QueuedResponse{});
      }
    });
#else
    cb(seq, notImplemented("tls.write"), QueuedResponse{});
#endif
  }

  void TLS::readStart (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.readStart"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      h->reading = true;
      const auto json = JSON::Object::Entries{{"source","tls.readStart"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::readStop (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.readStop"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      h->reading = false;
      const auto json = JSON::Object::Entries{{"source","tls.readStop"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::shutdown (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.shutdown"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }

      int rc = -1;
      if (h->client) {
        rc = h->client->shutdown();
        h->client->flushPendingWrites();
      } else if (h->server) {
        rc = h->server->shutdown();
        h->server->flushPendingWrites();
      }

      bool ok = (rc == 0);
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
      if (!ok && this->provider == tls::Provider::mbedTLS && (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)) ok = true;
#endif
#if defined(ORO_RUNTIME_TLS_OPENSSL)
      if (!ok && this->provider == tls::Provider::OpenSSL && rc == 0) ok = true;
#endif

      if (ok) {
        const auto json = JSON::Object::Entries{{"source","tls.shutdown"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
        cb(seq, json, QueuedResponse{});
      } else {
        const auto json = JSON::Object::Entries{{"source","tls.shutdown"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","TLS shutdown failed"}}}};
        cb(seq, json, QueuedResponse{});
      }
    });
  }

  void TLS::close (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (h) {
        if (h->tcp) h->tcp->close(nullptr);
        if (h->client) h->client->close();
        this->removeHandle(id);
      }
      cb(seq, JSON::Object::Entries{{"source","tls.close"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}}, QueuedResponse{});
    });
  }

  // --- TLS Server (stubs) ---
  void TLS::serverCreate (const String& seq, ID id, const ServerOptions& options, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = std::make_shared<ServerHandle>();
      h->id = id;
      int tcpErr = 0;
      h->tcp = this->services.tcp.manager.create(id, tcpErr);
      if (tcpErr || !h->tcp) {
        const auto json = JSON::Object::Entries{{"source","tls.server.create"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(tcpErr))}}}};
        return cb(seq, json, QueuedResponse{});
      }
      auto stored = options;
      stored.provider = this->provider;
      h->options = stored;
      this->setServer(id, h);
      const auto json = JSON::Object::Entries{{"source","tls.server.create"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::serverBind (const String& seq, ID id, const String& address, int port, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getServer(id);
      if (!h || !h->tcp) {
        const auto json = JSON::Object::Entries{{"source","tls.server.bind"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = h->tcp->bind(address, port);
      if (err) {
        const auto json = JSON::Object::Entries{{"source","tls.server.bind"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return cb(seq, json, QueuedResponse{});
      }
      const auto json = JSON::Object::Entries{{"source","tls.server.bind"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::serverListen (const String& seq, ID id, int backlog, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = this->getServer(id);
      if (!s || !s->tcp) {
        const auto json = JSON::Object::Entries{{"source","tls.server.listen"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      int err = s->tcp->listen(backlog, [=](int status){
        if (status >= 0) {
          const auto json = JSON::Object::Entries{{"source","tls.server.connection"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
          cb("-1", json, QueuedResponse{});
        }
      });
      if (err) {
        const auto json = JSON::Object::Entries{{"source","tls.server.listen"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(err))}}}};
        return cb(seq, json, QueuedResponse{});
      }
      const auto json = JSON::Object::Entries{{"source","tls.server.listen"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::serverAccept (const String& seq, ID serverId, ID clientId, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto srv = this->getServer(serverId);
      if (!srv || !srv->tcp) {
        const auto json = JSON::Object::Entries{{"source","tls.server.accept"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"message","ServerNotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }

      auto& registry = tls::ProviderRegistry::instance();
      if (!registry.ensure(this->provider)) {
        cb(seq, notImplemented("tls.server.accept"), QueuedResponse{});
        return;
      }

      int tcpErr = 0;
      auto clientTcp = this->services.tcp.manager.create(clientId, tcpErr);
      if (tcpErr || !clientTcp) {
        const auto json = JSON::Object::Entries{{"source","tls.server.accept"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"message",String(uv_strerror(tcpErr))}}}};
        return cb(seq, json, QueuedResponse{});
      }
      const int err = srv->tcp->accept(clientTcp.get());
      if (err) {
        clientTcp->close([this, clientId](){ this->services.tcp.manager.remove(clientId); });
        const auto json = JSON::Object::Entries{{"source","tls.server.accept"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"message",String(uv_strerror(err))}}}};
        return cb(seq, json, QueuedResponse{});
      }
      // Create server-side TLS session handle for this client
      auto h = std::make_shared<Handle>();
      h->id = clientId;
      h->tcp = clientTcp;
      tls::Server::Options so{};
      so.provider = this->provider;
      auto serverFactory = registry.serverFactory(this->provider);
      if (!serverFactory) {
        cb(seq, notImplemented("tls.server.accept"), QueuedResponse{});
        return;
      }

      h->server = serverFactory(this->loop, so);
      if (!h->server) {
        const auto json = JSON::Object::Entries{{"source","tls.server.accept"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"message","TLS server create failed"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      this->setHandle(clientId, h);

      // Configure TLS server session with server options
      tls::Server::Options topts{};
      topts.cert = srv->options.cert;
      topts.key = srv->options.key;
      topts.ca = srv->options.ca;
      topts.keyPassphrase = srv->options.keyPassphrase;
      topts.requestClientCert = srv->options.requestClientCert;
      topts.alpn = srv->options.alpn;
      topts.minVersion = srv->options.minVersion;
      topts.maxVersion = srv->options.maxVersion;
      topts.ciphers = srv->options.ciphers;
      topts.provider = this->provider;
      h->server->attachTransport(clientTcp);
      const int rc = h->server->setup(topts);
      if (rc) {
        auto messageText = h->server->lastErrorMessage();
        if (messageText.empty()) messageText = "TLS server setup failed";
        JSON::Object::Entries errEntries{{"serverId", std::to_string(serverId)}, {"clientId", std::to_string(clientId)}, {"message", messageText}};
        errEntries["provider"] = providerToString(this->provider);
        if (auto code = h->server->lastErrorCode()) {
          char buf[32];
          std::snprintf(buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(code));
          errEntries["code"] = String(buf);
        }
        const auto json = JSON::Object::Entries{{"source","tls.server.accept"},{"err",errEntries}};
        return cb(seq, json, QueuedResponse{});
      }
      // Drive handshake via transport reads
      const auto providerName = providerToString(this->provider);
      h->tcp->readStart([=](ssize_t nread, const uv_buf_t* buf){
        if (nread > 0 && buf && buf->base) {
          h->server->onEncryptedData(reinterpret_cast<unsigned char*>(buf->base), static_cast<size_t>(nread));
          int hr = h->server->driveHandshake();
          h->server->flushPendingWrites();
          if (hr == 0 && h->server->handshakeDone()) {
            if (!h->handshakeEmitted) {
              unsigned long vflags = 0; types::String vinfo; (void) h->server->getVerifyResult(vflags, vinfo);
              if (srv->options.requestClientCert && vflags != 0) {
                h->handshakeEmitted = true;
                const auto j = JSON::Object::Entries{{"source","tls.server.secureConnection"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"provider",providerName},{"code","CLIENT_CERT_VERIFY_FAILED"},{"message",vinfo}}}};
                cb("-1", j, QueuedResponse{});
              } else {
                h->handshakeEmitted = true;
                types::String protocol, cipher, subject, alpn; (void) h->server->getSessionInfo(protocol, cipher); (void) h->server->getPeerSubject(subject);
                types::Vector<types::String> sans; (void) h->server->getPeerSANs(sans); (void) h->server->getNegotiatedALPN(alpn);
                const auto j = JSON::Object::Entries{{"source","tls.server.secureConnection"},{"data",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"provider",providerName},{"protocol",protocol},{"cipher",cipher},{"alpn",alpn},{"subject",subject},{"sans",toJsonArray(sans)}}}};
                cb("-1", j, QueuedResponse{});
              }
              h->reading = true;
            }
          } else {
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
            if (hr != 0 && hr != MBEDTLS_ERR_SSL_WANT_READ && hr != MBEDTLS_ERR_SSL_WANT_WRITE) {
              if (!h->handshakeEmitted) {
                h->handshakeEmitted = true;
                const auto j = JSON::Object::Entries{{"source","tls.server.secureConnection"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"provider",providerName},{"message","TLS handshake failed"}}}};
                cb("-1", j, QueuedResponse{});
              }
            }
#elif defined(ORO_RUNTIME_TLS_OPENSSL)
            if (hr != 0 && hr != SSL_ERROR_WANT_READ && hr != SSL_ERROR_WANT_WRITE) {
              if (!h->handshakeEmitted) {
                h->handshakeEmitted = true;
                const auto j = JSON::Object::Entries{{"source","tls.server.secureConnection"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"provider",providerName},{"message","TLS handshake failed"}}}};
                cb("-1", j, QueuedResponse{});
              }
            }
#elif defined(ORO_RUNTIME_TLS_SCHANNEL)
            if (hr != 0 && hr != SSL_ERROR_WANT_READ && hr != SSL_ERROR_WANT_WRITE) {
              if (!h->handshakeEmitted) {
                h->handshakeEmitted = true;
                const auto j = JSON::Object::Entries{{"source","tls.server.secureConnection"},{"err",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)},{"provider",providerName},{"message","TLS handshake failed"}}}};
                cb("-1", j, QueuedResponse{});
              }
            }
#endif
          }

          if (h->reading && h->server->handshakeDone()) {
            const size_t kChunk = 16 * 1024;
            unsigned char* out = reinterpret_cast<unsigned char*>(new char[kChunk]);
            for (;;) {
              int rn = h->server->readOnce(out, kChunk);
              if (rn > 0) {
                http::Headers headers{{{"content-type","application/octet-stream"},{"content-length",rn}}};
                QueuedResponse post;
                post.id = oro::runtime::crypto::rand64();
                post.body.reset(reinterpret_cast<unsigned char*>(out));
                post.length = rn;
                post.headers = headers.str();
                const auto jr = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(clientId)},{"bytes",rn}}}};
                cb("-1", jr, post);
                out = reinterpret_cast<unsigned char*>(new char[kChunk]);
              } else if (rn == 0) {
                delete[] reinterpret_cast<char*>(out);
                const auto jr = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(clientId)},{"EOF",true}}}};
                cb("-1", jr, QueuedResponse{});
                break;
              } else if (
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
                rn == MBEDTLS_ERR_SSL_WANT_READ || rn == MBEDTLS_ERR_SSL_WANT_WRITE
#elif defined(ORO_RUNTIME_TLS_OPENSSL)
                rn == -2
#elif defined(ORO_RUNTIME_TLS_SCHANNEL)
                rn == -2
#else
                false
#endif
              ) {
                delete[] reinterpret_cast<char*>(out);
                break;
              } else {
                delete[] reinterpret_cast<char*>(out);
                const auto jr = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(clientId)},{"message","TLS read error"}}}};
                cb("-1", jr, QueuedResponse{});
                break;
              }
            }
          }
          delete[] buf->base;
        } else if (nread == 0 && buf == nullptr) {
          const auto j = JSON::Object::Entries{{"source","tls.read"},{"data",JSON::Object::Entries{{"id",std::to_string(clientId)},{"EOF",true}}}};
          cb("-1", j, QueuedResponse{});
        } else if (nread < 0) {
          const auto j = JSON::Object::Entries{{"source","tls.read"},{"err",JSON::Object::Entries{{"id",std::to_string(clientId)},{"message",String(uv_strerror((int)nread))}}}};
          cb("-1", j, QueuedResponse{});
        } else if (buf && buf->base) {
          delete[] buf->base;
        }
      });

      const auto json = JSON::Object::Entries{{"source","tls.server.accept"},{"data",JSON::Object::Entries{{"serverId",std::to_string(serverId)},{"clientId",std::to_string(clientId)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::serverReadStart (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.server.readStart"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      h->reading = true;
      const auto json = JSON::Object::Entries{{"source","tls.server.readStart"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::serverReadStop (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.server.readStop"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      h->reading = false;
      const auto json = JSON::Object::Entries{{"source","tls.server.readStop"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
      cb(seq, json, QueuedResponse{});
    });
  }

  void TLS::serverWrite (const String& seq, ID id, SharedPointer<unsigned char[]> bytes, size_t size, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h) {
        const auto json = JSON::Object::Entries{{"source","tls.server.write"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      int rc = -1;
      if (h->server && h->server->handshakeDone()) {
        rc = h->server->writeApp(bytes.get(), size);
      } else if (h->client && h->client->handshakeDone()) {
        rc = h->client->writeApp(bytes.get(), size);
      }
      if (rc > 0) {
        const auto json = JSON::Object::Entries{{
          "source", "tls.server.write"
        }, {
          "data",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "bytes", rc
          }}
        }};
        cb(seq, json, QueuedResponse{});
      } else if (rc == 0) {
        if (h->server && h->server->handshakeDone()) {
          h->server->flushPendingWrites();
        }
        if (h->client && h->client->handshakeDone()) {
          h->client->flushPendingWrites();
        }
        const auto json = JSON::Object::Entries{{
          "source", "tls.server.write"
        }, {
          "data",
          JSON::Object::Entries{{
            "id", std::to_string(id)
          }, {
            "bytes", 0
          }}
        }};
        cb(seq, json, QueuedResponse{});
      } else {
        bool handled = false;
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS)
        if (this->provider == tls::Provider::mbedTLS &&
            (rc == MBEDTLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_WANT_READ)) {
          if (h->server && h->server->handshakeDone()) {
            h->server->enqueueWrite(bytes.get(), size);
            h->server->flushPendingWrites();
          }
          const auto json = JSON::Object::Entries{{
            "source", "tls.server.write"
          }, {
            "data",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "bytes", 0
            }}
          }};
          cb(seq, json, QueuedResponse{});
          handled = true;
        }
#endif
        if (!handled) {
          const auto json = JSON::Object::Entries{{
            "source", "tls.server.write"
          }, {
            "err",
            JSON::Object::Entries{{
              "id", std::to_string(id)
            }, {
              "message", "TLS write error"
            }}
          }};
          cb(seq, json, QueuedResponse{});
        }
      }
    });
  }

  void TLS::serverShutdown (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto h = this->getHandle(id);
      if (!h || !h->tcp) {
        const auto json = JSON::Object::Entries{{"source","tls.server.shutdown"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
        return cb(seq, json, QueuedResponse{});
      }
      h->tcp->shutdown([=](int status){
        if (status) {
          const auto json = JSON::Object::Entries{{"source","tls.server.shutdown"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message",String(uv_strerror(status))}}}};
          cb(seq, json, QueuedResponse{});
        } else {
          const auto json = JSON::Object::Entries{{"source","tls.server.shutdown"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
          cb(seq, json, QueuedResponse{});
        }
      });
    });
  }

  void TLS::serverClose (const String& seq, ID id, const Callback cb) {
    this->loop.dispatch([=, this]{
      auto s = this->getServer(id);
      if (s && s->tcp) {
        s->tcp->close([=, this]{ this->removeServer(id); });
        const auto json = JSON::Object::Entries{{"source","tls.server.close"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
        return cb(seq, json, QueuedResponse{});
      }
      // also support closing a server-side client handle by id
      auto h = this->getHandle(id);
      if (h) {
        h->reading = false;
        if (h->tcp) { h->tcp->readStop(); h->tcp->close(nullptr); }
        if (h->client) h->client->close();
        if (h->server) h->server.reset();
        this->removeHandle(id);
        const auto json = JSON::Object::Entries{{"source","tls.server.close"},{"data",JSON::Object::Entries{{"id",std::to_string(id)}}}};
        return cb(seq, json, QueuedResponse{});
      }
      const auto json = JSON::Object::Entries{{"source","tls.server.close"},{"err",JSON::Object::Entries{{"id",std::to_string(id)},{"message","NotFound"}}}};
      cb(seq, json, QueuedResponse{});
    });
  }
}
