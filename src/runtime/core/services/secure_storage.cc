#include "../../string.hh"
#include "../../url.hh"
#include "../../debug.hh"
#include "../../app.hh"
#include "../../scheme.hh"

#if ORO_RUNTIME_PLATFORM_ANDROID
#  include "../../platform/android/environment.hh"
#endif

#include "secure_storage.hh"
#include "../services.hh"

#if ORO_RUNTIME_PLATFORM_APPLE
#  include <Security/Security.h>
#  include <CoreFoundation/CoreFoundation.h>
#  include <vector>

  namespace {
    using oro::runtime::String;

    CFStringRef createCFString (const String& input) {
      return CFStringCreateWithCString(
        kCFAllocatorDefault,
        input.c_str(),
        kCFStringEncodingUTF8
      );
    }

    String cfStringToStdString (CFStringRef cfString) {
      if (cfString == nullptr) {
        return "";
      }

      const char* ptr = CFStringGetCStringPtr(cfString, kCFStringEncodingUTF8);
      if (ptr != nullptr) {
        return String(ptr);
      }

      CFIndex length = CFStringGetLength(cfString);
      CFIndex size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
      std::vector<char> buffer(static_cast<size_t>(size));
      if (CFStringGetCString(cfString, buffer.data(), size, kCFStringEncodingUTF8)) {
        return String(buffer.data());
      }

      return "";
    }

    String osStatusMessage (OSStatus status) {
      String statusMessage = "OSStatus(" + std::to_string(status) + ")";
      if (auto messageRef = SecCopyErrorMessageString(status, nullptr)) {
        const auto converted = cfStringToStdString(messageRef);
        if (!converted.empty()) {
          statusMessage = converted;
        }
        CFRelease(messageRef);
      }

      return statusMessage;
    }
  }
#endif

#if ORO_RUNTIME_PLATFORM_WINDOWS
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <wincred.h>

  namespace {
    std::wstring utf8ToWide (const String& value) {
      if (value.empty()) {
        return std::wstring();
      }

      int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        -1,
        nullptr,
        0
      );

      if (required <= 0) {
        return std::wstring();
      }

      std::wstring result(static_cast<size_t>(required - 1), L'\0');
      MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        -1,
        result.data(),
        required
      );
      return result;
    }

    String wideToUtf8 (const wchar_t* value) {
      if (value == nullptr || *value == L'\0') {
        return "";
      }

      int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
      );

      if (required <= 0) {
        return "";
      }

      String result(static_cast<size_t>(required - 1), '\0');
      WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        -1,
        result.data(),
        required,
        nullptr,
        nullptr
      );
      return result;
    }

    std::wstring encodeComponent (const String& input) {
      return utf8ToWide(bytes::base64::encode(input));
    }

    String decodeComponent (const std::wstring& input) {
      return bytes::base64::decode(wideToUtf8(input.c_str()));
    }

    std::wstring makeTargetName (const String& scope, const String& key) {
      static const std::wstring prefix = L"oro:";
      return prefix + encodeComponent(scope) + L":" + encodeComponent(key);
    }

    std::wstring makeScopePrefix (const String& scope) {
      static const std::wstring prefix = L"oro:";
      return prefix + encodeComponent(scope) + L":";
    }

    String windowsErrorMessage (DWORD code) {
      LPWSTR buffer = nullptr;
      DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
      );

      String result = "Win32Error(" + std::to_string(code) + ")";
      if (size > 0 && buffer != nullptr) {
        const auto converted = wideToUtf8(buffer);
        if (!converted.empty()) {
          result = converted;
        }
      }

      if (buffer != nullptr) {
        LocalFree(buffer);
      }

      return result;
    }
  }
#endif

