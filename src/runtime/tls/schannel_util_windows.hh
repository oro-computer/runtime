#ifndef ORO_RUNTIME_TLS_SCHANNEL_UTIL_WINDOWS_H
#define ORO_RUNTIME_TLS_SCHANNEL_UTIL_WINDOWS_H

#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace oro::runtime::tls::schannel {
  enum class PrivateKeyFormat {
    PKCS8,
    RSA_PKCS1,
    EC_SEC1,
    PKCS8_ENCRYPTED
  };

  inline void trimWhitespace(std::string& value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
      return ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ';
    }), value.end());
  }

  inline bool decodePemSegment(const std::string& pem, const char* header, const char* footer, std::vector<unsigned char>& out) {
    if (!header || !footer) return false;
    size_t start = pem.find(header);
    if (start == std::string::npos) return false;
    start += std::strlen(header);
    size_t end = pem.find(footer, start);
    if (end == std::string::npos) return false;
    std::string body = pem.substr(start, end - start);
    trimWhitespace(body);
    DWORD required = 0;
    if (!CryptStringToBinaryA(body.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &required, nullptr, nullptr) || required == 0) {
      return false;
    }
    out.resize(required);
    if (!CryptStringToBinaryA(body.c_str(), 0, CRYPT_STRING_BASE64, out.data(), &required, nullptr, nullptr) || required == 0) {
      return false;
    }
    out.resize(required);
    return true;
  }

  inline bool decodePrivateKeyPem(const std::string& pem, std::vector<unsigned char>& der, PrivateKeyFormat& format) {
    struct Entry {
      const char* header;
      const char* footer;
      PrivateKeyFormat format;
    } entries[] = {
      {"-----BEGIN PRIVATE KEY-----", "-----END PRIVATE KEY-----", PrivateKeyFormat::PKCS8},
      {"-----BEGIN RSA PRIVATE KEY-----", "-----END RSA PRIVATE KEY-----", PrivateKeyFormat::RSA_PKCS1},
      {"-----BEGIN EC PRIVATE KEY-----", "-----END EC PRIVATE KEY-----", PrivateKeyFormat::EC_SEC1},
      {"-----BEGIN ENCRYPTED PRIVATE KEY-----", "-----END ENCRYPTED PRIVATE KEY-----", PrivateKeyFormat::PKCS8_ENCRYPTED}
    };

    for (const auto& entry : entries) {
      if (decodePemSegment(pem, entry.header, entry.footer, der)) {
        format = entry.format;
        return true;
      }
    }
    return false;
  }

  inline bool encodeOid(const char* oid, std::vector<unsigned char>& out) {
    out.clear();
    if (!oid || !*oid) return false;
    BYTE* encoded = nullptr;
    DWORD encodedSize = 0;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_OBJECT_IDENTIFIER, const_cast<char*>(oid), CRYPT_ENCODE_ALLOC_FLAG, nullptr, &encoded, &encodedSize)) {
      return false;
    }
    out.assign(encoded, encoded + encodedSize);
    LocalFree(encoded);
    return true;
  }

  inline bool convertRsaToPkcs8(const std::vector<unsigned char>& pkcs1, std::vector<unsigned char>& pkcs8) {
    if (pkcs1.empty() || pkcs1.size() > MAXDWORD) return false;
    CRYPT_PRIVATE_KEY_INFO info{};
    info.Version = 0;
    info.Algorithm.pszObjId = const_cast<char*>(szOID_RSA_RSA);
    static const BYTE kNullParams[] = {0x05, 0x00};
    info.Algorithm.Parameters.cbData = sizeof(kNullParams);
    info.Algorithm.Parameters.pbData = const_cast<BYTE*>(kNullParams);
    info.PrivateKey.cbData = static_cast<DWORD>(pkcs1.size());
    info.PrivateKey.pbData = const_cast<BYTE*>(pkcs1.data());

    BYTE* encoded = nullptr;
    DWORD encodedSize = 0;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_PRIVATE_KEY_INFO, &info, CRYPT_ENCODE_ALLOC_FLAG, nullptr, &encoded, &encodedSize)) {
      return false;
    }
    pkcs8.assign(encoded, encoded + encodedSize);
    LocalFree(encoded);
    return true;
  }

  inline bool readDerElement(const uint8_t*& cursor, size_t& remaining, uint8_t& tag, const uint8_t*& value, size_t& length, const uint8_t** elementStart = nullptr) {
    if (remaining < 2) return false;
    const uint8_t* start = cursor;
    tag = *cursor++;
    remaining--;
    uint8_t lenByte = *cursor++;
    remaining--;
    size_t len = 0;
    if ((lenByte & 0x80) == 0) {
      len = lenByte;
    } else {
      size_t count = lenByte & 0x7F;
      if (count == 0 || count > sizeof(size_t) || remaining < count) return false;
      len = 0;
      for (size_t i = 0; i < count; i++) {
        len = (len << 8u) | *cursor++;
        remaining--;
      }
    }
    if (remaining < len) return false;
    if (elementStart) *elementStart = start;
    value = cursor;
    cursor += len;
    remaining -= len;
    length = len;
    return true;
  }

  inline bool extractEcCurveOid(const std::vector<unsigned char>& sec1, std::vector<unsigned char>& oidEncoded) {
    const uint8_t* cursor = sec1.data();
    size_t remaining = sec1.size();
    uint8_t tag = 0;
    const uint8_t* value = nullptr;
    size_t length = 0;

    if (!readDerElement(cursor, remaining, tag, value, length) || tag != 0x30) return false; // SEQUENCE
    const uint8_t* innerCursor = value;
    size_t innerRemaining = length;

    if (!readDerElement(innerCursor, innerRemaining, tag, value, length) || tag != 0x02) return false; // version
    if (!readDerElement(innerCursor, innerRemaining, tag, value, length) || tag != 0x04) return false; // private key

    if (!readDerElement(innerCursor, innerRemaining, tag, value, length) || tag != 0xA0) return false; // parameters (named curve)
    const uint8_t* paramCursor = value;
    size_t paramRemaining = length;
    const uint8_t* oidStart = nullptr;
    if (!readDerElement(paramCursor, paramRemaining, tag, value, length, &oidStart) || tag != 0x06 || !oidStart) return false;
    ptrdiff_t headerLen = value - oidStart;
    if (headerLen < 0) return false;
    size_t total = static_cast<size_t>(headerLen) + length;
    oidEncoded.assign(oidStart, oidStart + total);
    return true;
  }

  inline bool convertEcToPkcs8(const std::vector<unsigned char>& sec1, std::vector<unsigned char>& pkcs8) {
    if (sec1.empty() || sec1.size() > MAXDWORD) return false;
    std::vector<unsigned char> oidEncoded;
    if (!extractEcCurveOid(sec1, oidEncoded)) {
      return false;
    }
    if (oidEncoded.size() > MAXDWORD) {
      return false;
    }

    CRYPT_PRIVATE_KEY_INFO info{};
    info.Version = 0;
    info.Algorithm.pszObjId = const_cast<char*>(szOID_ECC_PUBLIC_KEY);
    info.Algorithm.Parameters.cbData = static_cast<DWORD>(oidEncoded.size());
    info.Algorithm.Parameters.pbData = oidEncoded.data();
    info.PrivateKey.cbData = static_cast<DWORD>(sec1.size());
    info.PrivateKey.pbData = const_cast<BYTE*>(sec1.data());

    BYTE* encoded = nullptr;
    DWORD encodedSize = 0;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_PRIVATE_KEY_INFO, &info, CRYPT_ENCODE_ALLOC_FLAG, nullptr, &encoded, &encodedSize)) {
      return false;
    }
    pkcs8.assign(encoded, encoded + encodedSize);
    LocalFree(encoded);
    return true;
  }

  inline HRESULT ensurePkcs8(std::vector<unsigned char>& der, PrivateKeyFormat format, const std::wstring& passphrase = L"") {
    if (format == PrivateKeyFormat::PKCS8) {
      return der.empty() ? HRESULT_FROM_WIN32(ERROR_INVALID_DATA) : S_OK;
    }
    std::vector<unsigned char> converted;
    bool ok = false;
    if (format == PrivateKeyFormat::RSA_PKCS1) {
      ok = convertRsaToPkcs8(der, converted);
    } else if (format == PrivateKeyFormat::EC_SEC1) {
      ok = convertEcToPkcs8(der, converted);
    } else if (format == PrivateKeyFormat::PKCS8_ENCRYPTED) {
      return passphrase.empty() ? HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD) : S_OK;
    }
    if (!ok) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    der.swap(converted);
    return S_OK;
  }

  inline SECURITY_STATUS importPkcs8(
    NCRYPT_PROV_HANDLE provider,
    const std::vector<unsigned char>& der,
    PrivateKeyFormat format,
    const std::wstring& passphrase,
    NCRYPT_KEY_HANDLE* key
  ) {
    if (provider == 0 || key == nullptr || der.empty() || der.size() > MAXDWORD) {
      return NTE_INVALID_PARAMETER;
    }

    NCryptBuffer secret{};
    NCryptBufferDesc parameters{};
    NCryptBufferDesc* parameterList = nullptr;

    if (format == PrivateKeyFormat::PKCS8_ENCRYPTED) {
      if (passphrase.empty()) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD);
      }
      if (passphrase.size() >= MAXDWORD / sizeof(wchar_t)) {
        return NTE_INVALID_PARAMETER;
      }

      secret.BufferType = NCRYPTBUFFER_PKCS_SECRET;
      secret.cbBuffer = static_cast<ULONG>((passphrase.size() + 1) * sizeof(wchar_t));
      secret.pvBuffer = const_cast<wchar_t*>(passphrase.c_str());
      parameters.ulVersion = NCRYPTBUFFER_VERSION;
      parameters.cBuffers = 1;
      parameters.pBuffers = &secret;
      parameterList = &parameters;
    }

    return NCryptImportKey(
      provider,
      0,
      NCRYPT_PKCS8_PRIVATE_KEY_BLOB,
      parameterList,
      key,
      const_cast<unsigned char*>(der.data()),
      static_cast<DWORD>(der.size()),
      NCRYPT_DO_NOT_FINALIZE_FLAG
    );
  }

  inline bool parseCertificateChain(const std::string& pem, std::vector<PCCERT_CONTEXT>& certs) {
    certs.clear();
    if (pem.empty()) return false;
    size_t pos = 0;
    const std::string header = "-----BEGIN CERTIFICATE-----";
    const std::string footer = "-----END CERTIFICATE-----";
    while ((pos = pem.find(header, pos)) != std::string::npos) {
      size_t start = pos + header.size();
      size_t end = pem.find(footer, start);
      if (end == std::string::npos) break;
      std::string body = pem.substr(start, end - start);
      std::vector<unsigned char> der;
      if (!decodePemSegment(pem.substr(pos, end + footer.size() - pos), header.c_str(), footer.c_str(), der)) {
        for (auto ctx : certs) { if (ctx) CertFreeCertificateContext(ctx); }
        certs.clear();
        return false;
      }
      if (der.size() > MAXDWORD) {
        for (auto ctx : certs) { if (ctx) CertFreeCertificateContext(ctx); }
        certs.clear();
        return false;
      }
      PCCERT_CONTEXT ctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
      if (!ctx) {
        for (auto c : certs) { if (c) CertFreeCertificateContext(c); }
        certs.clear();
        return false;
      }
      certs.push_back(ctx);
      pos = end + footer.size();
    }
    return !certs.empty();
  }

  inline void freeCertificateChain(std::vector<PCCERT_CONTEXT>& certs) {
    for (auto ctx : certs) {
      if (ctx) CertFreeCertificateContext(ctx);
    }
    certs.clear();
  }
}

#endif
