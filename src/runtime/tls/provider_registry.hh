#ifndef ORO_RUNTIME_TLS_PROVIDER_REGISTRY_H
#define ORO_RUNTIME_TLS_PROVIDER_REGISTRY_H

#include "../tls.hh"
#include "client.hh"
#include "server.hh"
#include "../platform/types.hh"
#include "../loop.hh"

#include <functional>
#include <vector>

namespace oro::runtime::tls {
  class ProviderRegistry {
    public:
      using ClientFactory = std::function<types::SharedPointer<Client>(loop::Loop&, const Options&)>;
      using ServerFactory = std::function<types::SharedPointer<Server>(loop::Loop&, const Server::Options&)>;

      static ProviderRegistry& instance();

      void registerStatic(Provider provider, ClientFactory clientFactory, ServerFactory serverFactory, bool dynamic = false);
      bool ensure(Provider provider);
      bool available(Provider provider) const;

      ClientFactory clientFactory(Provider provider) const;
      ServerFactory serverFactory(Provider provider) const;

    private:
      struct Entry {
        ClientFactory makeClient;
        ServerFactory makeServer;
        bool isDynamic = false;
      };

      ProviderRegistry() = default;
      ProviderRegistry(const ProviderRegistry&) = delete;
      ProviderRegistry& operator=(const ProviderRegistry&) = delete;

      bool tryLoadModule(Provider provider);

      mutable types::Mutex mutex;
      types::Map<Provider, Entry> entries;
      std::vector<void*> dynamicHandles;
  };
}

#endif
