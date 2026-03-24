#include "../services.hh"
#include "ai.hh"
#include "../../config.hh"
#include "../../runtime.hh"
#include "../../bytes.hh"
#include "../../string.hh"

#include <cpp-httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <limits>

using oro::runtime::String;
using oro::runtime::Map;
using nlohmann::json;
using oro::runtime::string::trim;
using oro::runtime::string::toLowerCase;
using oro::runtime::string::split;
namespace JSON = oro::runtime::JSON;
namespace ann = oro::runtime::ann;

namespace {
  using LlamaServer = oro::runtime::ai::server::LlamaServer;

  static String normalizePrefix(const String& raw) {
    if (raw.size() == 0) return String("/ai/llama");
    String prefix = raw;
    if (prefix.front() != '/') prefix.insert(prefix.begin(), '/');
    while (prefix.size() > 1 && prefix.back() == '/') {
      prefix.pop_back();
    }
    return prefix.size() > 0 ? prefix : String("/ai/llama");
  }

  static bool parseBoolValue(const String& raw, bool fallback = false) {
    if (raw.size() == 0) return fallback;
    const auto s = toLowerCase(trim(raw));
    if (s == "1" || s == "true" || s == "on" || s == "yes") return true;
    if (s == "0" || s == "false" || s == "off" || s == "no") return false;
    return fallback;
  }

  static bool parseBoolOption(const Map<String, String>& uc, const String& key, bool fallback = false) {
    if (!uc.contains(key) || uc.at(key).empty()) return fallback;
    return parseBoolValue(uc.at(key), fallback);
  }

  static void parseIntOption(const Map<String, String>& uc, const String& key, int& target, int minValue, int maxValue) {
    if (!uc.contains(key) || uc.at(key).empty()) return;
    try {
      int value = std::stoi(uc.at(key));
      if (value < minValue) value = minValue;
      if (value > maxValue) value = maxValue;
      target = value;
    } catch (...) {}
  }

  static void parseSizeOption(const Map<String, String>& uc, const String& key, size_t& target, size_t minValue) {
    if (!uc.contains(key) || uc.at(key).empty()) return;
    try {
      size_t value = (size_t) std::stoull(uc.at(key));
      if (value < minValue) value = minValue;
      target = value;
    } catch (...) {}
  }

  static void parseFloatOption(const Map<String, String>& uc, const String& key, float& target) {
    if (!uc.contains(key) || uc.at(key).empty()) return;
    try {
      target = (float) std::stof(uc.at(key));
    } catch (...) {}
  }

  static LlamaServer::Options makeLlamaServerOptions() {
    auto uc = oro::runtime::config::getUserConfig();
    LlamaServer::Options opts;

    if (uc.contains("ai_llm_server_prefix") && uc.at("ai_llm_server_prefix").size() > 0) {
      opts.routePrefix = normalizePrefix(uc.at("ai_llm_server_prefix"));
    }

    if (uc.contains("ai_llm_default_model") && uc.at("ai_llm_default_model").size() > 0) {
      opts.defaultModelName = uc.at("ai_llm_default_model");
    }

    parseIntOption(uc, "ai_llm_rate_limit_concurrency", opts.maxConcurrent, 1, 64);
    parseIntOption(uc, "ai_llm_rate_limit_rps", opts.rateRPS, 0, std::numeric_limits<int>::max());
    parseIntOption(uc, "ai_llm_rate_limit_burst", opts.rateBurst, 0, std::numeric_limits<int>::max());
    parseIntOption(uc, "ai_llm_default_max_tokens", opts.defaultMaxTokens, 1, std::numeric_limits<int>::max());
    parseIntOption(uc, "ai_llm_hard_max_tokens", opts.hardMaxTokens, 1, std::numeric_limits<int>::max());

    parseSizeOption(uc, "ai_llm_max_prompt_bytes", opts.maxPromptBytes, 1024);
    parseSizeOption(uc, "ai_llm_embed_max_total_bytes", opts.embedMaxTotalBytes, 1024);

    parseFloatOption(uc, "ai_llm_default_temperature", opts.defaultTemperature);
    parseFloatOption(uc, "ai_llm_default_top_p", opts.defaultTopP);
    parseIntOption(uc, "ai_llm_default_top_k", opts.defaultTopK, 1, std::numeric_limits<int>::max());
    parseFloatOption(uc, "ai_llm_default_min_p", opts.defaultMinP);
    parseFloatOption(uc, "ai_llm_default_repeat_penalty", opts.defaultRepeatPenalty);
    parseIntOption(uc, "ai_llm_default_repeat_last_n", opts.defaultRepeatLastN, 0, std::numeric_limits<int>::max());
    parseFloatOption(uc, "ai_llm_default_frequency_penalty", opts.defaultFrequencyPenalty);
    parseFloatOption(uc, "ai_llm_default_presence_penalty", opts.defaultPresencePenalty);

    return opts;
  }

  static JSON::Object::Entries makeAnnError(const String& message) {
    return JSON::Object::Entries {{
      "err", JSON::Object::Entries {
        {"message", message}
      }
    }};
  }

  static JSON::Object::Entries serializeAnnReport(const ann::TrainingReport& report) {
    return JSON::Object::Entries {
      {"loss", report.loss},
      {"accuracy", report.accuracy},
      {"epochs", static_cast<int64_t>(report.epochs)},
      {"durationMs", report.durationMs}
    };
  }

  static JSON::Object::Entries serializeAnnInference(const ann::InferenceResult& result) {
    JSON::Array logits;
    for (size_t row = 0; row < result.rows; ++row) {
      JSON::Array values;
      for (size_t col = 0; col < result.cols; ++col) {
        const auto index = (row * result.cols) + col;
        const auto value = index < result.logits.size() ? result.logits[index] : 0.0f;
        values.push(value);
      }
      logits.push(values);
    }

    JSON::Array classes;
    for (const auto cls : result.classes) {
      classes.push(static_cast<int64_t>(cls));
    }

    return JSON::Object::Entries {
      {"rows", static_cast<int64_t>(result.rows)},
      {"cols", static_cast<int64_t>(result.cols)},
      {"logits", logits},
      {"classes", classes}
    };
  }
}


namespace oro::runtime::core::services {
  static JSON::Object::Entries serializeServerStats (
    const AI::ServerStats& stats
  ) {
    auto average = [](
      const std::atomic<long long>& sum,
      const std::atomic<long long>& count
    ) -> double {
      const auto n = count.load(std::memory_order_relaxed);
      if (n <= 0) {
        return 0.0;
      }

      return (double) sum.load(std::memory_order_relaxed) / (double) n;
    };

    const auto aggregate = JSON::Object::Entries {
      {"inflight", (int64_t) stats.inflight.load(std::memory_order_relaxed)},
      {"rateLimited", (int64_t) stats.rateLimited.load(std::memory_order_relaxed)},
      {"errors", (int64_t) stats.errors.load(std::memory_order_relaxed)},
      {"tooLarge", (int64_t) stats.tooLarge.load(std::memory_order_relaxed)},
      {"timeouts", (int64_t) stats.timeouts.load(std::memory_order_relaxed)},
      {"chat", JSON::Object::Entries {
        {"stream", (int64_t) stats.reqChatStream.load(std::memory_order_relaxed)},
        {"nonStream", (int64_t) stats.reqChatNonStream.load(std::memory_order_relaxed)},
        {"avgLatencyMs", average(stats.latChatStreamSum, stats.latChatStreamCount)},
        {"avgNonStreamLatencyMs", average(stats.latChatNonStreamSum, stats.latChatNonStreamCount)}
      }},
      {"completions", JSON::Object::Entries {
        {"stream", (int64_t) stats.reqTextStream.load(std::memory_order_relaxed)},
        {"nonStream", (int64_t) stats.reqTextNonStream.load(std::memory_order_relaxed)},
        {"avgLatencyMs", average(stats.latTextStreamSum, stats.latTextStreamCount)},
        {"avgNonStreamLatencyMs", average(stats.latTextNonStreamSum, stats.latTextNonStreamCount)}
      }},
      {"embeddings", JSON::Object::Entries {
        {"count", (int64_t) stats.reqEmbeddings.load(std::memory_order_relaxed)},
        {"avgLatencyMs", average(stats.latEmbeddingsSum, stats.latEmbeddingsCount)}
      }}
    };

    return aggregate;
  }

