#include "protected_keys.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include "desktop_dialog.hh"
#include "web_push.hh"
#include <sodium.h>
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) && ORO_RUNTIME_ENABLE_MBEDTLS
#include <mbedtls/gcm.h>
#include <mbedtls/ecdsa.h>
#endif
#include <condition_variable>
#include <thread>
#include <mutex>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <set>
#include <unistd.h>

namespace oro::runtime::native {
  namespace {
    struct Secret {
      RecordBytes bytes;
      explicit Secret(RecordBytes bytes = {}) : bytes(std::move(bytes)) {
      }
      ~Secret() {
        sodium_memzero(bytes.data(), bytes.size());
      }
      Secret(const Secret&) = delete;
      Secret& operator=(const Secret&) = delete;
    };
    RecordBytes encode (const JSON::Any& value) {
      const auto source = value.str();
      return {source.begin(), source.end()};
    }
    JSON::Object decode (const RecordBytes& value) {
      try {
        return JSON::parse(std::string(value.begin(), value.end())).as<JSON::Object>();
      } catch (...) {
        throw StateError("EBADMSG");
      }
    }
    std::string text (const JSON::Object& value, const std::string& name, size_t maximum) {
      if (!value.has(name) || value.get(name).type != JSON::Type::String)
        throw StateError("EINVAL");
      const auto result = value.get(name).as<JSON::String>().value();
      if (result.size() > maximum || result.find('\0') != std::string::npos ||
          !g_utf8_validate(result.c_str(), result.size(), nullptr))
        throw StateError("EINVAL");
      return result;
    }
    void fields (const JSON::Object& value, std::initializer_list<std::string> allowed) {
      for (const auto& [key, item] : value)
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
          throw StateError("EINVAL");
    }
    bool identifier (const std::string& value) {
      return !value.empty() && value.size() <= 128 &&
             value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
               std::string::npos;
    }
    std::string base64 (std::span<const unsigned char> bytes) {
      std::string output(sodium_base64_encoded_len(bytes.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
      sodium_bin2base64(output.data(), output.size(), bytes.data(), bytes.size(), sodium_base64_VARIANT_ORIGINAL);
      output.resize(strlen(output.c_str()));
      return output;
    }
    RecordBytes unbase64 (const std::string& value, size_t minimum, size_t maximum) {
      if (value.size() > sodium_base64_encoded_len(maximum, sodium_base64_VARIANT_ORIGINAL))
        throw StateError("E2BIG");
      Secret outputBuffer{RecordBytes(maximum)};
      auto& output = outputBuffer.bytes;
      size_t size = 0;
      if (sodium_base642bin(output.data(), output.size(), value.data(), value.size(), nullptr, &size, nullptr,
                            sodium_base64_VARIANT_ORIGINAL) ||
          size < minimum || size > maximum)
        throw StateError("EINVAL");
      output.resize(size);
      if (base64(output) != value)
        throw StateError("EINVAL");
      return std::move(output);
    }
    RecordBytes digest (std::span<const unsigned char> bytes) {
      RecordBytes output(32);
      crypto_hash_sha256(output.data(), bytes.data(), bytes.size());
      return output;
    }
    RecordBytes digest (const std::string& value) {
      return digest(std::span(reinterpret_cast<const unsigned char*>(value.data()), value.size()));
    }
    std::string hex (std::span<const unsigned char> bytes) {
      std::string output(bytes.size() * 2 + 1, '\0');
      sodium_bin2hex(output.data(), output.size(), bytes.data(), bytes.size());
      output.pop_back();
      return output;
    }
    void active (const ExecutionCancellation& signal) {
      if (signal.cancelled())
        throw StateError("ABORT_ERR");
    }
    std::string origin (const std::string& value) {
      if (value.empty() || value.size() > 1024)
        throw StateError("EINVAL");
      for (const auto ch : value)
        if (static_cast<unsigned char>(ch) <= 32 || ch == 127)
          throw StateError("EINVAL");
      GError* error = nullptr;
      auto uri = g_uri_parse(value.c_str(), G_URI_FLAGS_NONE, &error);
      if (!uri) {
        if (error)
          g_error_free(error);
        throw StateError("EINVAL");
      }
      const auto scheme = g_uri_get_scheme(uri);
      const auto host = g_uri_get_host(uri);
      const auto path = g_uri_get_path(uri);
      const auto port = g_uri_get_port(uri);
      const bool valid = scheme && std::string(scheme) == "https" && host && *host && !g_uri_get_userinfo(uri) &&
                         !g_uri_get_query(uri) && !g_uri_get_fragment(uri) &&
                         (!path || !*path || std::string(path) == "/") && port != 0;
      std::string output;
      if (valid) {
        auto lower = g_ascii_strdown(host, -1);
        output = std::string("https://") + (strchr(lower, ':') ? "[" + std::string(lower) + "]" : std::string(lower));
        g_free(lower);
        if (port != -1 && port != 443)
          output += ":" + std::to_string(port);
      }
      g_uri_unref(uri);
      if (!valid)
        throw StateError("EINVAL");
      return output;
    }
    int64_t now () {
      return g_get_real_time() / 1000;
    }
    int64_t expiry (const JSON::Object& options) {
      if (!options.has("expiresAt") || options.get("expiresAt").type != JSON::Type::Number)
        throw StateError("EINVAL");
      const auto value = options.get("expiresAt").as<JSON::Number>().value();
      if (!std::isfinite(value) || std::floor(value) != value || value <= now() || value > now() + 300000)
        throw StateError("EINVAL");
      return static_cast<int64_t>(value);
    }
    JSON::Object commonStatus (const std::string& policy, bool enabled, bool busy) {
      if (policy != "biometric" && policy != "biometric-or-device")
        throw StateError("EINVAL");
      return JSON::Object::Entries{{"platform", "linux"},
                                   {"supported", policy == "biometric-or-device"},
                                   {"enabled", enabled},
                                   {"canAuthenticate", false},
                                   {"statusCode", -1},
                                   {"policy", policy},
                                   {"cryptographicOperationsOnly", true},
                                   {"serverVerificationRequired", true},
                                   {"busy", busy},
                                   {"biometricOnlySupported", false},
                                   {"credentialType", "key-provider-pin"},
                                   {"providerPreference", "tpm2-then-hardware-pkcs11"},
                                   {"hardwareAttestationProvided", false}};
    }
#if defined(ORO_RUNTIME_ENABLE_MBEDTLS) && ORO_RUNTIME_ENABLE_MBEDTLS
    struct Gcm {
      mbedtls_gcm_context value;
      Gcm() {
        mbedtls_gcm_init(&value);
      }
      ~Gcm() {
        mbedtls_gcm_free(&value);
      }
    };
    RecordBytes crypt (bool decrypt, const RecordBytes& key, const RecordBytes& iv, const std::string& aad,
                       const RecordBytes& input) {
      if (key.size() != 32 || iv.size() != 12 || input.size() > 65552 || (decrypt && input.size() < 16))
        throw StateError("EINVAL");
      Gcm cipher;
      if (mbedtls_gcm_setkey(&cipher.value, MBEDTLS_CIPHER_ID_AES, key.data(), 256))
        throw StateError("ENOSYS");
      RecordBytes output(decrypt ? input.size() - 16 : input.size() + 16);
      const auto result =
        decrypt ? mbedtls_gcm_auth_decrypt(&cipher.value, output.size(), iv.data(), iv.size(),
                                           reinterpret_cast<const unsigned char*>(aad.data()), aad.size(),
                                           input.data() + output.size(), 16, input.data(), output.data())
                : mbedtls_gcm_crypt_and_tag(&cipher.value, MBEDTLS_GCM_ENCRYPT, input.size(), iv.data(), iv.size(),
                                            reinterpret_cast<const unsigned char*>(aad.data()), aad.size(),
                                            input.data(), output.data(), 16, output.data() + input.size());
      if (result) {
        sodium_memzero(output.data(), output.size());
        throw StateError("EBADMSG");
      }
      return output;
    }
    RecordBytes signature (const RecordBytes& publicKey, const RecordBytes& hash, const RecordBytes& raw) {
      if (publicKey.size() != 91 || hash.size() != 32 || raw.size() != 64)
        throw StateError("EBADMSG");
      validateWebPushPublicKey(std::span(publicKey.data() + 26, 65));
      mbedtls_ecp_group group;
      mbedtls_ecp_point point;
      mbedtls_mpi r, s;
      mbedtls_ecp_group_init(&group);
      mbedtls_ecp_point_init(&point);
      mbedtls_mpi_init(&r);
      mbedtls_mpi_init(&s);
      struct Cleanup {
        mbedtls_ecp_group* group;
        mbedtls_ecp_point* point;
        mbedtls_mpi* r;
        mbedtls_mpi* s;
        ~Cleanup() {
          mbedtls_ecp_group_free(group);
          mbedtls_ecp_point_free(point);
          mbedtls_mpi_free(r);
          mbedtls_mpi_free(s);
        }
      } cleanup{&group, &point, &r, &s};
      if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) ||
          mbedtls_ecp_point_read_binary(&group, &point, publicKey.data() + 26, 65) ||
          mbedtls_mpi_read_binary(&r, raw.data(), 32) || mbedtls_mpi_read_binary(&s, raw.data() + 32, 32) ||
          mbedtls_ecdsa_verify(&group, hash.data(), hash.size(), &point, &r, &s))
        throw StateError("EBADMSG");
      auto integer = [] (const unsigned char* bytes) {
        size_t skip = 0;
        while (skip < 31 && bytes[skip] == 0)
          skip++;
        RecordBytes result{2, static_cast<unsigned char>(32 - skip + ((bytes[skip] & 128) ? 1 : 0))};
        if (bytes[skip] & 128)
          result.push_back(0);
        result.insert(result.end(), bytes + skip, bytes + 32);
        return result;
      };
      const auto left = integer(raw.data());
      const auto right = integer(raw.data() + 32);
      RecordBytes result{0x30, static_cast<unsigned char>(left.size() + right.size())};
      result.insert(result.end(), left.begin(), left.end());
      result.insert(result.end(), right.begin(), right.end());
      return result;
    }
#endif
  }  // namespace

