#include "client.hh"
#include "../platform.hh"

#if ORO_RUNTIME_PLATFORM_WINDOWS && defined(ORO_RUNTIME_TLS_SCHANNEL)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#ifndef SECURITY_WIN32
#define SECURITY_WIN32 1
#endif
#include <security.h>
#include <schannel.h>
#include <schnlsp.h>

#include "../tcp.hh"
#include "schannel_util_windows.hh"

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

#ifndef SSL_ERROR_WANT_READ
#define SSL_ERROR_WANT_READ 2
#endif

#ifndef SSL_ERROR_WANT_WRITE
#define SSL_ERROR_WANT_WRITE 3
#endif

namespace oro::runtime::tls {
  namespace {
    using oro::runtime::types::String;
    using oro::runtime::types::Vector;

    inline String formatSystemMessage(HRESULT hr) {
      String message;
      DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
      LPSTR buffer = nullptr;
      DWORD code = static_cast<DWORD>(hr);
      if (FormatMessageA(flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr) && buffer) {
        message.assign(buffer);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) message.pop_back();
      }
      return message;
    }

    inline String describeIdentityError(HRESULT hr, bool isClient) {
      const char* role = isClient ? "client" : "server";
      if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD)) {
        return String("TLS ") + role + " private key passphrase is incorrect";
      }
      if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA)) {
        return String("TLS ") + role + " certificate or key is invalid";
      }
      auto sys = formatSystemMessage(hr);
      if (!sys.empty()) return sys;
      char buf[64] = {0};
      std::snprintf(buf, sizeof(buf), "TLS %s identity error (0x%08lx)", role, static_cast<unsigned long>(hr));
      return String(buf);
    }

    inline std::wstring widen(const String& value) {
      if (value.empty()) return std::wstring();
      int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
      if (needed <= 0) return std::wstring();
      std::wstring result(static_cast<size_t>(needed), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), needed);
      return result;
    }

    inline std::string narrow(const wchar_t* value) {
      if (!value) return std::string();
      int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
      if (needed <= 0) return std::string();
      std::string result(static_cast<size_t>(needed) - 1, '\0');
      if (needed > 1) WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), needed - 1, nullptr, nullptr);
      return result;
    }

    inline void trimWhitespace(std::string& value) {
      value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch){
        return ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ';
      }), value.end());
    }

    inline Vector<String> splitSAN(const CERT_ALT_NAME_INFO* info) {
      Vector<String> sans;
      if (!info) return sans;
      for (DWORD i = 0; i < info->cAltEntry; i++) {
        const CERT_ALT_NAME_ENTRY& entry = info->rgAltEntry[i];
        switch (entry.dwAltNameChoice) {
          case CERT_ALT_NAME_DNS_NAME:
            if (entry.pwszDNSName) sans.emplace_back(narrow(entry.pwszDNSName));
            break;
          case CERT_ALT_NAME_URL:
            if (entry.pwszURL) sans.emplace_back(narrow(entry.pwszURL));
            break;
          case CERT_ALT_NAME_IP_ADDRESS:
            if (entry.IPAddress.cbData == 4 || entry.IPAddress.cbData == 16) {
              char buf[INET6_ADDRSTRLEN] = {0};
              const void* src = entry.IPAddress.pbData;
              if (InetNtopA(entry.IPAddress.cbData == 4 ? AF_INET : AF_INET6, const_cast<void*>(src), buf, static_cast<DWORD>(sizeof(buf)))) {
                sans.emplace_back(buf);
              }
            }
            break;
        }
      }
      return sans;
    }

    inline std::string describeTrustError(DWORD flags) {
      if (flags == CERT_TRUST_NO_ERROR) return std::string();
      std::string result;
      auto append = [&](const char* text) {
        if (!result.empty()) result.append(",");
        result.append(text);
      };
      if (flags & CERT_TRUST_IS_NOT_TIME_VALID) append("CERT_NOT_TIME_VALID");
      if (flags & CERT_TRUST_IS_NOT_SIGNATURE_VALID) append("CERT_BAD_SIGNATURE");
      if (flags & CERT_TRUST_IS_NOT_VALID_FOR_USAGE) append("CERT_NOT_VALID_FOR_USAGE");
      if (flags & CERT_TRUST_IS_UNTRUSTED_ROOT) append("CERT_UNTRUSTED_ROOT");
      if (flags & CERT_TRUST_REVOCATION_STATUS_UNKNOWN) append("CERT_REVOCATION_UNKNOWN");
      if (flags & CERT_TRUST_IS_CYCLIC) append("CERT_CYCLIC");
      if (flags & CERT_TRUST_INVALID_EXTENSION) append("CERT_INVALID_EXTENSION");
      if (flags & CERT_TRUST_INVALID_POLICY_CONSTRAINTS) append("CERT_INVALID_POLICY");
      if (flags & CERT_TRUST_INVALID_BASIC_CONSTRAINTS) append("CERT_INVALID_BASIC_CONSTRAINTS");
      if (flags & CERT_TRUST_IS_NOT_TIME_NESTED) append("CERT_NOT_TIME_NESTED");
      if (flags & CERT_TRUST_IS_PARTIAL_CHAIN) append("CERT_PARTIAL_CHAIN");
      if (flags & CERT_TRUST_CTL_IS_NOT_TIME_VALID) append("CTL_NOT_TIME_VALID");
      if (flags & CERT_TRUST_CTL_IS_NOT_SIGNATURE_VALID) append("CTL_BAD_SIGNATURE");
      if (flags & CERT_TRUST_CTL_IS_NOT_VALID_FOR_USAGE) append("CTL_NOT_VALID_FOR_USAGE");
      if (flags & CERT_TRUST_HAS_EXACT_MATCH_ISSUER) append("CERT_EXACT_MATCH_ISSUER");
      if (flags & CERT_TRUST_HAS_KEY_MATCH_ISSUER) append("CERT_KEY_MATCH_ISSUER");
      if (flags & CERT_TRUST_HAS_NAME_MATCH_ISSUER) append("CERT_NAME_MATCH_ISSUER");
      if (flags & CERT_TRUST_IS_SELF_SIGNED) append("CERT_SELF_SIGNED");
      if (flags & CERT_TRUST_HAS_PREFERRED_ISSUER) append("CERT_PREFERRED_ISSUER");
      if (flags & CERT_TRUST_HAS_ISSUANCE_CHAIN_POLICY) append("CERT_ISSUANCE_POLICY");
      if (flags & CERT_TRUST_HAS_VALID_NAME_CONSTRAINTS) append("CERT_VALID_NAME_CONSTRAINTS");
      if (flags & CERT_TRUST_IS_PEER_TRUSTED) append("CERT_PEER_TRUSTED");
      if (flags & CERT_TRUST_HAS_CRL_VALIDITY_EXTENDED) append("CERT_CRL_EXTENDED");
      if (flags & CERT_TRUST_IS_FROM_EXCLUSIVE_TRUST_STORE) append("CERT_EXCLUSIVE_TRUST");
      if (result.empty()) result.assign("CERT_UNKNOWN_ERROR");
      return result;
    }

    inline void removeConsumed(std::vector<unsigned char>& buffer, size_t extra) {
      if (buffer.empty()) return;
      if (extra > buffer.size()) {
        buffer.clear();
        return;
      }
      const size_t consumed = buffer.size() - extra;
      if (consumed == 0) return;
      buffer.erase(buffer.begin(), buffer.begin() + consumed);
    }

    inline bool decodePemBlock(const String& pem, const char* header, const char* footer, std::vector<unsigned char>& out) {
      if (pem.empty() || !header || !footer) return false;
      size_t start = pem.find(header);
      if (start == String::npos) return false;
      start += strlen(header);
      size_t end = pem.find(footer, start);
      if (end == String::npos) return false;
      std::string body = pem.substr(start, end - start);
      trimWhitespace(body);
      DWORD needed = 0;
      if (!CryptStringToBinaryA(body.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &needed, nullptr, nullptr) || needed == 0) {
        return false;
      }
      out.resize(needed);
      if (!CryptStringToBinaryA(body.c_str(), 0, CRYPT_STRING_BASE64, out.data(), &needed, nullptr, nullptr) || needed == 0) {
        return false;
      }
      out.resize(needed);
      return true;
    }

    inline HCERTSTORE createRootStore(const String& pem) {
      if (pem.empty()) return nullptr;
      HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
      if (!store) return nullptr;
      std::string body = pem;
      const std::string header = "-----BEGIN CERTIFICATE-----";
      const std::string footer = "-----END CERTIFICATE-----";
      size_t pos = 0;
      bool added = false;
      while ((pos = body.find(header, pos)) != std::string::npos) {
        size_t start = pos + header.size();
        size_t end = body.find(footer, start);
        if (end == std::string::npos) break;
        std::string b64 = body.substr(start, end - start);
        trimWhitespace(b64);
        DWORD derSize = 0;
        if (!CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &derSize, nullptr, nullptr) || !derSize) {
          pos = end + footer.size();
          continue;
        }
        std::vector<unsigned char> der(derSize);
        if (!CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, der.data(), &derSize, nullptr, nullptr) || derSize == 0) {
          pos = end + footer.size();
          continue;
        }
        der.resize(derSize);
        PCCERT_CONTEXT ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(), derSize);
        if (!ctx) {
          pos = end + footer.size();
          continue;
        }
        if (CertAddCertificateContextToStore(store, ctx, CERT_STORE_ADD_REPLACE_EXISTING, nullptr)) {
          added = true;
        }
        CertFreeCertificateContext(ctx);
        pos = end + footer.size();
      }
      if (!added) {
        CertCloseStore(store, 0);
        store = nullptr;
      }
      return store;
    }

    inline DWORD protocolMask(const String& minVersion, const String& maxVersion) {
      auto versionToMask = [](const String& v) -> DWORD {
        if (v == "TLSv1" || v == "1.0") return SP_PROT_TLS1_0_CLIENT;
        if (v == "TLSv1.1" || v == "1.1") return SP_PROT_TLS1_1_CLIENT;
        if (v == "TLSv1.2" || v == "1.2") return SP_PROT_TLS1_2_CLIENT;
      #ifdef SP_PROT_TLS1_3_CLIENT
        if (v == "TLSv1.3" || v == "1.3") return SP_PROT_TLS1_3_CLIENT;
      #endif
        return 0;
      };
      DWORD minMask = versionToMask(minVersion);
      DWORD maxMask = versionToMask(maxVersion);
      if (!minMask && !maxMask) return 0;
      DWORD mask = 0;
      const struct { DWORD bit; } versions[] = {
        { SP_PROT_TLS1_0_CLIENT },
        { SP_PROT_TLS1_1_CLIENT },
        { SP_PROT_TLS1_2_CLIENT },
      #ifdef SP_PROT_TLS1_3_CLIENT
        { SP_PROT_TLS1_3_CLIENT },
      #endif
      };
      bool include = minMask == 0;
      for (const auto& entry : versions) {
        if (entry.bit == minMask) include = true;
        if (include) mask |= entry.bit;
        if (entry.bit == maxMask && maxMask != 0) break;
      }
      if (mask == 0) mask = minMask ? minMask : maxMask;
      return mask;
    }

    inline void configureALPN(CredHandle& cred, const Vector<String>& alpn) {
#if defined(SECPKG_CRED_ATTR_APPLICATION_PROTOCOLS)
      size_t payload = 0;
      for (const auto& proto : alpn) {
        if (proto.empty() || proto.size() > 255) continue;
        payload += 1 + proto.size();
      }
      if (payload == 0) return;
      const size_t listSize = sizeof(SecApplicationProtocolList) + payload;
      const size_t totalSize = sizeof(SecPkgContext_ApplicationProtocols) + listSize;
      std::vector<unsigned char> storage(totalSize);
      auto* protocols = reinterpret_cast<SecPkgContext_ApplicationProtocols*>(storage.data());
      auto* list = reinterpret_cast<SecApplicationProtocolList*>(storage.data() + sizeof(SecPkgContext_ApplicationProtocols));
      protocols->ProtocolListsSize = static_cast<unsigned long>(listSize);
      protocols->ProtocolLists = list;
      list->ProtocolExt = SecApplicationProtocolNegotiationExt_ALPN;
      list->ProtocolListSize = static_cast<unsigned short>(payload);
      unsigned char* cursor = list->ProtocolList;
      for (const auto& proto : alpn) {
        if (proto.empty() || proto.size() > 255) continue;
        *cursor++ = static_cast<unsigned char>(proto.size());
        memcpy(cursor, proto.data(), proto.size());
        cursor += proto.size();
      }
      SetCredentialsAttributes(&cred, SECPKG_CRED_ATTR_APPLICATION_PROTOCOLS, protocols, 0);
#else
      (void)cred;
      (void)alpn;
#endif
    }

    inline String nameFromCert(const PCCERT_CONTEXT cert) {
      if (!cert) return String();
      DWORD len = CertNameToStrA(cert->dwCertEncodingType, &cert->pCertInfo->Subject, CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG, nullptr, 0);
      if (!len) return String();
      std::string out(static_cast<size_t>(len), '\0');
      if (!CertNameToStrA(cert->dwCertEncodingType, &cert->pCertInfo->Subject, CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG, out.data(), len)) return String();
      if (!out.empty() && out.back() == '\0') out.pop_back();
      return out;
    }

  }

  struct Client::Impl {
    CredHandle cred{};
    CtxtHandle ctx{};
    TimeStamp expiry{};
    bool haveCred = false;
    bool haveContext = false;
    bool handshakeComplete = false;
    bool manualVerify = false;
    bool verifyDone = false;
    bool allowUnverified = false;
    DWORD requestedFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    DWORD contextFlags = 0;
    std::wstring target;
    std::vector<unsigned char> encryptedIn;
    std::vector<unsigned char> decryptedPending;
    SecPkgContext_StreamSizes sizes{};
    bool haveSizes = false;
    HCERTSTORE rootStore = nullptr;
    unsigned long verifyFlags = 0;
    String verifyInfo;
    Vector<String> cachedSANs;
    String cachedSubject;
    std::vector<PCCERT_CONTEXT> identityCerts;
    NCRYPT_KEY_HANDLE keyHandle = 0;
    NCRYPT_PROV_HANDLE keyProvider = 0;
    HRESULT lastErrorCode = S_OK;
    String lastErrorMessage;
  };

  static Vector<String> decodeSANs(const PCCERT_CONTEXT cert) {
    Vector<String> sans;
    if (!cert) return sans;
    PCERT_EXTENSION ext = CertFindExtension(szOID_SUBJECT_ALT_NAME2, cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension);
    if (!ext) return sans;
    DWORD decodedSize = 0;
    PCERT_ALT_NAME_INFO altInfo = nullptr;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2, ext->Value.pbData, ext->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr, &altInfo, &decodedSize)) {
      return sans;
    }
    Vector<String> entries = splitSAN(altInfo);
    sans = std::move(entries);
    if (altInfo) LocalFree(altInfo);
    return sans;
  }

  Client::Client(loop::Loop& l, const Options& opts) : loop(l), options(opts) {
    this->impl = new Impl();
    this->impl->encryptedIn.reserve(16 * 1024);
  }

  Client::~Client() {
    this->close();
  }

  void Client::attachTransport(types::SharedPointer<oro::runtime::tcp::Socket> socket) {
    this->transport = socket;
  }

  void Client::close() {
    if (!this->impl) return;
    if (this->impl->haveContext) {
      DeleteSecurityContext(&this->impl->ctx);
      this->impl->haveContext = false;
    }
    if (this->impl->haveCred) {
      FreeCredentialsHandle(&this->impl->cred);
      this->impl->haveCred = false;
    }
    if (this->impl->rootStore) {
      CertCloseStore(this->impl->rootStore, 0);
      this->impl->rootStore = nullptr;
    }
    schannel::freeCertificateChain(this->impl->identityCerts);
    if (this->impl->keyHandle) {
      NCryptFreeObject(this->impl->keyHandle);
      this->impl->keyHandle = 0;
    }
    if (this->impl->keyProvider) {
      NCryptFreeObject(this->impl->keyProvider);
      this->impl->keyProvider = 0;
    }
    this->impl->lastErrorCode = S_OK;
    this->impl->lastErrorMessage.clear();
    delete this->impl;
    this->impl = nullptr;
  }

  template <typename ClientImpl>
  static void evaluateVerification(ClientImpl* impl) {
    if (!impl || impl->verifyDone || !impl->handshakeComplete) return;
    PCCERT_CONTEXT cert = nullptr;
    if (QueryContextAttributes(&impl->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) != SEC_E_OK || !cert) {
      impl->verifyFlags = CERT_TRUST_IS_NOT_SIGNATURE_VALID;
      impl->verifyInfo = "Unable to obtain peer certificate";
      impl->verifyDone = true;
      return;
    }
    impl->cachedSubject = nameFromCert(cert);
    impl->cachedSANs = decodeSANs(cert);

    DWORD flags = 0;
    std::string message;

    if (!impl->manualVerify) {
      flags = CERT_TRUST_NO_ERROR;
    } else {
      CERT_CHAIN_PARA chainPara{};
      chainPara.cbSize = sizeof(chainPara);
      chainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
      chainPara.RequestedUsage.Usage.cUsageIdentifier = 0;
      HCERTCHAINENGINE chainEngine = nullptr;
      CERT_CHAIN_ENGINE_CONFIG config{};
      config.cbSize = sizeof(config);
      config.hExclusiveRoot = impl->rootStore;
      if (impl->rootStore) {
        CertCreateCertificateChainEngine(&config, &chainEngine);
      }
      PCCERT_CHAIN_CONTEXT chainContext = nullptr;
      if (!CertGetCertificateChain(chainEngine ? chainEngine : HCCE_CURRENT_USER, cert, nullptr, cert->hCertStore, &chainPara, 0, nullptr, &chainContext) || !chainContext) {
        flags = CERT_TRUST_IS_NOT_SIGNATURE_VALID;
        message = "CertGetCertificateChain failed";
      } else {
        flags = chainContext->TrustStatus.dwErrorStatus;
        message = describeTrustError(flags);
        CertFreeCertificateChain(chainContext);
      }
      if (chainEngine) {
        CertFreeCertificateChainEngine(chainEngine);
      }
    }

    impl->verifyFlags = flags;
    impl->verifyInfo = message;
    impl->verifyDone = true;
    CertFreeCertificateContext(cert);
  }

  template <typename ClientImpl>
  static HRESULT loadClientIdentity(ClientImpl* impl, const String& certPem, const String& keyPem, const String& passphrase) {
    if (!impl) return E_INVALIDARG;
    if (certPem.empty() || keyPem.empty()) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    std::vector<PCCERT_CONTEXT> certs;
    if (!schannel::parseCertificateChain(certPem, certs)) {
      return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    std::vector<unsigned char> keyDer;
    schannel::PrivateKeyFormat keyFormat;
    if (!schannel::decodePrivateKeyPem(keyPem, keyDer, keyFormat)) {
      schannel::freeCertificateChain(certs);
      return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    std::wstring keyPass = widen(passphrase);
    HRESULT hrPkcs8 = schannel::ensurePkcs8(keyDer, keyFormat, keyPass);
    if (FAILED(hrPkcs8)) {
      schannel::freeCertificateChain(certs);
      return hrPkcs8;
    }

    NCRYPT_PROV_HANDLE provider = 0;
    if (NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0) != ERROR_SUCCESS) {
      schannel::freeCertificateChain(certs);
      return HRESULT_FROM_WIN32(GetLastError());
    }

    NCRYPT_KEY_HANDLE key = 0;
    SECURITY_STATUS status = schannel::importPkcs8(provider, keyDer, keyFormat, keyPass, &key);
    if (status == ERROR_SUCCESS) {
      status = NCryptFinalizeKey(key, 0);
    }
    if (status != ERROR_SUCCESS) {
      if (key) NCryptFreeObject(key);
      NCryptFreeObject(provider);
      schannel::freeCertificateChain(certs);
      return HRESULT_FROM_WIN32(status);
    }

    CERT_KEY_CONTEXT keyContext{};
    keyContext.cbSize = sizeof(keyContext);
    keyContext.dwKeySpec = CERT_NCRYPT_KEY_SPEC;
    keyContext.hCryptProv = 0;
    keyContext.hNCryptKey = key;
    if (!CertSetCertificateContextProperty(certs.front(), CERT_KEY_CONTEXT_PROP_ID, CERT_STORE_NO_CRYPT_RELEASE_FLAG, &keyContext)) {
      HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
      NCryptFreeObject(key);
      NCryptFreeObject(provider);
      schannel::freeCertificateChain(certs);
      return hr;
    }

    schannel::freeCertificateChain(impl->identityCerts);
    if (impl->keyHandle) {
      NCryptFreeObject(impl->keyHandle);
      impl->keyHandle = 0;
    }
    if (impl->keyProvider) {
      NCryptFreeObject(impl->keyProvider);
      impl->keyProvider = 0;
    }

    impl->identityCerts = std::move(certs);
    impl->keyHandle = key;
    impl->keyProvider = provider;
    return S_OK;
  }

  int Client::connect(const ClientOptions& opts) {
    if (!this->impl) return -1;
    auto* c = this->impl;
    c->target = widen(!opts.servername.empty() ? opts.servername : opts.host);
    c->manualVerify = false;
    c->allowUnverified = !opts.rejectUnauthorized;
    c->handshakeComplete = false;
    c->verifyDone = false;
    c->verifyFlags = 0;
    c->verifyInfo.clear();
    c->cachedSANs.clear();
    c->cachedSubject.clear();
    c->encryptedIn.clear();
    c->decryptedPending.clear();
    c->lastErrorCode = S_OK;
    c->lastErrorMessage.clear();

    if (c->haveContext) {
      DeleteSecurityContext(&c->ctx);
      c->haveContext = false;
    }
    if (c->haveCred) {
      FreeCredentialsHandle(&c->cred);
      c->haveCred = false;
    }
    schannel::freeCertificateChain(c->identityCerts);
    if (c->keyHandle) {
      NCryptFreeObject(c->keyHandle);
      c->keyHandle = 0;
    }
    if (c->keyProvider) {
      NCryptFreeObject(c->keyProvider);
      c->keyProvider = 0;
    }

    SCHANNEL_CRED cred{};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
    if (opts.rejectUnauthorized && opts.ca.empty()) {
      cred.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION;
    } else {
      cred.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION;
      c->manualVerify = true;
    }
    cred.grbitEnabledProtocols = protocolMask(opts.minVersion, opts.maxVersion);

    bool hasCert = !opts.cert.empty();
    bool hasKey = !opts.key.empty();
    if (hasCert != hasKey) {
      c->lastErrorCode = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
      c->lastErrorMessage = "TLS client certificate and key must be provided together";
      return -1;
    }

    if (c->rootStore) {
      CertCloseStore(c->rootStore, 0);
      c->rootStore = nullptr;
    }

    if (!opts.ca.empty()) {
      c->rootStore = createRootStore(opts.ca);
      if (c->rootStore) {
        cred.hRootStore = c->rootStore;
      }
    }

    if (hasCert && hasKey) {
      schannel::freeCertificateChain(c->identityCerts);
      if (c->keyHandle) { NCryptFreeObject(c->keyHandle); c->keyHandle = 0; }
      if (c->keyProvider) { NCryptFreeObject(c->keyProvider); c->keyProvider = 0; }
      HRESULT hrIdentity = loadClientIdentity(c, opts.cert, opts.key, opts.keyPassphrase);
      if (FAILED(hrIdentity)) {
        c->lastErrorCode = hrIdentity;
        c->lastErrorMessage = describeIdentityError(hrIdentity, true);
        return -1;
      }
      cred.cCreds = static_cast<DWORD>(c->identityCerts.size());
      cred.paCred = c->identityCerts.empty() ? nullptr : c->identityCerts.data();
    } else {
      schannel::freeCertificateChain(c->identityCerts);
      if (c->keyHandle) { NCryptFreeObject(c->keyHandle); c->keyHandle = 0; }
      if (c->keyProvider) { NCryptFreeObject(c->keyProvider); c->keyProvider = 0; }
      cred.cCreds = 0;
      cred.paCred = nullptr;
    }

    TimeStamp expiry{};
    SECURITY_STATUS status = AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr, nullptr, &c->cred, &expiry);
    if (status != SEC_E_OK) {
      HRESULT hr = status;
      if (SUCCEEDED(hr)) hr = HRESULT_FROM_WIN32(status);
      c->lastErrorCode = hr;
      c->lastErrorMessage = formatSystemMessage(hr);
      if (c->lastErrorMessage.empty()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "AcquireCredentialsHandleW failed (0x%08lx)", static_cast<unsigned long>(hr));
        c->lastErrorMessage = buf;
      }
      return static_cast<int>(status);
    }

    configureALPN(c->cred, opts.alpn);

    c->expiry = expiry;
    c->haveCred = true;
    c->requestedFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    return 0;
  }

  int Client::write(const char* data, size_t len) {
    if (!this->transport) return -1;
    return this->transport->write(data, len, [this](int){ this->flushPendingWrites(); });
  }

  int Client::readStart() { return 0; }
  int Client::readStop() { return 0; }

  int Client::onEncryptedData(const unsigned char* data, size_t len) {
    if (!this->impl || !data || len == 0) return 0;
    this->impl->encryptedIn.insert(this->impl->encryptedIn.end(), data, data + len);
    return 0;
  }

  int Client::driveHandshake() {
    auto* c = this->impl;
    if (!c || !c->haveCred) return -1;

    SecBuffer outBuffers[1];
    outBuffers[0].BufferType = SECBUFFER_TOKEN;
    outBuffers[0].pvBuffer = nullptr;
    outBuffers[0].cbBuffer = 0;
    SecBufferDesc outDesc;
    outDesc.cBuffers = 1;
    outDesc.pBuffers = outBuffers;
    outDesc.ulVersion = SECBUFFER_VERSION;

    SecBuffer inBuffers[2];
    SecBufferDesc inDesc;
    inBuffers[0].BufferType = SECBUFFER_TOKEN;
    inBuffers[0].pvBuffer = c->encryptedIn.empty() ? nullptr : c->encryptedIn.data();
    inBuffers[0].cbBuffer = c->encryptedIn.empty() ? 0 : static_cast<DWORD>(c->encryptedIn.size());
    inBuffers[1].BufferType = SECBUFFER_EMPTY;
    inBuffers[1].pvBuffer = nullptr;
    inBuffers[1].cbBuffer = 0;
    inDesc.cBuffers = c->haveContext ? 2 : 0;
    inDesc.pBuffers = inBuffers;
    inDesc.ulVersion = SECBUFFER_VERSION;

    SECURITY_STATUS status;
    if (!c->haveContext) {
      status = InitializeSecurityContextW(&c->cred, nullptr, c->target.empty() ? nullptr : const_cast<wchar_t*>(c->target.c_str()), c->requestedFlags, 0, SECURITY_NATIVE_DREP, nullptr, 0, &c->ctx, &outDesc, &c->contextFlags, &c->expiry);
      if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED) c->haveContext = true;
    } else {
      inDesc.cBuffers = 2;
      status = InitializeSecurityContextW(&c->cred, &c->ctx, c->target.empty() ? nullptr : const_cast<wchar_t*>(c->target.c_str()), c->requestedFlags, 0, SECURITY_NATIVE_DREP, &inDesc, 0, nullptr, &outDesc, &c->contextFlags, &c->expiry);
    }

    if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0) {
      this->write(reinterpret_cast<const char*>(outBuffers[0].pvBuffer), outBuffers[0].cbBuffer);
      FreeContextBuffer(outBuffers[0].pvBuffer);
      outBuffers[0].pvBuffer = nullptr;
    }

    if (status == SEC_E_OK || status == SEC_I_CONTEXT_EXPIRED) {
      c->handshakeComplete = true;
      if (!c->haveSizes) {
        if (QueryContextAttributes(&c->ctx, SECPKG_ATTR_STREAM_SIZES, &c->sizes) == SEC_E_OK) {
          c->haveSizes = true;
        }
      }
      size_t extra = (inBuffers[1].BufferType == SECBUFFER_EXTRA) ? inBuffers[1].cbBuffer : 0;
      removeConsumed(c->encryptedIn, extra);
      evaluateVerification(c);
      return 0;
    }

    if (status == SEC_I_RENEGOTIATE) {
      c->handshakeComplete = false;
      size_t extra = (inBuffers[1].BufferType == SECBUFFER_EXTRA) ? inBuffers[1].cbBuffer : 0;
      removeConsumed(c->encryptedIn, extra);
      return SSL_ERROR_WANT_READ;
    }

    if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE) {
      size_t extra = (inBuffers[1].BufferType == SECBUFFER_EXTRA) ? inBuffers[1].cbBuffer : 0;
      removeConsumed(c->encryptedIn, extra);
      return SSL_ERROR_WANT_READ;
    }

    if (status == SEC_I_INCOMPLETE_CREDENTIALS) {
      return SSL_ERROR_WANT_READ;
    }

    return -1;
  }

  bool Client::handshakeDone() const {
    return this->impl && this->impl->handshakeComplete;
  }

  int Client::writeApp(const unsigned char* data, size_t len) {
    auto* c = this->impl;
    if (!c || !c->handshakeComplete) return -2;
    if (!data || len == 0) return 0;
    if (!c->haveSizes) {
      if (QueryContextAttributes(&c->ctx, SECPKG_ATTR_STREAM_SIZES, &c->sizes) != SEC_E_OK) return -1;
      c->haveSizes = true;
    }

    size_t offset = 0;
    int total = 0;
    while (offset < len) {
      size_t chunk = std::min<size_t>(len - offset, c->sizes.cbMaximumMessage ? c->sizes.cbMaximumMessage : len - offset);
      size_t totalSize = c->sizes.cbHeader + chunk + c->sizes.cbTrailer;
      std::vector<unsigned char> buffer(totalSize);

      SecBuffer buffers[4];
      buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
      buffers[0].pvBuffer = buffer.data();
      buffers[0].cbBuffer = c->sizes.cbHeader;
      buffers[1].BufferType = SECBUFFER_DATA;
      buffers[1].pvBuffer = buffer.data() + c->sizes.cbHeader;
      buffers[1].cbBuffer = static_cast<DWORD>(chunk);
      memcpy(buffers[1].pvBuffer, data + offset, chunk);
      buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
      buffers[2].pvBuffer = buffer.data() + c->sizes.cbHeader + chunk;
      buffers[2].cbBuffer = c->sizes.cbTrailer;
      buffers[3].BufferType = SECBUFFER_EMPTY;
      buffers[3].pvBuffer = nullptr;
      buffers[3].cbBuffer = 0;

      SecBufferDesc desc;
      desc.cBuffers = 4;
      desc.pBuffers = buffers;
      desc.ulVersion = SECBUFFER_VERSION;

      SECURITY_STATUS status = EncryptMessage(&c->ctx, 0, &desc, 0);
      if (status != SEC_E_OK) return -1;

      size_t sendLen = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
      this->write(reinterpret_cast<const char*>(buffer.data()), sendLen);
      offset += chunk;
      total += static_cast<int>(chunk);
    }
    return total;
  }

  void Client::enqueueWrite(const unsigned char* data, size_t len) {
    std::vector<unsigned char> copy(len);
    if (len) memcpy(copy.data(), data, len);
    pendingWrites.emplace_back(std::move(copy));
  }

  void Client::flushPendingWrites() {
    if (!this->impl || !this->impl->handshakeComplete) return;
    while (!pendingWrites.empty()) {
      auto payload = std::move(pendingWrites.front());
      pendingWrites.erase(pendingWrites.begin());
      if (payload.empty()) continue;
      int rc = this->writeApp(payload.data(), payload.size());
      if (rc < 0) break;
    }
  }

  int Client::readOnce(unsigned char* out, size_t maxlen) {
    auto* c = this->impl;
    if (!c || !c->handshakeComplete) return -2;
    if (!out || maxlen == 0) return 0;

    if (!c->decryptedPending.empty()) {
      size_t n = std::min(maxlen, c->decryptedPending.size());
      memcpy(out, c->decryptedPending.data(), n);
      c->decryptedPending.erase(c->decryptedPending.begin(), c->decryptedPending.begin() + n);
      return static_cast<int>(n);
    }

    if (c->encryptedIn.empty()) return -2;

    SecBuffer buffers[4];
    buffers[0].BufferType = SECBUFFER_DATA;
    buffers[0].pvBuffer = c->encryptedIn.data();
    buffers[0].cbBuffer = static_cast<DWORD>(c->encryptedIn.size());
    buffers[1].BufferType = SECBUFFER_EMPTY;
    buffers[1].pvBuffer = nullptr;
    buffers[1].cbBuffer = 0;
    buffers[2].BufferType = SECBUFFER_EMPTY;
    buffers[2].pvBuffer = nullptr;
    buffers[2].cbBuffer = 0;
    buffers[3].BufferType = SECBUFFER_EMPTY;
    buffers[3].pvBuffer = nullptr;
    buffers[3].cbBuffer = 0;

    SecBufferDesc desc;
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = buffers;

    SECURITY_STATUS status = DecryptMessage(&c->ctx, &desc, 0, nullptr);
    if (status == SEC_E_INCOMPLETE_MESSAGE) {
      return -2;
    }

    if (status == SEC_I_CONTEXT_EXPIRED) {
      c->encryptedIn.clear();
      return 0;
    }

    if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE) {
      return -1;
    }

    int copied = 0;
    for (int i = 0; i < 4; i++) {
      if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0 && buffers[i].pvBuffer) {
        size_t available = std::min<size_t>(buffers[i].cbBuffer, maxlen - copied);
        memcpy(out + copied, buffers[i].pvBuffer, available);
        copied += static_cast<int>(available);
        size_t remaining = buffers[i].cbBuffer - available;
        if (remaining > 0) {
          auto start = reinterpret_cast<unsigned char*>(buffers[i].pvBuffer) + available;
          c->decryptedPending.assign(start, start + remaining);
        }
      }
    }

    size_t extra = 0;
    for (int i = 0; i < 4; i++) {
      if (buffers[i].BufferType == SECBUFFER_EXTRA) {
        extra = buffers[i].cbBuffer;
        break;
      }
    }
    removeConsumed(c->encryptedIn, extra);

    if (status == SEC_I_RENEGOTIATE) {
      c->handshakeComplete = false;
    }

    if (copied == 0) return -2;
    return copied;
  }

  int Client::getVerifyResult(unsigned long& flags, types::String& info) {
    flags = 0;
    info.clear();
    if (!this->impl || !this->impl->handshakeComplete) return -1;
    evaluateVerification(this->impl);
    flags = this->impl->verifyFlags;
    info = this->impl->verifyInfo;
    return 0;
  }

  int Client::getSessionInfo(types::String& protocol, types::String& cipher) {
    protocol.clear();
    cipher.clear();
    if (!this->impl || !this->impl->handshakeComplete) return -1;

    SecPkgContext_ConnectionInfo conn{};
    if (QueryContextAttributes(&this->impl->ctx, SECPKG_ATTR_CONNECTION_INFO, &conn) == SEC_E_OK) {
      switch (conn.dwProtocol) {
        case SP_PROT_TLS1_0_CLIENT: protocol = "TLSv1.0"; break;
        case SP_PROT_TLS1_1_CLIENT: protocol = "TLSv1.1"; break;
        case SP_PROT_TLS1_2_CLIENT: protocol = "TLSv1.2"; break;
      #ifdef SP_PROT_TLS1_3_CLIENT
        case SP_PROT_TLS1_3_CLIENT: protocol = "TLSv1.3"; break;
      #endif
        default: protocol = ""; break;
      }
    }

    SecPkgContext_CipherInfo cipherInfo{};
    if (QueryContextAttributes(&this->impl->ctx, SECPKG_ATTR_CIPHER_INFO, &cipherInfo) == SEC_E_OK) {
      cipher = narrow(cipherInfo.szCipherSuite);
    }
    return 0;
  }

  int Client::getPeerSubject(types::String& subject) {
    subject.clear();
    if (!this->impl || !this->impl->handshakeComplete) return -1;
    evaluateVerification(this->impl);
    subject = this->impl->cachedSubject;
    return 0;
  }

  int Client::getPeerCertificateSha256(types::Vector<uint8_t>& digest) {
    digest.clear();
    if (!this->impl || !this->impl->handshakeComplete) return -1;

    PCCERT_CONTEXT cert = nullptr;
    if (QueryContextAttributes(&this->impl->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) != SEC_E_OK || !cert) {
      return -1;
    }

    DWORD digestSize = 0;
    const auto* certBytes = cert->pbCertEncoded;
    const auto certSize = cert->cbCertEncoded;

    if (!certBytes || certSize == 0) {
      CertFreeCertificateContext(cert);
      return -1;
    }

    if (!CryptHashCertificate(
          0,
          CALG_SHA_256,
          0,
          certBytes,
          certSize,
          nullptr,
          &digestSize
        ) || digestSize != 32) {
      CertFreeCertificateContext(cert);
      return -1;
    }

    digest.resize(digestSize);

    if (!CryptHashCertificate(
          0,
          CALG_SHA_256,
          0,
          certBytes,
          certSize,
          reinterpret_cast<BYTE*>(digest.data()),
          &digestSize
        ) || digestSize != 32) {
      digest.clear();
      CertFreeCertificateContext(cert);
      return -1;
    }

    digest.resize(digestSize);
    CertFreeCertificateContext(cert);
    return 0;
  }

  int Client::getPeerSANs(types::Vector<types::String>& sans) {
    sans.clear();
    if (!this->impl || !this->impl->handshakeComplete) return -1;
    evaluateVerification(this->impl);
    sans = this->impl->cachedSANs;
    return 0;
  }

  int Client::getNegotiatedALPN(types::String& alpn) {
    alpn.clear();
#if defined(SECPKG_ATTR_APPLICATION_PROTOCOL)
    if (!this->impl || !this->impl->handshakeComplete) return -1;
    SecPkgContext_ApplicationProtocol proto{};
    if (QueryContextAttributes(&this->impl->ctx, SECPKG_ATTR_APPLICATION_PROTOCOL, &proto) == SEC_E_OK) {
      if (proto.ProtoNegoStatus == SecApplicationProtocolNegotiationStatus_Success && proto.ProtocolIdSize > 0) {
        alpn.assign(reinterpret_cast<const char*>(proto.ProtocolId), proto.ProtocolIdSize);
      }
    }
#endif
    return 0;
  }

  int Client::shutdown() {
    if (!this->impl || !this->impl->handshakeComplete) return 0;
    DWORD shutdownToken = SCHANNEL_SHUTDOWN;
    SecBuffer buffer;
    buffer.BufferType = SECBUFFER_TOKEN;
    buffer.pvBuffer = &shutdownToken;
    buffer.cbBuffer = sizeof(shutdownToken);
    SecBufferDesc desc;
    desc.cBuffers = 1;
    desc.pBuffers = &buffer;
    desc.ulVersion = SECBUFFER_VERSION;
    SECURITY_STATUS status = ApplyControlToken(&this->impl->ctx, &desc);
    if (status != SEC_E_OK) return -1;

    SecBuffer out;
    out.BufferType = SECBUFFER_TOKEN;
    out.pvBuffer = nullptr;
    out.cbBuffer = 0;
    SecBufferDesc outDesc;
    outDesc.cBuffers = 1;
    outDesc.pBuffers = &out;
    outDesc.ulVersion = SECBUFFER_VERSION;
    status = InitializeSecurityContextW(&this->impl->cred, &this->impl->ctx, nullptr, this->impl->requestedFlags, 0, SECURITY_NATIVE_DREP, nullptr, 0, nullptr, &outDesc, &this->impl->contextFlags, &this->impl->expiry);
    if (status == SEC_E_OK || status == SEC_I_CONTEXT_EXPIRED) {
      if (out.pvBuffer && out.cbBuffer > 0) {
        this->write(reinterpret_cast<const char*>(out.pvBuffer), out.cbBuffer);
        FreeContextBuffer(out.pvBuffer);
      }
      return 0;
    }
    if (out.pvBuffer) FreeContextBuffer(out.pvBuffer);
    return -1;
  }

  const String& Client::lastErrorMessage() const {
    static String empty;
    if (!this->impl) return empty;
    return this->impl->lastErrorMessage;
  }

  long Client::lastErrorCode() const {
    return this->impl ? static_cast<long>(this->impl->lastErrorCode) : 0;
  }
}

#endif
