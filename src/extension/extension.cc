#include "../runtime/crypto.hh"
#include "../runtime/cwd.hh"
#include "../runtime/filesystem.hh"
#include "../runtime/io.hh"
#include "../runtime/json.hh"
#include "../runtime/bytes.hh"
#include "../runtime/uuid.hh"
#include "../runtime/platform.hh"
#include "../runtime/os.hh"
#include "../runtime/bridge.hh"
#include "../runtime/version.hh"

#include <algorithm>
#include <climits>
#include <cstring>
#include <array>
#include <unordered_map>
#include <mutex>
#include <vector>

#include "extension.hh"

using oro::runtime::config::getUserConfig;
using oro::runtime::string::replace;
using oro::runtime::string::split;
using oro::runtime::string::trim;
using oro::runtime::filesystem::Resource;
using oro::runtime::String;
using SecureStorageService = oro::runtime::core::services::SecureStorage;
using BytesBuffer = oro::runtime::bytes::Buffer;
namespace JSON = oro::runtime::JSON;

static oro::runtime::core::Services* oapi_get_services (const oapi_context_t* context) {
  if (context == nullptr || context->router == nullptr) {
    return nullptr;
  }

  auto* router = reinterpret_cast<oro::runtime::ipc::Router*>(context->router);
  auto* runtime = router != nullptr ? router->bridge.getRuntime() : nullptr;
  return runtime != nullptr ? &runtime->services : nullptr;
}

struct PermissionObserverRecord {
  oro::runtime::core::services::Notifications::PermissionChangeObserver observer;
  oapi_context_t* context = nullptr;
  oapi_notifications_permission_callback callback = nullptr;
  void* data = nullptr;
};

struct ResponseObserverRecord {
  oro::runtime::core::services::Notifications::NotificationResponseObserver observer;
  oapi_context_t* context = nullptr;
  oapi_notifications_response_callback callback = nullptr;
  void* data = nullptr;
};

struct PresentedObserverRecord {
  oro::runtime::core::services::Notifications::NotificationPresentedObserver observer;
  oapi_context_t* context = nullptr;
  oapi_notifications_presented_callback callback = nullptr;
  void* data = nullptr;
};

static std::mutex g_notificationsMutex;
static std::unordered_map<uint64_t, PermissionObserverRecord> g_permissionObservers;
static std::unordered_map<uint64_t, ResponseObserverRecord> g_responseObservers;
static std::unordered_map<uint64_t, PresentedObserverRecord> g_presentedObservers;

struct SecureStorageRequest {
  String seq;
  oapi_context_t* context = nullptr;
  oapi_secure_storage_callback callback = nullptr;
  void* data = nullptr;
};

static std::mutex g_secureStorageMutex;
static std::unordered_map<String, SecureStorageRequest> g_secureStorageRequests;

static String oapi_secure_storage_next_seq () {
  return String("secureStorage:") + std::to_string(oro::runtime::crypto::rand64());
}

static bool oapi_secure_storage_track_request (
  const String& seq,
  oapi_context_t* context,
  oapi_secure_storage_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr) {
    return false;
  }

  oapi_context_retain(context);

  SecureStorageRequest request;
  request.seq = seq;
  request.context = context;
  request.callback = callback;
  request.data = data;

  std::lock_guard<std::mutex> lock(g_secureStorageMutex);
  auto [it, inserted] = g_secureStorageRequests.emplace(seq, request);
  if (!inserted) {
    oapi_context_release(context);
    return false;
  }

  return true;
}

static bool oapi_secure_storage_consume_request (
  const String& seq,
  SecureStorageRequest& out
) {
  std::lock_guard<std::mutex> lock(g_secureStorageMutex);
  auto it = g_secureStorageRequests.find(seq);
  if (it == g_secureStorageRequests.end()) {
    return false;
  }

  out = it->second;
  g_secureStorageRequests.erase(it);
  return true;
}

static String oapi_secure_storage_make_error_json (
  const char* source,
  const char* type,
  const String& message
) {
  const auto errorType = (type != nullptr && type[0] != '\0')
    ? String(type)
    : String("Error");

  JSON::Object payload(JSON::Object::Entries {
    {"source", String("secureStorage.") + (source != nullptr ? source : "")},
    {"err", JSON::Object::Entries {
      {"type", errorType},
      {"message", message}
    }}
  });
  return payload.str();
}

template <typename Callback>
static void oapi_dispatch_json_payload(
  oapi_context_t* context,
  const String& payload,
  Callback callback,
  void* data
);

static bool oapi_resource_check(
  oapi_context_t* context,
  const char* path,
  const char* policy
);

static void oapi_secure_storage_dispatch_error (
  oapi_context_t* context,
  oapi_secure_storage_callback callback,
  void* data,
  const char* source,
  const char* type,
  const String& message
) {
  if (callback == nullptr) {
    return;
  }

  const auto payload = oapi_secure_storage_make_error_json(source, type, message);

  if (context != nullptr && context->router != nullptr) {
    oapi_dispatch_json_payload(context, payload, callback, data);
  } else {
    std::vector<char> temp(payload.begin(), payload.end());
    temp.push_back('\0');
    callback(context, temp.data(), data);
  }
}

namespace oro::extension {
  static Extension::Map extensions = {};
  static Vector<String> initializedExtensions;
  static Mutex mutex;

  // explicit template instantiations
  template char* oro::extension::Extension::Context::Memory::alloc<char> (size_t);
  template oapi_context_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_context_t> ();

  template oro::runtime::ipc::Router::ReplyCallback*
  oro::extension::Extension::Context::Memory::alloc<oro::runtime::ipc::Router::ReplyCallback> (
    oro::runtime::ipc::Router::ReplyCallback
  );

  template oapi_ipc_result_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_ipc_result_t> (oapi_context_t*);

  template oapi_process_exec_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_process_exec_t> (
    oro::runtime::process::ExecOutput&
  );

  template oapi_json_any_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_any_t> (
    oapi_context_t*
  );

  template oapi_json_object_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_object_t> (
    oapi_context_t*
  );

  template oapi_json_array_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_array_t> (
    oapi_context_t*
  );

  template oapi_json_string_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_string_t> (
    oapi_context_t*,
    const char*
  );

  template oapi_json_boolean_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_boolean_t> (
    oapi_context_t*,
    bool
  );

  template oapi_json_number_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_number_t> (
    oapi_context_t*,
    int64_t
  );

  template oapi_json_number_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_number_t> (
    oapi_context_t*,
    double
  );

  template oapi_json_raw_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_json_raw_t> (
    oapi_context_t*,
    const char*
  );

  template oapi_ipc_message_t*
  oro::extension::Extension::Context::Memory::alloc<oapi_ipc_message_t> (
    oapi_ipc_message_t
  );

  Extension::Context::Context (const Extension* extension) {
    this->extension = extension;
    this->router = extension->context.router;
    this->config = extension->context.config;
    this->data = extension->context.data;
    this->policies = extension->context.policies;
  }

  Extension::Context::Context (const Context& context) {
    this->extension = context.extension;
    this->router = context.router;
    this->config = context.config;
    this->data = context.data;
    this->policies = context.policies;
    this->internal = context.internal;
  }

  Extension::Context::Context (const Context* context) {
    if (context != nullptr) {
      this->extension = context->extension;
      this->router = context->router;
      this->config = context->config;
      this->data = context->data;
      this->policies = context->policies;
      this->internal = context->internal;
    }
  }