#if ORO_RUNTIME_PLATFORM_LINUX
#  include <dlfcn.h>
#  include <mutex>

  using oro::runtime::String;

  typedef int gboolean;
  typedef char gchar;
  typedef struct _GError GError;
  typedef struct _GCancellable GCancellable;

  enum SecretSchemaFlags {
    SECRET_SCHEMA_NONE = 0,
    SECRET_SCHEMA_DONT_MATCH_NAME = 1 << 0
  };

  enum SecretSchemaAttributeType {
    SECRET_SCHEMA_ATTRIBUTE_STRING = 0,
    SECRET_SCHEMA_ATTRIBUTE_INTEGER = 1,
    SECRET_SCHEMA_ATTRIBUTE_BOOLEAN = 2
  };

  struct SecretSchemaAttribute {
    const gchar* name;
    SecretSchemaAttributeType type;
  };

  struct SecretSchema {
    const gchar* name;
    SecretSchemaFlags flags;
    SecretSchemaAttribute attributes[32];
  };

  using secret_password_store_sync_fn = gboolean (*)(
    const SecretSchema*,
    const gchar*,
    const gchar*,
    const gchar*,
    GCancellable*,
    GError**, ...
  );

  using secret_password_lookup_sync_fn = gchar* (*)(
    const SecretSchema*,
    GCancellable*,
    GError**, ...
  );

  using secret_password_clear_sync_fn = gboolean (*)(
    const SecretSchema*,
    GCancellable*,
    GError**, ...
  );

  using g_free_fn = void (*)(void*);

  struct LibSecretBackend {
    void* secretHandle = nullptr;
    void* glibHandle = nullptr;
    secret_password_store_sync_fn store = nullptr;
    secret_password_lookup_sync_fn lookup = nullptr;
    secret_password_clear_sync_fn clear = nullptr;
    g_free_fn g_free = nullptr;

    bool load (String& error) {
      if (this->store && this->lookup && this->clear && this->g_free) {
        return true;
      }

      if (this->secretHandle == nullptr) {
        this->secretHandle = dlopen("libsecret-1.so.0", RTLD_LAZY);
        if (this->secretHandle == nullptr) {
          this->secretHandle = dlopen("libsecret-1.so", RTLD_LAZY);
        }
      }

      if (this->secretHandle == nullptr) {
        error = "Unable to load libsecret-1";
        return false;
      }

      this->store = reinterpret_cast<secret_password_store_sync_fn>(
        dlsym(this->secretHandle, "secret_password_store_sync")
      );
      this->lookup = reinterpret_cast<secret_password_lookup_sync_fn>(
        dlsym(this->secretHandle, "secret_password_lookup_sync")
      );
      this->clear = reinterpret_cast<secret_password_clear_sync_fn>(
        dlsym(this->secretHandle, "secret_password_clear_sync")
      );

      if (this->glibHandle == nullptr) {
        this->glibHandle = dlopen("libglib-2.0.so.0", RTLD_LAZY);
        if (this->glibHandle == nullptr) {
          this->glibHandle = dlopen("libglib-2.0.so", RTLD_LAZY);
        }
      }

      if (this->glibHandle != nullptr) {
        this->g_free = reinterpret_cast<g_free_fn>(
          dlsym(this->glibHandle, "g_free")
        );
      }

      if (!this->store || !this->lookup || !this->clear || !this->g_free) {
        error = "Failed to resolve libsecret symbols";
        return false;
      }

      return true;
    }
  };

  struct LibSecretContext {
    std::once_flag flag;
    LibSecretBackend backend;
    SecretSchema schema;
    String loadError;
    bool loaded = false;

    LibSecretContext () {
      this->schema.name = "oro_secure_storage";
      this->schema.flags = SECRET_SCHEMA_NONE;
      this->schema.attributes[0] = { "scope", SECRET_SCHEMA_ATTRIBUTE_STRING };
      this->schema.attributes[1] = { "key", SECRET_SCHEMA_ATTRIBUTE_STRING };
      this->schema.attributes[2] = { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING };
    }
  };

  LibSecretContext& libsecretContext () {
    static LibSecretContext context;
    return context;
  }

  bool ensureLibSecretBackend (LibSecretContext& context, String& error) {
    std::call_once(context.flag, [&context]() {
      context.loadError.clear();
      context.loaded = context.backend.load(context.loadError);
    });

    if (!context.loaded) {
      error = context.loadError.empty() ? "libsecret backend unavailable" : context.loadError;
      return false;
    }

    if (!context.backend.store || !context.backend.lookup || !context.backend.clear || !context.backend.g_free) {
      error = context.loadError.empty() ? "libsecret backend unavailable" : context.loadError;
      return false;
    }

    return true;
  }
#endif

using oro::runtime::string::toLowerCase;

namespace oro::runtime::core::services {
  namespace {
    inline JSON::Object makeSuccess (const char* source, JSON::Any data) {
      return JSON::Object::Entries {
        {"source", source},
        {"data", data}
      };
    }
  }

  bool SecureStorage::parseEncoding (const String& value, Encoding& encoding) {
    const auto normalized = toLowerCase(value);

    if (normalized.empty() || normalized == "utf8" || normalized == "utf-8") {
      encoding = Encoding::UTF8;
      return true;
    }

    if (normalized == "base64" || normalized == "b64") {
      encoding = Encoding::BASE64;
      return true;
    }

    if (normalized == "hex" || normalized == "hexadecimal") {
      encoding = Encoding::HEX;
      return true;
    }

    return false;
  }

  String SecureStorage::encodingToString (Encoding encoding) {
    switch (encoding) {
      case Encoding::BASE64:
        return "base64";
      case Encoding::HEX:
        return "hex";
      case Encoding::UTF8:
      default:
        return "utf8";
    }
  }

  bool SecureStorage::decodeValue (
    const String& value,
    Encoding encoding,
    bytes::Buffer& output,
    String& error
  ) {
    try {
      switch (encoding) {
        case Encoding::UTF8:
          output = bytes::Buffer::from(value);
          return true;
        case Encoding::BASE64: {
          const auto decoded = bytes::base64::decode(value);
          output = bytes::Buffer::from(decoded);
          return true;
        }
        case Encoding::HEX: {
          const auto decoded = bytes::decodeHexString(value);
          output = bytes::Buffer::from(decoded);
          return true;
        }
      }
    } catch (const std::exception& err) {
      error = err.what();
      return false;
    } catch (...) {
      error = "Unknown error decoding value";
      return false;
    }

    error = "Unsupported encoding";
    return false;
  }

  bool SecureStorage::encodeValue (
    const bytes::Buffer& value,
    Encoding encoding,
    String& output,
    String& error
  ) {
    (void)error;
    switch (encoding) {
      case Encoding::UTF8:
        output = value.str(bytes::Buffer::Encoding::UTF8);
        return true;
      case Encoding::BASE64:
        output = value.str(bytes::Buffer::Encoding::BASE64);
        return true;
      case Encoding::HEX:
        output = value.str(bytes::Buffer::Encoding::HEX);
        return true;
    }

    error = "Unsupported encoding";
    return false;
  }

  SecureStorage::SecureStorage (const Options& options)
    : core::Service(options)
  {}

  SecureStorage::~SecureStorage () {}

  bool SecureStorage::start () {
    if (!this->enabled) {
      return false;
    }

    return true;
  }

  bool SecureStorage::stop () {
    if (!this->enabled) {
      return false;
    }

    return true;
  }