  AI::AI (const Options& options)
    : core::Service(options),
      serverOptions(makeLlamaServerOptions()),
      server(serverOptions),
      llm(options),
      speech(options),
      ann(options),
      chat(options) {
    this->server.setHealthMetricsProvider([this](const serviceworker::Request& req) -> JSON::Any {
      const auto prefix = this->serverOptions.routePrefix.size() > 0
        ? this->serverOptions.routePrefix
        : String("/ai/llama");
      const auto origin = req.url.origin;

      SharedPointer<AI::ServerStats> aggregate;
      JSON::Array windows;

      {
        Lock lock(this->serverStatsMutex);
        const auto aggregateKey = origin + String("|") + prefix;
        if (this->serverStats.contains(aggregateKey)) {
          aggregate = this->serverStats.at(aggregateKey);
        }

        const auto windowPrefix = aggregateKey + String("|");
        for (const auto& entry : this->serverWindowStats) {
          if (!entry.first.starts_with(windowPrefix)) {
            continue;
          }

          const auto clientId = entry.first.substr(windowPrefix.size());
          windows.push(JSON::Object::Entries {
            {"clientId", clientId},
            {"metrics", serializeServerStats(*entry.second)}
          });
        }
      }

      auto payload = aggregate
        ? serializeServerStats(*aggregate)
        : JSON::Object::Entries {
            {"inflight", 0},
            {"rateLimited", 0},
            {"errors", 0},
            {"tooLarge", 0},
            {"timeouts", 0},
            {"chat", JSON::Object::Entries {
              {"stream", 0},
              {"nonStream", 0},
              {"avgLatencyMs", 0.0},
              {"avgNonStreamLatencyMs", 0.0}
            }},
            {"completions", JSON::Object::Entries {
              {"stream", 0},
              {"nonStream", 0},
              {"avgLatencyMs", 0.0},
              {"avgNonStreamLatencyMs", 0.0}
            }},
            {"embeddings", JSON::Object::Entries {
              {"count", 0},
              {"avgLatencyMs", 0.0}
            }}
          };

      payload["aggregate"] = JSON::Object(payload);
      payload["windows"] = windows;
      return JSON::Object(payload);
    });

    if (options.enabled) {
      // Autoload and prewarm default model if configured
      auto uc = oro::runtime::config::getUserConfig();
      if (uc.contains("ai_llm_default_model") && uc.at("ai_llm_default_model").size() > 0) {
        ai::llm::Model::Options mopt;
        mopt.name = uc.at("ai_llm_default_model");
        // Skip autoload if model already present
        bool present = false;
        {
          Lock lock(this->llm.manager.mutex);
          present = this->llm.manager.models.contains(mopt.name);
        }
        auto model = present ? this->llm.manager.models[mopt.name] : this->llm.manager.loadModel(mopt);
        if (model != nullptr) {
          // Per-model prewarm list first
          const auto listKey = oro::runtime::config::key({"ai_llm_model", model->name, "pool_prewarm_list"});
          if (uc.contains(listKey) && uc.at(listKey).size() > 0) {
            const auto s = uc.at(listKey);
            String cur; size_t size = 0, count = 0; bool parsingCount = false;
            auto flush = [&]() {
              if (size > 0 && count > 0) this->llm.manager.prewarm(model, size, count);
              size = 0; count = 0; parsingCount = false; cur.clear();
            };
            for (char c : s) {
              if (c == ':' && !parsingCount) {
                try { size = (size_t) std::stoull(cur); } catch (...) { size = 0; }
                cur.clear(); parsingCount = true;
              } else if (c == ',') {
                try { count = (size_t) std::stoull(cur); } catch (...) { count = 0; }
                flush();
              } else if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                cur.push_back(c);
              }
            }
            if (!cur.empty()) {
              try {
                if (parsingCount) {
                  count = (size_t) std::stoull(cur);
                } else {
                  size = (size_t) std::stoull(cur);
                }
              } catch (...) {}
            }
            flush();
          } else {
            size_t prewarmCount = 0;
            size_t prewarmSize = 2048;
            try {
              const auto key = oro::runtime::config::key({"ai_llm_model", model->name, "pool_prewarm"});
              if (uc.contains(key) && uc.at(key).size() > 0) prewarmCount = (size_t) std::stoull(uc.at(key));
            } catch (...) {}
            try {
              const auto key = oro::runtime::config::key({"ai_llm_model", model->name, "pool_prewarm_size"});
              if (uc.contains(key) && uc.at(key).size() > 0) prewarmSize = (size_t) std::stoull(uc.at(key));
            } catch (...) {}
            if (prewarmCount == 0) {
              try {
                if (uc.contains("ai_llm_pool_prewarm") && uc.at("ai_llm_pool_prewarm").size() > 0) {
                  prewarmCount = (size_t) std::stoull(uc.at("ai_llm_pool_prewarm"));
                }
              } catch (...) {}
            }
            if (prewarmSize == 0) prewarmSize = 2048;
            if (prewarmCount > 0) this->llm.manager.prewarm(model, prewarmSize, prewarmCount);
          }
        }
      }
    }
  }

  bool AI::start () {
    if (!core::Service::start()) {
      return false;
    }
    if (!this->startHttpServer()) {
      return false;
    }
    return true;
  }

  bool AI::stop () {
    this->stopHttpServer();
    return core::Service::stop();
  }

  bool AI::startHttpServer () {
    auto& runtime = static_cast<runtime::Runtime&>(this->context);
    const auto& userConfig = runtime.userConfig;

    const bool enabled = parseBoolOption(userConfig, "ai_llm_http_enable", false);
    if (!enabled) {
      return true;
    }

    if (userConfig.contains("ai_llm_http_host") && userConfig.at("ai_llm_http_host").size() > 0) {
      this->httpHost = userConfig.at("ai_llm_http_host");
    } else {
      this->httpHost = "127.0.0.1";
    }

    if (userConfig.contains("ai_llm_http_port") && userConfig.at("ai_llm_http_port").size() > 0) {
      try {
        this->httpPort = std::stoi(userConfig.at("ai_llm_http_port"));
      } catch (...) {
        this->httpPort = 0;
      }
    } else {
      this->httpPort = 0;
    }

    if (this->httpPort <= 0) {
      return true;
    }

    this->httpAllowCORS = parseBoolOption(userConfig, "ai_llm_http_allow_cors", false);
    this->httpSharedKey = userConfig.contains("ai_llm_http_shared_key")
      ? userConfig.at("ai_llm_http_shared_key")
      : String("");

    if (userConfig.contains("ai_llm_http_path") && userConfig.at("ai_llm_http_path").size() > 0) {
      this->httpRoutePrefix = normalizePrefix(userConfig.at("ai_llm_http_path"));
    } else {
      this->httpRoutePrefix = normalizePrefix(this->serverOptions.routePrefix);
    }

    this->httpUseTLS = parseBoolOption(userConfig, "ai_llm_http_tls", false);

    if (this->httpServerRunning.load(std::memory_order_relaxed)) {
      return true;
    }

    this->httpServerRunning.store(true, std::memory_order_relaxed);

    auto started = std::make_shared<std::promise<bool>>();
    auto ready = started->get_future();

    this->httpServerThread = std::thread([this, started]() mutable {
      try {
        auto server = std::make_shared<httplib::Server>();
        this->httpServer = server;

        server->set_exception_handler([](const auto&, auto& res, std::exception_ptr) {
          res.status = 500;
          res.set_content("{\"error\":{\"type\":\"internal_error\",\"message\":\"Internal Server Error\"}}", "application/json");
        });

        if (this->httpAllowCORS) {
          server->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
            if (req.method == "OPTIONS") {
              res.set_header("Access-Control-Allow-Origin", "*");
              res.set_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
              res.set_header("Access-Control-Allow-Headers", "Content-Type, X-Requested-With, X-ORO-Auth");
              res.status = 204;
              return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
          });
        }

        const bool bound = server->bind_to_port(this->httpHost.c_str(), this->httpPort);
        started->set_value(bound);
        if (!bound) {
          this->httpServerRunning.store(false, std::memory_order_relaxed);
          this->httpServer.reset();
          return;
        }

        auto handler = [this](const httplib::Request& req, httplib::Response& res) {
          if (!this->handleHttpRequest(req, res)) {
            if (this->httpAllowCORS) {
              res.set_header("Access-Control-Allow-Origin", "*");
            }
            if (!res.has_header("content-type")) {
              res.set_header("content-type", "application/json; charset=utf-8");
            }
            if (!res.has_header("runtime-preload-injection")) {
              res.set_header("runtime-preload-injection", "disabled");
            }
            if (res.body.empty()) {
              res.set_content("{\"error\":{\"type\":\"not_found_error\",\"message\":\"Not Found\"}}", "application/json");
            }
            if (res.status == 0) {
              res.status = 404;
            }
          }
        };

        const String pattern = this->httpRoutePrefix + "/.*";
        server->Get(pattern.c_str(), handler);
        server->Post(pattern.c_str(), handler);
        server->Get(this->httpRoutePrefix.c_str(), handler);
        server->Post(this->httpRoutePrefix.c_str(), handler);

        server->listen_after_bind();
        this->httpServer.reset();
        this->httpServerRunning.store(false, std::memory_order_relaxed);
      } catch (...) {
        try {
          started->set_value(false);
        } catch (...) {}
        this->httpServerRunning.store(false, std::memory_order_relaxed);
        this->httpServer.reset();
      }
    });

    const bool ok = ready.get();
    if (!ok) {
      if (this->httpServerThread.joinable()) {
        this->httpServerThread.join();
      }
      return false;
    }

    return true;
  }

  void AI::stopHttpServer () {
    if (!this->httpServerRunning.load(std::memory_order_relaxed)) {
      if (this->httpServerThread.joinable()) {
        this->httpServerThread.join();
      }
      return;
    }

    if (this->httpServer) {
      this->httpServer->stop();
    }

    if (this->httpServerThread.joinable()) {
      this->httpServerThread.join();
    }

    this->httpServer.reset();
    this->httpServerRunning.store(false, std::memory_order_relaxed);
  }

  bool AI::handleHttpRequest (const httplib::Request& req, httplib::Response& res) {
    const String prefix = this->httpRoutePrefix.size() > 0
      ? this->httpRoutePrefix
      : normalizePrefix(this->serverOptions.routePrefix);

    const String path = String(req.path);

    if (prefix.size() == 0) {
      return false;
    }

    if (path.rfind(prefix, 0) != 0) {
      return false;
    }

    if (path.size() > prefix.size()) {
      const char next = path[prefix.size()];
      if (next != '/') {
        return false;
      }
    }

    auto applyCors = [this, &res]() {
      if (this->httpAllowCORS) {
        res.set_header("Access-Control-Allow-Origin", "*");
      }
    };

    if (!this->enabled.load(std::memory_order_relaxed)) {
      applyCors();
      res.status = 503;
      res.set_header("content-type", "application/json; charset=utf-8");
      res.set_header("runtime-preload-injection", "disabled");
      res.set_content("{\"error\":{\"type\":\"service_unavailable\",\"message\":\"AI service disabled\"}}", "application/json");
      return true;
    }

    if (!this->httpSharedKey.empty()) {
      std::string authHeader = req.get_header_value("X-ORO-Auth");
      if (authHeader.empty()) {
        authHeader = req.get_header_value("x-ss-auth");
      }
      if (authHeader != this->httpSharedKey) {
        applyCors();
        res.status = 403;
        res.set_header("content-type", "application/json; charset=utf-8");
        res.set_header("runtime-preload-injection", "disabled");
        res.set_content("{\"error\":{\"type\":\"auth_error\",\"message\":\"Forbidden\"}}", "application/json");
        return true;
      }
    }

    serviceworker::Request aiReq;
    aiReq.method = req.method;
    aiReq.scheme = this->httpUseTLS ? "https" : "http";
    aiReq.url.scheme = aiReq.scheme;
    aiReq.url.pathname = path;
    aiReq.url.search = "";
    aiReq.url.query = "";

    String query;
    const std::string target = req.target;
    const auto qpos = target.find('?');
    if (qpos != std::string::npos && qpos + 1 < target.size()) {
      query = String(target.substr(qpos + 1));
    } else if (!req.params.empty()) {
      bool first = true;
      for (const auto& kv : req.params) {
        if (!first) query += "&";
        query += String(kv.first);
        query += "=";
        query += String(kv.second);
        first = false;
      }
    }
    if (query.size() > 0) {
      aiReq.url.search = String("?") + query;
      aiReq.url.query = query;
      aiReq.url.searchParams.set(query);
    }

    std::string hostHeader = req.get_header_value("Host");
    if (hostHeader.empty()) {
      hostHeader = req.get_header_value("host");
    }
    if (!hostHeader.empty()) {
      const auto colon = hostHeader.find(':');
      if (colon != std::string::npos) {
        aiReq.url.hostname = hostHeader.substr(0, colon);
        aiReq.url.port = hostHeader.substr(colon + 1);
      } else {
        aiReq.url.hostname = hostHeader;
        if (this->httpPort > 0) {
          aiReq.url.port = std::to_string(this->httpPort);
        }
      }
    } else {
      aiReq.url.hostname = this->httpHost;
      if (this->httpPort > 0) {
        aiReq.url.port = std::to_string(this->httpPort);
      }
    }
    aiReq.url.origin = aiReq.url.scheme + String("://") + aiReq.url.hostname;
    if (aiReq.url.port.size() > 0) {
      aiReq.url.origin += String(":") + aiReq.url.port;
    }

    for (const auto& header : req.headers) {
      aiReq.headers.set(header.first, header.second);
    }

    if (!req.body.empty()) {
      aiReq.body = bytes::Buffer::from(String(req.body));
    } else {
      aiReq.body = bytes::Buffer::empty();
    }

    const auto clientId = this->httpClientCounter.fetch_add(1, std::memory_order_relaxed);
    aiReq.client.id = clientId;

    const auto acceptRaw = aiReq.headers.has("accept")
      ? toLowerCase(trim(aiReq.headers.get("accept").value.str()))
      : String("");
    const auto contentTypeRaw = aiReq.headers.has("content-type")
      ? toLowerCase(trim(aiReq.headers.get("content-type").value.str()))
      : String("");

    bool wantsStream = false;
    const auto streamParam = toLowerCase(aiReq.url.searchParams.get("stream").str());
    if (streamParam == "true" || streamParam == "1" || streamParam == "yes") {
      wantsStream = true;
    }
    if (!wantsStream && acceptRaw.find("text/event-stream") != String::npos) {
      wantsStream = true;
    }

    json bodyJson;
    bool bodyParsed = false;
    constexpr size_t kMaxBodyInspectBytes = 1024 * 1024;
    if (!wantsStream && aiReq.body.size() > 0 && aiReq.body.size() <= kMaxBodyInspectBytes) {
      if (contentTypeRaw.find("json") != String::npos) {
        try {
          bodyJson = json::parse(aiReq.body.str());
          bodyParsed = true;
        } catch (...) {
          bodyParsed = false;
        }
      }
    }
    if (!wantsStream && bodyParsed && bodyJson.contains("stream")) {
      try {
        if (bodyJson["stream"].is_boolean()) {
          wantsStream = bodyJson["stream"].get<bool>();
        } else if (bodyJson["stream"].is_string()) {
          const auto v = toLowerCase(bodyJson["stream"].get<std::string>());
          wantsStream = (v == "1" || v == "true" || v == "yes");
        }
      } catch (...) {}
    }

    String suffix = path.size() > prefix.size()
      ? path.substr(prefix.size())
      : String("");
    if (suffix.size() == 0) {
      suffix = String("/");
    }
    if (suffix.front() != '/') {
      suffix.insert(suffix.begin(), '/');
    }

    auto stats = this->getServerStats(aiReq.url.origin, prefix);
    auto windowStats = this->getServerStatsForWindow(aiReq.url.origin, prefix, clientId);

    bool inflightIncremented = false;
    auto releaseInflight = [&]() {
      if (inflightIncremented) {
        stats->inflight.fetch_sub(1, std::memory_order_acq_rel);
        windowStats->inflight.fetch_sub(1, std::memory_order_acq_rel);
        inflightIncremented = false;
      }
    };

    const int maxConcurrent = std::max(1, this->serverOptions.maxConcurrent);
    const int inflightNow = stats->inflight.fetch_add(1, std::memory_order_acq_rel) + 1;
    windowStats->inflight.fetch_add(1, std::memory_order_acq_rel);
    inflightIncremented = true;

    auto finishError = [&](int status, const char* type, const char* message) {
      applyCors();
      res.status = status;
      res.set_header("content-type", "application/json; charset=utf-8");
      res.set_header("runtime-preload-injection", "disabled");
      res.set_content(String("{\"error\":{\"type\":\"") + type + "\",\"message\":\"" + message + "\"}}", "application/json");
    };

    if (inflightNow > maxConcurrent) {
      stats->errors.fetch_add(1, std::memory_order_relaxed);
      windowStats->errors.fetch_add(1, std::memory_order_relaxed);
      releaseInflight();
      finishError(429, "rate_limit_error", "Server is busy");
      return true;
    }

    if (!stats->tryConsumeToken(this->serverOptions.rateRPS, this->serverOptions.rateBurst)) {
      stats->rateLimited.fetch_add(1, std::memory_order_relaxed);
      windowStats->rateLimited.fetch_add(1, std::memory_order_relaxed);
      releaseInflight();
      finishError(429, "rate_limit_error", "Rate limit exceeded");
      return true;
    }

    const bool isChatEndpoint = (
      suffix == "/v1/chat/completions" ||
      suffix == "/chat/completions" ||
      suffix == "/api/chat"
    );
    const bool isCompletionEndpoint = (
      suffix == "/v1/completions" ||
      suffix == "/completions" ||
      suffix == "/completion"
    );
    const bool isEmbeddingsEndpoint = (
      suffix == "/v1/embeddings" ||
      suffix == "/embeddings" ||
      suffix == "/embedding"
    );

    const bool supportsStream = isChatEndpoint || isCompletionEndpoint;
    const bool doStream = wantsStream && supportsStream;

    using AIServerStats = oro::runtime::core::services::AI::ServerStats;

    auto bumpCounter = [&](std::atomic<uint64_t> AIServerStats::*member) {
      (stats.get()->*member).fetch_add(1, std::memory_order_relaxed);
      (windowStats.get()->*member).fetch_add(1, std::memory_order_relaxed);
    };

    auto bumpLatency = [&](std::atomic<long long> AIServerStats::*sum,
                           std::atomic<long long> AIServerStats::*count,
                           long long value) {
      (stats.get()->*sum).fetch_add(value, std::memory_order_relaxed);
      (stats.get()->*count).fetch_add(1, std::memory_order_relaxed);
      (windowStats.get()->*sum).fetch_add(value, std::memory_order_relaxed);
      (windowStats.get()->*count).fetch_add(1, std::memory_order_relaxed);
    };

    auto markError = [&]() {
      stats->errors.fetch_add(1, std::memory_order_relaxed);
      windowStats->errors.fetch_add(1, std::memory_order_relaxed);
    };

    const auto start = std::chrono::steady_clock::now();

    if (doStream && isChatEndpoint) {
      applyCors();
      res.status = 200;
      res.set_header("content-type", "text/event-stream; charset=utf-8");
      res.set_header("cache-control", "no-cache");
      res.set_header("connection", "keep-alive");
      res.set_header("transfer-encoding", "chunked");
      res.set_header("runtime-preload-injection", "disabled");

      auto queue = std::make_shared<std::deque<std::string>>();
      auto mutex = std::make_shared<std::mutex>();
      auto cv = std::make_shared<std::condition_variable>();
      auto finished = std::make_shared<std::atomic<bool>>(false);
      auto open = std::make_shared<std::atomic<bool>>(true);

      auto emit = [queue, mutex, cv, finished, open](const String& event, const String& data, bool done) mutable -> bool {
        if (!open->load()) return false;
        String chunk;
        if (event.size() > 0 && event != "message") {
          chunk += "event: " + event + "\n";
        }
        auto lines = split(data, "\n");
        if (lines.empty()) {
          chunk += "data:\n";
        } else {
          for (const auto& line : lines) {
            chunk += "data: " + line + "\n";
          }
        }
        chunk += "\n";
        {
          std::lock_guard<std::mutex> lock(*mutex);
          queue->push_back(chunk);
        }
        cv->notify_one();
        if (done) {
          finished->store(true);
          cv->notify_one();
        }
        return open->load();
      };

      const bool ok = this->server.streamChatCompletionsV1(aiReq, this->llm.manager, emit);
      releaseInflight();

      const auto end = std::chrono::steady_clock::now();
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      bumpCounter(&AIServerStats::reqChatStream);
      bumpLatency(&AIServerStats::latChatStreamSum, &AIServerStats::latChatStreamCount, duration);

      if (!ok) {
        markError();
      }

      res.set_chunked_content_provider("text/event-stream; charset=utf-8",
        [queue, mutex, cv, finished, open](size_t, httplib::DataSink& sink) mutable -> bool {
          std::unique_lock<std::mutex> lk(*mutex);
          cv->wait(lk, [&]() { return !queue->empty() || finished->load(); });
          while (!queue->empty()) {
            auto chunk = std::move(queue->front());
            queue->pop_front();
            lk.unlock();
            if (!sink.write(chunk.data(), chunk.size())) {
              open->store(false);
              return false;
            }
            lk.lock();
          }
          if (finished->load()) {
            sink.done();
            open->store(false);
            return true;
          }
          return true;
        });
      return true;
    }

    if (doStream && isCompletionEndpoint) {
      applyCors();
      res.status = 200;
      res.set_header("content-type", "text/event-stream; charset=utf-8");
      res.set_header("cache-control", "no-cache");
      res.set_header("connection", "keep-alive");
      res.set_header("transfer-encoding", "chunked");
      res.set_header("runtime-preload-injection", "disabled");

      auto queue = std::make_shared<std::deque<std::string>>();
      auto mutex = std::make_shared<std::mutex>();
      auto cv = std::make_shared<std::condition_variable>();
      auto finished = std::make_shared<std::atomic<bool>>(false);
      auto open = std::make_shared<std::atomic<bool>>(true);

      auto emit = [queue, mutex, cv, finished, open](const String& event, const String& data, bool done) mutable -> bool {
        if (!open->load()) return false;
        String chunk;
        if (event.size() > 0 && event != "message") {
          chunk += "event: " + event + "\n";
        }
        auto lines = split(data, "\n");
        if (lines.empty()) {
          chunk += "data:\n";
        } else {
          for (const auto& line : lines) {
            chunk += "data: " + line + "\n";
          }
        }
        chunk += "\n";
        {
          std::lock_guard<std::mutex> lock(*mutex);
          queue->push_back(chunk);
        }
        cv->notify_one();
        if (done) {
          finished->store(true);
          cv->notify_one();
        }
        return open->load();
      };

      const bool ok = this->server.streamCompletionsV1(aiReq, this->llm.manager, emit);
      releaseInflight();

      const auto end = std::chrono::steady_clock::now();
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      bumpCounter(&AIServerStats::reqTextStream);
      bumpLatency(&AIServerStats::latTextStreamSum, &AIServerStats::latTextStreamCount, duration);

      if (!ok) {
        markError();
      }

      res.set_chunked_content_provider("text/event-stream; charset=utf-8",
        [queue, mutex, cv, finished, open](size_t, httplib::DataSink& sink) mutable -> bool {
          std::unique_lock<std::mutex> lk(*mutex);
          cv->wait(lk, [&]() { return !queue->empty() || finished->load(); });
          while (!queue->empty()) {
            auto chunk = std::move(queue->front());
            queue->pop_front();
            lk.unlock();
            if (!sink.write(chunk.data(), chunk.size())) {
              open->store(false);
              return false;
            }
            lk.lock();
          }
          if (finished->load()) {
            sink.done();
            open->store(false);
            return true;
          }
          return true;
        });
      return true;
    }

    int statusCode = 200;
    http::Headers headersOut;
    bytes::Buffer bodyOut;

    const bool handled = this->server.handle(aiReq, statusCode, headersOut, bodyOut, this->llm.manager);

    releaseInflight();

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (!handled) {
      markError();
      finishError(404, "not_found_error", "Not Found");
      return true;
    }

    if (!headersOut.has("runtime-preload-injection")) {
      headersOut.set("runtime-preload-injection", "disabled");
    }

    applyCors();
    res.status = statusCode;

    String contentType = headersOut.has("content-type")
      ? headersOut.get("content-type").value.str()
      : String("application/json; charset=utf-8");

    for (const auto& header : headersOut.entries) {
      const auto lower = toLowerCase(header.name);
      if (lower == "content-type") {
        continue;
      }
      res.set_header(header.name.c_str(), header.value.str().c_str());
    }

    const String payload = bodyOut.size() > 0 ? bodyOut.str() : String("");
    res.set_content(payload, contentType);

    if (isChatEndpoint) {
      bumpCounter(&AIServerStats::reqChatNonStream);
      bumpLatency(&AIServerStats::latChatNonStreamSum, &AIServerStats::latChatNonStreamCount, duration);
    } else if (isCompletionEndpoint) {
      bumpCounter(&AIServerStats::reqTextNonStream);
      bumpLatency(&AIServerStats::latTextNonStreamSum, &AIServerStats::latTextNonStreamCount, duration);
    } else if (isEmbeddingsEndpoint) {
      bumpCounter(&AIServerStats::reqEmbeddings);
      bumpLatency(&AIServerStats::latEmbeddingsSum, &AIServerStats::latEmbeddingsCount, duration);
    }

    if (statusCode >= 400) {
      if (statusCode == 413) {
        stats->tooLarge.fetch_add(1, std::memory_order_relaxed);
        windowStats->tooLarge.fetch_add(1, std::memory_order_relaxed);
      } else if (statusCode == 408) {
        stats->timeouts.fetch_add(1, std::memory_order_relaxed);
        windowStats->timeouts.fetch_add(1, std::memory_order_relaxed);
      } else {
        markError();
      }
    }

    return true;
  }
  void AI::LLM::loadModel (
    const ipc::Message::Seq& seq,
    const LoadModelOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto model = this->manager.loadModel(options);
      if (model != nullptr) {
        const auto json = JSON::Object::Entries {
          {"data", model->json()}
        };
        // Optional: prewarm context pool after model load
        do {
          auto uc = oro::runtime::config::getUserConfig();
          // Per-model list: ai_llm_model_<normalizedName>_pool_prewarm_list = "2048:2,4096:1"
          const auto listKey = oro::runtime::config::key({"ai_llm_model", model->name, "pool_prewarm_list"});
          if (uc.contains(listKey) && uc.at(listKey).size() > 0) {
            const auto s = uc.at(listKey);
            String cur; size_t size = 0, count = 0; bool parsingCount = false;
            auto flush = [&]() {
              if (size > 0 && count > 0) this->manager.prewarm(model, size, count);
              size = 0; count = 0; parsingCount = false; cur.clear();
            };
            for (char c : s) {
              if (c == ':' && !parsingCount) {
                try { size = (size_t) std::stoull(cur); } catch (...) { size = 0; }
                cur.clear(); parsingCount = true;
              } else if (c == ',') {
                try { count = (size_t) std::stoull(cur); } catch (...) { count = 0; }
                flush();
              } else if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                cur.push_back(c);
              }
            }
            if (!cur.empty()) { try { (parsingCount ? count : size) = (size_t) std::stoull(cur); } catch (...) {} }
            flush();
            break;
          }
	          // Per-model single override
	          size_t prewarmCount = 0;
	          size_t prewarmSize = 2048;
	          try {
	            const auto key = oro::runtime::config::key({"ai_llm_model", model->name, "pool_prewarm"});
	            if (uc.contains(key) && uc.at(key).size() > 0) {
	              prewarmCount = (size_t) std::stoull(uc.at(key));
	            }
	          } catch (...) {}
	          try {
	            const auto key = oro::runtime::config::key({"ai_llm_model", model->name, "pool_prewarm_size"});
	            if (uc.contains(key) && uc.at(key).size() > 0) {
	              prewarmSize = (size_t) std::stoull(uc.at(key));
	            }
	          } catch (...) {}
          if (prewarmCount == 0) {
            try {
              if (uc.contains("ai_llm_pool_prewarm") && uc.at("ai_llm_pool_prewarm").size() > 0) prewarmCount = (size_t) std::stoull(uc.at("ai_llm_pool_prewarm"));
            } catch (...) {}
          }
          if (prewarmSize == 0) prewarmSize = 2048;
          if (prewarmCount > 0) this->manager.prewarm(model, prewarmSize, prewarmCount);
        } while (0);
        callback(seq, json, QueuedResponse{});
      } else {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Failed to load model"}
          }}
        };

        callback(seq, json, QueuedResponse{});
      }
    });
  }

  void AI::LLM::listModels (
    const ipc::Message::Seq& seq,
    const Callback& callback
  ) {
    JSON::Array models;
    Lock lock(this->manager.mutex);
    for (const auto& entry : this->manager.models) {
      models.push(entry.second->json());
    }
    const auto json = JSON::Object::Entries {{"data", models}};
    callback(seq, json, QueuedResponse{});
  }

  void AI::LLM::unloadModel (
    const ipc::Message::Seq& seq,
    const String& name,
    const ai::llm::ID id,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      bool ok = false;
      if (id > 0) ok = this->manager.unloadModel(id);
      if (!ok && name.size() > 0) ok = this->manager.unloadModel(name);
      if (!ok) {
        const auto json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"message", "Model not found"}}}};
        return callback(seq, json, QueuedResponse{});
      }
      return callback(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"ok", true}}}}, QueuedResponse{});
    });
  }

  void AI::LLM::loadLoRA (
    const ipc::Message::Seq& seq,
    const LoadLoRAOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto model = this->manager.loadModel(options.model);
      if (model == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Failed to load model"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      const auto lora = this->manager.loadLoRA(model, options);
      if (lora == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Failed to load lora"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }
      const auto json = JSON::Object::Entries {{"data", lora->json()}};
      return callback(seq, json, QueuedResponse{});
    });
  }

  void AI::LLM::attachLoRa (
    const ipc::Message::Seq& seq,
    const ai::llm::ID loraId,
    const ai::llm::ID contextId,
    const ai::llm::LoRA::AttachOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto lora = this->manager.loadLoRA(loraId);
      const auto context = this->manager.getContext(contextId);

      if (lora == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Failed to load lora"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to load context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      if (!lora->attach(context, options)) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to attach loRA to loaded context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      const auto json = JSON::Object::Entries {
        {"data", lora->json()}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void AI::LLM::detachLoRa (
    const ipc::Message::Seq& seq,
    const ai::llm::ID loraId,
    const ai::llm::ID contextId,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto lora = this->manager.loadLoRA(loraId);
      const auto context = this->manager.getContext(contextId);

      if (lora == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Failed to load lora"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to load context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      if (!lora->detach(context)) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to detach loRA to loaded context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      const auto json = JSON::Object::Entries {
        {"data", lora->json()}
      };

      callback(seq, json, QueuedResponse{});
    });
  }

  void AI::LLM::unloadLoRa (
    const ipc::Message::Seq& seq,
    const ai::llm::ID loraId,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      bool ok = this->manager.unloadLora(loraId);
      if (!ok) {
        const auto json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"message", "LoRA not found"}}}};
        return callback(seq, json, QueuedResponse{});
      }
      return callback(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"ok", true}}}}, QueuedResponse{});
    });
  }

  void AI::LLM::createContext (
    const ipc::Message::Seq& seq,
    const CreateContextOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto model = this->manager.loadModel(options.model);
      if (model == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Failed to load model"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      const auto context = this->manager.createContext(model, options);
      const auto json = JSON::Object::Entries {
        {"data", context->json()}
      };
      callback(seq, json, QueuedResponse{});
    });
  }

  void AI::LLM::destroyContext (
    const ipc::Message::Seq& seq,
    const ai::llm::ID id,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto context = this->manager.getContext(id);
      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to load context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      this->manager.destroyContext(id);
      return callback(seq, JSON::Object{}, QueuedResponse{});
    });
  }

  void AI::LLM::getContextStats (
    const ipc::Message::Seq& seq,
    const ai::llm::ID id,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto context = this->manager.getContext(id);
      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to load context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      const auto json = JSON::Object::Entries {
        {"data", context->json()}
      };
      return callback(seq, json, QueuedResponse{});
    });
  }

  void AI::LLM::dumpContextState (
    const ipc::Message::Seq& seq,
    const ai::llm::ID id,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto context = this->manager.getContext(id);
      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to load context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      const auto state = context->dump();
      const auto response = QueuedResponse {
        .body = state.shared(),
        .length = state.size()
      };

      return callback(seq, JSON::Object{}, response);
    });
  }

  void AI::LLM::restoreContextState (
    const ipc::Message::Seq& seq,
    const ai::llm::ID id,
    const bytes::Buffer& state,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      const auto context = this->manager.getContext(id);
      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to load context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      if (!context->restore(state)) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Failed to restore context"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      return callback(seq, JSON::Object{}, QueuedResponse{});
    });
  }

  void AI::LLM::prewarmPool (
    const ipc::Message::Seq& seq,
    const PrewarmPoolOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      // If model is already present, don't attempt to autoload from disk
      SharedPointer<ai::llm::Model> model = nullptr;
      {
        Lock lock(this->manager.mutex);
        if (this->manager.models.contains(options.model.name)) {
          model = this->manager.models.at(options.model.name);
        }
      }
      if (model == nullptr) {
        model = this->manager.loadModel(options.model);
      }
      if (model == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {{"message", "Failed to load model"}}}
        };
        return callback(seq, json, QueuedResponse{});
      }

      size_t before = 0;
      {
        Lock lock(this->manager.mutex);
        const auto key = model->name + String("|") + std::to_string(options.size);
        if (this->manager.contextPool.contains(key)) {
          before = this->manager.contextPool.at(key).size();
        }
      }

      this->manager.prewarm(model, options.size, options.count);

      size_t after = 0;
      {
        Lock lock(this->manager.mutex);
        const auto key = model->name + String("|") + std::to_string(options.size);
        if (this->manager.contextPool.contains(key)) {
          after = this->manager.contextPool.at(key).size();
        }
      }

      const auto json = JSON::Object::Entries {{"data", JSON::Object::Entries {
        {"model", model->json()},
        {"size", (int64_t) options.size},
        {"count", (int64_t) options.count},
        {"pooled", (int64_t) after},
        {"added", (int64_t) (after > before ? (after - before) : 0)}
      }}};
      return callback(seq, json, QueuedResponse{});
    });
  }

  void AI::ANN::createModel (
    const ipc::Message::Seq& seq,
    const CreateModelOptions& options,
    const Callback& callback
  ) {
    auto opts = options;
    this->queue.push([=, this]() mutable {
      try {
        auto model = this->manager.create(opts);
        if (model == nullptr) {
          callback(seq, makeAnnError("Failed to create ANN model"), QueuedResponse{});
          return;
        }
        const auto json = JSON::Object::Entries {{
          "data", model->json()
        }};
        callback(seq, json, QueuedResponse{});
      } catch (const std::exception& error) {
        callback(seq, makeAnnError(error.what()), QueuedResponse{});
      } catch (...) {
        callback(seq, makeAnnError("Unknown ANN error"), QueuedResponse{});
      }
    });
  }

  void AI::ANN::loadModel (
    const ipc::Message::Seq& seq,
    const LoadModelOptions& options,
    const Callback& callback
  ) {
    auto path = options.path;
    auto name = options.name;
    this->queue.push([=, this]() {
      if (path.empty()) {
        callback(seq, makeAnnError("Model path is required"), QueuedResponse{});
        return;
      }
      try {
        auto model = this->manager.load(path, name);
        if (model == nullptr) {
          callback(seq, makeAnnError("Failed to load ANN model"), QueuedResponse{});
          return;
        }
        const auto json = JSON::Object::Entries {{
          "data", model->json()
        }};
        callback(seq, json, QueuedResponse{});
      } catch (const std::exception& error) {
        callback(seq, makeAnnError(error.what()), QueuedResponse{});
      } catch (...) {
        callback(seq, makeAnnError("Unknown ANN error"), QueuedResponse{});
      }
    });
  }

  void AI::ANN::removeModel (
    const ipc::Message::Seq& seq,
    const RemoveModelOptions& options,
    const Callback& callback
  ) {
    auto opts = options;
    this->queue.push([=, this]() {
      bool removed = false;
      if (opts.id != 0) {
        removed = this->manager.remove(opts.id);
      }
      if (!removed && !opts.name.empty()) {
        removed = this->manager.remove(opts.name);
      }

      if (!removed) {
        callback(seq, makeAnnError("ANN model not found"), QueuedResponse{});
        return;
      }

      const auto json = JSON::Object::Entries {{
        "data", JSON::Object::Entries {{"ok", true}}
      }};
      callback(seq, json, QueuedResponse{});
    });
  }

  void AI::ANN::saveModel (
    const ipc::Message::Seq& seq,
    const SaveModelOptions& options,
    const Callback& callback
  ) {
    auto opts = options;
    this->queue.push([=, this]() {
      if (opts.id == 0 || opts.path.empty()) {
        callback(seq, makeAnnError("Model id and path are required"), QueuedResponse{});
        return;
      }

      auto model = this->manager.get(opts.id);
      if (model == nullptr) {
        callback(seq, makeAnnError("ANN model not found"), QueuedResponse{});
        return;
      }

      try {
        if (!model->save(opts.path)) {
          callback(seq, makeAnnError("Failed to save ANN model"), QueuedResponse{});
          return;
        }
        const auto json = JSON::Object::Entries {{
          "data", JSON::Object::Entries {{"ok", true}}
        }};
        callback(seq, json, QueuedResponse{});
      } catch (const std::exception& error) {
        callback(seq, makeAnnError(error.what()), QueuedResponse{});
      } catch (...) {
        callback(seq, makeAnnError("Unknown ANN error"), QueuedResponse{});
      }
    });
  }

  void AI::ANN::listModels (
    const ipc::Message::Seq& seq,
    const Callback& callback
  ) {
    this->queue.push([=, this]() {
      JSON::Array items;
      for (const auto& item : this->manager.list()) {
        items.push(item);
      }
      const auto json = JSON::Object::Entries {{
        "data", items
      }};
      callback(seq, json, QueuedResponse{});
    });
  }

  void AI::ANN::train (
    const ipc::Message::Seq& seq,
    const TrainRequest& request,
    const Callback& callback
  ) {
    auto req = request;
    this->queue.push([=, this]() mutable {
      auto model = this->manager.get(req.id);
      if (model == nullptr) {
        callback(seq, makeAnnError("ANN model not found"), QueuedResponse{});
        return;
      }

      if (req.features.size() != req.rows * req.featureColumns ||
          req.labels.size() != req.rows * req.labelColumns) {
        callback(seq, makeAnnError("ANN training data dimensions mismatch"), QueuedResponse{});
        return;
      }

      try {
        ann::DataView features { req.features.data(), req.rows, req.featureColumns };
        ann::DataView labels { req.labels.data(), req.rows, req.labelColumns };
        const auto report = model->train(req.options, features, labels);
        const auto json = JSON::Object::Entries {{
          "data", JSON::Object::Entries {
            {"report", serializeAnnReport(report)},
            {"model", model->json()}
          }
        }};
        callback(seq, json, QueuedResponse{});
      } catch (const std::exception& error) {
        callback(seq, makeAnnError(error.what()), QueuedResponse{});
      } catch (...) {
        callback(seq, makeAnnError("Unknown ANN error"), QueuedResponse{});
      }
    });
  }

  void AI::ANN::infer (
    const ipc::Message::Seq& seq,
    const PredictRequest& request,
    const Callback& callback
  ) {
    auto req = request;
    this->queue.push([=, this]() mutable {
      auto model = this->manager.get(req.id);
      if (model == nullptr) {
        callback(seq, makeAnnError("ANN model not found"), QueuedResponse{});
        return;
      }

      if (req.features.size() != req.rows * req.featureColumns) {
        callback(seq, makeAnnError("ANN inference data dimensions mismatch"), QueuedResponse{});
        return;
      }

      try {
        ann::DataView features { req.features.data(), req.rows, req.featureColumns };
        const auto result = model->infer(features);
        const auto json = JSON::Object::Entries {{
          "data", JSON::Object::Entries {
            {"result", serializeAnnInference(result)},
            {"model", model->json()}
          }
        }};
        callback(seq, json, QueuedResponse{});
      } catch (const std::exception& error) {
        callback(seq, makeAnnError(error.what()), QueuedResponse{});
      } catch (...) {
        callback(seq, makeAnnError("Unknown ANN error"), QueuedResponse{});
      }
    });
  }

  void AI::ANN::evaluate (
    const ipc::Message::Seq& seq,
    const AccuracyRequest& request,
    const Callback& callback
  ) {
    auto req = request;
    this->queue.push([=, this]() mutable {
      auto model = this->manager.get(req.id);
      if (model == nullptr) {
        callback(seq, makeAnnError("ANN model not found"), QueuedResponse{});
        return;
      }

      if (req.features.size() != req.rows * req.featureColumns ||
          req.labels.size() != req.rows * req.labelColumns) {
        callback(seq, makeAnnError("ANN evaluation data dimensions mismatch"), QueuedResponse{});
        return;
      }

      try {
        ann::DataView features { req.features.data(), req.rows, req.featureColumns };
        ann::DataView labels { req.labels.data(), req.rows, req.labelColumns };
        const auto value = model->accuracy(features, labels);
        const auto json = JSON::Object::Entries {{
          "data", JSON::Object::Entries {
            {"accuracy", value},
            {"model", model->json()}
          }
        }};
        callback(seq, json, QueuedResponse{});
      } catch (const std::exception& error) {
        callback(seq, makeAnnError(error.what()), QueuedResponse{});
      } catch (...) {
        callback(seq, makeAnnError("Unknown ANN error"), QueuedResponse{});
      }
    });
  }

  void AI::Speech::loadModel (
    const ipc::Message::Seq& seq,
    const LoadModelOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this]() {
      SharedPointer<ai::whisper::Model> model;
      if (options.id > 0) {
        model = this->manager.getModel(options.id);
      }
      if (model == nullptr) {
        ai::whisper::Model::Options opts;
        opts.name = options.name;
        opts.directory = options.directory;
        opts.threadCount = options.threadCount;
        opts.statePoolLimit = options.statePoolLimit;
        opts.useGPU = options.useGPU;
        opts.gpuDevice = options.gpuDevice;
        model = this->manager.loadModel(opts);
      }

      if (model == nullptr) {
        const auto json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"message", "Failed to load whisper model"}}}};
        callback(seq, json, QueuedResponse{});
        return;
      }

      const auto json = JSON::Object::Entries {{"data", model->json()}};
      callback(seq, json, QueuedResponse{});
    });
  }

  void AI::Speech::unloadModel (
    const ipc::Message::Seq& seq,
    const UnloadModelOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this]() {
      bool ok = false;
      if (options.id > 0) {
        ok = this->manager.unloadModel(options.id);
      }
      if (!ok && options.name.size() > 0) {
        ok = this->manager.unloadModel(options.name);
      }

      if (!ok) {
        const auto json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"message", "Whisper model not found"}}}};
        callback(seq, json, QueuedResponse{});
        return;
      }

      callback(seq, JSON::Object::Entries {{"data", JSON::Object::Entries {{"ok", true}}}}, QueuedResponse{});
    });
  }

  void AI::Speech::listModels (
    const ipc::Message::Seq& seq,
    const Callback& callback
  ) {
    JSON::Array list;
    for (const auto& item : this->manager.listModels()) {
      list.push(item);
    }
    callback(seq, JSON::Object::Entries {{"data", list}}, QueuedResponse{});
  }

  void AI::Speech::transcribe (
    const ipc::Message::Seq& seq,
    const TranscribeRequest& request,
    const Callback& callback
  ) {
    this->queue.push([=, this]() {
      SharedPointer<ai::whisper::Model> model;
      if (request.id > 0) {
        model = this->manager.getModel(request.id);
      }
      if (model == nullptr && request.name.size() > 0) {
        model = this->manager.getModel(request.name);
      }
      if (model == nullptr) {
        const auto json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"message", "Whisper model not loaded"}}}};
        callback(seq, json, QueuedResponse{});
        return;
      }

      ai::whisper::Manager::ProgressCallback progressCallback;
      if (request.options.stream) {
        const uint64_t conduitId = request.conduitId;
        progressCallback = [=, this](const ai::whisper::Segment& segment) {
          const auto textBuffer = bytes::Buffer::from(segment.text);
          const auto payload = textBuffer.shared();
          const auto length = textBuffer.size();

          oro::runtime::core::services::Conduit::Message::Options meta {
            {"source", "ai.whisper.transcribe.stream"},
            {"index", std::to_string(segment.index)},
            {"start", std::to_string(segment.start)},
            {"end", std::to_string(segment.end)},
            {"confidence", std::to_string(segment.tokenProbability)}
          };

          if (conduitId > 0 && this->services.conduit.has(conduitId)) {
            auto client = this->services.conduit.get(conduitId);
            if (client != nullptr) {
              client->send(meta, payload, length);
              return;
            }
          }

          const auto json = JSON::Object::Entries {{
            "source", "ai.whisper.transcribe.stream"
          }, {
            "data", JSON::Object::Entries {
              {"index", segment.index},
              {"start", segment.start},
              {"end", segment.end},
              {"text", segment.text},
              {"confidence", segment.tokenProbability}
            }
          }};

          const auto queued = QueuedResponse {
            .body = payload,
            .length = length
          };

          callback("-1", json, queued);
        };
      }

      this->manager.transcribe(model, request.options, [=, this](const ai::whisper::Manager::TaskResult& result) {
        if (!result.ok) {
          const auto json = JSON::Object::Entries {{"err", JSON::Object::Entries {{"message", result.error}}}};
          callback(seq, json, QueuedResponse{});
          return;
        }

        JSON::Array segments;
        for (const auto& segment : result.result.segments) {
          segments.push(JSON::Object::Entries {
            {"index", segment.index},
            {"start", segment.start},
            {"end", segment.end},
            {"text", segment.text},
            {"confidence", segment.tokenProbability}
          });
        }

        const auto json = JSON::Object::Entries {{
          "data",
          JSON::Object::Entries {
            {"text", result.result.text},
            {"language", result.result.language},
            {"audioMs", result.result.audioMs},
            {"processingMs", result.result.processingMs},
            {"inputSampleRate", result.result.sourceSampleRate},
            {"inputSamples", result.result.inputSamples},
            {"outputSamples", result.result.outputSamples},
            {"resampled", result.result.resampled},
            {"normalized", result.result.normalized},
            {"segments", segments}
          }
        }};

        if (request.options.stream && request.conduitId > 0 && this->services.conduit.has(request.conduitId)) {
          auto client = this->services.conduit.get(request.conduitId);
          if (client != nullptr) {
            const auto buffer = bytes::Buffer::from(result.result.text);
            oro::runtime::core::services::Conduit::Message::Options meta {
              {"source", "ai.whisper.transcribe.complete"},
              {"finished", "true"},
              {"language", result.result.language}
            };
            client->send(meta, buffer.shared(), buffer.size());
          }
        }

        callback(seq, json, QueuedResponse{});
      }, progressCallback);
    });
  }

  void AI::Chat::list (
    const ipc::Message::Seq& seq,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      Lock lock(this->mutex);
      JSON::Array sessions;
      for (const auto& entry : this->sessions) {
        sessions.push(entry.second->json());
      }
      const auto json = JSON::Object::Entries {
        {"data", sessions}
      };
      return callback(seq, json, QueuedResponse{});
    });
  }

  void AI::Chat::history (
    const ipc::Message::Seq& seq,
    const ai::llm::ID id,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      if (!this->sessions.contains(id)) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Context does not exist for session"}
          }}
        };
        return callback(seq, json, QueuedResponse{});
      }

      JSON::Array history;
      const auto session = this->sessions.at(id);
      for (const auto& entry : session->messages) {
        history.push(entry.json());
      }
      const auto json = JSON::Object::Entries {
        {"data", history}
      };
      return callback(seq, json, QueuedResponse{});
    });
  }

  void AI::Chat::generate (
    const ipc::Message::Seq& seq,
    const ai::chat::ID id,
    const GenerateOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      SharedPointer<ai::chat::Session> session;
      auto context = this->services.ai.llm.manager.getContext(id);
      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Context does not exist for session"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      do {
        Lock lock(this->mutex);
        if (!this->sessions.contains(id)) {
          this->sessions.insert_or_assign(id, std::make_shared<ai::chat::Session>(context, ai::chat::Session::Options {
            id
          }));
        }

        session = this->sessions.at(id);
      } while (0);

      const auto success = session->generate(options.prompt, { .antiprompts = options.antiprompts }, [=, this](auto buffer, auto eog) {
        const auto json = JSON::Object::Entries {
          {"source", "ai.chat.session.generate"},
          {"data", JSON::Object::Entries {
            {"eog", eog},
            {"id", id}
          }}
        };

        const auto queuedResponse = QueuedResponse {
          .body = buffer.shared(),
          .length = buffer.size()
        };

        callback("-1", json, queuedResponse);
      });

      if (success) {
        return callback(seq, JSON::Object {}, QueuedResponse{});
      } else {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Generation failed"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }
    });
  }

  void AI::Chat::message (
    const ipc::Message::Seq& seq,
    const ai::llm::ID id,
    const GenerateOptions& options,
    const Callback& callback
  ) {
    this->queue.push([=, this](){
      SharedPointer<ai::chat::Session> session;
      auto context = this->services.ai.llm.manager.getContext(id);
      if (context == nullptr) {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "NotFoundError"},
            {"message", "Context does not exist for session"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }

      do {
        Lock lock(this->mutex);
        if (!this->sessions.contains(id)) {
          this->sessions.insert_or_assign(id, std::make_shared<ai::chat::Session>(context, ai::chat::Session::Options {
            id
          }));
        }
        session = this->sessions.at(id);
      } while (0);

      const auto success = session->chat(options.prompt, { .antiprompts = options.antiprompts }, [=, this](auto id, auto buffer, auto eog) {
        const auto json = JSON::Object::Entries {
          {"source", "ai.chat.session.message"},
          {"data", JSON::Object::Entries {
            {"eog", eog},
            {"id", id}
          }}
        };

        const auto queuedResponse = QueuedResponse {
          .body = buffer.shared(),
          .length = buffer.size()
        };

        callback("-1", json, queuedResponse);
      });

      if (success) {
        return callback(seq, JSON::Object {}, QueuedResponse{});
      } else {
        const auto json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"message", "Generation failed"}
          }}
        };

        return callback(seq, json, QueuedResponse{});
      }
    });
  }
}