  Extension::Context::Context (
    const Context& context,
    ipc::Router* router
  ) : Context(router) {
      this->extension = context.extension;
      this->router = router;
      this->config = context.config;
      this->data = context.data;
      this->policies = context.policies;
      this->internal = context.internal;
    }

  Extension::Context::Context (ipc::Router* router) : Context() {
    this->router = router;
  }

  void Extension::Context::retain () {
    this->retain_count++;
  }

  bool Extension::Context::release () {
    if (this->retain_count == 0) {
      debug("WARN - Double release of runtime extension context");
      return false;
    }
    if (--this->retain_count == 0) {
      this->memory.release();
      return true;
    }
    return false;
  }

  Extension::Context* Extension::getContext (const String& name) {
    return &extensions.at(name)->context;
  }

  void Extension::Context::setPolicy (const String& name, bool allowed) {
    if (name.size() == 0) return;
    if (this->hasPolicy(name)) {
      auto policy = this->getPolicy(name);
      policy.allowed = allowed;
      policy.name = name;
    } else {
      this->policies.insert_or_assign(name, Policy { name, allowed });
    }
  }

  const Extension::Context::Policy& Extension::Context::getPolicy (
    const String& name
  ) const {
    return this->policies.at(name);
  }

  bool Extension::Context::hasPolicy (const String& name) const {
    return this->policies.contains(name);
  }

  bool Extension::Context::isAllowed (const String& name) const {
    if (this->policies.size() == 0) {
      return true;
    }
    auto names = split(name, ',');

    // parse each comma (',') separated policy
    for (const auto& value : names) {
      auto parts = split(trim(value), '_');
      String current = "";
      // try each part of the policy (ipc, ipc_router, ipc_router_map)
      for (const auto& part : parts) {
        current += part;
        if (this->hasPolicy(current) && this->getPolicy(current).allowed) {
          return true;
        }

        current += "_";
      }
    }

    return false;
  }

  Extension::Context::Memory::~Memory () {
    this->release();
  }

  void Extension::Context::Memory::release () {
    Lock lock(this->mutex);
    if (this->pool.size() == 0) {
      return;
    }

    for (const auto& releaseCallback : this->pool) {
      releaseCallback();
    }

    this->pool.clear();
  }

  String Extension::getExtensionsDirectory (const String& name) {
    auto cwd = runtime::getcwd();
  #if ORO_RUNTIME_PLATFORM_WINDOWS
    const auto oroPath = cwd + "\\oro\\extensions\\" + name + "\\";
    const auto socketPath = cwd + "\\socket\\extensions\\" + name + "\\";
  #else
    const auto oroPath = cwd + "/oro/extensions/" + name + "/";
    const auto socketPath = cwd + "/socket/extensions/" + name + "/";
  #endif

    const auto oroWasmPath = oroPath + (name + ".wasm");
    const auto oroLibraryPath = oroPath + (name + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME);
    if (fs::exists(oroWasmPath) || fs::exists(oroLibraryPath)) {
      return oroPath;
    }

    const auto socketWasmPath = socketPath + (name + ".wasm");
    const auto socketLibraryPath = socketPath + (name + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME);
    if (fs::exists(socketWasmPath) || fs::exists(socketLibraryPath)) {
      return socketPath;
    }

    return oroPath;
  }

  void Extension::Context::Memory::push (Function<void()> callback) {
    Lock lock(this->mutex);
    this->pool.push_back(callback);
  }

  Extension::Extension (const String& name, const Initializer initializer)
    : name(name), initializer(initializer) {
    static auto userConfig = getUserConfig();
    this->context.extension = this;
    this->path = getExtensionPath(name);
    this->type = getExtensionType(name);
    for (const auto& tuple : userConfig) {
      auto& key = tuple.first;
      auto& value = tuple.second;
      auto prefix = "extensions_" + name + "_";
      if (key.starts_with(prefix)) {
        auto name = replace(key, prefix, "");
        this->context.config[name] = value;
      }
    }
  }

  Extension::Extension (Extension& extension) {
    this->name = extension.name;
    this->context.config = extension.context.config;
    this->initializer = extension.initializer;
    this->path = extension.path;
    this->type = extension.type;
  }

  const Extension::Map& Extension::all () {
    return extensions;
  }

  const Extension::Entry Extension::get (const String& name) {
    Lock lock(mutex);
    return extensions.at(name);
  }

  bool Extension::setHandle (const String& name, void* handle) {
    if (!extensions.contains(name)) {
      runtime::io::write("WARN - extensions does not contain " + name);
      return false;
    }

    runtime::io::write("Registering extension handle " + name);
    auto extension = extensions.at(name);
    extension->handle = handle;
    extension->path = getExtensionPath(name);
    extension->type = getExtensionType(name);
    return true;
  }

  void Extension::setRouterContext (
    const String& name,
    ipc::Router* router,
    Context* context
  ) {
    if (!extensions.contains(name)) return;
    auto extension = extensions.at(name);
    extension->contexts[router] = context;
  }

  Extension::Context* Extension::getRouterContext (
    const String& name,
    ipc::Router* router
  ) {
    if (!extensions.contains(name)) return nullptr;
    return extensions.at(name)->contexts[router];
  }

  void Extension::removeRouterContext (
    const String& name,
    ipc::Router* router
  ) {
    if (!extensions.contains(name)) return;
    extensions.at(name)->contexts.erase(router);
  }

  bool Extension::isLoaded (const String& name) {
    Lock lock(mutex);
    return extensions.contains(name) && extensions.at(name) != nullptr;
  }

  bool Extension::isInitialized (const String& name) {
    Lock lock(mutex);
    return std::find(
      initializedExtensions.begin(),
      initializedExtensions.end(),
      name
    ) != initializedExtensions.end();
  }

  String Extension::getExtensionType (const String& name) {
    const auto libraryPath = getExtensionsDirectory(name) + (name + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME);
    const auto wasmPath = getExtensionsDirectory(name) + (name + ".wasm");
    if (fs::exists(wasmPath)) {
      return "wasm32";
    }

    if (fs::exists(libraryPath)) {
      return "shared";
    }

    return "unknown";
  }

  String Extension::getExtensionPath (const String& name) {
    const auto type = getExtensionType(name);
    if (type == "wasm32") {
      return getExtensionsDirectory(name) + (name + ".wasm");
    }

    if (type == "shared") {
      return getExtensionsDirectory(name) + (name + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME);
    }

    return "";
  }

