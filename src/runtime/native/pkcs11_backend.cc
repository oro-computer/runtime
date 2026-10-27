#include "pkcs11_backend.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include <pkcs11/pkcs11.h>
#include <sodium.h>
#include <dlfcn.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace oro::runtime::native {
  namespace {
    void checked (CK_RV value) {
      switch (value) {
        case CKR_OK:
          return;
        case CKR_PIN_INCORRECT:
        case CKR_PIN_INVALID:
        case CKR_PIN_LEN_RANGE:
        case CKR_USER_NOT_LOGGED_IN:
          throw StateError("EACCES");
        case CKR_PIN_LOCKED:
          throw StateError("EAUTHLOCKED");
        case CKR_TOKEN_NOT_PRESENT:
        case CKR_TOKEN_NOT_RECOGNIZED:
        case CKR_DEVICE_REMOVED:
        case CKR_SLOT_ID_INVALID:
        case CKR_SESSION_HANDLE_INVALID:
          throw StateError("ENODEV");
        case CKR_KEY_HANDLE_INVALID:
        case CKR_OBJECT_HANDLE_INVALID:
          throw StateError("EKEYREVOKED");
        case CKR_MECHANISM_INVALID:
        case CKR_MECHANISM_PARAM_INVALID:
        case CKR_FUNCTION_NOT_SUPPORTED:
        case CKR_TEMPLATE_INCONSISTENT:
        case CKR_ATTRIBUTE_TYPE_INVALID:
        case CKR_ATTRIBUTE_READ_ONLY:
        case CKR_USER_TYPE_INVALID:
          throw StateError("ENOTSUP");
        case CKR_ENCRYPTED_DATA_INVALID:
        case CKR_ENCRYPTED_DATA_LEN_RANGE:
        case CKR_SIGNATURE_INVALID:
          throw StateError("EBADMSG");
        case CKR_FUNCTION_CANCELED:
          throw StateError("ABORT_ERR");
        case CKR_DEVICE_MEMORY:
          throw StateError("ENOSPC");
        case CKR_BUFFER_TOO_SMALL:
          throw StateError("E2BIG");
        case CKR_SESSION_COUNT:
          throw StateError("EBUSY");
        case CKR_OPERATION_ACTIVE:
          throw StateError("EBUSY");
        case CKR_ARGUMENTS_BAD:
        case CKR_DATA_LEN_RANGE:
        case CKR_KEY_SIZE_RANGE:
          throw StateError("EINVAL");
        case CKR_TOKEN_WRITE_PROTECTED:
        case CKR_PIN_EXPIRED:
          throw StateError("EACCES");
        default:
          throw StateError("EIO");
      }
    }
    std::string hash (const std::string& value) {
      if (sodium_init() < 0)
        throw StateError("ENOSYS");
      std::array<unsigned char, 32> bytes;
      std::array<char, 65> output;
      crypto_hash_sha256(bytes.data(), reinterpret_cast<const unsigned char*>(value.data()), value.size());
      sodium_bin2hex(output.data(), output.size(), bytes.data(), bytes.size());
      return output.data();
    }
    std::string fixed (const unsigned char* value, size_t size) {
      std::string result(reinterpret_cast<const char*>(value), size);
      while (!result.empty() && (result.back() == ' ' || result.back() == '\0'))
        result.pop_back();
      return result;
    }
    bool functions (CK_FUNCTION_LIST_PTR value) {
      return value && (value->version.major == 2 || value->version.major == 3) && value->C_GetInfo &&
             value->C_GetSlotList && value->C_GetSlotInfo && value->C_GetTokenInfo && value->C_GetMechanismInfo &&
             value->C_OpenSession && value->C_CloseSession && value->C_Login && value->C_Logout &&
             value->C_FindObjectsInit && value->C_FindObjects && value->C_FindObjectsFinal &&
             value->C_GetAttributeValue && value->C_GenerateKeyPair && value->C_DestroyObject && value->C_SignInit &&
             value->C_Sign && value->C_EncryptInit && value->C_Encrypt && value->C_DecryptInit && value->C_Decrypt;
    }
    constexpr unsigned char curve[] = {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
    constexpr unsigned char spki[] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
                                      0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};
  }  // namespace

  class Pkcs11Module {
  public:
    CK_FUNCTION_LIST_PTR api = nullptr;
    std::string identity;
    std::string provider;
    std::mutex mutex;
    bool poisoned = false;
    Pkcs11Module(CK_FUNCTION_LIST_PTR api, const std::string& name) : api(api) {
      if (!functions(api))
        throw StateError("ENOTSUP");
      CK_INFO info{};
      checked(api->C_GetInfo(&info));
      const auto description = fixed(info.libraryDescription, sizeof(info.libraryDescription));
      auto lower = description;
      std::transform(lower.begin(), lower.end(), lower.begin(), [] (unsigned char ch) { return std::tolower(ch); });
      provider = lower.find("tpm") != std::string::npos ? "tpm2" : "pkcs11";
      identity = hash(name + ":" + fixed(info.manufacturerID, sizeof(info.manufacturerID)) + ":" + description);
    }
  };

  namespace {
    // Provider code and its function tables stay mapped until process exit.
    // Workers must drain before their runtime resources are destroyed.
    struct Modules {
      std::mutex mutex;
      std::map<std::string, std::vector<std::shared_ptr<Pkcs11Module>>> loaded;
      std::map<std::string, std::string> failed;
      std::vector<std::shared_ptr<Pkcs11Module>> get (const std::string& path) {
        std::lock_guard lock(mutex);
        if (const auto found = loaded.find(path); found != loaded.end())
          return found->second;
        if (const auto found = failed.find(path); found != failed.end())
          throw StateError(found->second);
        if (loaded.size() + failed.size() >= 32)
          throw StateError("E2BIG");
        std::vector<std::shared_ptr<Pkcs11Module>> result;
        try {
          if (!path.empty()) {
            struct stat info {};
            if (!std::filesystem::path(path).is_absolute() || stat(path.c_str(), &info) || !S_ISREG(info.st_mode) ||
                (info.st_uid != 0 && info.st_uid != getuid()) || (info.st_mode & 0022))
              throw StateError("EACCES");
            const auto library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
            if (!library)
              throw StateError("ENOTREADY");
            const auto get = reinterpret_cast<CK_RV (*)(CK_FUNCTION_LIST_PTR_PTR)>(dlsym(library, "C_GetFunctionList"));
            CK_FUNCTION_LIST_PTR api = nullptr;
            if (!get || get(&api) != CKR_OK || !functions(api) || !api->C_Initialize)
              throw StateError("ENOTSUP");
            CK_C_INITIALIZE_ARGS args{};
            args.flags = CKF_OS_LOCKING_OK;
            const auto initialized = api->C_Initialize(&args);
            if (initialized != CKR_OK && initialized != CKR_CRYPTOKI_ALREADY_INITIALIZED)
              checked(initialized);
            result.push_back(std::make_shared<Pkcs11Module>(api, path));
          } else {
            const auto library = dlopen("libp11-kit.so.0", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
            if (!library)
              throw StateError("ENOSYS");
            const auto load =
              reinterpret_cast<CK_FUNCTION_LIST_PTR* (*)(int)>(dlsym(library, "p11_kit_modules_load_and_initialize"));
            const auto name =
              reinterpret_cast<char* (*)(CK_FUNCTION_LIST_PTR)>(dlsym(library, "p11_kit_module_get_name"));
            if (!load || !name)
              throw StateError("ENOSYS");
            const auto modules = load(0);
            if (!modules)
              throw StateError("ENOTREADY");
            for (size_t i = 0; modules[i]; i++) {
              if (i >= 64)
                throw StateError("E2BIG");
              auto label = name(modules[i]);
              const std::string identifier = label ? label : "unnamed";
              free(label);
              try {
                result.push_back(std::make_shared<Pkcs11Module>(modules[i], identifier));
              } catch (const StateError& error) {
                if (error.code != "ENOTSUP")
                  throw;
              }
            }
          }
          loaded[path] = result;
          return result;
        } catch (const StateError& error) {
          failed[path] = error.code;
          throw;
        }
      }
    };
    Modules& modules () {
      static Modules instance;
      return instance;
    }
    bool mechanism (CK_FUNCTION_LIST_PTR api, CK_SLOT_ID slot, CK_MECHANISM_TYPE type, CK_FLAGS flags,
                    size_t bits = 0) {
      CK_MECHANISM_INFO info{};
      if (api->C_GetMechanismInfo(slot, type, &info) != CKR_OK || (info.flags & flags) != flags)
        return false;
      return !bits || (info.ulMinKeySize <= bits && info.ulMaxKeySize >= bits);
    }
  }  // namespace

  std::vector<Pkcs11Token> discoverPkcs11 (const Pkcs11Options& options) {
    if ((options.provider != "auto" && options.provider != "tpm2" && options.provider != "pkcs11") ||
        options.tokenSerial.size() > 16 ||
        (options.rsaBits != 2048 && options.rsaBits != 3072 && options.rsaBits != 4096))
      throw StateError("EINVAL");
    std::vector<Pkcs11Token> tokens;
    for (const auto& module : modules().get(options.module)) {
      std::lock_guard lock(module->mutex);
      if (module->poisoned)
        continue;
      if (options.provider != "auto" && options.provider != module->provider)
        continue;
      std::array<CK_SLOT_ID, 64> slots;
      CK_ULONG count = slots.size();
      checked(module->api->C_GetSlotList(CK_TRUE, slots.data(), &count));
      if (count > slots.size())
        throw StateError("E2BIG");
      for (size_t i = 0; i < count; i++) {
        CK_SLOT_INFO slot{};
        CK_TOKEN_INFO info{};
        if (module->api->C_GetSlotInfo(slots[i], &slot) != CKR_OK || !(slot.flags & CKF_HW_SLOT) ||
            module->api->C_GetTokenInfo(slots[i], &info) != CKR_OK || !(info.flags & CKF_TOKEN_INITIALIZED) ||
            !(info.flags & CKF_USER_PIN_INITIALIZED))
          continue;
        const auto serial = fixed(info.serialNumber, sizeof(info.serialNumber));
        if (serial.empty() || (!options.tokenSerial.empty() && serial != options.tokenSerial))
          continue;
        Pkcs11Token token;
        token.module = module;
        token.slot = slots[i];
        token.provider = module->provider;
        token.moduleIdentity = module->identity;
        token.tokenIdentity = hash(serial);
        token.protectedPath = info.flags & CKF_PROTECTED_AUTHENTICATION_PATH;
        token.pinLocked = info.flags & (CKF_USER_PIN_LOCKED | CKF_USER_PIN_FINAL_TRY);
        token.minimumPinBytes = std::max<size_t>(1, info.ulMinPinLen);
        token.maximumPinBytes = std::min<size_t>(256, info.ulMaxPinLen);
        token.signing = mechanism(module->api, slots[i], CKM_EC_KEY_PAIR_GEN, CKF_GENERATE_KEY_PAIR, 256) &&
                        mechanism(module->api, slots[i], CKM_ECDSA, CKF_SIGN, 256);
        token.sealing =
          mechanism(module->api, slots[i], CKM_RSA_PKCS_KEY_PAIR_GEN, CKF_GENERATE_KEY_PAIR, options.rsaBits) &&
          mechanism(module->api, slots[i], CKM_RSA_PKCS_OAEP, CKF_ENCRYPT | CKF_DECRYPT, options.rsaBits);
        if ((token.signing || token.sealing) && token.minimumPinBytes <= token.maximumPinBytes)
          tokens.push_back(token);
        if (tokens.size() > 32)
          throw StateError("E2BIG");
      }
    }
    std::stable_sort(tokens.begin(), tokens.end(), [] (const auto& left, const auto& right) {
      return left.provider == "tpm2" && right.provider != "tpm2";
    });
    return tokens;
  }

  struct Pkcs11Session::State {
    Pkcs11Token token;
    std::unique_lock<std::mutex> lock;
    CK_SESSION_HANDLE handle = CK_INVALID_HANDLE;
    bool ownLogin = false;
    explicit State(const Pkcs11Token& token) : token(token), lock(token.module->mutex) {
      if (token.module->poisoned)
        throw StateError("ENOTREADY");
    }
    ~State() {
      if (!handle)
        return;
      if (ownLogin)
        token.module->api->C_Logout(handle);
      const auto result = token.module->api->C_CloseSession(handle);
      if (result != CKR_OK && result != CKR_SESSION_HANDLE_INVALID && result != CKR_TOKEN_NOT_PRESENT &&
          result != CKR_DEVICE_REMOVED)
        token.module->poisoned = true;
    }
    CK_FUNCTION_LIST_PTR api () {
      return token.module->api;
    }
    CK_OBJECT_HANDLE find (std::span<const unsigned char> id, CK_OBJECT_CLASS kind) {
      if (id.empty() || id.size() > 64)
        throw StateError("EINVAL");
      CK_ATTRIBUTE attributes[] = {{CKA_CLASS, &kind, sizeof(kind)},
                                   {CKA_ID, const_cast<unsigned char*>(id.data()), id.size()}};
      checked(api()->C_FindObjectsInit(handle, attributes, 2));
      std::array<CK_OBJECT_HANDLE, 2> objects;
      CK_ULONG count = 0;
      const auto result = api()->C_FindObjects(handle, objects.data(), objects.size(), &count);
      const auto finished = api()->C_FindObjectsFinal(handle);
      checked(result);
      checked(finished);
      if (count > 1)
        throw StateError("EKEYREVOKED");
      return count ? objects[0] : CK_INVALID_HANDLE;
    }
    RecordBytes attribute (CK_OBJECT_HANDLE object, CK_ATTRIBUTE_TYPE type, size_t maximum = 1024) {
      CK_ATTRIBUTE value{type, nullptr, 0};
      checked(api()->C_GetAttributeValue(handle, object, &value, 1));
      if (value.ulValueLen == CK_UNAVAILABLE_INFORMATION || value.ulValueLen > maximum)
        throw StateError("EKEYREVOKED");
      RecordBytes bytes(value.ulValueLen);
      value.pValue = bytes.data();
      checked(api()->C_GetAttributeValue(handle, object, &value, 1));
      if (value.ulValueLen != bytes.size())
        throw StateError("EKEYREVOKED");
      return bytes;
    }
    template <typename T>
    T scalar (CK_OBJECT_HANDLE object, CK_ATTRIBUTE_TYPE type) {
      const auto bytes = attribute(object, type, sizeof(T));
      if (bytes.size() != sizeof(T))
        throw StateError("EKEYREVOKED");
      T value;
      memcpy(&value, bytes.data(), sizeof(T));
      return value;
    }
    CK_OBJECT_HANDLE privateKey (std::span<const unsigned char> id, ProtectedKeyKind kind) {
      const auto object = find(id, CKO_PRIVATE_KEY);
      if (!object)
        throw StateError("EKEYREVOKED");
      for (const auto type : {CKA_TOKEN, CKA_PRIVATE, CKA_SENSITIVE, CKA_ALWAYS_SENSITIVE, CKA_NEVER_EXTRACTABLE,
                              CKA_ALWAYS_AUTHENTICATE, CKA_LOCAL})
        if (scalar<CK_BBOOL>(object, type) != CK_TRUE)
          throw StateError("EKEYREVOKED");
      for (const auto type : {CKA_EXTRACTABLE, CKA_MODIFIABLE, CKA_COPYABLE})
        if (scalar<CK_BBOOL>(object, type) != CK_FALSE)
          throw StateError("EKEYREVOKED");
      if (scalar<CK_KEY_TYPE>(object, CKA_KEY_TYPE) != (kind == ProtectedKeyKind::Signing ? CKK_EC : CKK_RSA) ||
          scalar<CK_BBOOL>(object, kind == ProtectedKeyKind::Signing ? CKA_SIGN : CKA_DECRYPT) != CK_TRUE)
        throw StateError("EKEYREVOKED");
      return object;
    }
    void authenticate (std::span<const unsigned char> pin) {
      const auto result = api()->C_Login(handle, CKU_CONTEXT_SPECIFIC,
                                         token.protectedPath ? nullptr : const_cast<unsigned char*>(pin.data()),
                                         token.protectedPath ? 0 : pin.size());
      if (result == CKR_USER_ALREADY_LOGGED_IN)
        throw StateError("EACCES");
      checked(result);
    }
  };

  Pkcs11Session::Pkcs11Session(const Pkcs11Token& token, std::span<const unsigned char> pin)
      : state(std::make_unique<State>(token)) {
    if (token.pinLocked)
      throw StateError("EAUTHLOCKED");
    if (!token.protectedPath && (pin.size() < token.minimumPinBytes || pin.size() > token.maximumPinBytes))
      throw StateError("EACCES");
    checked(
      state->api()->C_OpenSession(token.slot, CKF_SERIAL_SESSION | CKF_RW_SESSION, nullptr, nullptr, &state->handle));
    const auto result = state->api()->C_Login(state->handle, CKU_USER,
                                              token.protectedPath ? nullptr : const_cast<unsigned char*>(pin.data()),
                                              token.protectedPath ? 0 : pin.size());
    state->ownLogin = result == CKR_OK;
    if (result != CKR_USER_ALREADY_LOGGED_IN)
      checked(result);
  }
  Pkcs11Session::~Pkcs11Session() = default;
  bool Pkcs11Session::exists(std::span<const unsigned char> id, ProtectedKeyKind) {
    return state->find(id, CKO_PRIVATE_KEY) || state->find(id, CKO_PUBLIC_KEY);
  }

  void Pkcs11Session::create(std::span<const unsigned char> id, const std::string& label, ProtectedKeyKind kind,
                             unsigned int rsaBits) {
    if (exists(id, kind))
      throw StateError("EKEYREVOKED");
    if (id.empty() || id.size() > 64 || label.empty() || label.size() > 128)
      throw StateError("EINVAL");
    CK_BBOOL yes = CK_TRUE, no = CK_FALSE;
    CK_ULONG bits = rsaBits;
    unsigned char exponent[] = {1, 0, 1};
    std::vector<CK_ATTRIBUTE> pub{{CKA_TOKEN, &yes, sizeof(yes)},
                                  {CKA_PRIVATE, &no, sizeof(no)},
                                  {CKA_ID, const_cast<unsigned char*>(id.data()), id.size()},
                                  {CKA_LABEL, const_cast<char*>(label.data()), label.size()},
                                  {CKA_MODIFIABLE, &no, sizeof(no)},
                                  {CKA_COPYABLE, &no, sizeof(no)}};
    std::vector<CK_ATTRIBUTE> priv{{CKA_TOKEN, &yes, sizeof(yes)},
                                   {CKA_PRIVATE, &yes, sizeof(yes)},
                                   {CKA_ID, const_cast<unsigned char*>(id.data()), id.size()},
                                   {CKA_LABEL, const_cast<char*>(label.data()), label.size()},
                                   {CKA_SENSITIVE, &yes, sizeof(yes)},
                                   {CKA_EXTRACTABLE, &no, sizeof(no)},
                                   {CKA_ALWAYS_AUTHENTICATE, &yes, sizeof(yes)},
                                   {CKA_MODIFIABLE, &no, sizeof(no)},
                                   {CKA_COPYABLE, &no, sizeof(no)},
                                   {CKA_DESTROYABLE, &yes, sizeof(yes)}};
    CK_MECHANISM mechanism{kind == ProtectedKeyKind::Signing ? CKM_EC_KEY_PAIR_GEN : CKM_RSA_PKCS_KEY_PAIR_GEN, nullptr,
                           0};
    if (kind == ProtectedKeyKind::Signing) {
      pub.push_back({CKA_EC_PARAMS, const_cast<unsigned char*>(curve), sizeof(curve)});
      pub.push_back({CKA_VERIFY, &yes, sizeof(yes)});
      priv.push_back({CKA_SIGN, &yes, sizeof(yes)});
    } else {
      if (rsaBits != 2048 && rsaBits != 3072 && rsaBits != 4096)
        throw StateError("EINVAL");
      pub.push_back({CKA_MODULUS_BITS, &bits, sizeof(bits)});
      pub.push_back({CKA_PUBLIC_EXPONENT, exponent, sizeof(exponent)});
      pub.push_back({CKA_ENCRYPT, &yes, sizeof(yes)});
      priv.push_back({CKA_DECRYPT, &yes, sizeof(yes)});
    }
    CK_OBJECT_HANDLE publicObject = 0, privateObject = 0;
    checked(state->api()->C_GenerateKeyPair(state->handle, &mechanism, pub.data(), pub.size(), priv.data(), priv.size(),
                                            &publicObject, &privateObject));
    inspect(id, kind);
  }

  ProtectedKeyMaterial Pkcs11Session::inspect(std::span<const unsigned char> id, ProtectedKeyKind kind) {
    state->privateKey(id, kind);
    const auto object = state->find(id, CKO_PUBLIC_KEY);
    if (!object || state->scalar<CK_BBOOL>(object, CKA_TOKEN) != CK_TRUE ||
        state->scalar<CK_KEY_TYPE>(object, CKA_KEY_TYPE) != (kind == ProtectedKeyKind::Signing ? CKK_EC : CKK_RSA))
      throw StateError("EKEYREVOKED");
    for (const auto type : {CKA_MODIFIABLE, CKA_COPYABLE})
      if (state->scalar<CK_BBOOL>(object, type) != CK_FALSE)
        throw StateError("EKEYREVOKED");
    ProtectedKeyMaterial result;
    if (kind == ProtectedKeyKind::Signing) {
      if (state->attribute(object, CKA_EC_PARAMS, sizeof(curve)) != RecordBytes(std::begin(curve), std::end(curve)))
        throw StateError("EKEYREVOKED");
      auto point = state->attribute(object, CKA_EC_POINT, 128);
      if (point.size() == 67 && point[0] == 4 && point[1] == 65)
        point.erase(point.begin(), point.begin() + 2);
      if (point.size() != 65 || point[0] != 4)
        throw StateError("EKEYREVOKED");
      result.publicKey.assign(std::begin(spki), std::end(spki));
      result.publicKey.insert(result.publicKey.end(), point.begin(), point.end());
      result.fingerprintMaterial = result.publicKey;
    } else {
      auto modulus = state->attribute(object, CKA_MODULUS, 512);
      const auto exponent = state->attribute(object, CKA_PUBLIC_EXPONENT, 32);
      if ((modulus.size() != 256 && modulus.size() != 384 && modulus.size() != 512) || !(modulus[0] & 128) ||
          exponent != (RecordBytes{1, 0, 1}))
        throw StateError("EKEYREVOKED");
      result.wrappingBytes = modulus.size();
      result.fingerprintMaterial = std::move(modulus);
      result.fingerprintMaterial.insert(result.fingerprintMaterial.end(), exponent.begin(), exponent.end());
    }
    return result;
  }

  RecordBytes Pkcs11Session::sign(std::span<const unsigned char> id, std::span<const unsigned char> digest,
                                  std::span<const unsigned char> pin) {
    if (digest.size() != 32)
      throw StateError("EINVAL");
    CK_MECHANISM mechanism{CKM_ECDSA, nullptr, 0};
    checked(state->api()->C_SignInit(state->handle, &mechanism, state->privateKey(id, ProtectedKeyKind::Signing)));
    state->authenticate(pin);
    RecordBytes signature(64);
    CK_ULONG size = signature.size();
    checked(state->api()->C_Sign(state->handle, const_cast<unsigned char*>(digest.data()), digest.size(),
                                 signature.data(), &size));
    if (size != signature.size())
      throw StateError("EBADMSG");
    return signature;
  }

  RecordBytes Pkcs11Session::wrap(std::span<const unsigned char> id, std::span<const unsigned char> key) {
    if (key.size() != 32)
      throw StateError("EINVAL");
    CK_RSA_PKCS_OAEP_PARAMS parameters{CKM_SHA256, CKG_MGF1_SHA256, CKZ_DATA_SPECIFIED, nullptr, 0};
    CK_MECHANISM mechanism{CKM_RSA_PKCS_OAEP, &parameters, sizeof(parameters)};
    const auto object = state->find(id, CKO_PUBLIC_KEY);
    if (!object)
      throw StateError("EKEYREVOKED");
    const auto material = inspect(id, ProtectedKeyKind::Secret);
    RecordBytes output(material.wrappingBytes);
    checked(state->api()->C_EncryptInit(state->handle, &mechanism, object));
    CK_ULONG size = output.size();
    checked(
      state->api()->C_Encrypt(state->handle, const_cast<unsigned char*>(key.data()), key.size(), output.data(), &size));
    if (size != output.size())
      throw StateError("EBADMSG");
    output.resize(size);
    return output;
  }

  RecordBytes Pkcs11Session::unwrap(std::span<const unsigned char> id, std::span<const unsigned char> ciphertext,
                                    std::span<const unsigned char> pin) {
    if (ciphertext.size() != 256 && ciphertext.size() != 384 && ciphertext.size() != 512)
      throw StateError("EINVAL");
    CK_RSA_PKCS_OAEP_PARAMS parameters{CKM_SHA256, CKG_MGF1_SHA256, CKZ_DATA_SPECIFIED, nullptr, 0};
    CK_MECHANISM mechanism{CKM_RSA_PKCS_OAEP, &parameters, sizeof(parameters)};
    checked(state->api()->C_DecryptInit(state->handle, &mechanism, state->privateKey(id, ProtectedKeyKind::Secret)));
    state->authenticate(pin);
    RecordBytes output(512);
    CK_ULONG size = output.size();
    const auto result = state->api()->C_Decrypt(state->handle, const_cast<unsigned char*>(ciphertext.data()),
                                                ciphertext.size(), output.data(), &size);
    if (result != CKR_OK || size != 32) {
      sodium_memzero(output.data(), output.size());
      checked(result);
      throw StateError("EBADMSG");
    }
    output.resize(size);
    return output;
  }

  void Pkcs11Session::remove(std::span<const unsigned char> id, const std::string& label, ProtectedKeyKind) {
    for (const auto kind : {CKO_PRIVATE_KEY, CKO_PUBLIC_KEY}) {
      const auto object = state->find(id, kind);
      if (!object)
        continue;
      const auto bytes = state->attribute(object, CKA_LABEL, 128);
      if (std::string(bytes.begin(), bytes.end()) != label)
        throw StateError("EACCES");
      checked(state->api()->C_DestroyObject(state->handle, object));
    }
    if (exists(id, ProtectedKeyKind::Secret))
      throw StateError("EUNKNOWN");
  }
}  // namespace oro::runtime::native
#endif