  struct ProtectedKeys::State : std::enable_shared_from_this<State> {
    RecordStore store;
    Pkcs11Options options;
    std::string application;
    std::shared_ptr<void> lease;
    PinPrompt prompt;
    mutable std::mutex mutex;
    std::condition_variable changed;
    bool running = false;
    bool closing = false;
    std::shared_ptr<ExecutionCancellation> cancellation;
    std::thread::id worker;
    State(RecordStore store, Pkcs11Options options, std::string application, std::shared_ptr<void> lease,
          PinPrompt prompt)
        : store(std::move(store)),
          options(std::move(options)),
          application(std::move(application)),
          lease(std::move(lease)),
          prompt(std::move(prompt)) {
    }

    JSON::Any perform (const std::string& action, const JSON::Object& input, const std::shared_ptr<GtkWindow>& parent,
                       const std::shared_ptr<ExecutionCancellation>& signal) {
#if !defined(ORO_RUNTIME_ENABLE_MBEDTLS) || !ORO_RUNTIME_ENABLE_MBEDTLS
      throw StateError("ENOSYS");
#else
      active(*signal);
      const auto id = text(input, "id", 128);
      const auto policy = text(input, "policy", 32);
      if (!identifier(id))
        throw StateError("EINVAL");
      if (policy != "biometric-or-device")
        throw StateError(policy == "biometric" ? "ENOTSUP" : "EINVAL");
      ProtectedKeyKind kind = ProtectedKeyKind::Secret;
      if (action == "createSigningKey" || action == "signChallenge")
        kind = ProtectedKeyKind::Signing;
      if (action == "removeKey") {
        fields(input, {"id", "policy", "kind"});
        const auto name = text(input, "kind", 16);
        if (name != "secret" && name != "signing")
          throw StateError("EINVAL");
        kind = name == "signing" ? ProtectedKeyKind::Signing : ProtectedKeyKind::Secret;
      } else if (action == "seal")
        fields(input, {"id", "policy", "value"});
      else if (action == "open")
        fields(input, {"id", "policy", "sealed"});
      else if (kind == ProtectedKeyKind::Signing)
        fields(input, {"id", "policy", "authority", "accountRef", "challenge", "expiresAt"});
      else
        throw StateError("EINVAL");
      const auto name = kind == ProtectedKeyKind::Secret ? "secret" : "signing";
      const auto keyId =
        digest("oro.protected.v2:" + std::to_string(getuid()) + ":" + application + ":" + id + ":" + name);
      const auto recordKey = "key." + hex(keyId);
      const auto label = "Oro:" + hex(digest(application)).substr(0, 16) + ":" + name;
      auto snapshot = store.read();
      const auto found = snapshot.records.find(recordKey);
      JSON::Object record;
      const bool existing = found != snapshot.records.end();
      if (existing) {
        record = decode(found->second);
        if (text(record, "id", 128) != id || text(record, "kind", 16) != name || text(record, "policy", 32) != policy)
          throw StateError("EKEYREVOKED");
        if (text(record, "phase", 16) != "ready" && action != "removeKey")
          throw StateError("EUNKNOWN");
      } else if (action != "seal" && action != "createSigningKey")
        throw StateError("ENOENT");
      if (!existing && snapshot.records.size() >= 64)
        throw StateError("ENOSPC");
      Secret plain;
      RecordBytes iv, ciphertext, wrapped;
      JSON::Object sealed;
      std::string signedPayload;
      int64_t deadline = 0;
      if (action == "seal")
        plain.bytes = unbase64(text(input, "value", 87384), 0, 65536);
      if (action == "open") {
        if (!input.has("sealed") || input.get("sealed").type != JSON::Type::Object)
          throw StateError("EINVAL");
        sealed = input.get("sealed").as<JSON::Object>();
        fields(sealed, {"schema", "id", "policy", "iv", "ciphertext", "wrappedKey", "binding", "wrappingKeyBits"});
        if (!sealed.has("schema") || sealed.get("schema").type != JSON::Type::Number ||
            sealed.get("schema").as<JSON::Number>().value() != 2 || text(sealed, "id", 128) != id ||
            text(sealed, "policy", 32) != policy)
          throw StateError("EINVAL");
        iv = unbase64(text(sealed, "iv", 24), 12, 12);
        ciphertext = unbase64(text(sealed, "ciphertext", 87408), 16, 65552);
        wrapped = unbase64(text(sealed, "wrappedKey", 684), 256, 512);
      }
      if (kind == ProtectedKeyKind::Signing && action != "removeKey") {
        const auto account = text(input, "accountRef", 128);
        if (!identifier(account))
          throw StateError("EINVAL");
        const auto authority = origin(text(input, "authority", 1024));
        const auto challenge = text(input, "challenge", 1368);
        unbase64(challenge, 16, 1024);
        deadline = expiry(input);
        signedPayload = JSON::Object(JSON::Object::Entries{{"schema", 1},
                                                           {"purpose", "oro-account-authentication"},
                                                           {"application", application},
                                                           {"authority", authority},
                                                           {"accountRef", account},
                                                           {"keyId", id},
                                                           {"challenge", challenge},
                                                           {"expiresAt", deadline}})
                          .str();
      }
      auto selectedOptions = options;
      if (existing && kind == ProtectedKeyKind::Secret && record.has("rsaBits")) {
        if (record.get("rsaBits").type != JSON::Type::Number)
          throw StateError("EBADMSG");
        selectedOptions.rsaBits = static_cast<unsigned int>(record.get("rsaBits").as<JSON::Number>().value());
      }
      auto candidates = discoverPkcs11(selectedOptions);
      std::vector<Pkcs11Token> eligible;
      for (const auto& token : candidates) {
        if (existing) {
          if (token.moduleIdentity == text(record, "module", 64) && token.tokenIdentity == text(record, "token", 64))
            eligible.push_back(token);
        } else if ((kind == ProtectedKeyKind::Secret && token.sealing) ||
                   (kind == ProtectedKeyKind::Signing && token.signing))
          eligible.push_back(token);
      }
      if (!existing && !eligible.empty() && eligible.front().provider == "tpm2")
        eligible.erase(
          std::remove_if(eligible.begin(), eligible.end(), [] (const auto& value) { return value.provider != "tpm2"; }),
          eligible.end());
      if (eligible.empty())
        throw StateError("ENODEV");
      if (eligible.size() != 1)
        throw StateError("EAGAIN");
      const auto token = eligible.front();
      if (token.pinLocked)
        throw StateError("EAUTHLOCKED");
      active(*signal);
      Secret pin(prompt ? prompt(token, signal)
                        : promptNativePin(parent.get(),
                                          token.provider == "tpm2" ? "Authorize TPM key operation"
                                                                   : "Authorize security token operation",
                                          token.protectedPath, signal));
      active(*signal);
      Pkcs11Session session(token, pin.bytes);
      active(*signal);
      if (action == "removeKey") {
        record["phase"] = "removing";
        snapshot = store.apply(snapshot.revision, {{recordKey, encode(record)}});
        try {
          session.remove(keyId, label, kind);
        } catch (...) {
          throw StateError("EUNKNOWN");
        }
        active(*signal);
        store.apply(snapshot.revision, {{recordKey, std::nullopt}});
        return nullptr;
      }
      if (!existing) {
        if (session.exists(keyId, kind))
          throw StateError("EKEYREVOKED");
        record = JSON::Object::Entries{{"id", id},
                                       {"kind", name},
                                       {"policy", policy},
                                       {"phase", "creating"},
                                       {"module", token.moduleIdentity},
                                       {"token", token.tokenIdentity},
                                       {"provider", token.provider},
                                       {"rsaBits", static_cast<int>(options.rsaBits)},
                                       {"fingerprint", ""}};
        snapshot = store.apply(snapshot.revision, {{recordKey, encode(record)}});
        active(*signal);
        session.create(keyId, label, kind, options.rsaBits);
        active(*signal);
      }
      const auto material = session.inspect(keyId, kind);
      const auto fingerprint = hex(digest(material.fingerprintMaterial));
      if (existing && fingerprint != text(record, "fingerprint", 64))
        throw StateError("EKEYREVOKED");
      if (!existing) {
        record["fingerprint"] = fingerprint;
        record["phase"] = "ready";
        snapshot = store.apply(snapshot.revision, {{recordKey, encode(record)}});
      }
      const auto binding = hex(digest(application + ":" + id + ":" + policy + ":" + token.moduleIdentity + ":" +
                                      token.tokenIdentity + ":" + fingerprint));
      if (kind == ProtectedKeyKind::Signing) {
        if (deadline <= now())
          throw StateError("EACCES");
        const auto hash = digest(signedPayload);
        const auto output = signature(material.publicKey, hash, session.sign(keyId, hash, pin.bytes));
        active(*signal);
        if (deadline <= now())
          throw StateError("EACCES");
        const RecordBytes payload(signedPayload.begin(), signedPayload.end());
        return JSON::Object::Entries{{"algorithm", "ECDSA_P256_SHA256"},
                                     {"publicKey", base64(material.publicKey)},
                                     {"payload", base64(payload)},
                                     {"signature", base64(output)}};
      }
      if (action == "open" &&
          (text(sealed, "binding", 64) != binding || wrapped.size() != material.wrappingBytes ||
           !sealed.has("wrappingKeyBits") || sealed.get("wrappingKeyBits").type != JSON::Type::Number ||
           sealed.get("wrappingKeyBits").as<JSON::Number>().value() != material.wrappingBytes * 8))
        throw StateError("EKEYREVOKED");
      Secret dataKey;
      if (action == "seal") {
        dataKey.bytes.resize(32);
        randombytes_buf(dataKey.bytes.data(), dataKey.bytes.size());
        const auto expected = digest(dataKey.bytes);
        wrapped = session.wrap(keyId, dataKey.bytes);
        sodium_memzero(dataKey.bytes.data(), dataKey.bytes.size());
        dataKey.bytes = session.unwrap(keyId, wrapped, pin.bytes);
        const auto actual = digest(dataKey.bytes);
        if (sodium_memcmp(actual.data(), expected.data(), actual.size()))
          throw StateError("EBADMSG");
        iv.resize(12);
        randombytes_buf(iv.data(), iv.size());
      } else
        dataKey.bytes = session.unwrap(keyId, wrapped, pin.bytes);
      active(*signal);
      const auto aad = "oro.secret.v2:" + binding + ":" + base64(wrapped);
      Secret output(crypt(action == "open", dataKey.bytes, iv, aad, action == "open" ? ciphertext : plain.bytes));
      active(*signal);
      if (action == "open")
        return JSON::Object::Entries{{"value", base64(output.bytes)}};
      return JSON::Object::Entries{{"schema", 2},
                                   {"id", id},
                                   {"policy", policy},
                                   {"binding", binding},
                                   {"wrappedKey", base64(wrapped)},
                                   {"wrappingKeyBits", static_cast<int>(material.wrappingBytes * 8)},
                                   {"iv", base64(iv)},
                                   {"ciphertext", base64(output.bytes)}};
#endif
    }
  };