  bool Extension::load (const String& name) {
    Lock lock(mutex);

    static auto userConfig = getUserConfig();
    // Enforce safe extension name (alnum, dash, underscore)
    {
      auto isSafe = [](const String& s) {
        for (const auto ch : s) {
          if (!(
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_'
          )) {
            return false;
          }
        }
        return s.size() > 0;
      };
      if (!isSafe(name)) {
        debug("Refusing to load extension with unsafe name: %s", name.c_str());
        return false;
      }
    }
    // check if extension is already known
    if (isLoaded(name)) {
      return true;
    }

    auto path = getExtensionsDirectory(name) + (name + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME);

    // Optional allowlist of extension roots
    {
      const auto allowedRootsStr = userConfig.contains("extensions_allowed_roots")
        ? userConfig.at("extensions_allowed_roots")
        : String("");
      if (allowedRootsStr.size() > 0) {
        auto roots = oro::runtime::string::split(allowedRootsStr, ' ');
        bool ok = false;
        for (auto root : roots) {
          try {
            auto r = fs::weakly_canonical(root);
            auto p = fs::weakly_canonical(fs::path(path).remove_filename());
            auto rs = r.string();
            auto ps = p.string();
          #if !ORO_RUNTIME_PLATFORM_WINDOWS
            std::replace(rs.begin(), rs.end(), '\\', '/');
            std::replace(ps.begin(), ps.end(), '\\', '/');
          #endif
            if (!rs.empty() && rs.back() != '/' && rs.back() != '\\') {
              rs += rs.find('\\') != String::npos ? "\\" : "/";
            }
            if (ps == r.string() || ps.rfind(rs, 0) == 0) {
              ok = true;
              break;
            }
          } catch (...) {}
        }
        if (!ok) {
          debug("Refusing to load extension '%s' from non-allowlisted root", name.c_str());
          return false;
        }
      }
    }

  #if ORO_RUNTIME_PLATFORM_WINDOWS
    auto handle = LoadLibrary(path.c_str());
    if (handle == nullptr) {
      return false;
    }
    auto extension_entry = (oapi_extension_registration_entry) GetProcAddress(handle, "__oapi_extension_init");
    if (!extension_entry) {
      extension_entry = (oapi_extension_registration_entry) GetProcAddress(handle, "__sapi_extension_init");
    }
    if (!extension_entry) {
      return false;
    }
  #else
  #if ORO_RUNTIME_PLATFORM_ANDROID
    auto handle = dlopen(String("libextension-" + name + ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME).c_str(), RTLD_NOW | RTLD_LOCAL);
  #else
    auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  #endif

    if (handle == nullptr) {
      return false;
    }

    auto extension_entry = (oapi_extension_registration_entry) dlsym(handle, "__oapi_extension_init");
    if (!extension_entry) {
      extension_entry = (oapi_extension_registration_entry) dlsym(handle, "__sapi_extension_init");
    }
    if (!extension_entry) {
      return false;
    }
  #endif

    if (extension_entry != nullptr) {
      oapi_extension_registration_t* registration = extension_entry();
      if (registration != nullptr) {
        auto abi = registration->abi;
        if (abi != ORO_RUNTIME_EXTENSION_ABI_VERSION) {
          if (userConfig["build_extensions_abi_strict"] != "false") {
            debug(
              "Failed to load extension: %s - Extension ABI %lu does not match runtime ABI %lu",
              registration->name,
              abi,
              (unsigned long) ORO_RUNTIME_EXTENSION_ABI_VERSION
            );
            return false;
          } else if (abi < ORO_RUNTIME_EXTENSION_ABI_VERSION) {
            debug(
              "WARNING: Extension %s ABI %lu does not match runtime ABI %lu - trying to load anyways.\n"
              "Remove `[build.extensions.abi] strict = false' to disable this",
              registration->name,
              abi,
              (unsigned long) ORO_RUNTIME_EXTENSION_ABI_VERSION
            );
          } else {
            debug(
              "Refusing to load extension: %s - "
              "Extension ABI %lu is incompatible with runtime ABI %lu.\n"
              "Rebuild the extension to fix this",
              registration->name,
              abi,
              (unsigned long) ORO_RUNTIME_EXTENSION_ABI_VERSION
            );
            return false;
          }
        }

        debug("Registering loaded extension: %s", registration->name);
        if (oapi_extension_register(registration)) {
          return Extension::setHandle(name, reinterpret_cast<void*>(handle));
        }
      }
    }

    return false;
  }

  bool Extension::unload (Context* ctx, const String& name, bool shutdown) {
    Lock lock(mutex);

    if (!Extension::isLoaded(name)) {
      return false;
    }

    auto extension = Extension::get(name);

    if (!extension->deinitializer(ctx, ctx->data)) {
      return false;
    }

    if (!shutdown) {
      return true;
    }

  #if ORO_RUNTIME_PLATFORM_WINDOWS
    if (!FreeLibrary(reinterpret_cast<HMODULE>(extension->handle))) {
      return false;
    }
  #else
    if (dlclose(extension->handle)) {
      return false;
    }
  #endif

    if (extensions.contains(name)) {
      extensions.erase(name);
    }

    return true;
  }

  void Extension::create (
    const String& name,
    const Initializer initializer
  ) {
    Lock lock(mutex);
    // return early if exists
    if (isLoaded(name)) {
      return;
    }

    auto extension = std::make_shared<Extension>(name, initializer);
    extensions.insert_or_assign(name, extension);
  }

  bool Extension::initialize (
    Context* ctx,
    const String& name,
    const void* data
  ) {
    Lock lock(mutex);
    // return early if not exists
    if (!isLoaded(name)) {
      return false;
    }

    if (std::count(initializedExtensions.begin(), initializedExtensions.end(), name) > 0) {
      debug("Extension '%s' already loaded", name.c_str());
      // already initialized
      return true;
    }

    auto extension = extensions.at(name);
    if (extension != nullptr && extension->initializer != nullptr) {
      debug("Initializing loaded extension: %s", name.c_str());
      extension->context.data = data;
      auto didInitialize = extension->initializer(ctx, data);
      if (didInitialize) {
        initializedExtensions.push_back(name);
      }
      return didInitialize;
    }

    return false;
  }
}

bool oapi_extension_register (
  const oapi_extension_registration_t* registration
) {
  oro::runtime::Lock lock(oro::extension::mutex);

  if (registration == nullptr) return false;
  if (registration->abi == 0) return false;
  if (registration->initializer == nullptr) return false;

  oro::extension::Extension::create(registration->name, [registration](
    auto ctx,
    auto data
  ) mutable {
    return registration->initializer(reinterpret_cast<oapi_context_t*>(ctx), data);
  });

  // should exist if `create()` above succeeded
  if (oro::extension::Extension::isLoaded(registration->name)) {
    auto extension = oro::extension::extensions.at(registration->name);

    extension->abi = registration->abi;
    extension->deinitializer = [registration] (auto ctx, auto data) {
      auto deinitializer = registration->deinitializer;
      if (deinitializer != nullptr) {
        debug("Unloading extension: %s", registration->name);
        auto initializedExtensionsCursor = std::find(
          oro::extension::initializedExtensions.begin(),
          oro::extension::initializedExtensions.end(),
          oro::extension::String(registration->name)
         );

        if (initializedExtensionsCursor != oro::extension::initializedExtensions.end()) {
          oro::extension::initializedExtensions.erase(initializedExtensionsCursor);
        }

        return deinitializer(reinterpret_cast<oapi_context_t*>(ctx), data);
      }

      return true;
    };

    if (registration->description != nullptr) {
      extension->description = registration->description;
    }

    if (registration->version != nullptr) {
      extension->version = registration->version;
    }

    extension->registration = registration;

    return true;
  }

  return false;
}

bool oapi_extension_is_allowed (oapi_context_t* context, const char *allowed) {
  if (context == nullptr) return false;
  if (allowed == nullptr) return false;
  return context->isAllowed(allowed);
}

const oapi_extension_registration_t* oapi_extension_get (
  const oapi_context_t* context,
  const char *name
) {
  if (!oro::extension::Extension::isLoaded(name)) {
    return nullptr;
  }

  auto extension = oro::extension::Extension::get(name).get();
  if (extension) {
    return reinterpret_cast<const oapi_extension_registration_t*>(
      extension->registration
    );
  }

  return nullptr;
}

