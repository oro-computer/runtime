#include "web_push.hh"

#include <sodium.h>
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) && ORO_RUNTIME_ENABLE_MBEDTLS
#include <mbedtls/ecp.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#endif
#include <algorithm>

namespace oro::runtime::native {
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) && ORO_RUNTIME_ENABLE_MBEDTLS
  namespace {
    int random (void*, unsigned char* output, size_t size) {
      if (sodium_init() < 0)
        return -1;
      randombytes_buf(output, size);
      return 0;
    }
    struct Group {
      mbedtls_ecp_group value;
      Group() {
        mbedtls_ecp_group_init(&value);
        if (mbedtls_ecp_group_load(&value, MBEDTLS_ECP_DP_SECP256R1)) {
          mbedtls_ecp_group_free(&value);
          throw StateError("ENOSYS");
        }
      }
      ~Group() {
        mbedtls_ecp_group_free(&value);
      }
    };
    struct Scalar {
      mbedtls_mpi value;
      Scalar() {
        mbedtls_mpi_init(&value);
      }
      ~Scalar() {
        mbedtls_mpi_free(&value);
      }
    };
    struct Point {
      mbedtls_ecp_point value;
      Point() {
        mbedtls_ecp_point_init(&value);
      }
      ~Point() {
        mbedtls_ecp_point_free(&value);
      }
    };
    struct Gcm {
      mbedtls_gcm_context value;
      Gcm() {
        mbedtls_gcm_init(&value);
      }
      ~Gcm() {
        mbedtls_gcm_free(&value);
      }
    };
    template <size_t N>
    struct Secret {
      std::array<unsigned char, N> value;
      ~Secret() {
        sodium_memzero(value.data(), value.size());
      }
    };
    void check (int result) {
      if (result)
        throw StateError("EBADMSG");
    }
  }  // namespace

#endif

  bool webPushAvailable () {
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) && ORO_RUNTIME_ENABLE_MBEDTLS
    return true;
#else
    return false;
#endif
  }

  WebPushKeys::~WebPushKeys() {
    sodium_memzero(this->privateKey.data(), this->privateKey.size());
    sodium_memzero(this->authentication.data(), this->authentication.size());
  }

#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) && ORO_RUNTIME_ENABLE_MBEDTLS
  WebPushKeys WebPushKeys::generate() {
    if (sodium_init() < 0)
      throw StateError("EIO");
    Group group;
    Scalar secret;
    Point publicKey;
    check(mbedtls_ecp_gen_keypair(&group.value, &secret.value, &publicKey.value, random, nullptr));
    WebPushKeys result;
    check(mbedtls_mpi_write_binary(&secret.value, result.privateKey.data(), result.privateKey.size()));
    size_t size = 0;
    check(mbedtls_ecp_point_write_binary(&group.value, &publicKey.value, MBEDTLS_ECP_PF_UNCOMPRESSED, &size,
                                         result.publicKey.data(), result.publicKey.size()));
    if (size != 65)
      throw StateError("EIO");
    randombytes_buf(result.authentication.data(), result.authentication.size());
    return result;
  }

#else
  WebPushKeys WebPushKeys::generate() {
    throw StateError("ENOSYS");
  }
