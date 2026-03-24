#include "../bridge.hh"
#include "../serviceworker.hh"
#include "../scheme.hh"

namespace oro::runtime::serviceworker {
  Manager::Manager (
    context::RuntimeContext& context,
    const Options& options
  ) : windowManager(options.windowManager),
      context(context)
  {}

  SharedPointer<Server> Manager::init (const String& origin, const Server::Options& options) {
    Lock lock(this->mutex);

    const auto canonicalOrigin = webview::Origin(scheme::canonicalURL(origin)).name();

    if (this->servers.contains(canonicalOrigin)) {
      return this->servers.at(canonicalOrigin);
    }

    const auto server = std::make_shared<Server>(*this, Server::Options {
      .origin = canonicalOrigin,
      .userConfig = options.userConfig,
      .window = options.window
    });

    this->servers.insert_or_assign(server->origin.name(), server);

    if (server->init()) {
      return this->servers.at(server->origin.name());
    }

    this->servers.erase(server->origin.name());

    return nullptr;
  }

  SharedPointer<Server> Manager::get (const String& origin) {
    Lock lock(this->mutex);
    const auto key = webview::Origin(scheme::canonicalURL(origin)).name();
    return this->servers.contains(key) ? this->servers.at(key) : nullptr;
  }

  SharedPointer<Server> Manager::get (const bridge::Bridge* bridge) {
    Lock lock(this->mutex);
    for (const auto& entry : this->servers) {
      if (dynamic_cast<bridge::Bridge*>(entry.second->bridge.get()) == bridge) {
        return entry.second;
      }
    }

    return nullptr;
  }

  bool Manager::destroy (const String& origin) {
    Lock lock(this->mutex);
    const auto key = webview::Origin(scheme::canonicalURL(origin)).name();
    if (this->servers.contains(key)) {
      this->servers.erase(key);
      return true;
    }

    return false;
  }

  bool Manager::fetch (
    const Request& request,
    const Fetch::Options& options,
    const Fetch::Callback callback
  ) {
    Lock lock(this->mutex);
    const auto key = webview::Origin(scheme::canonicalURL(request.url.origin)).name();
    if (this->servers.contains(key)) {
      return this->servers.at(key)->fetch(request, options, callback);
    }

    return false;
  }
}