  void SecureStorage::set (
    const String& seq,
    String scope,
    String key,
    bytes::Buffer value,
    const Callback callback
  ) {
    if (!this->enabled) {
      callback(seq, this->makeDisabledError("set"), QueuedResponse{});
      return;
    }

    this->loop.dispatch([=, this,
      scope = std::move(scope),
      key = std::move(key),
      value = std::move(value)
    ]() mutable {
      String normalizedScope;
      String error;

      if (!this->normalizeScope(scope, normalizedScope, error)) {
        callback(seq, this->makeError("set", error, "TypeError"), QueuedResponse{});
        return;
      }

      if (key.empty()) {
        callback(seq, this->makeError("set", "Key must not be empty", "TypeError"), QueuedResponse{});
        return;
      }

      String backendError;

      if (!this->platformSet(normalizedScope, key, value, backendError)) {
        if (backendError.empty()) {
          backendError = "Secure storage backend error";
        }

        callback(seq, this->makeError("set", backendError, "InternalError"), QueuedResponse{});
        return;
      }

      callback(seq, makeSuccess("set", JSON::Object::Entries {
        {"key", key}
      }), QueuedResponse{});
    });
  }

  void SecureStorage::get (
    const String& seq,
    String scope,
    String key,
    Encoding encoding,
    const Callback callback
  ) {
    if (!this->enabled) {
      callback(seq, this->makeDisabledError("get"), QueuedResponse{});
      return;
    }

    this->loop.dispatch([=, this,
      scope = std::move(scope),
      key = std::move(key)
    ]() mutable {
      String normalizedScope;
      String error;

      if (!this->normalizeScope(scope, normalizedScope, error)) {
        callback(seq, this->makeError("get", error, "TypeError"), QueuedResponse{});
        return;
      }

      if (key.empty()) {
        callback(seq, this->makeError("get", "Key must not be empty", "TypeError"), QueuedResponse{});
        return;
      }

      bytes::Buffer value;
      bool found = false;
      String backendError;

      if (!this->platformGet(normalizedScope, key, value, found, backendError)) {
        if (backendError.empty()) {
          backendError = "Secure storage backend error";
        }

        callback(seq, this->makeError("get", backendError, "InternalError"), QueuedResponse{});
        return;
      }

      if (!found) {
        callback(seq, this->makeError("get", "Key not found", "NotFoundError"), QueuedResponse{});
        return;
      }

      String encoded;
      String encodeError;

      if (!encodeValue(value, encoding, encoded, encodeError)) {
        if (encodeError.empty()) {
          encodeError = "Failed to encode value";
        }

        callback(seq, this->makeError("get", encodeError, "EncodingError"), QueuedResponse{});
        return;
      }

      callback(seq, makeSuccess("get", JSON::Object::Entries {
        {"key", key},
        {"encoding", encodingToString(encoding)},
        {"value", encoded}
      }), QueuedResponse{});
    });
  }

  void SecureStorage::remove (
    const String& seq,
    String scope,
    String key,
    const Callback callback
  ) {
    if (!this->enabled) {
      callback(seq, this->makeDisabledError("remove"), QueuedResponse{});
      return;
    }

    this->loop.dispatch([=, this,
      scope = std::move(scope),
      key = std::move(key)
    ]() mutable {
      String normalizedScope;
      String error;

      if (!this->normalizeScope(scope, normalizedScope, error)) {
        callback(seq, this->makeError("remove", error, "TypeError"), QueuedResponse{});
        return;
      }

      if (key.empty()) {
        callback(seq, this->makeError("remove", "Key must not be empty", "TypeError"), QueuedResponse{});
        return;
      }

      String backendError;

      if (!this->platformRemove(normalizedScope, key, backendError)) {
        if (backendError.empty()) {
          backendError = "Secure storage backend error";
        }

        callback(seq, this->makeError("remove", backendError, "InternalError"), QueuedResponse{});
        return;
      }

      callback(seq, makeSuccess("remove", JSON::Object::Entries {
        {"key", key}
      }), QueuedResponse{});
    });
  }

  void SecureStorage::clear (
    const String& seq,
    String scope,
    const Callback callback
  ) {
    if (!this->enabled) {
      callback(seq, this->makeDisabledError("clear"), QueuedResponse{});
      return;
    }

    this->loop.dispatch([=, this, scope = std::move(scope)]() mutable {
      String normalizedScope;
      String error;

      if (!this->normalizeScope(scope, normalizedScope, error)) {
        callback(seq, this->makeError("clear", error, "TypeError"), QueuedResponse{});
        return;
      }

      String backendError;

      if (!this->platformClear(normalizedScope, backendError)) {
        if (backendError.empty()) {
          backendError = "Secure storage backend error";
        }

        callback(seq, this->makeError("clear", backendError, "InternalError"), QueuedResponse{});
        return;
      }

      callback(seq, makeSuccess("clear", JSON::Object::Entries {}), QueuedResponse{});
    });
  }

  void SecureStorage::keys (
    const String& seq,
    String scope,
    const Callback callback
  ) {
    if (!this->enabled) {
      callback(seq, this->makeDisabledError("keys"), QueuedResponse{});
      return;
    }

    this->loop.dispatch([=, this, scope = std::move(scope)]() mutable {
      String normalizedScope;
      String error;

      if (!this->normalizeScope(scope, normalizedScope, error)) {
        callback(seq, this->makeError("keys", error, "TypeError"), QueuedResponse{});
        return;
      }

      String backendError;
      Vector<String> list;

      if (!this->platformListKeys(normalizedScope, list, backendError)) {
        if (backendError.empty()) {
          backendError = "Secure storage backend error";
        }

        callback(seq, this->makeError("keys", backendError, "InternalError"), QueuedResponse{});
        return;
      }

      JSON::Array::Entries entries;
      entries.reserve(list.size());
      for (const auto& key : list) {
        entries.emplace_back(key);
      }

      callback(seq, makeSuccess("keys", JSON::Object::Entries {
        {"keys", entries}
      }), QueuedResponse{});
    });
  }

