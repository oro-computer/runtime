#ifndef ORO_RUNTIME_NATIVE_PKCS11_BACKEND_H
#define ORO_RUNTIME_NATIVE_PKCS11_BACKEND_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "records.hh"
#include <span>

namespace oro::runtime::native {
  enum class ProtectedKeyKind { Secret, Signing };
  struct Pkcs11Options {
    std::string provider = "auto";
    std::string module;
    std::string tokenSerial;
    unsigned int rsaBits = 2048;
  };
  class Pkcs11Module;
  struct Pkcs11Token {
    std::shared_ptr<Pkcs11Module> module;
    unsigned long slot = 0;
    std::string provider;
    std::string moduleIdentity;
    std::string tokenIdentity;
    bool protectedPath = false;
    bool signing = false;
    bool sealing = false;
    bool pinLocked = false;
    size_t minimumPinBytes = 1;
    size_t maximumPinBytes = 256;
  };
  struct ProtectedKeyMaterial {
    RecordBytes fingerprintMaterial;
    RecordBytes publicKey;
    size_t wrappingBytes = 0;
  };
  /** Native-only provider discovery. No login, protected-key creation or secret retrieval. */
  std::vector<Pkcs11Token> discoverPkcs11 (const Pkcs11Options&);

  /** Serialized provider session. Private operations require a fresh context-specific login. */
  class Pkcs11Session {
  public:
    Pkcs11Session(const Pkcs11Token&, std::span<const unsigned char> pin);
    ~Pkcs11Session();
    Pkcs11Session(const Pkcs11Session&) = delete;
    Pkcs11Session& operator=(const Pkcs11Session&) = delete;
    bool exists (std::span<const unsigned char> id, ProtectedKeyKind);
    void create (std::span<const unsigned char> id, const std::string& label, ProtectedKeyKind, unsigned int rsaBits);
    ProtectedKeyMaterial inspect (std::span<const unsigned char> id, ProtectedKeyKind);
    RecordBytes sign (std::span<const unsigned char> id, std::span<const unsigned char> digest,
                      std::span<const unsigned char> pin);
    RecordBytes wrap (std::span<const unsigned char> id, std::span<const unsigned char> key);
    RecordBytes unwrap (std::span<const unsigned char> id, std::span<const unsigned char> ciphertext,
                        std::span<const unsigned char> pin);
    void remove (std::span<const unsigned char> id, const std::string& label, ProtectedKeyKind);

  private:
    struct State;
    std::unique_ptr<State> state;
  };
}  // namespace oro::runtime::native
#endif
#endif