  ProtectedKeys::ProtectedKeys(RecordStore store, Pkcs11Options options, std::string application,
                               std::shared_ptr<void> lease, PinPrompt prompt)
      : state(std::make_shared<State>(std::move(store), std::move(options), std::move(application), std::move(lease),
                                      std::move(prompt))) {
    if (!state->lease || state->application.empty() || state->application.size() > 255)
      throw StateError("EINVAL");
    if (sodium_init() < 0)
      throw StateError("ENOSYS");
  }
  ProtectedKeys::~ProtectedKeys() {
    shutdown();
  }
  bool ProtectedKeys::busy() const {
    std::lock_guard lock(state->mutex);
    return state->running;
  }
  JSON::Object ProtectedKeys::status(const Pkcs11Options& options, const std::string& policy, bool enabled, bool busy) {
    auto result = commonStatus(policy, enabled, busy);
#if !defined(ORO_RUNTIME_ENABLE_MBEDTLS) || !ORO_RUNTIME_ENABLE_MBEDTLS
    result["supported"] = false;
    result["reason"] = "cryptography-unavailable";
    return result;
#endif
    if (policy == "biometric") {
      result["reason"] = "biometric-policy-not-attested";
      return result;
    }
    if (!enabled || busy) {
      result["reason"] = busy ? "authentication-in-progress" : "capability-disabled";
      return result;
    }
    JSON::Array providers;
    bool signing = false, sealing = false, unlocked = false;
    try {
      const auto tokens = discoverPkcs11(options);
      std::set<std::string> kinds;
      for (const auto& token : tokens) {
        kinds.insert(token.provider);
        signing = signing || token.signing;
        sealing = sealing || token.sealing;
        unlocked = unlocked || !token.pinLocked;
      }
      for (const auto& kind : kinds)
        providers.push(kind);
      result["canAuthenticate"] = unlocked && (signing || sealing);
      result["reason"] = tokens.empty() ? "hardware-provider-unavailable" : unlocked ? "" : "provider-pin-locked";
    } catch (const StateError& error) {
      if (error.code == "EINVAL")
        throw;
      result["reason"] = "hardware-provider-unavailable";
      result["providerStatusCode"] = error.code;
    }
    result["providers"] = providers;
    result["signingAvailable"] = signing;
    result["sealingAvailable"] = sealing;
    result["newWrappingKeyBits"] = static_cast<int>(options.rsaBits);
    return result;
  }
  JSON::Any ProtectedKeys::request(const std::string& action, const JSON::Object& options, GtkWindow* parent,
                                   std::shared_ptr<ExecutionCancellation> signal) {
    struct Response {
      bool ready = false;
      JSON::Any value;
      std::string error;
    };
    auto response = std::make_shared<Response>();
    auto state = this->state;
    const auto owner = retainNativeWindow(parent);
    {
      std::lock_guard lock(state->mutex);
      if (state->closing)
        throw StateError("ESHUTDOWN");
      if (state->running)
        throw StateError("EBUSY");
      if (!signal || (!parent && !state->prompt))
        throw StateError("EINVAL");
      active(*signal);
      state->running = true;
      state->cancellation = signal;
    }
    try {
      std::thread([state, response, action, options, owner, signal] {
        {
          std::lock_guard lock(state->mutex);
          state->worker = std::this_thread::get_id();
        }
        JSON::Any result;
        std::string error;
        try {
          result = state->perform(action, options, owner, signal);
        } catch (const StateError& failure) {
          error = failure.code;
        } catch (...) {
          error = "EIO";
        }
        {
          std::lock_guard lock(state->mutex);
          if (!signal->cancelled())
            response->value = std::move(result);
          response->error = error;
          response->ready = true;
          state->running = false;
          state->worker = {};
          state->cancellation.reset();
          state->changed.notify_all();
        }
      }).detach();
    } catch (...) {
      std::lock_guard lock(state->mutex);
      state->running = false;
      state->cancellation.reset();
      state->changed.notify_all();
      throw;
    }
    std::unique_lock lock(state->mutex);
    while (!response->ready) {
      if (signal->cancelled())
        throw StateError("ABORT_ERR");
      state->changed.wait_for(lock, std::chrono::milliseconds(25));
    }
    active(*signal);
    if (!response->error.empty())
      throw StateError(response->error);
    return response->value;
  }
  void ProtectedKeys::shutdown() {
    std::lock_guard lock(state->mutex);
    state->closing = true;
    if (state->cancellation)
      state->cancellation->cancel();
  }
  void ProtectedKeys::drain() {
    std::unique_lock lock(state->mutex);
    if (state->worker == std::this_thread::get_id())
      throw StateError("EDEADLK");
    state->changed.wait(lock, [&] { return !state->running; });
  }
}  // namespace oro::runtime::native
#endif
