#include "provider_registry.hh"

#include "../env.hh"
#include "../debug.hh"
#include "../string.hh"

#if ORO_RUNTIME_PLATFORM_POSIX
  #include <dlfcn.h>
#elif ORO_RUNTIME_PLATFORM_WINDOWS
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

#include <algorithm>
#include <string>

namespace oro::runtime::tls {
  namespace {
    const char* providerName (Provider provider) {
      switch (provider) {
        case Provider::mbedTLS: return "mbedtls";
        case Provider::OpenSSL: return "openssl";
        case Provider::GnuTLS: return "gnutls";
        case Provider::SecureTransport: return "securetransport";
        case Provider::Schannel: return "schannel";
        case Provider::Android: return "android";
        case Provider::Auto: break;
      }
      return "auto";
    }

    std::string moduleFileName (Provider provider) {
      const char* base = providerName(provider);
      std::string stem = std::string("oro_tls_provider_") + base;
    #if ORO_RUNTIME_PLATFORM_LINUX
      return "lib" + stem + ".so";
    #elif ORO_RUNTIME_PLATFORM_DARWIN
      return "lib" + stem + ".dylib";
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      return stem + ".dll";
    #else
      return stem;
    #endif
    }

    using RegisterFn = void (*)();
  }

  ProviderRegistry& ProviderRegistry::instance () {
    static ProviderRegistry registry;
    return registry;
  }

  void ProviderRegistry::registerStatic (Provider provider, ClientFactory clientFactory, ServerFactory serverFactory, bool dynamic) {
    types::Lock lock(this->mutex);
    this->entries[provider] = Entry{std::move(clientFactory), std::move(serverFactory), dynamic};
  }

  bool ProviderRegistry::available (Provider provider) const {
    types::Lock lock(this->mutex);
    return this->entries.contains(provider) && static_cast<bool>(this->entries.at(provider).makeClient);
  }

  ProviderRegistry::ClientFactory ProviderRegistry::clientFactory (Provider provider) const {
    types::Lock lock(this->mutex);
    if (!this->entries.contains(provider)) return {};
    return this->entries.at(provider).makeClient;
  }

  ProviderRegistry::ServerFactory ProviderRegistry::serverFactory (Provider provider) const {
    types::Lock lock(this->mutex);
    if (!this->entries.contains(provider)) return {};
    return this->entries.at(provider).makeServer;
  }

  bool ProviderRegistry::ensure (Provider provider) {
    if (provider == Provider::Auto) return false;
    if (this->available(provider)) return true;
    return this->tryLoadModule(provider) && this->available(provider);
  }

  bool ProviderRegistry::tryLoadModule (Provider provider) {
#if ORO_RUNTIME_PLATFORM_POSIX || ORO_RUNTIME_PLATFORM_WINDOWS
    const auto customDir = env::get("ORO_TLS_PROVIDER_DIR");
    std::vector<std::string> candidates;
    if (!customDir.empty()) {
      auto dir = customDir;
      if (dir.back() != '/' && dir.back() != '\\') dir.push_back('/');
      candidates.push_back(dir + moduleFileName(provider));
    }
    candidates.push_back(moduleFileName(provider));

    for (const auto& path : candidates) {
    #if ORO_RUNTIME_PLATFORM_POSIX
      void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!handle) continue;
      auto fn = reinterpret_cast<RegisterFn>(dlsym(handle, "oro_tls_register_provider"));
      if (!fn) {
        dlclose(handle);
        continue;
      }
      fn();
      {
        types::Lock lock(this->mutex);
        this->dynamicHandles.push_back(handle);
      }
      return true;
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      const auto widePath = string::convertStringToWString(path);
      if (!path.empty() && widePath.empty()) continue;
      HMODULE mod = LoadLibraryW(widePath.c_str());
      if (!mod) continue;
      auto fn = reinterpret_cast<RegisterFn>(GetProcAddress(mod, "oro_tls_register_provider"));
      if (!fn) {
        FreeLibrary(mod);
        continue;
      }
      fn();
      {
        types::Lock lock(this->mutex);
        this->dynamicHandles.push_back(reinterpret_cast<void*>(mod));
      }
      return true;
    #endif
    }
#endif
    debug("tls provider module not found: %s", providerName(provider));
    return false;
  }
}
