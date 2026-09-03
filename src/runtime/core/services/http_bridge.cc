#include "../../runtime.hh"
#include "../../config.hh"
#include "../../string.hh"
#include "../../url.hh"
#include "../../bytes.hh"

#include "http_bridge.hh"

#include <cpp-httplib/httplib.h>
#include <future>
#include <chrono>
#include <deque>
#include <condition_variable>
#include <atomic>
#include <cstring>

using oro::runtime::config::getUserConfig;
using oro::runtime::string::trim;
using oro::runtime::string::toLowerCase;
using oro::runtime::url::encodeURIComponent;

namespace oro::runtime::core::services {
  static inline bool parseBool (const String& v, bool fallback = false) {
    if (v.size() == 0) return fallback;
    const auto s = toLowerCase(trim(v));
    return s == "1" || s == "true" || s == "yes" || s == "on";
  }

  HTTPBridge::HTTPBridge (const Options& options)
    : core::Service(options)
  {}

  HTTPBridge::~HTTPBridge () noexcept {
    this->stop();
  }

  bool HTTPBridge::start () {
    // Opt-in via config (http_ipc_enable=true)
    const auto& userConfig = static_cast<runtime::Runtime&>(this->context).userConfig;

    const bool enabled = parseBool(userConfig.contains("http_ipc_enable") ? userConfig.at("http_ipc_enable") : "false", false);
    if (!enabled) {
      return true; // disabled by config, but service considered started OK
    }

    if (userConfig.contains("http_ipc_host") && userConfig.at("http_ipc_host").size() > 0) {
      this->hostname = userConfig.at("http_ipc_host");
    }

    if (userConfig.contains("http_ipc_port") && userConfig.at("http_ipc_port").size() > 0) {
      try { this->port = std::stoi(userConfig.at("http_ipc_port")); } catch (...) { this->port = 0; }
    }

    if (userConfig.contains("http_ipc_path") && userConfig.at("http_ipc_path").size() > 0) {
      this->pathPrefix = userConfig.at("http_ipc_path");
      if (this->pathPrefix.size() == 0 || this->pathPrefix[0] != '/') {
        this->pathPrefix = String("/") + this->pathPrefix;
      }
    }

    this->allowCORS = parseBool(userConfig.contains("http_ipc_allow_cors") ? userConfig.at("http_ipc_allow_cors") : "false", false);

    if (userConfig.contains("http_ipc_shared_key")) {
      this->sharedKey = userConfig.at("http_ipc_shared_key");
    }

    if (userConfig.contains("http_ipc_timeout_ms") && userConfig.at("http_ipc_timeout_ms").size() > 0) {
      try { this->timeoutMs = std::max(0, std::stoi(userConfig.at("http_ipc_timeout_ms"))); } catch (...) {}
    }

    // Require a valid port to bind
    if (this->port <= 0) {
      return true; // treated as disabled unless a port is provided
    }

    // Choose a bridge to route through. Prefer window 0 if it exists so
    // window.* routes resolve against a real window bridge.
    auto& runtime = static_cast<runtime::Runtime&>(this->context);
    SharedPointer<bridge::Bridge> bridge = nullptr;
    if (runtime.bridgeManager.has(0)) {
      bridge = runtime.bridgeManager.get(0);
    } else {
      // Create a standalone bridge (not attached to a window). This will
      // work for routes that don't require a window.
      bridge = runtime.bridgeManager.get(0, { .userConfig = runtime.userConfig });
      if (bridge != nullptr) {
        bridge->init();
      }
    }

    if (bridge == nullptr) {
      return false;
    }

    if (this->running) {
      return true;
    }

    this->running = true;

    // Launch HTTP server thread, and confirm bind success
    auto started = std::make_shared<std::promise<bool>>();
    auto ready = started->get_future();

    this->serverThread = std::thread([this, started]() mutable {
      // signal bind result back to caller
      try {
        auto svr = std::make_shared<httplib::Server>();
        this->server = svr;

        // exception handler for robustness
        svr->set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
          (void) req;
          try {
            if (ep) std::rethrow_exception(ep);
          } catch (const std::exception&) {}
          res.status = 500;
          res.set_content("{\"err\":{\"message\":\"Internal Server Error\"}}", "application/json");
        });

        // Preflight handler if enabled is set up in serverMain later, but we need to set it here too since we are hosting the main logic here now
        if (this->allowCORS) {
          svr->set_pre_routing_handler([this](const httplib::Request &req, httplib::Response &res) {
            if (req.method == "OPTIONS") {
              res.set_header("Access-Control-Allow-Origin", "*");
              res.set_header("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
              res.set_header("Access-Control-Allow-Headers", "Content-Type, X-Requested-With, X-IPC-URI, X-ORO-Auth");
              res.status = 204;
              return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
          });
        }

        // Register handlers and bind/listen inside serverMain; here we only bind to validate port
        bool bound = svr->bind_to_port(this->hostname.c_str(), this->port);
        started->set_value(bound);
        if (!bound) {
          this->running = false;
          this->server.reset();
          return; // thread exits
        }

        // Now that bound, complete server setup and start the loop
        // Move setup into a small lambda to reuse existing logic
        // Note: we will reuse svr that is already bound; listen_after_bind will block
        // Re-register routes (since they were not set yet)
        const auto apply_cors = [this](httplib::Response &res) {
          if (this->allowCORS) res.set_header("Access-Control-Allow-Origin", "*");
        };

        auto handler = [this, apply_cors](const httplib::Request& req, httplib::Response& res) {
          // Resolve bridge per request (optional window index header)
          auto &runtime = static_cast<runtime::Runtime&>(this->context);
          SharedPointer<bridge::Bridge> bridge = nullptr;
          int requestedIndex = -1;
          if (auto it = req.headers.find("X-IPC-Window-Index"); it != req.headers.end()) {
            try { requestedIndex = std::stoi(it->second); } catch (...) { requestedIndex = -1; }
          }
          if (requestedIndex >= 0 && runtime.bridgeManager.has(requestedIndex)) {
            bridge = runtime.bridgeManager.get(requestedIndex);
          } else if (runtime.bridgeManager.has(0)) {
            bridge = runtime.bridgeManager.get(0);
          } else {
            // Fall back to a standalone bridge if no window bridge exists
            bridge = runtime.bridgeManager.get(0, { .userConfig = runtime.userConfig });
            if (bridge != nullptr) bridge->init();
          }
          if (!bridge) {
            apply_cors(res);
            res.status = 500;
            res.set_content("{\"err\":{\"message\":\"No bridge available\"}}", "application/json");
            return;
          }
          // Optional header auth
          if (!this->sharedKey.empty()) {
            auto it = req.headers.find("X-ORO-Auth");
            if (it == req.headers.end() || it->second != this->sharedKey) {
              apply_cors(res);
              res.status = 403;
              res.set_content("{\"err\":{\"message\":\"Forbidden\"}}", "application/json");
              return;
            }
          }

          // Reconstruct query from params map
          String query;
          if (!req.params.empty()) {
            bool first = true;
            for (const auto &kv : req.params) {
              if (!first) query += "&";
              query += kv.first;
              query += "=";
              query += kv.second;
              first = false;
            }
          }

          const auto& full = req.path;
          String routeName;

          // Support header-based IPC URI
          if (full == this->pathPrefix) {
            auto it = req.headers.find("X-IPC-URI");
            if (it == req.headers.end()) {
              apply_cors(res);
              res.status = 404;
              res.set_content("{\"err\":{\"message\":\"Not Found\"}}", "application/json");
              return;
            }

            const String uri = it->second;
            ipc::Message message(uri, true);
            message.isHTTP = true;
            if (!message.has("seq")) message.seq = "0";

            const auto size = req.body.size();
            SharedPointer<unsigned char[]> bytes = nullptr;
            if (size > 0) {
              bytes = std::make_shared<unsigned char[]>(size);
              memcpy(bytes.get(), req.body.data(), size);
            }

            // Block until result
            std::promise<ipc::Result> p; auto f = p.get_future();
            const bool invoked = bridge->router.invoke(message, bytes, size, [&p](ipc::Result result) mutable {
              p.set_value(result);
            });

            if (!invoked) {
              apply_cors(res);
              res.status = 404;
              res.set_content("{\"err\":{\"message\":\"Not Found\"}}", "application/json");
              return;
            }

            // Wait up to timeout for first result
            const auto wait_status = f.wait_for(std::chrono::milliseconds(this->timeoutMs));
            if (wait_status != std::future_status::ready) {
              apply_cors(res);
              res.status = 504;
              res.set_content("{\"err\":{\"message\":\"Gateway Timeout\"}}", "application/json");
              return;
            }
            const auto result = f.get();
            apply_cors(res);

            // Streaming (SSE)
            if (result.queuedResponse.eventStreamCallback != nullptr) {
              // Prepare SSE response
              res.set_header("content-type", "text/event-stream; charset=utf-8");
              res.set_header("cache-control", "no-store");
              // Apply any extra headers
              for (const auto &h : result.headers.entries) {
                if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
              }
              for (const auto &h : result.queuedResponse.headers.entries) {
                if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
              }

              auto queue = std::make_shared<std::deque<std::string>>();
              auto mutex = std::make_shared<std::mutex>();
              auto cv = std::make_shared<std::condition_variable>();
              auto finished = std::make_shared<std::atomic<bool>>(false);
              auto open = std::make_shared<std::atomic<bool>>(true);

              // Adapter for router → queue
              *result.queuedResponse.eventStreamCallback = [queue, mutex, cv, finished, open](
                const char* name,
                const unsigned char* data,
                bool is_last
              ) mutable -> bool {
                if (!open->load()) return false;
                std::string s;
                if (name && *name) {
                  s += "event: "; s += name; s += "\n";
                }
                if (data) {
                  s += "data: "; s += reinterpret_cast<const char*>(data); s += "\n";
                }
                s += "\n";
                {
                  std::lock_guard<std::mutex> lk(*mutex);
                  queue->push_back(std::move(s));
                }
                cv->notify_one();
                if (is_last) {
                  finished->store(true);
                  cv->notify_one();
                }
                return open->load();
              };

              res.set_chunked_content_provider("text/event-stream; charset=utf-8",
                [queue, mutex, cv, finished, open](size_t /*offset*/, httplib::DataSink &sink) mutable -> bool {
                  std::unique_lock<std::mutex> lk(*mutex);
                  cv->wait(lk, [&]() { return !queue->empty() || finished->load() == true; });
                  while (!queue->empty()) {
                    auto s = std::move(queue->front());
                    queue->pop_front();
                    lk.unlock();
                    if (!sink.write(s.data(), s.size())) { open->store(false); return false; }
                    lk.lock();
                  }
                  if (finished->load()) {
                    sink.done();
                    open->store(false);
                    return true;
                  }
                  return true;
                });
              if (result.queuedResponse.streamStartCallback != nullptr) {
                result.queuedResponse.streamStartCallback();
              }
              return; // handler returns; streaming continues
            }

            // Streaming (chunked binary)
            if (result.queuedResponse.chunkStreamCallback != nullptr) {
              // Apply any extra headers; default content-type if not set
              bool has_ct = false;
              for (const auto &h : result.headers.entries) {
                if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
                if (toLowerCase(h.name) == "content-type") has_ct = true;
              }
              for (const auto &h : result.queuedResponse.headers.entries) {
                if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
                if (toLowerCase(h.name) == "content-type") has_ct = true;
              }
              if (!has_ct) res.set_header("content-type", "application/octet-stream");

              auto queue = std::make_shared<std::deque<std::string>>();
              auto mutex = std::make_shared<std::mutex>();
              auto cv = std::make_shared<std::condition_variable>();
              auto finished = std::make_shared<std::atomic<bool>>(false);
              auto open = std::make_shared<std::atomic<bool>>(true);

              *result.queuedResponse.chunkStreamCallback = [queue, mutex, cv, finished, open](
                const unsigned char* chunk,
                size_t size,
                bool is_last
              ) mutable -> bool {
                if (!open->load()) return false;
                if (chunk && size > 0) {
                  std::string s(reinterpret_cast<const char*>(chunk), reinterpret_cast<const char*>(chunk) + size);
                  {
                    std::lock_guard<std::mutex> lk(*mutex);
                    queue->push_back(std::move(s));
                  }
                  cv->notify_one();
                }
                if (is_last) {
                  finished->store(true);
                  cv->notify_one();
                }
                return open->load();
              };

              res.set_chunked_content_provider(res.get_header_value("content-type"),
                [queue, mutex, cv, finished, open](size_t /*offset*/, httplib::DataSink &sink) mutable -> bool {
                  std::unique_lock<std::mutex> lk(*mutex);
                  cv->wait(lk, [&]() { return !queue->empty() || finished->load() == true; });
                  while (!queue->empty()) {
                    auto s = std::move(queue->front());
                    queue->pop_front();
                    lk.unlock();
                    if (!sink.write(s.data(), s.size())) { open->store(false); return false; }
                    lk.lock();
                  }
                  if (finished->load()) {
                    sink.done();
                    open->store(false);
                    return true;
                  }
                  return true;
                });
              if (result.queuedResponse.streamStartCallback != nullptr) {
                result.queuedResponse.streamStartCallback();
              }
              return;
            }

            // Non-streaming
            for (const auto &h : result.headers.entries) {
              res.set_header(h.name.c_str(), h.value.c_str());
            }
            for (const auto &h : result.queuedResponse.headers.entries) {
              if (!res.has_header(h.name.c_str())) {
                res.set_header(h.name.c_str(), h.value.c_str());
              }
            }
            if (result.queuedResponse.body != nullptr) {
              res.set_content(reinterpret_cast<const char*>(result.queuedResponse.body.get()), result.queuedResponse.length, "application/octet-stream");
            } else {
              const auto body = result.json().str();
              res.set_content(body, "application/json");
            }
            return;
          }

          // Path-based mapping
          const String base = this->pathPrefix.back() == '/' ? this->pathPrefix.substr(0, this->pathPrefix.size() - 1) : this->pathPrefix;
          if (full.size() > base.size() + 1 && full.rfind(base + "/", 0) == 0) {
            routeName = full.substr(base.size() + 1);
          } else {
            apply_cors(res);
            res.status = 404;
            res.set_content("{\"err\":{\"message\":\"Not Found\"}}", "application/json");
            return;
          }

          String uri = String("ipc://") + routeName;
          if (query.size() > 0) {
            uri += "?" + query + "&seq=0";
          } else {
            uri += "?seq=0";
          }

          ipc::Message message(uri, true);
          message.isHTTP = true;

          const auto size = req.body.size();
          SharedPointer<unsigned char[]> bytes = nullptr;
          if (size > 0) {
            bytes = std::make_shared<unsigned char[]>(size);
            memcpy(bytes.get(), req.body.data(), size);
          }

          std::promise<ipc::Result> p; auto f = p.get_future();
          const bool invoked = bridge->router.invoke(message, bytes, size, [&p](ipc::Result result) mutable {
            p.set_value(result);
          });

          if (!invoked) {
            apply_cors(res);
            res.status = 404;
            res.set_content("{\"err\":{\"message\":\"Not Found\"}}", "application/json");
            return;
          }

          const auto wait_status = f.wait_for(std::chrono::milliseconds(this->timeoutMs));
          if (wait_status != std::future_status::ready) {
            apply_cors(res);
            res.status = 504;
            res.set_content("{\"err\":{\"message\":\"Gateway Timeout\"}}", "application/json");
            return;
          }
          const auto result = f.get();
          apply_cors(res);

          // Streaming (SSE)
          if (result.queuedResponse.eventStreamCallback != nullptr) {
            res.set_header("content-type", "text/event-stream; charset=utf-8");
            res.set_header("cache-control", "no-store");
            for (const auto &h : result.headers.entries) {
              if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
            }
            for (const auto &h : result.queuedResponse.headers.entries) {
              if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
            }

            auto queue = std::make_shared<std::deque<std::string>>();
            auto mutex = std::make_shared<std::mutex>();
            auto cv = std::make_shared<std::condition_variable>();
            auto finished = std::make_shared<std::atomic<bool>>(false);
            auto open = std::make_shared<std::atomic<bool>>(true);

            *result.queuedResponse.eventStreamCallback = [queue, mutex, cv, finished, open](
              const char* name,
              const unsigned char* data,
              bool is_last
            ) mutable -> bool {
              if (!open->load()) return false;
              std::string s;
              if (name && *name) { s += "event: "; s += name; s += "\n"; }
              if (data) { s += "data: "; s += reinterpret_cast<const char*>(data); s += "\n"; }
              s += "\n";
              { std::lock_guard<std::mutex> lk(*mutex); queue->push_back(std::move(s)); }
              cv->notify_one();
              if (is_last) { finished->store(true); cv->notify_one(); }
              return open->load();
            };

            res.set_chunked_content_provider("text/event-stream; charset=utf-8",
              [queue, mutex, cv, finished, open](size_t /*offset*/, httplib::DataSink &sink) mutable -> bool {
                std::unique_lock<std::mutex> lk(*mutex);
                cv->wait(lk, [&]() { return !queue->empty() || finished->load() == true; });
                while (!queue->empty()) {
                  auto s = std::move(queue->front()); queue->pop_front();
                  lk.unlock();
                  if (!sink.write(s.data(), s.size())) { open->store(false); return false; }
                  lk.lock();
                }
                if (finished->load()) {
                  sink.done(); open->store(false); return true;
                }
                return true;
              });
            if (result.queuedResponse.streamStartCallback != nullptr) {
              result.queuedResponse.streamStartCallback();
            }
            return;
          }

          // Streaming (chunked)
          if (result.queuedResponse.chunkStreamCallback != nullptr) {
            bool has_ct = false;
            for (const auto &h : result.headers.entries) {
              if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
              if (toLowerCase(h.name) == "content-type") has_ct = true;
            }
            for (const auto &h : result.queuedResponse.headers.entries) {
              if (!res.has_header(h.name.c_str())) res.set_header(h.name.c_str(), h.value.c_str());
              if (toLowerCase(h.name) == "content-type") has_ct = true;
            }
            if (!has_ct) res.set_header("content-type", "application/octet-stream");

            auto queue = std::make_shared<std::deque<std::string>>();
            auto mutex = std::make_shared<std::mutex>();
            auto cv = std::make_shared<std::condition_variable>();
            auto finished = std::make_shared<std::atomic<bool>>(false);
            auto open = std::make_shared<std::atomic<bool>>(true);

            *result.queuedResponse.chunkStreamCallback = [queue, mutex, cv, finished, open](
              const unsigned char* chunk,
              size_t size,
              bool is_last
            ) mutable -> bool {
              if (!open->load()) return false;
              if (chunk && size > 0) {
                std::string s(reinterpret_cast<const char*>(chunk), reinterpret_cast<const char*>(chunk) + size);
                { std::lock_guard<std::mutex> lk(*mutex); queue->push_back(std::move(s)); }
                cv->notify_one();
              }
              if (is_last) { finished->store(true); cv->notify_one(); }
              return open->load();
            };

            res.set_chunked_content_provider(res.get_header_value("content-type"),
              [queue, mutex, cv, finished, open](size_t /*offset*/, httplib::DataSink &sink) mutable -> bool {
                std::unique_lock<std::mutex> lk(*mutex);
                cv->wait(lk, [&]() { return !queue->empty() || finished->load() == true; });
                while (!queue->empty()) {
                  auto s = std::move(queue->front()); queue->pop_front();
                  lk.unlock();
                  if (!sink.write(s.data(), s.size())) { open->store(false); return false; }
                  lk.lock();
                }
                if (finished->load()) { sink.done(); open->store(false); return true; }
                return true;
              });
            if (result.queuedResponse.streamStartCallback != nullptr) {
              result.queuedResponse.streamStartCallback();
            }
            return;
          }

          // Non-streaming
          for (const auto &h : result.headers.entries) { res.set_header(h.name.c_str(), h.value.c_str()); }
          for (const auto &h : result.queuedResponse.headers.entries) {
            if (!res.has_header(h.name.c_str())) { res.set_header(h.name.c_str(), h.value.c_str()); }
          }
          if (result.queuedResponse.body != nullptr) {
            res.set_content(reinterpret_cast<const char*>(result.queuedResponse.body.get()), result.queuedResponse.length, "application/octet-stream");
          } else {
            const auto body = result.json().str(); res.set_content(body, "application/json");
          }
        };

        const String pattern = this->pathPrefix + "/.*";
        svr->Get(pattern.c_str(), handler);
        svr->Post(pattern.c_str(), handler);
        svr->Put(pattern.c_str(), handler);
        svr->Delete(pattern.c_str(), handler);
        svr->Get(this->pathPrefix.c_str(), handler);
        svr->Post(this->pathPrefix.c_str(), handler);
        svr->Put(this->pathPrefix.c_str(), handler);
        svr->Delete(this->pathPrefix.c_str(), handler);

        // Run the server loop (blocking)
        svr->listen_after_bind();
        this->server.reset();
        this->running = false;
      } catch (...) {
        try { started->set_value(false); } catch (...) {}
        this->running = false;
        this->server.reset();
      }
    });

    // Wait for bind result so start() reports accurately
    const bool ok = ready.get();
    if (!ok) {
      if (this->serverThread.joinable()) this->serverThread.join();
      return false;
    }

    return true;
  }

  bool HTTPBridge::stop () {
    if (!this->running) {
      return true;
    }

    // Signal server to stop and join thread
    if (this->server) {
      this->server->stop();
    }

    if (this->serverThread.joinable()) {
      this->serverThread.join();
    }

    this->server.reset();
    this->running = false;
    return true;
  }

  void HTTPBridge::serverMain (SharedPointer<bridge::Bridge> /*bridge*/) {}
}