#endif

  std::string webPushBase64 (std::span<const unsigned char> input) {
    if (input.size() > 4096)
      throw StateError("E2BIG");
    std::vector<char> encoded(sodium_base64_ENCODED_LEN(input.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING));
    sodium_bin2base64(encoded.data(), encoded.size(), input.data(), input.size(),
                      sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    return encoded.data();
  }

  RecordBytes webPushDecode (const std::string& input) {
    if (input.empty() || input.size() > 5500 ||
        input.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") !=
          std::string::npos)
      throw StateError("EINVAL");
    RecordBytes output(input.size());
    size_t size = 0;
    if (sodium_base642bin(output.data(), output.size(), input.data(), input.size(), nullptr, &size, nullptr,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING))
      throw StateError("EINVAL");
    output.resize(size);
    return output;
  }

#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) && ORO_RUNTIME_ENABLE_MBEDTLS
  void validateWebPushPublicKey (std::span<const unsigned char> key) {
    if (key.size() != 65 || key[0] != 4)
      throw StateError("EINVAL");
    Group group;
    Point point;
    if (mbedtls_ecp_point_read_binary(&group.value, &point.value, key.data(), key.size()) ||
        mbedtls_ecp_check_pubkey(&group.value, &point.value))
      throw StateError("EINVAL");
  }

  RecordBytes decryptWebPush (const WebPushKeys& keys, std::span<const unsigned char> encrypted) {
    if (encrypted.size() < 103 || encrypted.size() > 4096 || encrypted[20] != 65)
      throw StateError("EBADMSG");
    uint32_t recordSize = 0;
    for (size_t i = 16; i < 20; i++)
      recordSize = (recordSize << 8) | encrypted[i];
    const auto ciphertext = encrypted.subspan(86);
    if (recordSize <= ciphertext.size())
      throw StateError("EBADMSG");
    const auto sender = encrypted.subspan(21, 65);
    try {
      validateWebPushPublicKey(sender);
    } catch (...) {
      throw StateError("EBADMSG");
    }
    Group group;
    Point senderPoint;
    Scalar secret;
    Scalar agreement;
    check(mbedtls_ecp_point_read_binary(&group.value, &senderPoint.value, sender.data(), sender.size()));
    check(mbedtls_mpi_read_binary(&secret.value, keys.privateKey.data(), keys.privateKey.size()));
    check(mbedtls_ecp_check_privkey(&group.value, &secret.value));
    check(
      mbedtls_ecdh_compute_shared(&group.value, &agreement.value, &senderPoint.value, &secret.value, random, nullptr));
    Secret<32> shared;
    check(mbedtls_mpi_write_binary(&agreement.value, shared.value.data(), shared.value.size()));
    const auto sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha)
      throw StateError("ENOSYS");
    Secret<32> prkKey;
    Secret<32> ikm;
    Secret<32> prk;
    Secret<16> cek;
    Secret<12> nonce;
    check(mbedtls_hkdf_extract(sha, keys.authentication.data(), keys.authentication.size(), shared.value.data(),
                               shared.value.size(), prkKey.value.data()));
    constexpr unsigned char info[] = "WebPush: info";
    RecordBytes keyInfo(info, info + sizeof(info));
    keyInfo.insert(keyInfo.end(), keys.publicKey.begin(), keys.publicKey.end());
    keyInfo.insert(keyInfo.end(), sender.begin(), sender.end());
    check(mbedtls_hkdf_expand(sha, prkKey.value.data(), prkKey.value.size(), keyInfo.data(), keyInfo.size(),
                              ikm.value.data(), ikm.value.size()));
    check(mbedtls_hkdf_extract(sha, encrypted.data(), 16, ikm.value.data(), ikm.value.size(), prk.value.data()));
    constexpr unsigned char cekInfo[] = "Content-Encoding: aes128gcm";
    constexpr unsigned char nonceInfo[] = "Content-Encoding: nonce";
    check(mbedtls_hkdf_expand(sha, prk.value.data(), prk.value.size(), cekInfo, sizeof(cekInfo), cek.value.data(),
                              cek.value.size()));
    check(mbedtls_hkdf_expand(sha, prk.value.data(), prk.value.size(), nonceInfo, sizeof(nonceInfo), nonce.value.data(),
                              nonce.value.size()));
    Gcm cipher;
    check(mbedtls_gcm_setkey(&cipher.value, MBEDTLS_CIPHER_ID_AES, cek.value.data(), 128));
    RecordBytes plaintext(ciphertext.size() - 16);
    const auto result =
      mbedtls_gcm_auth_decrypt(&cipher.value, plaintext.size(), nonce.value.data(), nonce.value.size(), nullptr, 0,
                               ciphertext.data() + plaintext.size(), 16, ciphertext.data(), plaintext.data());
    if (result) {
      sodium_memzero(plaintext.data(), plaintext.size());
      throw StateError("EBADMSG");
    }
    while (!plaintext.empty() && plaintext.back() == 0)
      plaintext.pop_back();
    if (plaintext.empty() || plaintext.back() != 2) {
      sodium_memzero(plaintext.data(), plaintext.size());
      throw StateError("EBADMSG");
    }
    plaintext.pop_back();
    return plaintext;
  }
#else
  void validateWebPushPublicKey (std::span<const unsigned char>) {
    throw StateError("ENOSYS");
  }
  RecordBytes decryptWebPush (const WebPushKeys&, std::span<const unsigned char>) {
    throw StateError("ENOSYS");
  }
#endif

}  // namespace oro::runtime::native