bool oapi_extension_load (
    oapi_context_t* context,
    const char *name,
    const void* data
    ) {
  if (name == nullptr) {
    return false;
  }

  if (context != nullptr && !context->isAllowed("extension_load")) {
    oapi_debug(context, "'extension_load' is not allowed.");
    return false;
  }

  if (!oro::extension::Extension::load(name)) {
    return false;
  }

  if (!oro::extension::Extension::initialize(context, name, data)) {
    return false;
  }

  return true;
}

bool oapi_extension_unload (oapi_context_t* context, const char *name) {
  if (name == nullptr) {
    return false;
  }

  if (context != nullptr && !context->isAllowed("extension_unload")) {
    oapi_debug(context, "'extension_unload' is not allowed.");
    return false;
  }

  if (!oro::extension::Extension::isLoaded(name)) {
    return false;
  }

  if (!oro::extension::Extension::unload(context, name, true)) {
    return false;
  }

  return true;
}

bool oapi_extension_is_loaded (const char *name) {
  if (name == nullptr) {
    return false;
  }

  return oro::extension::Extension::isLoaded(name);
}

const char* oapi_extension_get_path (
  oapi_context_t* context,
  const char* name
) {
  if (context == nullptr || name == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("extension_get_path")) {
    oapi_debug(context, "'extension_get_path' is not allowed.");
    return nullptr;
  }

  auto path = oro::extension::Extension::getExtensionPath(name);
  if (path.size() == 0) {
    return nullptr;
  }

  auto buffer = context->memory.alloc<char>(path.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  memcpy(buffer, path.c_str(), path.size() + 1);

  return buffer;
}

const char* oapi_extension_get_type (
  oapi_context_t* context,
  const char* name
) {
  if (context == nullptr || name == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("extension_get_type")) {
    oapi_debug(context, "'extension_get_type' is not allowed.");
    return nullptr;
  }

  auto type = oro::extension::Extension::getExtensionType(name);
  if (type.size() == 0) {
    return nullptr;
  }

  auto buffer = context->memory.alloc<char>(type.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  memcpy(buffer, type.c_str(), type.size() + 1);

  return buffer;
}

size_t oapi_extension_enumerate (
  oapi_context_t* context,
  oapi_extension_enumerate_callback callback,
  void* data
) {
  if (callback == nullptr) {
    return 0;
  }

  if (context != nullptr && !context->isAllowed("extension_enumerate")) {
    oapi_debug(context, "'extension_enumerate' is not allowed.");
    return 0;
  }

  oro::runtime::Vector<oro::extension::Extension::Entry> entries;
  oro::runtime::Vector<oro::runtime::String> initialized;

  {
    oro::runtime::Lock lock(oro::extension::mutex);
    initialized = oro::extension::initializedExtensions;
    for (const auto& tuple : oro::extension::Extension::all()) {
      if (tuple.second != nullptr) {
        entries.push_back(tuple.second);
      }
    }
  }

  size_t count = 0;
  for (const auto& entry : entries) {
    if (entry == nullptr) {
      continue;
    }

    const bool isInitialized = std::find(
      initialized.begin(),
      initialized.end(),
      entry->name
    ) != initialized.end();

    const oapi_extension_info_t info = {
      .abi = entry->abi,
      .name = entry->name.c_str(),
      .description = entry->description.size() > 0
        ? entry->description.c_str()
        : nullptr,
      .version = entry->version.size() > 0
        ? entry->version.c_str()
        : nullptr,
      .path = entry->path.size() > 0
        ? entry->path.c_str()
        : nullptr,
      .type = entry->type.size() > 0
        ? entry->type.c_str()
        : nullptr,
      .loaded = entry->handle != nullptr,
      .initialized = isInitialized
    };

    callback(&info, data);
    count++;
  }

  return count;
}

int oapi_rand32 () {
  return oro::runtime::crypto::randint();
}

int oapi_rand_range (int min, int max) {
  if (max < min) {
    std::swap(min, max);
  }

  if (min == max) {
    return min;
  }

  return oro::runtime::crypto::randint(min, max);
}

const unsigned char* oapi_rand_bytes (
  oapi_context_t* context,
  size_t size
) {
  if (context == nullptr || size == 0) {
    return nullptr;
  }

  if (!context->isAllowed("crypto_rand_bytes")) {
    oapi_debug(context, "'crypto_rand_bytes' is not allowed.");
    return nullptr;
  }

  auto buffer = context->memory.alloc<unsigned char>(size);
  if (buffer == nullptr) {
    return nullptr;
  }

  size_t offset = 0;
  while (offset < size) {
    auto value = oro::runtime::crypto::rand64();
    const size_t chunk = std::min<size_t>(size - offset, sizeof(value));
    std::memcpy(buffer + offset, &value, chunk);
    offset += chunk;
  }

  return buffer;
}

const char* oapi_crypto_sha1 (
  oapi_context_t* context,
  const unsigned char* bytes,
  size_t size
) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("crypto_sha1")) {
    oapi_debug(context, "'crypto_sha1' is not allowed.");
    return nullptr;
  }

  if (bytes == nullptr && size > 0) {
    return nullptr;
  }

  const auto hash = oro::runtime::crypto::sha1(
    bytes != nullptr ? bytes : reinterpret_cast<const unsigned char*>(""),
    size
  );

  auto buffer = context->memory.alloc<char>(hash.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  std::memcpy(buffer, hash.c_str(), hash.size());
  buffer[hash.size()] = '\0';
  return buffer;
}

const char* oapi_uuid_v7 (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("uuid_generate")) {
    oapi_debug(context, "'uuid_generate' is not allowed.");
    return nullptr;
  }

  const auto uuid = oro::runtime::uuid::v7();
  auto buffer = context->memory.alloc<char>(uuid.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  std::memcpy(buffer, uuid.c_str(), uuid.size());
  buffer[uuid.size()] = '\0';
  return buffer;
}

static const char* oapi_copy_string (
  oapi_context_t* context,
  const String& value
) {
  if (context == nullptr) {
    return nullptr;
  }

  auto buffer = context->memory.alloc<char>(value.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  std::memcpy(buffer, value.c_str(), value.size());
  buffer[value.size()] = '\0';
  return buffer;
}

template <typename Callback>
static void oapi_dispatch_json_payload (
  oapi_context_t* context,
  const String& payload,
  Callback callback,
  void* data
) {
  if (context == nullptr || context->router == nullptr) {
    return;
  }

  auto json = payload;
  oapi_context_retain(context);
  auto dispatched = context->router->bridge.dispatch([context, callback, data, json]() {
    auto buffer = context->memory.alloc<char>(json.size() + 1);
    if (buffer != nullptr) {
      std::memcpy(buffer, json.c_str(), json.size() + 1);
      callback(context, buffer, data);
    }
    oapi_context_release(context);
  });

  if (!dispatched) {
    oapi_context_release(context);
  }
}

const char* oapi_runtime_version (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("runtime_version")) {
    oapi_debug(context, "'runtime_version' is not allowed.");
    return nullptr;
  }

  return oapi_copy_string(context, oro::runtime::version::VERSION_STRING);
}

const char* oapi_runtime_version_hash (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("runtime_version_hash")) {
    oapi_debug(context, "'runtime_version_hash' is not allowed.");
    return nullptr;
  }

  return oapi_copy_string(context, oro::runtime::version::VERSION_HASH_STRING);
}

const char* oapi_runtime_version_full (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("runtime_version_full")) {
    oapi_debug(context, "'runtime_version_full' is not allowed.");
    return nullptr;
  }

  return oapi_copy_string(context, oro::runtime::version::VERSION_FULL_STRING);
}

