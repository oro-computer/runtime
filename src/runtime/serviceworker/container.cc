#include "../runtime.hh"
#include "../bridge.hh"
#include "../app.hh"
#include "../config.hh"
#include "../crypto.hh"
#include "../string.hh"

#include "../serviceworker.hh"

using oro::runtime::crypto::rand64;
using oro::runtime::config::getUserConfig;
using oro::runtime::string::split;
using oro::runtime::string::join;
using oro::runtime::string::toLowerCase;
using oro::runtime::string::trim;
using oro::runtime::app::App;

namespace oro::runtime::serviceworker {
  Container::Container ()
    : protocols(*this)
  {}

  Container::~Container () {
    if (this->bridge != nullptr) {
      this->bridge->emit("serviceWorker.destroy", JSON::Object {});
    }
  }

  bool Container::ready () {
    return this->isReady.load(std::memory_order_relaxed);
  }

  void Container::reset () {
    Lock lock(this->mutex);

    for (auto& entry : this->registrations) {
      entry.second.state = Registration::State::Registered;
      this->registerServiceWorker(entry.second.options);
    }
  }

  void Container::init (SharedPointer<bridge::Bridge> bridge) {
    Lock lock(this->mutex);

    this->reset();
    this->bridge = bridge;
    this->isReady = true;

    this->bridge->router.map("serviceWorker.fetch.request.body", [this](auto message, auto router, auto reply) mutable {
      SharedPointer<Fetch> fetch = nullptr;
      ID id = 0;

      try {
        id = std::stoull(message.get("id"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'id' given in parameters"}
        }});
      }

      do {
        Lock lock(this->mutex);

        if (!this->fetches.contains(id)) {
          return reply(ipc::Result::Err { message, JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Callback 'id' given in parameters does not have a 'Request'"}
          }});
        }

        fetch = this->fetches.at(id);
      } while (0);

      const auto queuedResponse = QueuedResponse {
        0,
        0,
        fetch->request.body.buffer.as<unsigned char>(),
        fetch->request.body.size()
      };

      reply(ipc::Result { message.seq, message, JSON::Object {}, queuedResponse });
    });

    this->bridge->router.map("serviceWorker.fetch.response.write", [this](auto message, auto router, auto reply) mutable {
      SharedPointer<Fetch> fetch = nullptr;
      ID clientId = 0;
      ID id = 0;

      int statusCode = 200;

      try {
        id = std::stoull(message.get("id"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'id' given in parameters"}
        }});
      }

      try {
        clientId = std::stoull(message.get("clientId"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'clientId' given in parameters"}
        }});
      }

      do {
        Lock lock(this->mutex);
        if (!this->fetches.contains(id)) {
          return reply(ipc::Result::Err { message, JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Callback 'id' given in parameters does not have a 'Request'"}
          }});
        }

        fetch = this->fetches.at(id);
      } while (0);

      try {
        statusCode = std::stoi(message.get("statusCode"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'statusCode' given in parameters"}
        }});
      }

      do {
        Lock lock(fetch->mutex);
        fetch->response.client.id = clientId;
        fetch->response.status = statusCode;
        fetch->response.statusCode = statusCode;

        if (fetch->response.headers.size() == 0 && message.has("headers")) {
          fetch->response.headers = http::Headers(message.get("headers"));
        }

        // Determine streaming mode from headers (chunked or SSE)
        if (!fetch->streaming) {
          const auto te = toLowerCase(fetch->response.headers.get("transfer-encoding").value.str());
          const auto ct = toLowerCase(fetch->response.headers.get("content-type").value.str());
          if (te == "chunked" || ct.find("text/event-stream") != String::npos) {
            fetch->streaming = true;
          }
        }

        if (message.buffer.size() > 0) {
          if (!fetch->streaming) {
            // Buffer for non-streaming path
            fetch->write(message.buffer);
          }

          if (fetch->streaming && this->bridge != nullptr) {
            // Forward chunk to the originating window Bridge for immediate streaming
            const auto headers = fetch->response.headers;
            const auto scheme = fetch->request.scheme;
            const auto hostname = fetch->request.url.hostname;
            auto pathname = fetch->request.url.pathname;
            auto query = fetch->request.url.search;

            // Remove protocol handler scope prefix for reserved schemes like 'npm'
            if (fetch->request.scheme == "npm") {
              const auto scope = this->protocols.getServiceWorkerScope(fetch->request.scheme);
              if (scope.size() > 0) {
                const auto scoped = URL(scope, this->origin);
                if (scoped.pathname.size() > 0) {
                  const auto pos = pathname.find(scoped.pathname);
                  if (pos == 0) {
                    pathname = pathname.substr(scoped.pathname.size());
                    if (pathname.size() == 0) pathname = "/";
                  }
                }
              }
            }

            auto bytes = message.buffer.shared();
            auto size = message.buffer.size();

            // Resolve the target Bridge for the originating client
            this->bridge->dispatch([this, id, clientId, statusCode, headers, bytes, size, scheme, hostname, pathname, query]() mutable {
              auto targetBridge = this->bridge; // fallback
              do {
                auto app = App::sharedApplication();
                if (!app) break;
                oro::runtime::bridge::Client client; client.id = clientId;
                auto window = app->runtime.windowManager.getWindowForClient(client);
                if (window && window->bridge) {
                  targetBridge = window->bridge;
                }
              } while (0);

              if (!targetBridge) return;

              // Ensure correlation runs on the target Bridge thread
              targetBridge->dispatch([targetBridge, id, clientId, statusCode, headers, bytes, size, scheme, hostname, pathname, query]() mutable {
                const auto makeKey = [](uint64_t c, const String& sch, const String& host, const String& path, const String& q) {
                  return std::to_string(c) + "|" + sch + "|" + host + "|" + path + "|" + q;
                };

                auto& pendingReq = targetBridge->swPendingRequest;
                auto& pendingCb = targetBridge->swPendingCallback;
                auto& activeRes = targetBridge->swActiveResponse;
                auto& activeCb = targetBridge->swActiveCallback;

                if (!activeRes.contains(id)) {
                  auto normalizedQuery = query;
                  if (normalizedQuery.size() > 0 && normalizedQuery[0] == '?') {
                    normalizedQuery = normalizedQuery.substr(1);
                  }
                  const auto key = makeKey(clientId, scheme, hostname, pathname, normalizedQuery);
                  if (pendingReq.contains(key)) {
                    auto response = std::make_shared<webview::SchemeHandlers::Response>(pendingReq.at(key));
                    response->writeHead(statusCode, headers);
                    activeRes.insert_or_assign(id, response);
                    activeCb.insert_or_assign(id, pendingCb.at(key));
                    pendingReq.erase(key);
                    pendingCb.erase(key);
                  }
                }

                if (activeRes.contains(id)) {
                  auto res = activeRes.at(id);
                  if (res != nullptr) {
                    res->write(size, bytes);
                  }
                }
              });
            });
          }
        }
      } while (0);

      reply(ipc::Result { message.seq, message });
    });

    // Completes a fetch with a one-shot response (typically for early
    // error or not-found conditions). This avoids the need to send any
    // body chunks and mirrors the non-streaming finish path.
    this->bridge->router.map("serviceWorker.fetch.response", [this](auto message, auto router, auto reply) mutable {
      SharedPointer<Fetch> fetch = nullptr;
      ID clientId = 0;
      ID id = 0;

      int statusCode = 200;

      try {
        id = std::stoull(message.get("id"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {{"message", "Invalid 'id' given in parameters"}} });
      }

      try {
        clientId = std::stoull(message.get("clientId"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {{"message", "Invalid 'clientId' given in parameters"}} });
      }

      if (message.has("statusCode")) {
        try { statusCode = std::stoi(message.get("statusCode")); }
        catch (...) {
          return reply(ipc::Result::Err { message, JSON::Object::Entries {{"message", "Invalid 'statusCode' given in parameters"}} });
        }
      }

      do {
        Lock lock(this->mutex);
        if (!this->fetches.contains(id)) {
          return reply(ipc::Result::Err { message, JSON::Object::Entries {{"type", "NotFoundError"}, {"message", "Callback 'id' given in parameters does not have a 'Request'"}} });
        }
        fetch = this->fetches.at(id);
      } while (0);

      do {
        Lock lock(fetch->mutex);
        fetch->response.client.id = clientId;
        fetch->response.status = statusCode;
        fetch->response.statusCode = statusCode;
        if (fetch->response.headers.size() == 0 && message.has("headers")) {
          fetch->response.headers = http::Headers(message.get("headers"));
        }
        if (message.buffer.size() > 0) {
          fetch->write(message.buffer);
        }
        fetch->finish();
      } while (0);

      do {
        Lock lock(this->mutex);
        this->fetches.erase(id);
      } while (0);

      // Acknowledge the request before delivering the response back to the webview
      reply(ipc::Result { message.seq, message });

      // Deliver the actual response
      this->bridge->dispatch([=](){
        fetch->callback(fetch->response);
      });
    });

    this->bridge->router.map("serviceWorker.fetch.response.finish", [this](auto message, auto router, auto reply) mutable {
      SharedPointer<Fetch> fetch = nullptr;
      ID clientId = 0;
      ID id = 0;

      int statusCode = 200;

      try {
        id = std::stoull(message.get("id"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'id' given in parameters"}
        }});
      }

      if (message.has("statusCode")) {
        try {
          statusCode = std::stoi(message.get("statusCode"));
        } catch (...) {
          return reply(ipc::Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'statusCode' given in parameters"}
          }});
        }
      }

      try {
        clientId = std::stoull(message.get("clientId"));
      } catch (...) {
        return reply(ipc::Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'clientId' given in parameters"}
        }});
      }

      do {
        Lock lock(this->mutex);
        if (!this->fetches.contains(id)) {
          return reply(ipc::Result::Err { message, JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Callback 'id' given in parameters does not have a 'Request'"}
          }});
        }

        fetch = this->fetches.at(id);
      } while (0);

      bool wasStreaming = false;
      do {
        Lock lock(fetch->mutex);
        fetch->response.client.id = clientId;
        fetch->response.status = statusCode;
        fetch->response.statusCode = statusCode;

        if (fetch->response.headers.size() == 0 && message.has("headers")) {
          fetch->response.headers = http::Headers(message.get("headers"));
        }

        wasStreaming = fetch->streaming;
        if (!fetch->streaming && message.buffer.size() > 0) {
          fetch->write(message.buffer);
        }
      } while (0);

      if (wasStreaming && this->bridge != nullptr) {
        // Prepare context to initialize response if no chunks were written
        const auto headers = fetch->response.headers;
        const auto scheme = fetch->request.scheme;
        const auto hostname = fetch->request.url.hostname;
        auto pathname = fetch->request.url.pathname;
        auto query = fetch->request.url.search;

        // Remove protocol handler scope prefix for reserved schemes like 'npm'
        if (fetch->request.scheme == "npm") {
          const auto scope = this->protocols.getServiceWorkerScope(fetch->request.scheme);
          if (scope.size() > 0) {
            const auto scoped = URL(scope, this->origin);
            if (scoped.pathname.size() > 0) {
              const auto pos = pathname.find(scoped.pathname);
              if (pos == 0) {
                pathname = pathname.substr(scoped.pathname.size());
                if (pathname.size() == 0) pathname = "/";
              }
            }
          }
        }

        // Resolve the target Bridge for the originating client and finish on its thread
        this->bridge->dispatch([this, id, clientId, statusCode, headers, scheme, hostname, pathname, query]() mutable {
          auto targetBridge = this->bridge; // fallback
          do {
            auto app = App::sharedApplication();
            if (!app) break;
            oro::runtime::bridge::Client client; client.id = clientId;
            auto window = app->runtime.windowManager.getWindowForClient(client);
            if (window && window->bridge) {
              targetBridge = window->bridge;
            }
          } while (0);

          if (!targetBridge) return;

          targetBridge->dispatch([targetBridge, id, clientId, statusCode, headers, scheme, hostname, pathname, query]() mutable {
            auto& activeRes = targetBridge->swActiveResponse;
            auto& activeCb = targetBridge->swActiveCallback;
            const auto makeKey = [](uint64_t c, const String& sch, const String& host, const String& path, const String& q) {
              return std::to_string(c) + "|" + sch + "|" + host + "|" + path + "|" + q;
            };

            // Initialize response if not yet active (no chunks written)
            if (!activeRes.contains(id)) {
              auto normalizedQuery = query;
              if (normalizedQuery.size() > 0 && normalizedQuery[0] == '?') {
                normalizedQuery = normalizedQuery.substr(1);
              }
              const auto key = makeKey(clientId, scheme, hostname, pathname, normalizedQuery);
              auto& pendingReq = targetBridge->swPendingRequest;
              auto& pendingCb = targetBridge->swPendingCallback;
              if (pendingReq.contains(key)) {
                auto response = std::make_shared<webview::SchemeHandlers::Response>(pendingReq.at(key));
                response->writeHead(statusCode, headers);
                activeRes.insert_or_assign(id, response);
                activeCb.insert_or_assign(id, pendingCb.at(key));
                pendingReq.erase(key);
                pendingCb.erase(key);
              }
            }

            if (activeRes.contains(id)) {
              auto res = activeRes.at(id);
              webview::SchemeHandlers::HandlerCallback cb;
              if (activeCb.contains(id)) cb = activeCb.at(id);
              if (res != nullptr) {
                if (cb) cb(*res);
              }
              activeRes.erase(id);
              activeCb.erase(id);
            }
          });
        });
        // Do not invoke aggregated callback path
        do {
          Lock lock(this->mutex);
          this->fetches.erase(id);
        } while (0);

        reply(ipc::Result { message.seq, message });
        return;
      }

      fetch->finish();

      // XXX(@jwerle): we handle this in the android runtime
      const auto extname = Path(fetch->request.url.pathname).extension().string();
      auto html = (fetch->response.body.data() != nullptr && fetch->response.body.size() > 0)
        ? String(reinterpret_cast<char*>(fetch->response.body.data()), fetch->response.body.size())
        : String("");

      if (
        html.size() > 0 &&
        message.get("runtime-preload-injection") != "disabled"
      ) {
        const auto ct = toLowerCase(trim(fetch->response.headers.get("content-type").value.str()));
        const bool isHTML = ct.starts_with("text/html");
        if (isHTML) {
          auto preloadOptions = fetch->request.client.preload.options;
          preloadOptions.metadata["runtime-frame-source"] = "serviceworker";

        auto preload = webview::Preload::compile(preloadOptions);
        auto begin = String("<meta name=\"begin-runtime-preload\">");
        auto end = String("<meta name=\"end-runtime-preload\">");
        auto x = html.find(begin);
        auto y = html.find(end);

        if (x != String::npos && y != String::npos) {
          html.erase(x, (y - x) + end.size());
        }

          html = preload.insertIntoHTML(html, {
            .protocolHandlerSchemes = this->protocols.getSchemes()
          });

          fetch->response.body = bytes::Buffer::from(html);
        }
      }

      do {
        Lock lock(this->mutex);
        this->fetches.erase(id);
      } while (0);

      // Reply to the JS finish request immediately to avoid blocking
      // the service worker's respondWith() promise chain.
      reply(ipc::Result { message.seq, message });

      // Then schedule the callback on the bridge thread to deliver the
      // actual webview response.
      this->bridge->dispatch([=](){
        fetch->callback(fetch->response);
      });
    });
  }

  const Registration& Container::registerServiceWorker (
    const Registration::Options& options
  ) {
    Lock lock(this->mutex);
    auto scope = options.scope;
    auto scriptURL = options.scriptURL;
    auto userConfig = this->bridge != nullptr
      ? this->bridge->getRuntime()->userConfig
      : getUserConfig();

    if (scope.size() == 0) {
      auto url = URL(
        scriptURL,
        "oro://" + userConfig["meta_bundle_identifier"]
      );

    #if ORO_RUNTIME_PLATFORM_ANDROID
      url.scheme = "https";
    #else
      url.scheme = "oro";
    #endif

      scriptURL = url.str();

      const auto parts = split(url.pathname, "/");
      scope = parts.size() > 2
        ?  join(Vector<String>(parts.begin(), parts.end() - 1), "/")
        : "/";
    }

    scope = normalizeScope(scope);

    const auto key = Registration::key(scope, this->origin, options.scheme);
    if (this->registrations.contains(key)) {
      const auto& registration = this->registrations.at(key);

      if (this->bridge != nullptr) {
        this->bridge->emit("serviceWorker.register", registration.json(true).str());
      }

      return registration;
    }

    for (const auto& entry : this->registrations) {
      const auto& registration = entry.second;
      if (registration.options.scriptURL == scriptURL) {
        if (this->bridge != nullptr) {
          this->bridge->emit("serviceWorker.register", registration.json(true).str());
        }

        return registration;
      }
    }

    const auto id = options.id > 0 ? options.id : rand64();
    this->registrations.insert_or_assign(key, Registration(
      id,
      Registration::State::Registered,
      this->origin,
      Registration::Options {
        options.type,
        options.scriptURL,
        scope,
        options.scheme,
        options.serializedWorkerArgs,
        options.priority,
        id
      }
    ));

    const auto& registration = this->registrations.at(key);

    if (this->bridge != nullptr) {
      this->bridge->emit("serviceWorker.register", registration.json(true));
    }

    return registration;
  }

  bool Container::unregisterServiceWorker (String scopeOrScriptURL) {
    Lock lock(this->mutex);

    const auto& scope = normalizeScope(scopeOrScriptURL);
    const auto& scriptURL = scopeOrScriptURL;
    const auto key = Registration::key(scope, this->origin);

    if (this->registrations.contains(key)) {
      const auto& registration = this->registrations.at(key);
      if (this->bridge != nullptr) {
        return this->bridge->emit("serviceWorker.unregister", registration.json());
      }

      this->registrations.erase(scope);
      return true;
    }

    for (const auto& entry : this->registrations) {
      if (entry.second.options.scriptURL == scriptURL) {
        const auto& registration = entry.second;

        if (this->bridge != nullptr) {
          return this->bridge->emit("serviceWorker.unregister", registration.json().str());
        }

        this->registrations.erase(entry.first);
        return true;
      }
    }

    return false;
  }

  bool Container::unregisterServiceWorker (ID id) {
    Lock lock(this->mutex);

    for (const auto& entry : this->registrations) {
      if (entry.second.id == id) {
        const auto& registration = entry.second;

        if (this->bridge != nullptr) {
          return this->bridge->emit("serviceWorker.unregister", registration.json().str());
        }

        this->registrations.erase(entry.first);
        return true;
      }
    }

    return false;
  }

  void Container::skipWaiting (ID id) {
    Lock lock(this->mutex);

    for (auto& entry : this->registrations) {
      if (entry.second.id == id) {
        auto& registration = entry.second;
        if (
          registration.state == Registration::State::Installing ||
          registration.state == Registration::State::Installed
        ) {
          registration.state = Registration::State::Activating;

          if (this->bridge != nullptr) {
            this->bridge->emit("serviceWorker.skipWaiting", registration.json().str());
          }
        }
        break;
      }
    }
  }

  void Container::updateState (ID id, const String& stateString) {
    Lock lock(this->mutex);

    for (auto& entry : this->registrations) {
      if (entry.second.id == id) {
        auto& registration = entry.second;
        if (stateString == "error") {
          registration.state = Registration::State::Error;
        } else if (stateString == "registered") {
          registration.state = Registration::State::Registered;
        } else if (stateString == "installing") {
          registration.state = Registration::State::Installing;
        } else if (stateString == "installed") {
          registration.state = Registration::State::Installed;
        } else if (stateString == "activating") {
          registration.state = Registration::State::Activating;
        } else if (stateString == "activated") {
          registration.state = Registration::State::Activated;
        } else {
          break;
        }

        if (this->bridge != nullptr) {
          this->bridge->emit("serviceWorker.updateState", registration.json().str());
        }

        break;
      }
    }
  }

  bool Container::fetch (
    const Request& request,
    const Fetch::Options& options,
    const Fetch::Callback callback
  ) {
    Lock lock(this->mutex);
    auto fetch = std::make_shared<Fetch>(*this, request, options);
    this->fetches.insert_or_assign(fetch->id, fetch);
    return fetch->init(callback);
  }
}