  String SecureStorage::computeDefaultScope () const {
    String bundle = "app";

    if (auto* runtime = this->context.getRuntime()) {
      const auto it = runtime->userConfig.find("meta_bundle_identifier");
      if (it != runtime->userConfig.end() && !it->second.empty()) {
        bundle = it->second;
      }
    }

    return String("oro://") + bundle;
  }

  bool SecureStorage::normalizeScope (
    const String& requested,
    String& normalized,
    String& error
  ) const {
    if (requested.empty()) {
      normalized = this->computeDefaultScope();
      return true;
    }

    url::URL parsed(requested, true);

    if (parsed.origin.empty() || parsed.scheme.empty() || parsed.hostname.empty()) {
      error = "Scope must be a valid origin";
      return false;
    }

    if (!parsed.search.empty() || !parsed.hash.empty() || (!parsed.pathname.empty() && parsed.pathname != "/")) {
      error = "Scope must be an origin (scheme://host[:port])";
      return false;
    }

    normalized = scheme::canonicalURL(parsed.origin);
    return true;
  }

  String SecureStorage::indexScopeFor (const String& scope) const {
    return scope + "::secure-storage-index";
  }

  bool SecureStorage::registerKey (const String& scope, const String& key) {
    if (!this->services.state.isReady()) {
      return false;
    }

    return this->services.state.putString(
      this->indexScopeFor(scope),
      key,
      "1"
    );
  }

  bool SecureStorage::unregisterKey (const String& scope, const String& key) {
    if (!this->services.state.isReady()) {
      return false;
    }

    return this->services.state.remove(
      this->indexScopeFor(scope),
      key
    );
  }

  bool SecureStorage::clearRegisteredKeys (const String& scope) {
    if (!this->services.state.isReady()) {
      return false;
    }

    return this->services.state.clear(this->indexScopeFor(scope));
  }

  Vector<String> SecureStorage::registeredKeys (const String& scope) const {
    if (!this->services.state.isReady()) {
      return {};
    }

    return this->services.state.listKeys(this->indexScopeFor(scope));
  }

  JSON::Object SecureStorage::makeError (
    const char* source,
    const String& message,
    const String& type
  ) const {
    return JSON::Object::Entries {
      {"source", String("secureStorage.") + source},
      {"err", JSON::Object::Entries {
        {"type", type},
        {"message", message}
      }}
    };
  }

  JSON::Object SecureStorage::makeDisabledError (const char* source) const {
    return this->makeError(source, "Secure storage service disabled", "NotAllowedError");
  }

  bool SecureStorage::platformSet (
    const String& scope,
    const String& key,
    const bytes::Buffer& value,
    String& error
  ) {
#if ORO_RUNTIME_PLATFORM_APPLE
    const unsigned char* bytes = value.size() > 0 ? value.data() : reinterpret_cast<const unsigned char*>("");
    CFStringRef service = createCFString(scope);
    CFStringRef account = createCFString(key);
    CFDataRef data = CFDataCreate(
      kCFAllocatorDefault,
      bytes,
      static_cast<CFIndex>(value.size())
    );

    if (service == nullptr || account == nullptr || data == nullptr) {
      if (service) CFRelease(service);
      if (account) CFRelease(account);
      if (data) CFRelease(data);
      error = "Failed to allocate CF objects";
      return false;
    }

    const void* keys[] = {
      kSecClass,
      kSecAttrService,
      kSecAttrAccount,
      kSecValueData
    };

    const void* values[] = {
      kSecClassGenericPassword,
      service,
      account,
      data
    };

    CFDictionaryRef addQuery = CFDictionaryCreate(
      kCFAllocatorDefault,
      keys,
      values,
      4,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    OSStatus status = SecItemAdd(addQuery, nullptr);

    if (status == errSecDuplicateItem) {
      const void* matchKeys[] = {
        kSecClass,
        kSecAttrService,
        kSecAttrAccount
      };

      const void* matchValues[] = {
        kSecClassGenericPassword,
        service,
        account
      };

      CFDictionaryRef matchQuery = CFDictionaryCreate(
        kCFAllocatorDefault,
        matchKeys,
        matchValues,
        3,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
      );

      const void* updateKeys[] = {
        kSecValueData
      };

      const void* updateValues[] = {
        data
      };

      CFDictionaryRef updateDict = CFDictionaryCreate(
        kCFAllocatorDefault,
        updateKeys,
        updateValues,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
      );

      status = SecItemUpdate(matchQuery, updateDict);

      CFRelease(matchQuery);
      CFRelease(updateDict);
    }

    if (addQuery) CFRelease(addQuery);
    CFRelease(data);
    CFRelease(account);
    CFRelease(service);

    if (status != errSecSuccess) {
      error = osStatusMessage(status);
      return false;
    }

    this->registerKey(scope, key);
    return true;
#elif ORO_RUNTIME_PLATFORM_WINDOWS
    if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
      error = "Value exceeds Windows credential size limit (5120 bytes)";
      return false;
    }

    std::wstring target = makeTargetName(scope, key);

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.empty() ? nullptr : const_cast<LPWSTR>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(value.size());
    credential.CredentialBlob = (value.size() > 0)
      ? const_cast<LPBYTE>(reinterpret_cast<const BYTE*>(value.data()))
      : nullptr;
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"");

    if (!CredWriteW(&credential, 0)) {
      error = windowsErrorMessage(GetLastError());
      return false;
    }

