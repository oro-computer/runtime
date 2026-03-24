#include "dns.hh"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace oro::runtime::core::services {
  namespace {
    struct LookupRequestContext : core::Service::RequestContext {
      String hostname;
      bool includeAll = false;
      bool verbatim = false;
      int requestedFamily = 0;
    };

    static inline String trimAddress (String address) {
      const auto terminator = address.find('\0');
      if (terminator != String::npos) {
        address.erase(terminator);
      }
      return address;
    }

    static inline String mapErrorCode (int status) {
      const auto name = uv_err_name(status);
      if (name == nullptr) {
        return "EAI_FAIL";
      }

      String code(name);

      if (code.rfind("UV_EAI_", 0) == 0) {
        code.erase(0, 3);
        if (code == "EAI_NONAME") {
          return "ENOTFOUND";
        } else if (code == "EAI_NODATA") {
          return "ENODATA";
        }
        return code;
      }

      if (code.rfind("UV_", 0) == 0) {
        code.erase(0, 3);
      }

      return code;
    }

    static inline JSON::Object::Entries makeErrorResult (
      const String& hostname,
      int status
    ) {
      const auto code = mapErrorCode(status);
      const auto message = String("getaddrinfo ") + code + " " + hostname;

      return JSON::Object::Entries {
        {"source", "dns.lookup"},
        {"err", JSON::Object::Entries {
          {"code", code},
          {"errno", code},
          {"status", std::to_string(status)},
          {"syscall", "getaddrinfo"},
          {"hostname", hostname},
          {"message", message}
        }}
      };
    }

    static void handleLookupResult (
      uv_getaddrinfo_t* resolver,
      int status,
      struct addrinfo* res
    ) {
      const auto ctx = static_cast<LookupRequestContext*>(resolver->data);

      if (status < 0) {
        auto json = makeErrorResult(ctx->hostname, status);
        ctx->callback(ctx->seq, json, QueuedResponse{});
        if (res != nullptr) {
          uv_freeaddrinfo(res);
        }
        delete resolver;
        delete ctx;
        return;
      }

      std::vector<std::pair<String, int>> results;
      results.reserve(4);

      for (auto current = res; current != nullptr; current = current->ai_next) {
        if (current->ai_family != AF_INET && current->ai_family != AF_INET6) {
          continue;
        }

        if (current->ai_family == AF_INET) {
          char addr[17] = {'\0'};
          uv_ip4_name(reinterpret_cast<struct sockaddr_in*>(current->ai_addr), addr, 16);
          results.emplace_back(trimAddress(String(addr, 17)), 4);
        } else if (current->ai_family == AF_INET6) {
          char addr[40] = {'\0'};
          uv_ip6_name(reinterpret_cast<struct sockaddr_in6*>(current->ai_addr), addr, 39);
          results.emplace_back(trimAddress(String(addr, 40)), 6);
        }
      }

      if (res != nullptr) {
        uv_freeaddrinfo(res);
      }

      if (results.empty()) {
        auto json = makeErrorResult(ctx->hostname, UV_EAI_NONAME);
        ctx->callback(ctx->seq, json, QueuedResponse{});
        delete resolver;
        delete ctx;
        return;
      }

      if (!ctx->verbatim && ctx->requestedFamily == 0) {
        std::stable_partition(
          results.begin(),
          results.end(),
          [](const auto& entry) {
            return entry.second == 4;
          }
        );
      }

      JSON::Object::Entries json;

      if (ctx->includeAll) {
        JSON::Array::Entries entries(results.size());
        for (size_t i = 0; i < results.size(); ++i) {
          entries[i] = JSON::Object::Entries {
            {"address", results[i].first},
            {"family", results[i].second}
          };
        }

        json = JSON::Object::Entries {
          {"source", "dns.lookup"},
          {"data", JSON::Object::Entries {
            {"addresses", entries}
          }}
        };
      } else {
        const auto& first = results.front();
        json = JSON::Object::Entries {
          {"source", "dns.lookup"},
          {"data", JSON::Object::Entries {
            {"address", first.first},
            {"family", first.second}
          }}
        };
      }

      ctx->callback(ctx->seq, json, QueuedResponse{});
      delete resolver;
      delete ctx;
    }
  }

  void DNS::lookup (
    const String& seq,
    const LookupOptions& options,
    const Callback callback
  ) const {
    const auto opts = options;

    this->loop.dispatch([this, seq, callback, opts]() {
      const auto hostname = opts.hostname;
      const auto requestedFamily = opts.family;
      const auto includeAll = opts.all;
      const auto verbatim = opts.verbatim;
      const auto hintsMask = opts.hints;

      const auto ctx = new LookupRequestContext;
      ctx->seq = seq;
      ctx->callback = callback;
      ctx->hostname = hostname;
      ctx->includeAll = includeAll;
      ctx->verbatim = verbatim;
      ctx->requestedFamily = requestedFamily;
      auto loop = this->loop.get();

      struct addrinfo hints = {0};
      hints.ai_socktype = 0;
      hints.ai_protocol = 0;
      hints.ai_flags = hintsMask;

      if (requestedFamily == 6) {
        hints.ai_family = AF_INET6;
      } else if (requestedFamily == 4) {
        hints.ai_family = AF_INET;
      } else {
        hints.ai_family = AF_UNSPEC;
      }

      const auto resolver = new uv_getaddrinfo_t;
      resolver->data = ctx;

      const auto status = uv_getaddrinfo(
        loop,
        resolver,
        handleLookupResult,
        hostname.c_str(),
        nullptr,
        &hints
      );

      if (status < 0) {
        auto json = makeErrorResult(hostname, status);
        ctx->callback(seq, json, QueuedResponse{});
        delete resolver;
        delete ctx;
      }
    });
  }
}