bool oapi_config_has (
  oapi_context_t* context,
  const char* key
) {
  if (context == nullptr || key == nullptr) {
    return false;
  }

  if (!context->isAllowed("config_has")) {
    oapi_debug(context, "'config_has' is not allowed.");
    return false;
  }

  static const auto userConfig = getUserConfig();
  return userConfig.contains(key);
}

const char* oapi_config_get (
  oapi_context_t* context,
  const char* key
) {
  if (context == nullptr || key == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("config_get")) {
    oapi_debug(context, "'config_get' is not allowed.");
    return nullptr;
  }

  static const auto userConfig = getUserConfig();
  if (!userConfig.contains(key)) {
    return nullptr;
  }

  return oapi_copy_string(context, userConfig.at(key));
}

const char* oapi_config_get_json (
  oapi_context_t* context,
  const char* prefix
) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("config_get_json")) {
    oapi_debug(context, "'config_get_json' is not allowed.");
    return nullptr;
  }

  static const auto userConfig = getUserConfig();
  String filter = prefix != nullptr ? String(prefix) : String("");

  JSON::Object object;
  for (const auto& entry : userConfig) {
    if (filter.size() > 0 && entry.first.rfind(filter, 0) != 0) {
      continue;
    }

    object.set(entry.first, JSON::String(entry.second));
  }

  return oapi_copy_string(context, object.str());
}

const char* oapi_config_keys_json (
  oapi_context_t* context,
  const char* prefix
) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("config_keys_json")) {
    oapi_debug(context, "'config_keys_json' is not allowed.");
    return nullptr;
  }

  static const auto userConfig = getUserConfig();
  String filter = prefix != nullptr ? String(prefix) : String("");

  JSON::Array array;
  for (const auto& entry : userConfig) {
    if (filter.size() > 0 && entry.first.rfind(filter, 0) != 0) {
      continue;
    }

    array.push(JSON::String(entry.first));
  }

  return oapi_copy_string(context, array.str());
}

uint64_t oapi_notifications_on_permission_change (
  oapi_context_t* context,
  oapi_notifications_permission_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr) {
    return 0;
  }

  if (!context->isAllowed("notifications_permission_observe")) {
    oapi_debug(context, "'notifications_permission_observe' is not allowed.");
    return 0;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Notifications observers require a router context.");
    return 0;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->notifications.enabled) {
    return 0;
  }

  auto observer = oro::runtime::core::services::Notifications::PermissionChangeObserver();
  const auto observerId = observer.id;

  auto observerCallback = [context, callback, data](const JSON::Object& payload) {
    oapi_dispatch_json_payload(context, payload.str(), callback, data);
  };

  observer.callback = observerCallback;

  if (!services->notifications.addPermissionChangeObserver(observer, observerCallback)) {
    return 0;
  }

  oapi_context_retain(context);

  {
    std::lock_guard<std::mutex> lock(g_notificationsMutex);
    auto [it, inserted] = g_permissionObservers.emplace(
      observerId,
      PermissionObserverRecord{observer, context, callback, data}
    );

    if (!inserted) {
      services->notifications.removePermissionChangeObserver(observer);
      oapi_context_release(context);
      return 0;
    }
  }

  return observerId;
}

bool oapi_notifications_off_permission_change (
  oapi_context_t* context,
  uint64_t token
) {
  if (token == 0 || context == nullptr) {
    return false;
  }

  if (!context->isAllowed("notifications_permission_observe")) {
    oapi_debug(context, "'notifications_permission_observe' is not allowed.");
    return false;
  }

  PermissionObserverRecord record;
  {
    std::lock_guard<std::mutex> lock(g_notificationsMutex);
    auto it = g_permissionObservers.find(token);
    if (it == g_permissionObservers.end()) {
      return false;
    }
    record = it->second;
    g_permissionObservers.erase(it);
  }

  if (auto* services = oapi_get_services(record.context); services != nullptr && services->notifications.enabled) {
    services->notifications.removePermissionChangeObserver(record.observer);
  }

  oapi_context_release(record.context);
  return true;
}

uint64_t oapi_notifications_on_response (
  oapi_context_t* context,
  oapi_notifications_response_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr) {
    return 0;
  }

  if (!context->isAllowed("notifications_response_observe")) {
    oapi_debug(context, "'notifications_response_observe' is not allowed.");
    return 0;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Notifications observers require a router context.");
    return 0;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->notifications.enabled) {
    return 0;
  }

  auto observer = oro::runtime::core::services::Notifications::NotificationResponseObserver();
  const auto observerId = observer.id;

  auto observerCallback = [context, callback, data](const JSON::Object& payload) {
    oapi_dispatch_json_payload(context, payload.str(), callback, data);
  };

  observer.callback = observerCallback;

  if (!services->notifications.addNotificationResponseObserver(observer, observerCallback)) {
    return 0;
  }

  oapi_context_retain(context);

  {
    std::lock_guard<std::mutex> lock(g_notificationsMutex);
    auto [it, inserted] = g_responseObservers.emplace(
      observerId,
      ResponseObserverRecord{observer, context, callback, data}
    );

    if (!inserted) {
      services->notifications.removeNotificationResponseObserver(observer);
      oapi_context_release(context);
      return 0;
    }
  }

  return observerId;
}

bool oapi_notifications_off_response (
  oapi_context_t* context,
  uint64_t token
) {
  if (token == 0 || context == nullptr) {
    return false;
  }

  if (!context->isAllowed("notifications_response_observe")) {
    oapi_debug(context, "'notifications_response_observe' is not allowed.");
    return false;
  }

  ResponseObserverRecord record;
  {
    std::lock_guard<std::mutex> lock(g_notificationsMutex);
    auto it = g_responseObservers.find(token);
    if (it == g_responseObservers.end()) {
      return false;
    }
    record = it->second;
    g_responseObservers.erase(it);
  }

  if (auto* services = oapi_get_services(record.context); services != nullptr && services->notifications.enabled) {
    services->notifications.removeNotificationResponseObserver(record.observer);
  }

  oapi_context_release(record.context);
  return true;
}

uint64_t oapi_notifications_on_presented (
  oapi_context_t* context,
  oapi_notifications_presented_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr) {
    return 0;
  }

  if (!context->isAllowed("notifications_presented_observe")) {
    oapi_debug(context, "'notifications_presented_observe' is not allowed.");
    return 0;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Notifications observers require a router context.");
    return 0;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->notifications.enabled) {
    return 0;
  }

  auto observer = oro::runtime::core::services::Notifications::NotificationPresentedObserver();
  const auto observerId = observer.id;

  auto observerCallback = [context, callback, data](const JSON::Object& payload) {
    oapi_dispatch_json_payload(context, payload.str(), callback, data);
  };

  observer.callback = observerCallback;

  if (!services->notifications.addNotificationPresentedObserver(observer, observerCallback)) {
    return 0;
  }

  oapi_context_retain(context);

  {
    std::lock_guard<std::mutex> lock(g_notificationsMutex);
    auto [it, inserted] = g_presentedObservers.emplace(
      observerId,
      PresentedObserverRecord{observer, context, callback, data}
    );

    if (!inserted) {
      services->notifications.removeNotificationPresentedObserver(observer);
      oapi_context_release(context);
      return 0;
    }
  }

  return observerId;
}