    this->registerKey(scope, key);
    return true;
#elif ORO_RUNTIME_PLATFORM_ANDROID
    auto app = app::App::sharedApplication();
    if (app == nullptr) {
      error = "Application unavailable";
      return false;
    }

    auto& android = app->runtime.android;
    if (!android.jvm || !android.activity) {
      error = "Android runtime unavailable";
      return false;
    }

    android::JNIEnvironmentAttachment attachment(android.jvm);
    if (!attachment.env) {
      error = "JNI environment unavailable";
      return false;
    }

    jclass cls = attachment.env->FindClass("oro/runtime/securestorage/SecureStorageManager");
    if (cls == nullptr) {
      attachment.printException();
      error = "SecureStorageManager class not found";
      return false;
    }

    jmethodID method = attachment.env->GetStaticMethodID(
      cls,
      "set",
      "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;[B)Z"
    );

    if (method == nullptr) {
      attachment.printException();
      attachment.env->DeleteLocalRef(cls);
      error = "SecureStorageManager.set not available";
      return false;
    }

    jstring jScope = attachment.env->NewStringUTF(scope.c_str());
    jstring jKey = attachment.env->NewStringUTF(key.c_str());
    jbyteArray jValue = attachment.env->NewByteArray(static_cast<jsize>(value.size()));

    if (jValue != nullptr && value.size() > 0) {
      attachment.env->SetByteArrayRegion(
        jValue,
        0,
        static_cast<jsize>(value.size()),
        reinterpret_cast<const jbyte*>(value.data())
      );
    }

    jboolean result = attachment.env->CallStaticBooleanMethod(
      cls,
      method,
      android.activity,
      jScope,
      jKey,
      jValue
    );

    bool ok = (result == JNI_TRUE) && !attachment.hasException();
    if (attachment.hasException()) {
      attachment.printException();
      attachment.env->ExceptionClear();
      ok = false;
    }

    attachment.env->DeleteLocalRef(jScope);
    attachment.env->DeleteLocalRef(jKey);
    if (jValue != nullptr) {
      attachment.env->DeleteLocalRef(jValue);
    }
    attachment.env->DeleteLocalRef(cls);

    if (!ok) {
      error = "Failed to store value";
      return false;
    }

    this->registerKey(scope, key);
    return true;
#elif ORO_RUNTIME_PLATFORM_LINUX
    auto& context = libsecretContext();
    if (!ensureLibSecretBackend(context, error)) {
      return false;
    }

    const auto encoded = value.str(bytes::Buffer::Encoding::BASE64);
    const auto label = scope + " :: " + key;

    const gboolean ok = context.backend.store(
      &context.schema,
      nullptr,
      label.c_str(),
      encoded.c_str(),
      nullptr,
      nullptr,
      "scope", scope.c_str(),
      "key", key.c_str(),
      nullptr
    );

    if (!ok) {
      error = "Failed to store secret via libsecret";
      return false;
    }

    this->registerKey(scope, key);
    return true;
#else
    (void)scope;
    (void)key;
    (void)value;
    error = "Secure storage backend not implemented";
    return false;
#endif
  }

  bool SecureStorage::platformGet (
    const String& scope,
    const String& key,
    bytes::Buffer& value,
    bool& found,
    String& error
  ) {
#if ORO_RUNTIME_PLATFORM_APPLE
    found = false;

    CFStringRef service = createCFString(scope);
    CFStringRef account = createCFString(key);

    if (service == nullptr || account == nullptr) {
      if (service) CFRelease(service);
      if (account) CFRelease(account);
      error = "Failed to allocate CF objects";
      return false;
    }

    const void* keys[] = {
      kSecClass,
      kSecAttrService,
      kSecAttrAccount,
      kSecReturnData,
      kSecMatchLimit
    };

    const void* values[] = {
      kSecClassGenericPassword,
      service,
      account,
      kCFBooleanTrue,
      kSecMatchLimitOne
    };

    CFDictionaryRef query = CFDictionaryCreate(
      kCFAllocatorDefault,
      keys,
      values,
      5,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);

    CFRelease(query);
    CFRelease(service);
    CFRelease(account);

    if (status == errSecItemNotFound) {
      found = false;
      if (result) CFRelease(result);
      return true;
    }

    if (status != errSecSuccess) {
      if (result) CFRelease(result);
      error = osStatusMessage(status);
      return false;
    }

    if (result == nullptr || CFGetTypeID(result) != CFDataGetTypeID()) {
      if (result) CFRelease(result);
      found = false;
      return true;
    }

    CFDataRef dataRef = static_cast<CFDataRef>(result);
    const CFIndex length = CFDataGetLength(dataRef);
    value = bytes::Buffer(static_cast<size_t>(length));

    if (length > 0) {
      CFDataGetBytes(
        dataRef,
        CFRangeMake(0, length),
        value.data()
      );
    }

    CFRelease(result);
    found = true;
    return true;
#elif ORO_RUNTIME_PLATFORM_WINDOWS
    found = false;

    std::wstring target = makeTargetName(scope, key);
    PCREDENTIALW credential = nullptr;

    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
      const DWORD errorCode = GetLastError();
      if (errorCode == ERROR_NOT_FOUND) {
        return true;
      }

      error = windowsErrorMessage(errorCode);
      return false;
    }

    auto size = static_cast<size_t>(credential->CredentialBlobSize);
    value = bytes::Buffer(size);

    if (size > 0 && credential->CredentialBlob != nullptr) {
      value.set(
        reinterpret_cast<const unsigned char*>(credential->CredentialBlob),
        size,
        0
      );
    }

    CredFree(credential);
    found = true;
    return true;
