#ifndef ORO_RUNTIME_TLS_COMMON_MBEDTLS_H
#define ORO_RUNTIME_TLS_COMMON_MBEDTLS_H

#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_crt.h>
#include <cctype>
#include <cstring>

namespace oro::runtime::tls::mbedtls_helpers {
  inline int seeded_ctr_drbg (mbedtls_ctr_drbg_context* ctr_drbg, mbedtls_entropy_context* entropy) {
    static const char* pers = "oro-runtime-tls";
    return mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy,
                                 reinterpret_cast<const unsigned char*>(pers), std::strlen(pers));
  }

  inline bool equalsNoCase(const ::oro::runtime::types::String& a, const ::oro::runtime::types::String& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
      if (std::tolower(a[i]) != std::tolower(b[i])) return false;
    }
    return true;
  }

  inline void configure_versions(mbedtls_ssl_config* conf, const ::oro::runtime::types::String& minV, const ::oro::runtime::types::String& maxV) {
    (void) conf; (void) minV; (void) maxV;
  #if defined(MBEDTLS_SSL_MINOR_VERSION_3)
    auto mapVer = [](const ::oro::runtime::types::String& v) -> int {
      if (equalsNoCase(v, "TLSv1.2") || equalsNoCase(v, "1.2")) return MBEDTLS_SSL_MINOR_VERSION_3;
    #if defined(MBEDTLS_SSL_MINOR_VERSION_4)
      if (equalsNoCase(v, "TLSv1.3") || equalsNoCase(v, "1.3")) return MBEDTLS_SSL_MINOR_VERSION_4;
    #endif
      return -1;
    };
    int min_m = mapVer(minV);
    int max_m = mapVer(maxV);
    if (min_m >= 0) mbedtls_ssl_conf_min_version(conf, MBEDTLS_SSL_MAJOR_VERSION_3, min_m);
    if (max_m >= 0) mbedtls_ssl_conf_max_version(conf, MBEDTLS_SSL_MAJOR_VERSION_3, max_m);
  #endif
  }

  inline void configure_ciphers(mbedtls_ssl_config* conf, const ::oro::runtime::types::Vector<::oro::runtime::types::String>& ciphers) {
    if (ciphers.empty()) return;
    static thread_local std::vector<int> ids;
    ids.clear();
    for (const auto& s : ciphers) {
      if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
        int id = 0;
        try { id = std::stoi(s, nullptr, 16); } catch (...) { continue; }
        ids.push_back(id);
      }
    }
    if (!ids.empty()) {
      ids.push_back(0);
      const int* list = ids.data();
      mbedtls_ssl_conf_ciphersuites(conf, list);
    }
  }
}

#endif