bool oapi_notifications_off_presented (
  oapi_context_t* context,
  uint64_t token
) {
  if (token == 0 || context == nullptr) {
    return false;
  }

  if (!context->isAllowed("notifications_presented_observe")) {
    oapi_debug(context, "'notifications_presented_observe' is not allowed.");
    return false;
  }

  PresentedObserverRecord record;
  {
    std::lock_guard<std::mutex> lock(g_notificationsMutex);
    auto it = g_presentedObservers.find(token);
    if (it == g_presentedObservers.end()) {
      return false;
    }
    record = it->second;
    g_presentedObservers.erase(it);
  }

  if (auto* services = oapi_get_services(record.context); services != nullptr && services->notifications.enabled) {
    services->notifications.removeNotificationPresentedObserver(record.observer);
  }

  oapi_context_release(record.context);
  return true;
}

struct ServiceEntry {
  const char* name;
  oro::runtime::core::Service* instance;
};

static std::array<ServiceEntry, 28> oapi_collect_services (oro::runtime::core::Services& services) {
  return std::array<ServiceEntry, 28> {{
    {"ai", &services.ai},
    {"conduit", &services.conduit},
    {"broadcastChannel", &services.broadcastChannel},
    {"dns", &services.dns},
    {"diagnostics", &services.diagnostics},
    {"fs", &services.fs},
    {"geolocation", &services.geolocation},
    {"mediaDevices", &services.mediaDevices},
    {"networkStatus", &services.networkStatus},
    {"notifications", &services.notifications},
    {"iroh", &services.iroh},
    {"otp", &services.otp},
    {"os", &services.os},
    {"permissions", &services.permissions},
    {"platform", &services.platform},
    {"process", &services.process},
    {"sqlite", &services.sqlite},
    {"secureStorage", &services.secureStorage},
    {"hid", &services.hid},
    {"bluetooth", &services.bluetooth},
    {"usb", &services.usb},
    {"dbus", &services.dbus},
    {"timers", &services.timers},
    {"udp", &services.udp},
    {"tcp", &services.tcp},
    {"tls", &services.tls},
    {"httpBridge", &services.httpBridge},
    {"mcp", &services.mcp}
  }};
}

size_t oapi_services_enumerate (
  oapi_context_t* context,
  oapi_service_enumerate_callback callback,
  void* data
) {
  if (callback == nullptr) {
    return 0;
  }

  if (context == nullptr) {
    return 0;
  }

  if (!context->isAllowed("services_enumerate")) {
    oapi_debug(context, "'services_enumerate' is not allowed.");
    return 0;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr) {
    return 0;
  }

  size_t count = 0;
  for (const auto& entry : oapi_collect_services(*services)) {
    if (entry.instance == nullptr) {
      continue;
    }

    const oapi_service_info_t info = {
      entry.name,
      entry.instance->enabled
    };

    callback(&info, data);
    count++;
  }

  return count;
}

bool oapi_service_is_enabled (
  oapi_context_t* context,
  const char* name
) {
  if (context == nullptr || name == nullptr) {
    return false;
  }

  if (!context->isAllowed("services_is_enabled")) {
    oapi_debug(context, "'services_is_enabled' is not allowed.");
    return false;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr) {
    return false;
  }

  for (const auto& entry : oapi_collect_services(*services)) {
    if (entry.instance == nullptr) {
      continue;
    }

    if (std::strcmp(entry.name, name) == 0) {
      return entry.instance->enabled;
    }
  }

  return false;
}

const char* oapi_services_json (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("services_json")) {
    oapi_debug(context, "'services_json' is not allowed.");
    return nullptr;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr) {
    return nullptr;
  }

  JSON::Array array;
  for (const auto& entry : oapi_collect_services(*services)) {
    if (entry.instance == nullptr) {
      continue;
    }

    JSON::Object object;
    object.set("name", JSON::String(entry.name));
    object.set("enabled", JSON::Boolean(entry.instance->enabled));
    array.push(object);
  }

  return oapi_copy_string(context, array.str());
}

bool oapi_secure_storage_set (
  oapi_context_t* context,
  const char* scope,
  const char* key,
  const unsigned char* value,
  size_t value_size,
  oapi_secure_storage_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr || key == nullptr) {
    return false;
  }

  if (!context->isAllowed("secure_storage")) {
    oapi_secure_storage_dispatch_error(context, callback, data, "set", "NotAllowedError", "Secure storage access denied");
    return false;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Secure storage operations require an IPC router context.");
    return false;
  }

  if (value_size > 0 && value == nullptr) {
    oapi_secure_storage_dispatch_error(context, callback, data, "set", "TypeError", "Value pointer must not be null when size is greater than zero");
    return false;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->secureStorage.enabled) {
    oapi_secure_storage_dispatch_error(context, callback, data, "set", "NotAllowedError", "Secure storage service unavailable");
    return false;
  }

  String scopeStr = scope != nullptr ? String(scope) : String();
  String keyStr = String(key);

  BytesBuffer buffer;
  if (value_size > 0) {
    buffer = BytesBuffer::from(value, static_cast<BytesBuffer::size_type>(value_size));
  } else {
    static const unsigned char empty[] = {0};
    buffer = BytesBuffer::from(empty, 0);
  }

  const auto seq = oapi_secure_storage_next_seq();
  if (!oapi_secure_storage_track_request(seq, context, callback, data)) {
    oapi_secure_storage_dispatch_error(context, callback, data, "set", "InternalError", "Failed to register secure storage request");
    return false;
  }

  services->secureStorage.set(seq, std::move(scopeStr), std::move(keyStr), std::move(buffer), [](const String& responseSeq, JSON::Any payload, oro::runtime::QueuedResponse) {
    SecureStorageRequest request;
    if (!oapi_secure_storage_consume_request(responseSeq, request)) {
      return;
    }

    const auto payloadStr = payload.str();
    oapi_dispatch_json_payload(request.context, payloadStr, request.callback, request.data);
    oapi_context_release(request.context);
  });

  return true;
}

bool oapi_secure_storage_get (
  oapi_context_t* context,
  const char* scope,
  const char* key,
  const char* encoding,
  oapi_secure_storage_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr || key == nullptr) {
    return false;
  }

  if (!context->isAllowed("secure_storage")) {
    oapi_secure_storage_dispatch_error(context, callback, data, "get", "NotAllowedError", "Secure storage access denied");
    return false;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Secure storage operations require an IPC router context.");
    return false;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->secureStorage.enabled) {
    oapi_secure_storage_dispatch_error(context, callback, data, "get", "NotAllowedError", "Secure storage service unavailable");
    return false;
  }

  SecureStorageService::Encoding encodingEnum = SecureStorageService::Encoding::UTF8;
  if (encoding != nullptr && encoding[0] != '\0') {
    if (!SecureStorageService::parseEncoding(String(encoding), encodingEnum)) {
      oapi_secure_storage_dispatch_error(context, callback, data, "get", "TypeError", String("Unsupported encoding: ") + encoding);
      return false;
    }
  }

  String scopeStr = scope != nullptr ? String(scope) : String();
  String keyStr = String(key);

  const auto seq = oapi_secure_storage_next_seq();
  if (!oapi_secure_storage_track_request(seq, context, callback, data)) {
    oapi_secure_storage_dispatch_error(context, callback, data, "get", "InternalError", "Failed to register secure storage request");
    return false;
  }

  services->secureStorage.get(seq, std::move(scopeStr), std::move(keyStr), encodingEnum, [](const String& responseSeq, JSON::Any payload, oro::runtime::QueuedResponse) {
    SecureStorageRequest request;
    if (!oapi_secure_storage_consume_request(responseSeq, request)) {
      return;
    }

    const auto payloadStr = payload.str();
    oapi_dispatch_json_payload(request.context, payloadStr, request.callback, request.data);
    oapi_context_release(request.context);
  });

  return true;
}