#elif ORO_RUNTIME_PLATFORM_ANDROID
    found = false;

    auto app = app::App::sharedApplication();
    if (app == nullptr) {
      error = "Application unavailable";
      return false;
    }

    auto& android = app->runtime.android;
    if (!android.jvm || !android.activity) {
      error = "Android runtime unavailable";
      return false;
    }

    android::JNIEnvironmentAttachment attachment(android.jvm);
    if (!attachment.env) {
      error = "JNI environment unavailable";
      return false;
    }

    jclass cls = attachment.env->FindClass("oro/runtime/securestorage/SecureStorageManager");
    if (cls == nullptr) {
      attachment.printException();
      error = "SecureStorageManager class not found";
      return false;
    }

    jmethodID method = attachment.env->GetStaticMethodID(
      cls,
      "get",
      "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)[B"
    );

    if (method == nullptr) {
      attachment.printException();
      attachment.env->DeleteLocalRef(cls);
      error = "SecureStorageManager.get not available";
      return false;
    }

    jstring jScope = attachment.env->NewStringUTF(scope.c_str());
    jstring jKey = attachment.env->NewStringUTF(key.c_str());

    jbyteArray jResult = static_cast<jbyteArray>(attachment.env->CallStaticObjectMethod(
      cls,
      method,
      android.activity,
      jScope,
      jKey
    ));

    bool ok = !attachment.hasException();
    if (attachment.hasException()) {
      attachment.printException();
      attachment.env->ExceptionClear();
      ok = false;
    }

    if (!ok) {
      error = "Failed to read value";
    }

    if (jResult != nullptr && ok) {
      const jsize length = attachment.env->GetArrayLength(jResult);
      value = bytes::Buffer(static_cast<size_t>(length));
      if (length > 0) {
        attachment.env->GetByteArrayRegion(
          jResult,
          0,
          length,
          reinterpret_cast<jbyte*>(value.data())
        );
      }
      found = true;
      attachment.env->DeleteLocalRef(jResult);
    } else if (jResult == nullptr && ok) {
      found = false;
    }

    attachment.env->DeleteLocalRef(jScope);
    attachment.env->DeleteLocalRef(jKey);
    attachment.env->DeleteLocalRef(cls);

    return ok;
#elif ORO_RUNTIME_PLATFORM_LINUX
    auto& context = libsecretContext();
    if (!ensureLibSecretBackend(context, error)) {
      return false;
    }

    gchar* result = context.backend.lookup(
      &context.schema,
      nullptr,
      nullptr,
      "scope", scope.c_str(),
      "key", key.c_str(),
      nullptr
    );

    if (result == nullptr) {
      found = false;
      return true;
    }

    String encoded(result);
    context.backend.g_free(result);

    try {
      const auto decoded = bytes::base64::decode(encoded);
      value = bytes::Buffer::from(decoded);
      found = true;
      return true;
    } catch (...) {
      error = "Failed to decode stored secret";
      return false;
    }
#else
    (void)scope;
    (void)key;
    (void)value;
    found = false;
    error = "Secure storage backend not implemented";
    return false;