bool oapi_secure_storage_remove (
  oapi_context_t* context,
  const char* scope,
  const char* key,
  oapi_secure_storage_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr || key == nullptr) {
    return false;
  }

  if (!context->isAllowed("secure_storage")) {
    oapi_secure_storage_dispatch_error(context, callback, data, "remove", "NotAllowedError", "Secure storage access denied");
    return false;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Secure storage operations require an IPC router context.");
    return false;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->secureStorage.enabled) {
    oapi_secure_storage_dispatch_error(context, callback, data, "remove", "NotAllowedError", "Secure storage service unavailable");
    return false;
  }

  String scopeStr = scope != nullptr ? String(scope) : String();
  String keyStr = String(key);

  const auto seq = oapi_secure_storage_next_seq();
  if (!oapi_secure_storage_track_request(seq, context, callback, data)) {
    oapi_secure_storage_dispatch_error(context, callback, data, "remove", "InternalError", "Failed to register secure storage request");
    return false;
  }

  services->secureStorage.remove(seq, std::move(scopeStr), std::move(keyStr), [](const String& responseSeq, JSON::Any payload, oro::runtime::QueuedResponse) {
    SecureStorageRequest request;
    if (!oapi_secure_storage_consume_request(responseSeq, request)) {
      return;
    }

    const auto payloadStr = payload.str();
    oapi_dispatch_json_payload(request.context, payloadStr, request.callback, request.data);
    oapi_context_release(request.context);
  });

  return true;
}

bool oapi_secure_storage_clear (
  oapi_context_t* context,
  const char* scope,
  oapi_secure_storage_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr) {
    return false;
  }

  if (!context->isAllowed("secure_storage")) {
    oapi_secure_storage_dispatch_error(context, callback, data, "clear", "NotAllowedError", "Secure storage access denied");
    return false;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Secure storage operations require an IPC router context.");
    return false;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->secureStorage.enabled) {
    oapi_secure_storage_dispatch_error(context, callback, data, "clear", "NotAllowedError", "Secure storage service unavailable");
    return false;
  }

  String scopeStr = scope != nullptr ? String(scope) : String();

  const auto seq = oapi_secure_storage_next_seq();
  if (!oapi_secure_storage_track_request(seq, context, callback, data)) {
    oapi_secure_storage_dispatch_error(context, callback, data, "clear", "InternalError", "Failed to register secure storage request");
    return false;
  }

  services->secureStorage.clear(seq, std::move(scopeStr), [](const String& responseSeq, JSON::Any payload, oro::runtime::QueuedResponse) {
    SecureStorageRequest request;
    if (!oapi_secure_storage_consume_request(responseSeq, request)) {
      return;
    }

    const auto payloadStr = payload.str();
    oapi_dispatch_json_payload(request.context, payloadStr, request.callback, request.data);
    oapi_context_release(request.context);
  });

  return true;
}

bool oapi_secure_storage_keys (
  oapi_context_t* context,
  const char* scope,
  oapi_secure_storage_callback callback,
  void* data
) {
  if (context == nullptr || callback == nullptr) {
    return false;
  }

  if (!context->isAllowed("secure_storage")) {
    oapi_secure_storage_dispatch_error(context, callback, data, "keys", "NotAllowedError", "Secure storage access denied");
    return false;
  }

  if (context->router == nullptr) {
    oapi_debug(context, "Secure storage operations require an IPC router context.");
    return false;
  }

  auto* services = oapi_get_services(context);
  if (services == nullptr || !services->secureStorage.enabled) {
    oapi_secure_storage_dispatch_error(context, callback, data, "keys", "NotAllowedError", "Secure storage service unavailable");
    return false;
  }

  String scopeStr = scope != nullptr ? String(scope) : String();

  const auto seq = oapi_secure_storage_next_seq();
  if (!oapi_secure_storage_track_request(seq, context, callback, data)) {
    oapi_secure_storage_dispatch_error(context, callback, data, "keys", "InternalError", "Failed to register secure storage request");
    return false;
  }

  services->secureStorage.keys(seq, std::move(scopeStr), [](const String& responseSeq, JSON::Any payload, oro::runtime::QueuedResponse) {
    SecureStorageRequest request;
    if (!oapi_secure_storage_consume_request(responseSeq, request)) {
      return;
    }

    const auto payloadStr = payload.str();
    oapi_dispatch_json_payload(request.context, payloadStr, request.callback, request.data);
    oapi_context_release(request.context);
  });

  return true;
}

const char* oapi_runtime_getcwd (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("runtime_getcwd")) {
    oapi_debug(context, "'runtime_getcwd' is not allowed.");
    return nullptr;
  }

  return oapi_copy_string(context, oro::runtime::getcwd());
}

void oapi_runtime_sleep (uint64_t milliseconds) {
  if (milliseconds == 0) {
    return;
  }

  oro::runtime::msleep(milliseconds);
}

const oapi_runtime_platform_info_t* oapi_runtime_platform () {
  static oapi_runtime_platform_info_t info = {
    .arch = nullptr,
    .os = nullptr,
    .mac = false,
    .ios = false,
    .win = false,
    .android = false,
    .linux = false,
    .unix = false
  };

  info.arch = oro::runtime::platform.arch.c_str();
  info.os = oro::runtime::platform.os.c_str();
  info.mac = oro::runtime::platform.mac;
  info.ios = oro::runtime::platform.ios;
  info.win = oro::runtime::platform.win;
  info.android = oro::runtime::platform.android;
  info.linux = oro::runtime::platform.linux;
  info.unix = oro::runtime::platform.unix;
  return &info;
}

const char* oapi_os_constants_json (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("os_constants")) {
    oapi_debug(context, "'os_constants' is not allowed.");
    return nullptr;
  }

  JSON::Object object;
  const auto& constants = oro::runtime::os::constants();
  for (const auto& entry : constants) {
    object.set(entry.first, JSON::Number(entry.second));
  }

  return oapi_copy_string(context, object.str());
}

const char* oapi_base64_encode (
  oapi_context_t* context,
  const unsigned char* bytes,
  size_t size
) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("base64_encode")) {
    oapi_debug(context, "'base64_encode' is not allowed.");
    return nullptr;
  }

  if (size > 0 && bytes == nullptr) {
    return nullptr;
  }

  const String input(
    reinterpret_cast<const char*>(bytes != nullptr ? bytes : reinterpret_cast<const unsigned char*>("")),
    size
  );

  const auto encoded = oro::runtime::bytes::base64::encode(input);
  return oapi_copy_string(context, encoded);
}