#endif
  }

  bool SecureStorage::platformRemove (
    const String& scope,
    const String& key,
    String& error
  ) {
#if ORO_RUNTIME_PLATFORM_APPLE
    CFStringRef service = createCFString(scope);
    CFStringRef account = createCFString(key);

    if (service == nullptr || account == nullptr) {
      if (service) CFRelease(service);
      if (account) CFRelease(account);
      error = "Failed to allocate CF objects";
      return false;
    }

    const void* keys[] = {
      kSecClass,
      kSecAttrService,
      kSecAttrAccount
    };

    const void* values[] = {
      kSecClassGenericPassword,
      service,
      account
    };

    CFDictionaryRef query = CFDictionaryCreate(
      kCFAllocatorDefault,
      keys,
      values,
      3,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    OSStatus status = SecItemDelete(query);

    CFRelease(query);
    CFRelease(service);
    CFRelease(account);

    if (status == errSecSuccess || status == errSecItemNotFound) {
      this->unregisterKey(scope, key);
      return true;
    }

    error = osStatusMessage(status);
    return false;
#elif ORO_RUNTIME_PLATFORM_WINDOWS
    std::wstring target = makeTargetName(scope, key);

    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
      const DWORD errorCode = GetLastError();
      if (errorCode == ERROR_NOT_FOUND) {
        this->unregisterKey(scope, key);
        return true;
      }

      error = windowsErrorMessage(errorCode);
      return false;
    }

    this->unregisterKey(scope, key);
    return true;
#elif ORO_RUNTIME_PLATFORM_ANDROID
    auto app = app::App::sharedApplication();
    if (app == nullptr) {
      error = "Application unavailable";
      return false;
    }

    auto& android = app->runtime.android;
    if (!android.jvm || !android.activity) {
      error = "Android runtime unavailable";
      return false;
    }

    android::JNIEnvironmentAttachment attachment(android.jvm);
    if (!attachment.env) {
      error = "JNI environment unavailable";
      return false;
    }

    jclass cls = attachment.env->FindClass("oro/runtime/securestorage/SecureStorageManager");
    if (cls == nullptr) {
      attachment.printException();
      error = "SecureStorageManager class not found";
      return false;
    }

    jmethodID method = attachment.env->GetStaticMethodID(
      cls,
      "remove",
      "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z"
    );

    if (method == nullptr) {
      attachment.printException();
      attachment.env->DeleteLocalRef(cls);
      error = "SecureStorageManager.remove not available";
      return false;
    }

    jstring jScope = attachment.env->NewStringUTF(scope.c_str());
    jstring jKey = attachment.env->NewStringUTF(key.c_str());

    jboolean result = attachment.env->CallStaticBooleanMethod(
      cls,
      method,
      android.activity,
      jScope,
      jKey
    );

    bool ok = (result == JNI_TRUE) && !attachment.hasException();
    if (attachment.hasException()) {
      attachment.printException();
      attachment.env->ExceptionClear();
      ok = false;
    }

    attachment.env->DeleteLocalRef(jScope);
    attachment.env->DeleteLocalRef(jKey);
    attachment.env->DeleteLocalRef(cls);

    if (!ok) {
      error = "Failed to remove value";
      return false;
    }

    this->unregisterKey(scope, key);
    return true;
#elif ORO_RUNTIME_PLATFORM_LINUX
    auto& context = libsecretContext();
    if (!ensureLibSecretBackend(context, error)) {
      return false;
    }

    const gboolean ok = context.backend.clear(
      &context.schema,
      nullptr,
      nullptr,
      "scope", scope.c_str(),
      "key", key.c_str(),
      nullptr
    );

    if (!ok) {
      error = "Failed to remove secret";
      return false;
    }

    this->unregisterKey(scope, key);
    return true;
#else
    (void)scope;
    (void)key;
    error = "Secure storage backend not implemented";
    return false;
#endif
  }

  bool SecureStorage::platformClear (
    const String& scope,
    String& error
  ) {
#if ORO_RUNTIME_PLATFORM_APPLE
    CFStringRef service = createCFString(scope);

    if (service == nullptr) {
      error = "Failed to allocate CF objects";
      return false;
    }

    const void* keys[] = {
      kSecClass,
      kSecAttrService
    };

    const void* values[] = {
      kSecClassGenericPassword,
      service
    };

    CFDictionaryRef query = CFDictionaryCreate(
      kCFAllocatorDefault,
      keys,
      values,
      2,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    OSStatus status = SecItemDelete(query);

    CFRelease(query);
    CFRelease(service);

    if (status == errSecSuccess || status == errSecItemNotFound) {
      this->clearRegisteredKeys(scope);
      return true;
    }

    error = osStatusMessage(status);
    return false;
#elif ORO_RUNTIME_PLATFORM_WINDOWS
    const std::wstring prefix = makeScopePrefix(scope);

    DWORD count = 0;
    PCREDENTIALW* credentials = nullptr;

    if (!CredEnumerateW(L"oro:*", 0, &count, &credentials)) {
      const DWORD errorCode = GetLastError();
      if (errorCode == ERROR_NOT_FOUND) {
        return true;
      }

      error = windowsErrorMessage(errorCode);
      return false;
    }

    bool ok = true;
    String lastError;

    for (DWORD i = 0; i < count; ++i) {
      const auto cred = credentials[i];
      if (cred == nullptr || cred->TargetName == nullptr) {
        continue;
      }

      std::wstring target(cred->TargetName);
      if (target.rfind(prefix, 0) != 0) {
        continue;
      }

      if (!CredDeleteW(cred->TargetName, CRED_TYPE_GENERIC, 0)) {
        ok = false;
        lastError = windowsErrorMessage(GetLastError());
      }
    }

    CredFree(credentials);

    if (!ok) {
      error = lastError.empty() ? "Failed to clear secure storage entries" : lastError;
      return false;
    }

    this->clearRegisteredKeys(scope);
    return true;
#elif ORO_RUNTIME_PLATFORM_ANDROID
    auto app = app::App::sharedApplication();
    if (app == nullptr) {
      error = "Application unavailable";
      return false;
    }

    auto& android = app->runtime.android;
    if (!android.jvm || !android.activity) {
      error = "Android runtime unavailable";
      return false;
    }

    android::JNIEnvironmentAttachment attachment(android.jvm);
    if (!attachment.env) {
      error = "JNI environment unavailable";
      return false;
    }

    jclass cls = attachment.env->FindClass("oro/runtime/securestorage/SecureStorageManager");
    if (cls == nullptr) {
      attachment.printException();
      error = "SecureStorageManager class not found";
      return false;
    }

    jmethodID method = attachment.env->GetStaticMethodID(
      cls,
      "clear",
      "(Landroid/content/Context;Ljava/lang/String;)Z"
    );

    if (method == nullptr) {
      attachment.printException();
      attachment.env->DeleteLocalRef(cls);
      error = "SecureStorageManager.clear not available";
      return false;
    }

    jstring jScope = attachment.env->NewStringUTF(scope.c_str());
    jboolean result = attachment.env->CallStaticBooleanMethod(
      cls,
      method,
      android.activity,
      jScope
    );

    bool ok = (result == JNI_TRUE) && !attachment.hasException();
    if (attachment.hasException()) {
      attachment.printException();
      attachment.env->ExceptionClear();
      ok = false;
    }

    attachment.env->DeleteLocalRef(jScope);
    attachment.env->DeleteLocalRef(cls);

    if (!ok) {
      error = "Failed to clear values";
      return false;
    }

    this->clearRegisteredKeys(scope);
    return true;
#elif ORO_RUNTIME_PLATFORM_LINUX
    auto& context = libsecretContext();
    if (!ensureLibSecretBackend(context, error)) {
      return false;
    }

    const gboolean ok = context.backend.clear(
      &context.schema,
      nullptr,
      nullptr,
      "scope", scope.c_str(),
      nullptr
    );

    if (!ok) {
      error = "Failed to clear secrets";
      return false;
    }

    this->clearRegisteredKeys(scope);
    return true;
#else
    (void)scope;
    error = "Secure storage backend not implemented";
    return false;
#endif
  }

  bool SecureStorage::platformListKeys (
    const String& scope,
    Vector<String>& keys,
    String& error
  ) {
#if ORO_RUNTIME_PLATFORM_APPLE
    keys.clear();

    CFStringRef service = createCFString(scope);

    if (service == nullptr) {
      error = "Failed to allocate CF objects";
      return false;
    }

    const void* keysArr[] = {
      kSecClass,
      kSecAttrService,
      kSecReturnAttributes,
      kSecMatchLimit
    };

    const void* valuesArr[] = {
      kSecClassGenericPassword,
      service,
      kCFBooleanTrue,
      kSecMatchLimitAll
    };

    CFDictionaryRef query = CFDictionaryCreate(
      kCFAllocatorDefault,
      keysArr,
      valuesArr,
      4,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);

    CFRelease(query);
    CFRelease(service);

    if (status == errSecItemNotFound) {
      if (result) CFRelease(result);
      return true;
    }

    if (status != errSecSuccess) {
      if (result) CFRelease(result);
      error = osStatusMessage(status);
      return false;
    }

    if (result == nullptr) {
      return true;
    }

    if (CFGetTypeID(result) == CFArrayGetTypeID()) {
      CFArrayRef array = static_cast<CFArrayRef>(result);
      const CFIndex count = CFArrayGetCount(array);

      keys.reserve(static_cast<size_t>(count));
      for (CFIndex i = 0; i < count; ++i) {
        auto item = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(array, i));
        if (item == nullptr) {
          continue;
        }

        auto account = static_cast<CFStringRef>(CFDictionaryGetValue(item, kSecAttrAccount));
        if (account) {
          keys.push_back(cfStringToStdString(account));
        }
      }
    } else if (CFGetTypeID(result) == CFDictionaryGetTypeID()) {
      auto dict = static_cast<CFDictionaryRef>(result);
      auto account = static_cast<CFStringRef>(CFDictionaryGetValue(dict, kSecAttrAccount));
      if (account) {
        keys.push_back(cfStringToStdString(account));
      }
    }

    CFRelease(result);
    return true;
#elif ORO_RUNTIME_PLATFORM_WINDOWS
    keys.clear();

    const std::wstring prefix = makeScopePrefix(scope);

    DWORD count = 0;
    PCREDENTIALW* credentials = nullptr;

    if (!CredEnumerateW(L"oro:*", 0, &count, &credentials)) {
      const DWORD errorCode = GetLastError();
      if (errorCode == ERROR_NOT_FOUND) {
        return true;
      }

      error = windowsErrorMessage(errorCode);
      return false;
    }

    for (DWORD i = 0; i < count; ++i) {
      const auto cred = credentials[i];
      if (cred == nullptr || cred->TargetName == nullptr) {
        continue;
      }

      std::wstring target(cred->TargetName);
      if (target.rfind(prefix, 0) != 0) {
        continue;
      }

      std::wstring encodedKey = target.substr(prefix.size());
      try {
        keys.push_back(decodeComponent(encodedKey));
      } catch (...) {
        // ignore decoding errors to avoid aborting enumeration
      }
    }

    CredFree(credentials);
    return true;
#elif ORO_RUNTIME_PLATFORM_ANDROID
    keys.clear();

    auto app = app::App::sharedApplication();
    if (app == nullptr) {
      error = "Application unavailable";
      return false;
    }

    auto& android = app->runtime.android;
    if (!android.jvm || !android.activity) {
      error = "Android runtime unavailable";
      return false;
    }

    android::JNIEnvironmentAttachment attachment(android.jvm);
    if (!attachment.env) {
      error = "JNI environment unavailable";
      return false;
    }

    jclass cls = attachment.env->FindClass("oro/runtime/securestorage/SecureStorageManager");
    if (cls == nullptr) {
      attachment.printException();
      error = "SecureStorageManager class not found";
      return false;
    }

    jmethodID method = attachment.env->GetStaticMethodID(
      cls,
      "keys",
      "(Landroid/content/Context;Ljava/lang/String;)[Ljava/lang/String;"
    );

    if (method == nullptr) {
      attachment.printException();
      attachment.env->DeleteLocalRef(cls);
      error = "SecureStorageManager.keys not available";
      return false;
    }

    jstring jScope = attachment.env->NewStringUTF(scope.c_str());
    jobjectArray array = static_cast<jobjectArray>(attachment.env->CallStaticObjectMethod(
      cls,
      method,
      android.activity,
      jScope
    ));

    bool ok = !attachment.hasException();
    if (attachment.hasException()) {
      attachment.printException();
      attachment.env->ExceptionClear();
      ok = false;
    }

    if (ok && array != nullptr) {
      const jsize length = attachment.env->GetArrayLength(array);
      keys.reserve(static_cast<size_t>(length));
      for (jsize i = 0; i < length; ++i) {
        auto element = static_cast<jstring>(attachment.env->GetObjectArrayElement(array, i));
        const char* chars = attachment.env->GetStringUTFChars(element, nullptr);
        if (chars != nullptr) {
          keys.emplace_back(chars);
          attachment.env->ReleaseStringUTFChars(element, chars);
        }
        attachment.env->DeleteLocalRef(element);
      }
      attachment.env->DeleteLocalRef(array);
    } else if (!ok) {
      error = "Failed to list keys";
    }

    attachment.env->DeleteLocalRef(jScope);
    attachment.env->DeleteLocalRef(cls);

    return ok;
#elif ORO_RUNTIME_PLATFORM_LINUX
    keys = this->registeredKeys(scope);
    return true;
#else
    (void)scope;
    (void)keys;
    error = "Secure storage backend not implemented";
    return false;
#endif
  }
}