const unsigned char* oapi_base64_decode (
  oapi_context_t* context,
  const char* string,
  size_t* size
) {
  if (context == nullptr || string == nullptr) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  if (!context->isAllowed("base64_decode")) {
    oapi_debug(context, "'base64_decode' is not allowed.");
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  const auto decoded = oro::runtime::bytes::base64::decode(String(string));
  const auto length = decoded.size();

  auto buffer = context->memory.alloc<unsigned char>(length + 1);
  if (buffer == nullptr) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  if (length > 0) {
    std::memcpy(buffer, decoded.data(), length);
  }
  buffer[length] = 0;

  if (size != nullptr) {
    *size = length;
  }

  return buffer;
}

const char* oapi_hex_encode (
  oapi_context_t* context,
  const unsigned char* bytes,
  size_t size
) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("hex_encode")) {
    oapi_debug(context, "'hex_encode' is not allowed.");
    return nullptr;
  }

  if (size > 0 && bytes == nullptr) {
    return nullptr;
  }

  const String input(
    reinterpret_cast<const char*>(bytes != nullptr ? bytes : reinterpret_cast<const unsigned char*>("")),
    size
  );

  const auto encoded = oro::runtime::bytes::encodeHexString(input);
  return oapi_copy_string(context, encoded);
}

const unsigned char* oapi_hex_decode (
  oapi_context_t* context,
  const char* string,
  size_t* size
) {
  if (context == nullptr || string == nullptr) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  if (!context->isAllowed("hex_decode")) {
    oapi_debug(context, "'hex_decode' is not allowed.");
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  const auto decoded = oro::runtime::bytes::decodeHexString(String(string));
  const auto length = decoded.size();

  auto buffer = context->memory.alloc<unsigned char>(length + 1);
  if (buffer == nullptr) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  if (length > 0) {
    std::memcpy(buffer, decoded.data(), length);
  }
  buffer[length] = 0;

  if (size != nullptr) {
    *size = length;
  }

  return buffer;
}

bool oapi_resource_is_file (
  oapi_context_t* context,
  const char* path
) {
  if (!oapi_resource_check(context, path, "resource_is_file")) {
    return false;
  }

  return Resource::isFile(String(path));
}

bool oapi_resource_is_directory (
  oapi_context_t* context,
  const char* path
) {
  if (!oapi_resource_check(context, path, "resource_is_directory")) {
    return false;
  }

  return Resource::isDirectory(String(path));
}

bool oapi_resource_is_mounted_path (
  oapi_context_t* context,
  const char* path
) {
  if (!oapi_resource_check(context, path, "resource_is_mounted_path")) {
    return false;
  }

  return Resource::isMountedPath(String(path));
}

const char* oapi_resource_get_resources_path (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("resource_get_resources_path")) {
    oapi_debug(context, "'resource_get_resources_path' is not allowed.");
    return nullptr;
  }

  const auto resourcesPath = Resource::getResourcesPath().string();
  return oapi_copy_string(context, resourcesPath);
}

const char* oapi_resource_mounted_paths_json (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("resource_mounted_paths")) {
    oapi_debug(context, "'resource_mounted_paths' is not allowed.");
    return nullptr;
  }

  const auto mounted = Resource::getMountedPaths();
  JSON::Object object;
  for (const auto& entry : mounted) {
    object.set(entry.first, JSON::String(entry.second));
  }

  return oapi_copy_string(context, object.str());
}

static bool oapi_resource_check (
  oapi_context_t* context,
  const char* path,
  const char* policy
) {
  if (context == nullptr || path == nullptr) {
    return false;
  }

  if (!context->isAllowed(policy)) {
    auto message = String("'") + policy + "' is not allowed.";
    oapi_debug(context, message.c_str());
    return false;
  }

  return true;
}

bool oapi_resource_exists (
  oapi_context_t* context,
  const char* path
) {
  if (!oapi_resource_check(context, path, "resource_exists")) {
    return false;
  }

  Resource resource{String(path)};
  return resource.exists();
}

size_t oapi_resource_size (
  oapi_context_t* context,
  const char* path
) {
  if (!oapi_resource_check(context, path, "resource_size")) {
    return 0;
  }

  Resource resource{String(path)};
  if (!resource.exists()) {
    return 0;
  }

  return resource.size();
}

const unsigned char* oapi_resource_read (
  oapi_context_t* context,
  const char* path,
  bool cached,
  size_t* size
) {
  if (!oapi_resource_check(context, path, "resource_read")) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  Resource resource{String(path)};
  if (!resource.exists()) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  const auto length = resource.size(cached);
  const auto data = resource.read(cached);

  if (length > 0 && data == nullptr) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  auto buffer = context->memory.alloc<unsigned char>(length + 1);
  if (buffer == nullptr) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  if (length > 0 && data != nullptr) {
    std::memcpy(buffer, data, length);
  }

  buffer[length] = 0;

  if (size != nullptr) {
    *size = length;
  }

  return buffer;
}

const char* oapi_resource_read_string (
  oapi_context_t* context,
  const char* path,
  bool cached
) {
  if (!oapi_resource_check(context, path, "resource_read_string")) {
    return nullptr;
  }

  Resource resource{String(path)};
  if (!resource.exists()) {
    return nullptr;
  }

  const auto contents = resource.str(cached);
  auto buffer = context->memory.alloc<char>(contents.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  std::memcpy(buffer, contents.c_str(), contents.size());
  buffer[contents.size()] = '\0';
  return buffer;
}

const char* oapi_resource_resolve (
  oapi_context_t* context,
  const char* path
) {
  if (!oapi_resource_check(context, path, "resource_resolve")) {
    return nullptr;
  }

  const auto resolvedPath = Resource::resolve(String(path));
  const auto resolved = resolvedPath.string();
  if (resolved.empty()) {
    return nullptr;
  }

  auto buffer = context->memory.alloc<char>(resolved.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  std::memcpy(buffer, resolved.c_str(), resolved.size());
  buffer[resolved.size()] = '\0';
  return buffer;
}

const char* oapi_resource_well_known_paths (oapi_context_t* context) {
  if (context == nullptr) {
    return nullptr;
  }

  if (!context->isAllowed("resource_well_known_paths")) {
    oapi_debug(context, "'resource_well_known_paths' is not allowed.");
    return nullptr;
  }

  const auto json = Resource::getWellKnownPaths().json().str();
  auto buffer = context->memory.alloc<char>(json.size() + 1);
  if (buffer == nullptr) {
    return nullptr;
  }

  std::memcpy(buffer, json.c_str(), json.size());
  buffer[json.size()] = '\0';
  return buffer;
}

void oapi_log (const oapi_context_t* ctx, const char* message) {
  if (message == nullptr) return;

  oro::runtime::String output;

  if (ctx && ctx->extension && ctx->extension->name.size() > 0) {
    auto extension = ctx->extension;
    output = "[" + extension->name + "] " + message;
  } else {
    output = message;
  }

  #if ORO_RUNTIME_PLATFORM_ANDROID
    __android_log_print(ANDROID_LOG_INFO, "Console", "%s", message);
  #else
    oro::runtime::io::write(output, false);
  #endif

  #if ORO_RUNTIME_PLATFORM_APPLE
    static auto userConfig = oro::runtime::config::getUserConfig();
    static auto bundleIdentifier = userConfig["meta_bundle_identifier"];
    static auto ORO_RUNTIME_OS_LOG_INFO = os_log_create(
      bundleIdentifier.c_str(),
      "oro.runtime"
    );
    os_log_with_type(ORO_RUNTIME_OS_LOG_INFO, OS_LOG_TYPE_INFO, "%{public}s", output.c_str());
  #endif
}

void oapi_debug (const oapi_context_t* ctx, const char* message) {
  if (message == nullptr) return;
  if (ctx && ctx->extension && ctx->extension->name.size() > 0) {
    auto extension = ctx->extension;
    debug("[%s] %s", extension->name.c_str(), message);
  } else {
    debug("%s", message);
  }
}

uint64_t oapi_rand64 () {
  return oro::runtime::crypto::rand64();
}
