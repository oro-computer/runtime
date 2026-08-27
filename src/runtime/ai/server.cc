#include "server.hh"
#include "../debug.hh"
#include <chrono>
#include <sstream>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <limits>

#include <nlohmann/json.hpp>
using nlohmann::json;

#if defined(__clang__) || defined(__GNUC__)
__attribute__((weak)) int LLAMA_BUILD_NUMBER = 0;
#elif defined(_MSC_VER)
__declspec(selectany) int LLAMA_BUILD_NUMBER = 0;
#else
int LLAMA_BUILD_NUMBER = 0;
#endif

namespace oro::runtime::ai::server {
  using oro::runtime::JSON::Object;
  using oro::runtime::JSON::Array;
  using ::LLAMA_BUILD_NUMBER;

  struct SamplingOverrides {
    float repeatPenalty;
    int repeatLastN;
    float frequencyPenalty;
    float presencePenalty;
    Vector<llama_logit_bias> logitBias;
  };

  static SamplingOverrides make_sampling_defaults(const LlamaServer::Options& options) {
    return SamplingOverrides {
      options.defaultRepeatPenalty,
      options.defaultRepeatLastN,
      options.defaultFrequencyPenalty,
      options.defaultPresencePenalty,
      {}
    };
  }

  static long long nowMsSteady() {
    using namespace std::chrono;
    return (long long) duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  static bool route_metrics_enabled() {
    static bool enabled = []() {
      bool dev = oro::runtime::config::isDebugEnabled();
      auto uc = oro::runtime::config::getUserConfig();
      const auto key = String("ai_llm_debug_route_metrics");
      bool force = false;
      if (uc.contains(key) && uc.at(key).size() > 0) {
        const auto v = uc.at(key);
        force = (v == "1" || v == "true" || v == "on" || v == "yes");
      }
      return dev || force;
    }();
    return enabled;
  }

  // Optional per-route diagnostics (counts + recent ring)
  struct RouteMetrics {
    Mutex mutex;
    Map<String, uint64_t> counts;
    Vector<std::pair<String, long long>> recent;
    Map<String, Map<String, uint64_t>> statusCounts;
    size_t cap = 64;
    uint64_t recentWindowMs = []() {
      uint64_t def = 300000; // 5 minutes
      auto uc = oro::runtime::config::getUserConfig();
      const auto key = String("ai_llm_debug_route_metrics_recent_ms");
      if (uc.contains(key) && uc.at(key).size() > 0) {
        try { return (uint64_t) std::stoull(uc.at(key)); } catch (...) {}
      }
      return def;
    }();

    void record(const String& route) {
      if (!route_metrics_enabled()) return;
      Lock lock(mutex);
      counts[route] += 1;
      const long long now = nowMsSteady();
      recent.emplace_back(route, now);
      if (recentWindowMs > 0) {
        const long long cutoff = (long long) recentWindowMs;
        size_t i = 0;
        while (i < recent.size()) {
          if (now - recent[i].second > cutoff) {
            i++;
          } else {
            break;
          }
        }
        if (i > 0) recent.erase(recent.begin(), recent.begin() + (ptrdiff_t)i);
      }
      if (recent.size() > cap) {
        recent.erase(recent.begin(), recent.begin() + (recent.size() - cap));
      }
    }

    static inline const char* bucketForStatus (int status) {
      if (status >= 200 && status < 300) return "2xx";
      if (status >= 400 && status < 500) return "4xx";
      if (status >= 500 && status < 600) return "5xx";
      return "other";
    }

    void recordStatus(const String& route, int status) {
      if (!route_metrics_enabled()) return;
      Lock lock(mutex);
      statusCounts[route][bucketForStatus(status)] += 1;
    }

    void snapshot(Map<String, uint64_t>& outCounts, Vector<String>& outRecent, Map<String, Map<String, uint64_t>>& outStatus) {
      if (!route_metrics_enabled()) return;
      Lock lock(mutex);
      outCounts = counts;
      outRecent.clear();
      outRecent.reserve(recent.size());
      for (const auto& it : recent) {
        outRecent.push_back(it.first);
      }
      outStatus = statusCounts;
    }
  };

  static RouteMetrics g_route_metrics;

  static bool send_error_response(
    int code,
    const String& type,
    const String& message,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    const String& routeTag
  ) {
    statusCode = code;
    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{
      "error", JSON::Object::Entries {{"code", (int64_t)code}, {"type", type}, {"message", message}}
    }}).str());
    if (routeTag.size() > 0) {
      g_route_metrics.recordStatus(routeTag, statusCode);
    }
    return true;
  }

  static String normalize_path(const String& p) {
    if (p.size() == 0) return String("/");
    // Ensure leading slash
    if (p[0] != '/') return String("/") + p;
    return p;
  }

  static bool parse_json_body(const serviceworker::Request& req, json& out) {
    if (req.body.size() == 0) return false;
    // Avoid parsing excessively large bodies to reduce risk/memory pressure
    static constexpr size_t kMaxParseBytes = 1024 * 1024; // 1 MiB
    if (req.body.size() > kMaxParseBytes) return false;
    try {
      out = json::parse(req.body.str());
      return true;
    } catch (...) {
      return false;
    }
  }

  static Vector<String> parse_stop_params_from_json(const json& j) {
    Vector<String> stops;
    if (j.contains("stop")) {
      if (j["stop"].is_string()) {
        stops.push_back(j["stop"].get<std::string>());
      } else if (j["stop"].is_array()) {
        for (const auto& s : j["stop"]) {
          if (s.is_string()) stops.push_back(s.get<std::string>());
        }
      }
    } else if (j.contains("stop_sequences") && j["stop_sequences"].is_array()) {
      for (const auto& s : j["stop_sequences"]) {
        if (s.is_string()) stops.push_back(s.get<std::string>());
      }
    }
    return stops;
  }

  static String render_chat_prompt_from_json(ai::llm::Model* model, const json& j_messages) {
    if (!j_messages.is_array()) return {};
    Vector<std::string> roles;
    Vector<std::string> contents;
    Vector<llama_chat_message> msgs;
    for (const auto& m : j_messages) {
      if (!m.is_object()) continue;
      const auto role    = m.contains("role")    && m["role"].is_string()    ? m["role"].get<std::string>()    : std::string();
      const auto content = m.contains("content") && m["content"].is_string() ? m["content"].get<std::string>() : std::string();
      roles.push_back(role);
      contents.push_back(content);
    }
    msgs.reserve(roles.size());
    for (size_t i = 0; i < roles.size(); ++i) {
      llama_chat_message msg { roles[i].c_str(), contents[i].c_str() };
      msgs.push_back(msg);
    }

    const char* tmpl = llama_model_chat_template(model->model, nullptr);
    if (tmpl == nullptr) {
      // fallback: join messages in order
      String joined;
      for (size_t i = 0; i < contents.size(); ++i) {
        if (!joined.empty()) joined += "\n";
        joined += contents[i];
      }
      return joined;
    }

    // first pass to get required size
    int32_t need = llama_chat_apply_template(
      tmpl,
      msgs.data(),
      msgs.size(),
      true /* add assistant prefix */,
      nullptr,
      0
    );

    if (need <= 0) return {};
    Vector<char> buf((size_t)need + 1);
    int32_t wrote = llama_chat_apply_template(
      tmpl,
      msgs.data(),
      msgs.size(),
      true,
      buf.data(),
      (int32_t)buf.size()
    );
    if (wrote <= 0) return {};
    return String(buf.data(), (size_t)wrote);
  }

  static Vector<llama_token> tokenize_string(ai::llm::Model* model, const std::string& text) {
    Vector<llama_token> tokens;
    if (!model || !model->vocab) return tokens;
    const int needed = llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), nullptr, 0, false, true);
    if (needed <= 0) return tokens;
    tokens.resize((size_t)needed);
    llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), tokens.data(), (int32_t)tokens.size(), false, true);
    return tokens;
  }

  static String token_to_piece(const llama_vocab* vocab, llama_token token);

  struct CappedTextResult {
    String text;
    size_t tokenCount = 0;
    bool truncated = false;
  };

  static CappedTextResult cap_text_to_tokens(
    ai::llm::Model* model,
    const String& text,
    size_t maxTokens
  ) {
    CappedTextResult result;
    result.text = text;

    if (!model || !model->vocab) {
      return result;
    }

    const auto tokens = tokenize_string(model, text);
    result.tokenCount = tokens.size();

    if (maxTokens == 0 || tokens.size() <= maxTokens) {
      return result;
    }

    result.truncated = true;
    result.tokenCount = maxTokens;
    result.text.clear();

    for (size_t i = 0; i < maxTokens; ++i) {
      result.text += token_to_piece(model->vocab, tokens[i]);
    }

    return result;
  }

  static bool parse_messages_json(const json& jMessages, Vector<ai::chat::Message>& out) {
    if (!jMessages.is_array()) return false;
    for (const auto& entry : jMessages) {
      if (!entry.is_object()) continue;
      if (!entry.contains("role") || !entry["role"].is_string()) continue;
      const auto role = entry["role"].get<std::string>();
      if (role.empty()) continue;

      String content;
      if (entry.contains("content")) {
        const auto& c = entry["content"];
        if (c.is_string()) {
          content = c.get<std::string>();
        } else if (c.is_array()) {
          // Join string parts for multi-part content
          String joined;
          for (const auto& part : c) {
            if (part.is_string()) {
              joined += part.get<std::string>();
            }
          }
          content = joined;
        } else {
          continue;
        }
      }

      ai::chat::Message message;
      message.role = role;
      message.content = content;
      out.push_back(std::move(message));
    }

    return !out.empty();
  }

  static Vector<llama_logit_bias> parse_logit_bias_field(ai::llm::Model* model, const json& body) {
    Vector<llama_logit_bias> out;
    if (!model || !model->vocab) return out;
    const int32_t nVocab = llama_vocab_n_tokens(model->vocab);
    if (!body.contains("logit_bias")) return out;
    const auto& logitBias = body["logit_bias"];
    auto appendBias = [&](llama_token tok, float bias) {
      if (tok >= 0 && tok < nVocab) {
        out.push_back({tok, bias});
      }
    };
    if (logitBias.is_array()) {
      for (const auto& el : logitBias) {
        if (!el.is_array() || el.size() != 2) continue;
        float bias = 0.0f;
        if (el[1].is_number()) {
          bias = (float)el[1].get<double>();
        } else if (el[1].is_boolean() && !el[1].get<bool>()) {
          bias = -std::numeric_limits<float>::infinity();
        } else {
          continue;
        }
        if (el[0].is_number_integer()) {
          llama_token tok = el[0].get<llama_token>();
          appendBias(tok, bias);
        } else if (el[0].is_string()) {
          const auto tokens = tokenize_string(model, el[0].get<std::string>());
          for (auto tok : tokens) appendBias(tok, bias);
        }
      }
    } else if (logitBias.is_object()) {
      for (const auto& item : logitBias.items()) {
        float bias = 0.0f;
        const auto& value = item.value();
        if (value.is_number()) {
          bias = (float)value.get<double>();
        } else if (value.is_boolean() && !value.get<bool>()) {
          bias = -std::numeric_limits<float>::infinity();
        } else {
          continue;
        }
        llama_token tok = 0;
        const auto& key = item.key();
        bool parsedAsId = false;
        try {
          tok = (llama_token) std::stoll(key);
          parsedAsId = true;
        } catch (...) {}
        if (parsedAsId) {
          appendBias(tok, bias);
        } else {
          const auto tokens = tokenize_string(model, key);
          for (auto t : tokens) appendBias(t, bias);
        }
      }
    }
    return out;
  }

  static float parse_float_param(const String& value, float fallback) {
    if (value.size() == 0) return fallback;
    try { return (float)std::stof(value); } catch (...) { return fallback; }
  }

  static SamplingOverrides parse_sampling_overrides(
    ai::llm::Model* model,
    const serviceworker::Request& req,
    const json* body,
    const LlamaServer::Options& defaults
  ) {
    auto out = make_sampling_defaults(defaults);

    out.repeatPenalty = parse_float_param(req.url.searchParams.get("repeat_penalty").str(), out.repeatPenalty);
    out.frequencyPenalty = parse_float_param(req.url.searchParams.get("frequency_penalty").str(), out.frequencyPenalty);
    out.presencePenalty = parse_float_param(req.url.searchParams.get("presence_penalty").str(), out.presencePenalty);
    const auto lastNStr = req.url.searchParams.get("repeat_last_n").str();
    if (lastNStr.size() > 0) {
      try { out.repeatLastN = std::max(0, std::stoi(lastNStr)); } catch (...) {}
    }

    if (body) {
      const auto& j = *body;
      if (j.contains("repeat_penalty") && j["repeat_penalty"].is_number()) {
        out.repeatPenalty = (float)j["repeat_penalty"].get<double>();
      }
      if (j.contains("frequency_penalty") && j["frequency_penalty"].is_number()) {
        out.frequencyPenalty = (float)j["frequency_penalty"].get<double>();
      }
      if (j.contains("presence_penalty") && j["presence_penalty"].is_number()) {
        out.presencePenalty = (float)j["presence_penalty"].get<double>();
      }
      if (j.contains("repeat_last_n") && j["repeat_last_n"].is_number_integer()) {
        out.repeatLastN = std::max(0, (int)j["repeat_last_n"].get<int64_t>());
      }
      if (j.contains("logit_bias")) {
        out.logitBias = parse_logit_bias_field(model, j);
      }
    }

    if (out.repeatLastN < 0) out.repeatLastN = 0;
    return out;
  }

  static Vector<String> parse_stop_params(const serviceworker::Request& req) {
    Vector<String> stops;
    auto raw = req.url.searchParams.get("stop").str();
    if (raw.size() == 0) raw = req.url.searchParams.get("stop_sequences").str();
    if (raw.size() == 0) return stops;
    auto parts = string::split(raw, ',');
    for (auto & p : parts) {
      auto s = string::trim(p);
      if (s.size() > 0) stops.push_back(s);
    }
    return stops;
  }

  static bool tail_has_stop(const String& tail, const Vector<String>& stops) {
    for (const auto& s : stops) {
      if (s.size() == 0) continue;
      if (tail.size() >= s.size()) {
        if (tail.compare(tail.size() - s.size(), s.size(), s) == 0) return true;
      }
    }
    return false;
  }

  // Development-only KV-clear log sampling (guarded by oro.toml or debug)
  static bool kv_log_enabled() {
    static bool enabled = []() {
      bool dev = oro::runtime::config::isDebugEnabled();
      auto uc = oro::runtime::config::getUserConfig();
      bool force = false;
      const auto key = String("ai_llm_debug_kv_log");
      if (uc.contains(key) && uc.at(key).size() > 0) {
        const auto v = uc.at(key);
        force = (v == "1" || v == "true" || v == "on" || v == "yes");
      }
      return dev || force;
    }();
    return enabled;
  }

  static uint64_t kv_log_interval_ms() {
    static uint64_t interval = []() {
      uint64_t def = 5000; // 5s default
      auto uc = oro::runtime::config::getUserConfig();
      const auto key = String("ai_llm_debug_kv_log_interval_ms");
      if (uc.contains(key) && uc.at(key).size() > 0) {
        try { return (uint64_t) std::stoull(uc.at(key)); } catch (...) {}
      }
      return def;
    }();
    return interval;
  }

  static void maybe_log_kv_metrics(const char* where, ai::llm::Manager* pllm) {
    if (!kv_log_enabled()) return;
    using namespace oro::runtime::ai::llm;
    static std::atomic<uint64_t> lastAttempted{0};
    static std::atomic<uint64_t> lastSucceeded{0};
    static std::atomic<long long> lastLogMs{0};
    const auto att = kvClearAttempted.load(std::memory_order_relaxed);
    const auto suc = kvClearSucceeded.load(std::memory_order_relaxed);
    const bool changed = (att != lastAttempted.load(std::memory_order_relaxed) || suc != lastSucceeded.load(std::memory_order_relaxed));
    const long long now = nowMsSteady();
    const long long prev = lastLogMs.load(std::memory_order_relaxed);
    if (!changed && (kv_log_interval_ms() > 0) && (now - prev) < (long long)kv_log_interval_ms()) {
      return;
    }

    // Gather optional pool stats if manager available
    int64_t poolContexts = 0, poolCreated = 0, poolReused = 0, poolDropped = 0;
    if (pllm != nullptr) {
      Lock lock(pllm->mutex);
      for (const auto& kv : pllm->contextPool) poolContexts += (int64_t) kv.second.size();
      poolCreated = (int64_t) pllm->poolCreated.load();
      poolReused  = (int64_t) pllm->poolReused.load();
      poolDropped = (int64_t) pllm->poolDropped.load();
    }

    debug("[ai.llama] kv_clear %s attempted=%llu succeeded=%llu lastAttemptMs=%lld lastSuccessMs=%lld pool{contexts=%lld created=%lld reused=%lld dropped=%lld}",
      where ? where : "",
      (unsigned long long)att,
      (unsigned long long)suc,
      (long long)kvClearLastAttemptMs.load(std::memory_order_relaxed),
      (long long)kvClearLastSuccessMs.load(std::memory_order_relaxed),
      (long long)poolContexts,
      (long long)poolCreated,
      (long long)poolReused,
      (long long)poolDropped
    );

    lastAttempted.store(att, std::memory_order_relaxed);
    lastSucceeded.store(suc, std::memory_order_relaxed);
    lastLogMs.store(now, std::memory_order_relaxed);
  }

  // Top-K filter for route metrics in /health output
  static size_t route_metrics_top_k() {
    static size_t topk = []() {
      size_t def = 32;
      auto uc = oro::runtime::config::getUserConfig();
      const auto key = String("ai_llm_debug_route_metrics_top_k");
      if (uc.contains(key) && uc.at(key).size() > 0) {
        try {
          auto v = (size_t) std::stoull(uc.at(key));
          return v;
        } catch (...) {}
      }
      return def;
    }();
    return topk;
  }

  bool LlamaServer::handle(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const auto path = normalize_path(req.url.pathname);
    if (!matches(path)) return false;

    const auto& prefix = options.routePrefix;

    // Oro, OpenAI-compatible, and Ollama-compatible routes.
    if (path == prefix + "/health") {
      return handleHealth(req, statusCode, headers, body, llm);
    } else if (path == prefix + "/metrics") {
      if (!options.enableMetrics) {
        return send_error_response(404, "not_found_error", "Metrics endpoint disabled", statusCode, headers, body, prefix + "/metrics");
      }
      return handleMetrics(req, statusCode, headers, body, llm);
    } else if (path == prefix + "/tokenize") {
      return handleTokenize(req, statusCode, headers, body, llm);
    } else if (path == prefix + "/detokenize") {
      return handleDetokenize(req, statusCode, headers, body, llm);
    } else if (path == prefix + "/models" || path == prefix + "/api/tags") {
      return handleModelListAliases(req, statusCode, headers, body, llm);
    } else if (path == prefix + "/v1/models") {
      return handleModelsV1(req, statusCode, headers, body, llm);
    } else if (
      path == prefix + "/embedding" ||
      path == prefix + "/embeddings" ||
      path == prefix + "/v1/embeddings"
    ) {
      return handleEmbeddingsV1(req, statusCode, headers, body, llm);
    } else if (
      path == prefix + "/completion" ||
      path == prefix + "/completions" ||
      path == prefix + "/v1/completions"
    ) {
      return handleCompletionsV1(req, statusCode, headers, body, llm);
    } else if (
      path == prefix + "/chat/completions" ||
      path == prefix + "/v1/chat/completions" ||
      path == prefix + "/api/chat"
    ) {
      return handleChatCompletionsV1(req, statusCode, headers, body, llm);
    } else if (path == prefix + "/apply-template") {
      return handleApplyTemplate(req, statusCode, headers, body, llm);
    } else if (
      path == prefix + "/props" ||
      path == prefix + "/api/show"
    ) {
      if (path == prefix + "/props") {
        if (!options.enableProps) {
          return send_error_response(501, "not_supported_error", "Props endpoint disabled", statusCode, headers, body, prefix + "/props");
        }
        if (req.method == "POST") {
          return handlePropsPost(req, statusCode, headers, body, llm);
        }
        return handleProps(req, statusCode, headers, body, llm);
      }
      return handleApiShow(req, statusCode, headers, body, llm);
    } else if (
      path == prefix + "/infill"
    ) {
      return send_error_response(501, "not_supported_error", "Infill endpoint not supported", statusCode, headers, body, path);
    } else if (
      path == prefix + "/rerank" ||
      path == prefix + "/reranking" ||
      path == prefix + "/v1/rerank" ||
      path == prefix + "/v1/reranking"
    ) {
      if (!options.enableRerank) {
        return send_error_response(501, "not_supported_error", "Rerank endpoint disabled", statusCode, headers, body, path);
      }
      return handleRerank(req, statusCode, headers, body, llm);
    } else if (path == prefix + "/lora-adapters") {
      return handleLoraAdapters(req, statusCode, headers, body, llm);
    } else if (path.rfind(prefix + "/slots", 0) == 0) {
      return handleSlots(req, statusCode, headers, body, llm);
    }

    // Not found under our prefix
    statusCode = 404;
    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {
      {"error", JSON::Object::Entries {{"message", "Not Found"}}},
      {"path", path}
    }).str());
    return true;
  }

  bool LlamaServer::handleHealth(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    (void) req;
    // Ensure LLM is initialized lazily
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    headers.set("content-type", "application/json; charset=utf-8");

    Array models;
    int64_t poolContexts = 0;
    int64_t poolCreated = 0;
    int64_t poolReused = 0;
    int64_t poolDropped = 0;
    // per-model pool breakdown: model -> [{ size, count }]
    Map<String, Map<String, uint64_t>> perModelSizes; // size as string key for stable JSON
    bool ready = false;
    {
      Lock lock(llm.mutex);
      for (const auto& entry : llm.models) {
        models.push(entry.second->json());
        if (entry.second && entry.second->loaded()) {
          ready = true;
        }
      }
      for (const auto& kv : llm.contextPool) {
        poolContexts += (int64_t)kv.second.size();
        // key format: "<model>|<size>"
        const auto& key = kv.first;
        auto bar = key.find('|');
        if (bar != String::npos) {
          auto modelName = key.substr(0, bar);
          auto sizeStr = key.substr(bar + 1);
          perModelSizes[modelName][sizeStr] += (uint64_t) kv.second.size();
        }
      }
      poolCreated = (int64_t)llm.poolCreated.load();
      poolReused  = (int64_t)llm.poolReused.load();
      poolDropped = (int64_t)llm.poolDropped.load();
    }

    // Optional route diagnostics snapshot
    Map<String, uint64_t> routeCounts;
    Vector<String> routeRecent;
    Map<String, Map<String, uint64_t>> routeStatus;
    g_route_metrics.snapshot(routeCounts, routeRecent, routeStatus);

    // Apply top-K filtering to counts/status to keep health small
    Vector<std::pair<String, uint64_t>> pairs;
    pairs.reserve(routeCounts.size());
    for (const auto& kv : routeCounts) pairs.emplace_back(kv.first, kv.second);
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    const size_t k = route_metrics_top_k();
    if (k > 0 && pairs.size() > k) pairs.resize(k);

    // marshal pool.detail breakdown
    JSON::Array poolDetail;
    for (const auto &pm : perModelSizes) {
      JSON::Array sizesArr;
      for (const auto &sz : pm.second) {
        sizesArr.push(JSON::Object::Entries {{"size", sz.first}, {"count", (int64_t)sz.second}});
      }
      poolDetail.push(JSON::Object::Entries {{"model", pm.first}, {"sizes", sizesArr}});
    }

    if (!ready) {
      const auto json = JSON::Object::Entries {
        {"error", JSON::Object::Entries {
          {"code", 503},
          {"message", "Loading model"},
          {"type", "unavailable_error"}
        }},
        {"llmInitialized", llm.isInitialized.load(std::memory_order_acquire)},
        {"models", models}
      };
      body = bytes::Buffer::from(JSON::Object(json).str());
      statusCode = 503;
      g_route_metrics.recordStatus(this->options.routePrefix + "/health", statusCode);
      return true;
    }

    const auto metrics = this->options.healthMetricsProvider
      ? this->options.healthMetricsProvider(req)
      : JSON::Any(JSON::Null());

    const auto json = Object::Entries {
      {"status", "ok"},
      {"llmInitialized", llm.isInitialized.load(std::memory_order_acquire)},
      {"models", models},
      {"metrics", metrics},
      {"pool", Object::Entries {{"contexts", poolContexts}, {"created", poolCreated}, {"reused", poolReused}, {"dropped", poolDropped}, {"detail", poolDetail}}},
      {"kvClear", Object::Entries {{"available", oro::runtime::ai::llm::isKVClearAvailable()}, {"attempted", (int64_t)oro::runtime::ai::llm::kvClearAttempted.load()}, {"succeeded", (int64_t)oro::runtime::ai::llm::kvClearSucceeded.load()}, {"lastAttemptMs", (int64_t)oro::runtime::ai::llm::kvClearLastAttemptMs.load()}, {"lastSuccessMs", (int64_t)oro::runtime::ai::llm::kvClearLastSuccessMs.load()}}},
      {"routes", [&]() -> JSON::Any {
        if (!route_metrics_enabled()) return JSON::Null();
        JSON::Object::Entries counts;
        for (const auto& p : pairs) counts[p.first] = (int64_t)p.second;
        JSON::Array recent;
        for (const auto& r : routeRecent) recent.push(r);
        JSON::Object::Entries status;
        for (const auto& p : pairs) {
          const auto it = routeStatus.find(p.first);
          if (it == routeStatus.end()) continue;
          JSON::Object::Entries buckets;
          for (const auto& b : it->second) buckets[b.first] = (int64_t)b.second;
          status[p.first] = JSON::Object::Entries{buckets};
        }
        return JSON::Object::Entries {{"counts", counts}, {"recent", recent}, {"status", status}};
      }()}
    };
    body = bytes::Buffer::from(JSON::Object(json).str());
    statusCode = 200;
    g_route_metrics.recordStatus(this->options.routePrefix + "/health", statusCode);
    maybe_log_kv_metrics("/health", &llm);
    return true;
  }

  static ai::llm::Model* select_model(ai::llm::Manager& llm, const String& name, const String& fallback) {
    Lock lock(llm.mutex);
    if (name.size() > 0) {
      if (llm.models.contains(name)) {
        return llm.models.at(name).get();
      }
    }
    if (fallback.size() > 0 && llm.models.contains(fallback)) {
      return llm.models.at(fallback).get();
    }
    if (llm.models.size() > 0) {
      return llm.models.begin()->second.get();
    }
    return nullptr;
  }

  static bool parse_bool(const String& s, bool def) {
    if (s.size() == 0) return def;
    const auto v = s;
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return def;
  }

  bool LlamaServer::handleTokenize(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    // Model selection
    const String routeTag = this->options.routePrefix + "/tokenize";
    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->vocab) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "No model loaded"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    // Input text: prefer query ?text=, else body as raw
    String text = req.url.searchParams.get("text").str();
    if (text.size() == 0 && req.body.size() > 0) {
      text = req.body.str();
    }
    if (text.size() == 0) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Missing 'text'"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    // Guard excessively large inputs (align with prompt guard)
    if (text.size() > this->options.maxPromptBytes) {
      statusCode = 413;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Text too large"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    const bool add_special   = parse_bool(req.url.searchParams.get("add_special").str(), true);
    const bool parse_special = parse_bool(req.url.searchParams.get("parse_special").str(), true);
    const bool with_pieces   = parse_bool(req.url.searchParams.get("pieces").str(), false);

    const int n_needed = -llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), nullptr, 0, add_special, parse_special);
    if (n_needed <= 0) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Tokenization failed"}}}}).str());
      return true;
    }

    Vector<llama_token> tokens(n_needed);
    const int n_out = llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), tokens.data(), (int32_t)tokens.size(), add_special, parse_special);
    if (n_out < 0) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Tokenization failed"}}}}).str());
      return true;
    }

    JSON::Array jtoks;
    JSON::Array jpieces;
    for (int i = 0; i < n_out; ++i) {
      jtoks.push((int64_t)tokens[i]);
      if (with_pieces) {
        static thread_local Vector<char> pieceBuf;
        if (pieceBuf.empty()) pieceBuf.resize(256);
        int n = llama_token_to_piece(model->vocab, tokens[i], pieceBuf.data(), (int32_t)pieceBuf.size(), 0, true);
        if (n < 0) {
          int tries = 0; size_t cap = pieceBuf.size();
          while (n < 0 && tries++ < 4 && cap < 8192) {
            cap *= 2; pieceBuf.resize(cap);
            n = llama_token_to_piece(model->vocab, tokens[i], pieceBuf.data(), (int32_t)pieceBuf.size(), 0, true);
          }
        }
        if (n < 0) n = 0;
        jpieces.push(String(pieceBuf.data(), (size_t)n));
      }
    }

    JSON::Object::Entries out {
      {"tokens", jtoks}
    };
    if (with_pieces) out["pieces"] = jpieces;

    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(out).str());
    statusCode = 200;
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }

  static Vector<llama_token> parse_tokens_param(const String& s) {
    Vector<llama_token> out;
    String cur;
    for (char c : s) {
      if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        if (cur.size() > 0) {
          try { out.push_back((llama_token)std::stoi(cur)); } catch (...) {}
          cur.clear();
        }
      } else if (c >= '0' && c <= '9' || c == '-' ) {
        cur.push_back(c);
      }
    }
    if (cur.size() > 0) {
      try { out.push_back((llama_token)std::stoi(cur)); } catch (...) {}
    }
    return out;
  }

  struct RerankEmbedding {
    Vector<double> values;
    int64_t tokens = 0;
  };

  static String token_to_piece(const llama_vocab* vocab, llama_token token) {
    if (!vocab) return {};
    static thread_local Vector<char> buf;
    if (buf.empty()) buf.resize(256);
    int wrote = llama_token_to_piece(vocab, token, buf.data(), (int32_t)buf.size(), 0, true);
    if (wrote < 0) {
      size_t cap = buf.size(); int tries = 0;
      while (wrote < 0 && tries++ < 4 && cap < 8192) {
        cap *= 2; buf.resize(cap);
        wrote = llama_token_to_piece(vocab, token, buf.data(), (int32_t)buf.size(), 0, true);
      }
    }
    if (wrote <= 0) return {};
    return String(buf.data(), (size_t)wrote);
  }

  static bool compute_embedding_single(
    const String& text,
    const SharedPointer<ai::llm::Model>& model,
    ai::llm::Manager& llm,
    const LlamaServer::Options& opts,
    RerankEmbedding& out,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    const String& routeTag
  ) {
    if (!model || !model->vocab) {
      return send_error_response(400, "invalid_request_error", "No model loaded", statusCode, headers, body, routeTag);
    }
    const int n_tokens = -llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), nullptr, 0, true, true);
    if (n_tokens <= 0) {
      return send_error_response(500, "server_error", "Tokenization failed", statusCode, headers, body, routeTag);
    }
    if ((size_t)text.size() > opts.embedMaxTotalBytes) {
      return send_error_response(413, "invalid_request_error", "Input too large", statusCode, headers, body, routeTag);
    }
    ai::llm::Context::Options copt;
    copt.size = std::max<size_t>(2048, (size_t)n_tokens + 64);
    auto ctx = llm.acquirePooledContext(model, copt);
    if (!ctx || !ctx->context) {
      return send_error_response(500, "server_error", "Failed to create context", statusCode, headers, body, routeTag);
    }
    bool released = false;
    auto releaseCtx = [&]() {
      if (!released) {
        llm.releasePooledContext(ctx);
        released = true;
      }
    };
    struct ReleaseGuard { decltype(releaseCtx)& f; ~ReleaseGuard(){ f(); } } guard{releaseCtx};
    llama_set_embeddings(ctx->context, true);
    Vector<llama_token> toks((size_t)n_tokens);
    if (llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), toks.data(), (int32_t)toks.size(), true, true) < 0) {
      return send_error_response(500, "server_error", "Tokenization failed", statusCode, headers, body, routeTag);
    }
    llama_batch batch = llama_batch_get_one(toks.data(), (int32_t)toks.size());
    if (ctx->used() + (size_t)batch.n_tokens > ctx->size()) {
      return send_error_response(400, "invalid_request_error", "Context too small", statusCode, headers, body, routeTag);
    }
    if (llama_decode(ctx->context, batch)) {
      return send_error_response(500, "server_error", "Decode failed", statusCode, headers, body, routeTag);
    }
    ctx->usedTokens.fetch_add((size_t)batch.n_tokens, std::memory_order_acq_rel);
    const int32_t n_embd = llama_model_n_embd(model->model);
    float* emb = llama_get_embeddings_ith(ctx->context, -1);
    if (!emb || n_embd <= 0) {
      return send_error_response(500, "server_error", "Embeddings unavailable", statusCode, headers, body, routeTag);
    }
    out.values.resize((size_t)n_embd);
    for (int32_t i = 0; i < n_embd; ++i) {
      out.values[(size_t)i] = emb[i];
    }
    out.tokens = n_tokens;
    (void) ctx->reset();
    llama_set_embeddings(ctx->context, true);
    return true;
  }

  bool LlamaServer::handleDetokenize(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const String routeTag = this->options.routePrefix + "/detokenize";
    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->vocab) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "No model loaded"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    Vector<llama_token> tokens;
    const String tokensParam = req.url.searchParams.get("tokens").str();
    if (tokensParam.size() > 0) {
      tokens = parse_tokens_param(tokensParam);
    } else if (req.body.size() > 0) {
      tokens = parse_tokens_param(req.body.str());
    }
    if (tokens.size() == 0) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Missing 'tokens'"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    const bool remove_special  = parse_bool(req.url.searchParams.get("remove_special").str(), false);
    const bool unparse_special = parse_bool(req.url.searchParams.get("unparse_special").str(), true);

    int32_t need = llama_detokenize(model->vocab, tokens.data(), (int32_t)tokens.size(), nullptr, 0, remove_special, unparse_special);
    if (need < 0) need = -need;
    Vector<char> out((size_t)need + 1);
    const int32_t wrote = llama_detokenize(model->vocab, tokens.data(), (int32_t)tokens.size(), out.data(), (int32_t)out.size(), remove_special, unparse_special);
    if (wrote < 0) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Detokenization failed"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"text", String(out.data(), (size_t)wrote)}}).str());
    statusCode = 200;
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }

  bool LlamaServer::handleMetrics(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    (void) req;
    size_t modelCount = 0;
    size_t contexts = 0;
    uint64_t poolCreated = 0;
    uint64_t poolReused = 0;
    uint64_t poolDropped = 0;
    {
      Lock lock(llm.mutex);
      modelCount = llm.models.size();
      for (const auto& kv : llm.contextPool) {
        contexts += kv.second.size();
      }
      poolCreated = llm.poolCreated.load();
      poolReused = llm.poolReused.load();
      poolDropped = llm.poolDropped.load();
    }

    std::ostringstream os;
    os << "# HELP oro_llm_models_loaded Number of llama.cpp models loaded" << '\n';
    os << "# TYPE oro_llm_models_loaded gauge" << '\n';
    os << "oro_llm_models_loaded " << modelCount << '\n';
    os << "# HELP oro_llm_context_pool_size Contexts currently pooled" << '\n';
    os << "# TYPE oro_llm_context_pool_size gauge" << '\n';
    os << "oro_llm_context_pool_size " << contexts << '\n';
    os << "# HELP oro_llm_context_pool_created Total contexts created for pool" << '\n';
    os << "# TYPE oro_llm_context_pool_created counter" << '\n';
    os << "oro_llm_context_pool_created " << poolCreated << '\n';
    os << "# HELP oro_llm_context_pool_reused Total contexts reused" << '\n';
    os << "# TYPE oro_llm_context_pool_reused counter" << '\n';
    os << "oro_llm_context_pool_reused " << poolReused << '\n';
    os << "# HELP oro_llm_context_pool_dropped Total contexts dropped" << '\n';
    os << "# TYPE oro_llm_context_pool_dropped counter" << '\n';
    os << "oro_llm_context_pool_dropped " << poolDropped << '\n';

    headers.set("content-type", "text/plain; version=0.0.4; charset=utf-8");
    body = bytes::Buffer::from(os.str());
    statusCode = 200;
    g_route_metrics.recordStatus(this->options.routePrefix + "/metrics", statusCode);
    return true;
  }

  bool LlamaServer::handleProps(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const auto modelName = req.url.searchParams.get("model").str();
    auto* rawModel = select_model(llm, modelName, this->options.defaultModelName);
    SharedPointer<ai::llm::Model> modelShared;
    if (rawModel) {
      Lock lock(llm.mutex);
      if (llm.models.contains(rawModel->name)) {
        modelShared = llm.models.at(rawModel->name);
      }
    }

    const auto bos = modelShared ? token_to_piece(modelShared->vocab, llama_vocab_bos(modelShared->vocab)) : String();
    const auto eos = modelShared ? token_to_piece(modelShared->vocab, llama_vocab_eos(modelShared->vocab)) : String();
    const auto modelPath = modelShared ? String(modelShared->filename.string()) : String();
    const auto modelNameOut = modelShared ? modelShared->name : String();

    JSON::Object::Entries defaultGen {
      {"max_tokens", (int64_t)this->options.defaultMaxTokens},
      {"max_decode_ms", (int64_t)this->options.defaultMaxDecodeMs},
      {"temperature", this->options.defaultTemperature},
      {"top_p", this->options.defaultTopP},
      {"top_k", (int64_t)this->options.defaultTopK},
      {"min_p", this->options.defaultMinP}
    };

    JSON::Object::Entries modalities {{"vision", false}, {"audio", false}};

    JSON::Object::Entries buildInfo {{"llama_cpp_build", (int64_t)LLAMA_BUILD_NUMBER}};

    // Expose available chat templates via llama.cpp when a model is loaded
    if (modelShared && modelShared->model != nullptr) {
      const char* tmpl = llama_model_chat_template(modelShared->model, nullptr);
      if (tmpl != nullptr) {
        buildInfo["chat_template_default"] = String(tmpl);
      }
      const size_t maxTemplates = 32;
      const char* names[maxTemplates];
      int32_t count = llama_chat_builtin_templates(names, maxTemplates);
      if (count > 0) {
        JSON::Array arr;
        for (int32_t i = 0; i < count && i < (int32_t)maxTemplates; ++i) {
          if (names[i] != nullptr) {
            arr.push(String(names[i]));
          }
        }
        buildInfo["chat_templates_builtin"] = arr;
      }
    }

    const auto jsonOut = JSON::Object::Entries {
      {"default_generation_settings", JSON::Object(defaultGen)},
      {"total_slots", (int64_t)std::max(1, this->options.maxConcurrent)},
      {"model", modelNameOut},
      {"model_path", modelPath},
      {"modalities", JSON::Object(modalities)},
      {"endpoint_slots", false},
      {"endpoint_props", this->options.enableProps},
      {"endpoint_metrics", this->options.enableMetrics},
      {"webui", false},
      {"chat_template", String()},
      {"bos_token", bos},
      {"eos_token", eos},
      {"build_info", JSON::Object(buildInfo)}
    };

    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(jsonOut).str());
    statusCode = 200;
    g_route_metrics.recordStatus(this->options.routePrefix + "/props", statusCode);
    return true;
  }

  bool LlamaServer::handlePropsPost(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    (void) llm;
    json payload;
    if (!parse_json_body(req, payload)) {
      return send_error_response(400, "invalid_request_error", "Invalid JSON body", statusCode, headers, body, this->options.routePrefix + "/props");
    }
    // Mutable props are not yet supported; match llama.cpp semantics by returning a not-supported error.
    return send_error_response(501, "not_supported_error", "Changing global properties is not supported", statusCode, headers, body, this->options.routePrefix + "/props");
  }

  bool LlamaServer::handleApiShow(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const auto modelName = req.url.searchParams.get("model").str();
    auto* rawModel = select_model(llm, modelName, this->options.defaultModelName);
    if (!rawModel) {
      return send_error_response(400, "invalid_request_error", "No model loaded", statusCode, headers, body, this->options.routePrefix + "/api/show");
    }

    int64_t contextLen = 0;
    {
      Lock lock(llm.mutex);
      if (llm.models.contains(rawModel->name)) {
        // Attempt to read existing pooled context size if available
        for (const auto& kv : llm.contextPool) {
          if (kv.first.rfind(rawModel->name + "|", 0) == 0 && !kv.second.empty()) {
            contextLen = (int64_t)kv.second.front()->size();
            break;
          }
        }
      }
    }

    JSON::Object::Entries modelInfo {{"llama.context_length", contextLen}};
    if (rawModel && rawModel->model != nullptr) {
      const char* tmpl = llama_model_chat_template(rawModel->model, nullptr);
      if (tmpl != nullptr) {
        modelInfo["llama.chat_template"] = String(tmpl);
      }
    }
    const auto jsonOut = JSON::Object::Entries {
      {"template", String()},
      {"model_info", JSON::Object(modelInfo)}
    };

    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(jsonOut).str());
    statusCode = 200;
    g_route_metrics.recordStatus(this->options.routePrefix + "/api/show", statusCode);
    return true;
  }

  bool LlamaServer::handleRerank(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    json payload;
    if (!parse_json_body(req, payload)) {
      return send_error_response(400, "invalid_request_error", "Invalid JSON body", statusCode, headers, body, this->options.routePrefix + "/rerank");
    }

    String query;
    if (payload.contains("query") && payload["query"].is_string()) {
      query = payload["query"].get<std::string>();
    } else {
      return send_error_response(400, "invalid_request_error", "'query' must be provided", statusCode, headers, body, this->options.routePrefix + "/rerank");
    }

    Vector<String> documents;
    bool isTeiFormat = false;
    if (payload.contains("documents") && payload["documents"].is_array()) {
      for (const auto& doc : payload["documents"]) {
        if (doc.is_string()) documents.push_back(doc.get<std::string>());
      }
    } else if (payload.contains("texts") && payload["texts"].is_array()) {
      isTeiFormat = true;
      for (const auto& doc : payload["texts"]) {
        if (doc.is_string()) documents.push_back(doc.get<std::string>());
      }
    }

    if (documents.empty()) {
      return send_error_response(400, "invalid_request_error", "'documents' must be a non-empty array", statusCode, headers, body, this->options.routePrefix + "/rerank");
    }

    const auto modelName = payload.contains("model") && payload["model"].is_string()
      ? String(payload["model"].get<std::string>())
      : req.url.searchParams.get("model").str();

    auto* rawModel = select_model(llm, modelName, this->options.defaultModelName);
    if (!rawModel) {
      return send_error_response(400, "invalid_request_error", "No model loaded", statusCode, headers, body, this->options.routePrefix + "/rerank");
    }

    SharedPointer<ai::llm::Model> modelShared;
    {
      Lock lock(llm.mutex);
      if (llm.models.contains(rawModel->name)) {
        modelShared = llm.models.at(rawModel->name);
      }
    }
    if (!modelShared) {
      return send_error_response(400, "invalid_request_error", "Model unavailable", statusCode, headers, body, this->options.routePrefix + "/rerank");
    }

    const String routeTag = this->options.routePrefix + "/rerank";
    RerankEmbedding queryEmb;
    if (!compute_embedding_single(query, modelShared, llm, this->options, queryEmb, statusCode, headers, body, routeTag)) {
      return true;
    }

    Vector<RerankEmbedding> docEmbeddings(documents.size());
    for (size_t i = 0; i < documents.size(); ++i) {
      if (!compute_embedding_single(documents[i], modelShared, llm, this->options, docEmbeddings[i], statusCode, headers, body, routeTag)) {
        return true;
      }
    }

    auto norm = [](const Vector<double>& vec) -> double {
      double sum = std::inner_product(vec.begin(), vec.end(), vec.begin(), 0.0);
      return sum > 0.0 ? std::sqrt(sum) : 1.0;
    };

    const double queryNorm = norm(queryEmb.values);
    int64_t promptTokensTotal = queryEmb.tokens;

    struct ScoreEntry {
      size_t index;
      double score;
      int64_t tokens;
    };
    Vector<ScoreEntry> scores;
    scores.reserve(docEmbeddings.size());

    for (size_t i = 0; i < docEmbeddings.size(); ++i) {
      const auto& emb = docEmbeddings[i];
      promptTokensTotal += emb.tokens;
      double score = 0.0;
      if (!queryEmb.values.empty() && queryEmb.values.size() == emb.values.size()) {
        score = std::inner_product(queryEmb.values.begin(), queryEmb.values.end(), emb.values.begin(), 0.0);
        const double docNorm = norm(emb.values);
        if (docNorm > 0.0 && queryNorm > 0.0) {
          score /= (docNorm * queryNorm);
        }
      }
      scores.push_back(ScoreEntry{ i, score, (int64_t)(queryEmb.tokens + emb.tokens) });
    }

    // Sort by score descending to mimic llama.cpp behaviour
    std::sort(scores.begin(), scores.end(), [](const ScoreEntry& a, const ScoreEntry& b) {
      return a.score > b.score;
    });

    String responseStr;
    if (isTeiFormat) {
      const bool returnText = payload.contains("return_text") && payload["return_text"].is_boolean() ? payload["return_text"].get<bool>() : false;
      JSON::Array arr;
      for (const auto& item : scores) {
        JSON::Object::Entries obj {{"index", (int64_t)item.index}, {"score", item.score}};
        if (returnText && item.index < documents.size()) {
          obj["text"] = documents[item.index];
        }
        arr.push(JSON::Object(obj));
      }
      responseStr = JSON::Array(arr).str();
    } else {
      JSON::Array resultArr;
      for (const auto& item : scores) {
        resultArr.push(JSON::Object::Entries {{"index", (int64_t)item.index}, {"relevance_score", item.score}});
      }
      const auto usage = JSON::Object::Entries {{"prompt_tokens", (int64_t)promptTokensTotal}, {"total_tokens", (int64_t)promptTokensTotal}};
      const auto obj = JSON::Object::Entries {
        {"model", modelShared->name},
        {"object", "list"},
        {"usage", JSON::Object(usage)},
        {"results", resultArr}
      };
      responseStr = JSON::Object(obj).str();
    }

    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(responseStr);
    statusCode = 200;
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }

  bool LlamaServer::handleSlots(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const String routeTag = this->options.routePrefix + "/slots";
    const auto path = normalize_path(req.url.pathname);
    const auto base = this->options.routePrefix + "/slots";

    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    if (req.method == "GET") {
      JSON::Array data;
      int64_t pooled = 0;
      int64_t active = 0;
      {
        Lock lock(llm.mutex);
        std::unordered_set<ai::llm::ID> pooledIds;
        for (const auto& kv : llm.contextPool) {
          for (const auto& ctx : kv.second) {
            if (ctx) pooledIds.insert(ctx->id);
          }
        }
        for (const auto& entry : llm.contexts) {
          const auto& ctx = entry.second;
          if (!ctx) {
            continue;
          }
          const bool inPool = pooledIds.count(ctx->id) > 0;
          if (inPool) {
            pooled++;
          } else {
            active++;
          }
          data.push(JSON::Object::Entries {
            {"id", std::to_string(ctx->id)},
            {"model", ctx->model ? ctx->model->name : String()},
            {"size", (int64_t)ctx->options.size},
            {"used", (int64_t)ctx->used()},
            {"pooled", inPool},
            {"loras", (int64_t)ctx->loras.size()}
          });
        }
      }
      const auto totals = JSON::Object::Entries {{"pooled", pooled}, {"active", active}, {"total", pooled + active}};
      const auto jsonOut = JSON::Object::Entries {{"object", "list"}, {"data", data}, {"totals", JSON::Object(totals)}};
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(jsonOut).str());
      statusCode = 200;
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    if (req.method != "POST") {
      return send_error_response(405, "invalid_request_error", "Method not allowed", statusCode, headers, body, routeTag);
    }

    String idSegment;
    if (path.size() > base.size() && path[base.size()] == '/') {
      idSegment = path.substr(base.size() + 1);
    }

    json payload;
    if (req.body.size() > 0) {
      if (!parse_json_body(req, payload)) {
        return send_error_response(400, "invalid_request_error", "Invalid JSON body", statusCode, headers, body, routeTag);
      }
    }

    const String action = payload.contains("action") && payload["action"].is_string()
      ? payload["action"].get<std::string>()
      : (idSegment.size() > 0 ? String("release") : String("drain"));

    if (idSegment.size() > 0) {
      ai::llm::ID ctxId = 0;
      try { ctxId = (ai::llm::ID) std::stoull(idSegment); } catch (...) {}
      if (ctxId == 0) {
        return send_error_response(400, "invalid_request_error", "Invalid slot id", statusCode, headers, body, routeTag);
      }
      if (action != "release") {
        return send_error_response(400, "invalid_request_error", "Unsupported action", statusCode, headers, body, routeTag);
      }
      if (!llm.removePooledContext(ctxId)) {
        return send_error_response(409, "conflict", "Slot not available in pool", statusCode, headers, body, routeTag);
      }
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"released", true}}).str());
      statusCode = 200;
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    if (action != "drain") {
      return send_error_response(400, "invalid_request_error", "Unsupported action", statusCode, headers, body, routeTag);
    }

    const auto modelParam = payload.contains("model") && payload["model"].is_string()
      ? payload["model"].get<std::string>()
      : req.url.searchParams.get("model").str();

    auto* rawModel = select_model(llm, modelParam, this->options.defaultModelName);
    if (!rawModel) {
      bool empty = false;
      {
        Lock lock(llm.mutex);
        empty = llm.models.empty();
      }
      if (empty) {
        headers.set("content-type", "application/json; charset=utf-8");
        body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"dropped", 0}}).str());
        statusCode = 200;
        g_route_metrics.recordStatus(routeTag, statusCode);
        return true;
      }
      return send_error_response(400, "invalid_request_error", "No model loaded", statusCode, headers, body, routeTag);
    }

    SharedPointer<ai::llm::Model> modelShared;
    {
      Lock lock(llm.mutex);
      for (const auto& entry : llm.models) {
        if (entry.second.get() == rawModel) {
          modelShared = entry.second;
          break;
        }
      }
      if (!modelShared) {
        for (const auto& entry : llm.models) {
          if (entry.second && entry.second->name == rawModel->name) {
            modelShared = entry.second;
            break;
          }
        }
      }
    }

    if (!modelShared) {
      return send_error_response(400, "invalid_request_error", "Model unavailable", statusCode, headers, body, routeTag);
    }

    size_t minKeep = 0;
    if (payload.contains("keep") && payload["keep"].is_number_unsigned()) {
      minKeep = (size_t) payload["keep"].get<uint64_t>();
    }

    const size_t dropped = llm.drainAll(modelShared, minKeep);
    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"dropped", (int64_t)dropped}}).str());
    statusCode = 200;
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }

  bool LlamaServer::handleApplyTemplate(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const String routeTag = this->options.routePrefix + "/apply-template";
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->model) {
      return send_error_response(400, "invalid_request_error", "No model loaded", statusCode, headers, body, routeTag);
    }

    json payload;
    if (!parse_json_body(req, payload)) {
      return send_error_response(400, "invalid_request_error", "Invalid JSON body", statusCode, headers, body, routeTag);
    }

    if (payload.contains("messages") && payload["messages"].is_array()) {
      for (auto& msg : payload["messages"]) {
        if (!msg.is_object()) continue;
        if (msg.contains("content") && msg["content"].is_array()) {
          std::string combined;
          for (const auto& part : msg["content"]) {
            if (part.is_string()) {
              combined += part.get<std::string>();
            } else if (part.is_object()) {
              if (part.contains("text") && part["text"].is_string()) {
                combined += part["text"].get<std::string>();
              }
            }
          }
          msg["content"] = combined;
        }
      }
    }

    String prompt;
    if (payload.contains("messages")) {
      try {
        prompt = render_chat_prompt_from_json(model, payload["messages"]);
      } catch (...) {}
    }
    if (prompt.size() == 0 && payload.contains("prompt") && payload["prompt"].is_string()) {
      prompt = payload["prompt"].get<std::string>();
    }
    if (prompt.size() == 0 && payload.contains("input") && payload["input"].is_string()) {
      prompt = payload["input"].get<std::string>();
    }

    if (prompt.size() == 0) {
      return send_error_response(400, "invalid_request_error", "Unable to derive prompt", statusCode, headers, body, routeTag);
    }

    if (prompt.size() > this->options.maxPromptBytes) {
      return send_error_response(413, "invalid_request_error", "Prompt too large", statusCode, headers, body, routeTag);
    }

    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"prompt", prompt}}).str());
    statusCode = 200;
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }

  bool LlamaServer::handleLoraAdapters(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const String routeTag = this->options.routePrefix + "/lora-adapters";
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    const auto modelFilter = req.url.searchParams.get("model").str();

    auto buildResponse = [&](const String& filter, JSON::Array& out) {
      std::unordered_map<ai::llm::ID, float> scaleById;
      {
        Lock lock(llm.mutex);
        if (filter.size() > 0) {
          if (llm.activeLoras.contains(filter)) {
            for (const auto& pair : llm.activeLoras.at(filter)) {
              if (pair.first) {
                scaleById[pair.first->id] = pair.second;
              }
            }
          }
        } else {
          for (const auto& kv : llm.activeLoras) {
            for (const auto& pair : kv.second) {
              if (pair.first) {
                scaleById[pair.first->id] = pair.second;
              }
            }
          }
        }

        for (const auto& entry : llm.loras) {
          const auto& lora = entry.second;
          if (!lora) continue;
          const auto modelName = lora->model ? lora->model->name : String();
          if (filter.size() > 0 && modelName != filter) continue;
          double scale = 0.0;
          if (scaleById.contains(lora->id)) {
            scale = scaleById.at(lora->id);
          }
          out.push(JSON::Object::Entries {{"id", std::to_string(lora->id)}, {"name", lora->name}, {"model", modelName}, {"loaded", lora->loaded()}, {"filename", lora->filename}, {"scale", scale}});
        }
      }
    };

    if (req.method == "GET") {
      JSON::Array data;
      buildResponse(modelFilter, data);
      const auto jsonOut = JSON::Object::Entries {{"object", "list"}, {"data", data}};
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(jsonOut).str());
      statusCode = 200;
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    if (req.method != "POST") {
      return send_error_response(405, "invalid_request_error", "Method not allowed", statusCode, headers, body, routeTag);
    }

    json payload;
    if (!parse_json_body(req, payload) || !payload.is_array()) {
      return send_error_response(400, "invalid_request_error", "Request body must be an array", statusCode, headers, body, routeTag);
    }

    auto* rawModel = select_model(llm, modelFilter, this->options.defaultModelName);
    if (!rawModel) {
      return send_error_response(400, "invalid_request_error", "No model loaded", statusCode, headers, body, routeTag);
    }

    SharedPointer<ai::llm::Model> modelShared;
    Vector<SharedPointer<ai::llm::LoRA>> loraList;
    {
      Lock lock(llm.mutex);
      for (const auto& entry : llm.models) {
        if (entry.second.get() == rawModel) {
          modelShared = entry.second;
          break;
        }
      }
      if (!modelShared) {
        for (const auto& entry : llm.models) {
          if (entry.second && entry.second->name == rawModel->name) {
            modelShared = entry.second;
            break;
          }
        }
      }
      for (const auto& entry : llm.loras) {
        if (entry.second && entry.second->model && modelShared && entry.second->model->name == modelShared->name) {
          loraList.push_back(entry.second);
        }
      }
    }

    if (!modelShared) {
      return send_error_response(400, "invalid_request_error", "Model unavailable", statusCode, headers, body, routeTag);
    }

    std::unordered_map<ai::llm::ID, size_t> indexById;
    Vector<std::pair<SharedPointer<ai::llm::LoRA>, float>> assignments;
    assignments.reserve(loraList.size());
    for (size_t i = 0; i < loraList.size(); ++i) {
      assignments.push_back({loraList[i], 0.0f});
      indexById[loraList[i]->id] = i;
    }

    auto findByName = [&](const String& name) -> size_t {
      for (size_t i = 0; i < loraList.size(); ++i) {
        if (loraList[i] && loraList[i]->name == name) return i;
      }
      return (size_t)-1;
    };

    try {
      for (const auto& item : payload) {
        if (!item.is_object()) continue;
        size_t idx = (size_t)-1;
        if (item.contains("id")) {
          try {
            auto idStr = item["id"].get<std::string>();
            ai::llm::ID id = 0;
            try { id = (ai::llm::ID) std::stoull(idStr); } catch (...) {}
            if (id == 0 && item["id"].is_number()) {
              id = (ai::llm::ID) item["id"].get<int64_t>();
            }
            if (id != 0 && indexById.contains(id)) {
              idx = indexById.at(id);
            }
          } catch (...) {
            // ignore invalid id formats
          }
        }
        if (idx == (size_t)-1 && item.contains("name") && item["name"].is_string()) {
          idx = findByName(item["name"].get<std::string>());
        }
        if (idx == (size_t)-1) {
          throw std::runtime_error("Unknown LoRA adapter reference");
        }
        float scale = 0.0f;
        if (item.contains("scale")) {
          const auto& s = item["scale"];
          if (s.is_number()) {
            scale = (float)s.get<double>();
          } else if (s.is_boolean()) {
            scale = s.get<bool>() ? 1.0f : 0.0f;
          } else if (s.is_null()) {
            scale = 0.0f;
          } else if (s.is_string()) {
            try { scale = std::stof(s.get<std::string>()); } catch (...) { scale = 0.0f; }
          }
        } else {
          scale = 1.0f;
        }
        assignments[idx].second = scale;
      }
    } catch (const std::exception& ex) {
      return send_error_response(400, "invalid_request_error", ex.what(), statusCode, headers, body, routeTag);
    }

    llm.setActiveLoras(modelShared->name, assignments);

    JSON::Array data;
    buildResponse(modelShared->name, data);
    const auto jsonOut = JSON::Object::Entries {{"object", "list"}, {"data", data}};
    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(jsonOut).str());
    statusCode = 200;
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }


  bool LlamaServer::handleModelsV1(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    (void) req;
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    headers.set("content-type", "application/json; charset=utf-8");

    JSON::Array data;
    {
      Lock lock(llm.mutex);
      for (const auto& entry : llm.models) {
        const auto& model = entry.second;
        const auto created = (int64_t) (time(nullptr));
        const JSON::Object meta = model->json();
        JSON::Array caps;
        caps.push("completion");
        data.push(JSON::Object::Entries {
          {"id", model->name},
          {"object", "model"},
          {"owned_by", "oro-runtime"},
          {"created", created},
          {"capabilities", caps},
          {"meta", meta}
        });
      }
    }

    const auto json = JSON::Object::Entries {
      {"object", "list"},
      {"data", data}
    };

    body = bytes::Buffer::from(JSON::Object(json).str());
    statusCode = 200;
    g_route_metrics.recordStatus(this->options.routePrefix + "/v1/models", statusCode);
    return true;
  }

  bool LlamaServer::handleModelListAliases(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    (void) req;
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    headers.set("content-type", "application/json; charset=utf-8");

    JSON::Array ollamaModels;
    JSON::Array data;
    const auto now = (int64_t)(time(nullptr));

    {
      Lock lock(llm.mutex);
      for (const auto& entry : llm.models) {
        const auto& model = entry.second;
        const bool loaded = model && model->loaded();
        JSON::Array capabilities;
        capabilities.push("completion");
        JSON::Array tags;
        tags.push("");
        JSON::Object::Entries details {
          {"parent_model", ""},
          {"format", "gguf"},
          {"family", ""},
          {"families", JSON::Array{}},
          {"parameter_size", ""},
          {"quantization_level", ""}
        };
        ollamaModels.push(JSON::Object::Entries {
          {"name", model->name},
          {"model", model->name},
          {"modified_at", ""},
          {"size", ""},
          {"digest", ""},
          {"type", "model"},
          {"description", ""},
          {"tags", tags},
          {"capabilities", capabilities},
          {"parameters", ""},
          {"details", JSON::Object(details)},
          {"loaded", loaded}
        });

        const JSON::Object meta = model->json();
        data.push(JSON::Object::Entries {
          {"id", model->name},
          {"object", "model"},
          {"created", now},
          {"owned_by", "oro-runtime"},
          {"meta", meta}
        });
      }
    }

    const auto json = JSON::Object::Entries {
      {"models", ollamaModels},
      {"object", "list"},
      {"data", data}
    };

    body = bytes::Buffer::from(JSON::Object(json).str());
    statusCode = 200;
    g_route_metrics.recordStatus(this->options.routePrefix + "/models", statusCode);
    return true;
  }

  bool LlamaServer::handleChatCompletionsV1(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    const String routeTag = this->options.routePrefix + "/v1/chat/completions";
    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->vocab) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "No model loaded"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    json bodyJson;
    const json* bodyPtr = nullptr;
    if (req.body.size() > 0) {
      if (parse_json_body(req, bodyJson)) {
        bodyPtr = &bodyJson;
      }
    }

    const json* messagesJson = nullptr;
    if (bodyPtr != nullptr && bodyPtr->contains("messages") && (*bodyPtr)["messages"].is_array()) {
      messagesJson = &(*bodyPtr)["messages"];
    }

    Vector<ai::chat::Message> chatMessages;
    if (messagesJson != nullptr) {
      if (!parse_messages_json(*messagesJson, chatMessages)) {
        return send_error_response(400, "invalid_request_error", "messages must include string content", statusCode, headers, body, routeTag);
      }
    } else {
      String rawPrompt = req.url.searchParams.get("prompt").str();
      if (rawPrompt.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("prompt") && (*bodyPtr)["prompt"].is_string()) {
        rawPrompt = (*bodyPtr)["prompt"].get<std::string>();
      }
      if (rawPrompt.size() == 0 && req.body.size() > 0 && bodyPtr == nullptr) {
        rawPrompt = req.body.str();
      }

      if (rawPrompt.size() == 0) {
        return send_error_response(400, "invalid_request_error", "Missing 'prompt'", statusCode, headers, body, routeTag);
      }

      ai::chat::Message userMessage;
      userMessage.role = "user";
      userMessage.content = rawPrompt;
      chatMessages.push_back(std::move(userMessage));
    }

    json promptSource;
    if (messagesJson != nullptr) {
      promptSource = *messagesJson;
    } else {
      promptSource = json::array();
      for (const auto& message : chatMessages) {
        promptSource.push_back(json::object({
          {"role", message.role},
          {"content", message.content}
        }));
      }
    }

    String prompt = render_chat_prompt_from_json(model, promptSource);
    if (prompt.size() == 0) {
      // Fall back to concatenated message text if template absent.
      for (const auto& message : chatMessages) {
        if (!prompt.empty()) prompt += '\n';
        prompt += message.content;
      }
    }

    if (prompt.size() == 0) {
      return send_error_response(400, "invalid_request_error", "Prompt resolved to empty string", statusCode, headers, body, routeTag);
    }

    // Safety limits
    const size_t kMaxPromptBytes = this->options.maxPromptBytes; // 128 KiB by default
    if (prompt.size() > kMaxPromptBytes) {
      statusCode = 413;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Prompt too large"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    // Sampling controls (defaults mirror runtime defaults)
    int max_tokens = [&]() {
      auto s = req.url.searchParams.get("max_tokens").str();
      if (s.size() == 0) s = req.url.searchParams.get("n_predict").str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("max_tokens")) {
        try { s = std::to_string((int)(*bodyPtr)["max_tokens"].get<int>()); } catch (...) {}
      }
      if (s.size() == 0) return std::max(1, this->options.defaultMaxTokens);
      try { return std::max(1, std::stoi(s)); } catch (...) { return std::max(1, this->options.defaultMaxTokens); }
    }();
    max_tokens = std::min(max_tokens, this->options.hardMaxTokens);
    const int max_decode_ms = [&]() {
      auto s = req.url.searchParams.get("max_decode_ms").str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("max_decode_ms")) {
        try { s = std::to_string((*bodyPtr)["max_decode_ms"].get<int>()); } catch (...) {}
      }
      if (s.size() == 0) return this->options.defaultMaxDecodeMs;
      try { return std::max(1, std::stoi(s)); } catch (...) { return this->options.defaultMaxDecodeMs; }
    }();

    const float temperature = [&]() {
      auto s = req.url.searchParams.get("temperature").str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("temperature")) {
        try { s = std::to_string((*bodyPtr)["temperature"].get<double>()); } catch (...) {}
      }
      if (s.size() == 0) return this->options.defaultTemperature;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTemperature; }
    }();

    const float top_p = [&]() {
      auto s = req.url.searchParams.get("top_p").str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("top_p")) {
        try { s = std::to_string((*bodyPtr)["top_p"].get<double>()); } catch (...) {}
      }
      if (s.size() == 0) return this->options.defaultTopP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTopP; }
    }();

    const int top_k = [&]() {
      auto s = req.url.searchParams.get("top_k").str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("top_k")) {
        try { s = std::to_string((*bodyPtr)["top_k"].get<int>()); } catch (...) {}
      }
      if (s.size() == 0) return std::max(1, this->options.defaultTopK);
      try { return std::max(1, std::stoi(s)); } catch (...) { return std::max(1, this->options.defaultTopK); }
    }();

    const float min_p = [&]() {
      auto s = req.url.searchParams.get("min_p").str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("min_p")) {
        try { s = std::to_string((*bodyPtr)["min_p"].get<double>()); } catch (...) {}
      }
      if (s.size() == 0) return this->options.defaultMinP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultMinP; }
    }();

    const int seed = [&]() {
      auto s = req.url.searchParams.get("seed").str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains("seed")) {
        try { s = std::to_string((*bodyPtr)["seed"].get<int>()); } catch (...) {}
      }
      if (s.size() == 0) return -1;
      try { return std::stoi(s); } catch (...) { return -1; }
    }();

    // Advanced sampling and context controls (optional)
    auto read_float = [&](const char* key, float fallback) -> float {
      String s = req.url.searchParams.get(key).str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains(key)) {
        const auto& jv = (*bodyPtr)[key];
        if (jv.is_number()) {
          try { return (float)jv.get<double>(); } catch (...) {}
        }
      }
      if (s.size() == 0) return fallback;
      try { return (float)std::stod(s); } catch (...) { return fallback; }
    };

    auto read_int = [&](const char* key, int fallback) -> int {
      String s = req.url.searchParams.get(key).str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains(key)) {
        const auto& jv = (*bodyPtr)[key];
        if (jv.is_number_integer()) {
          try { return (int)jv.get<int64_t>(); } catch (...) {}
        }
      }
      if (s.size() == 0) return fallback;
      try { return std::stoi(s); } catch (...) { return fallback; }
    };

    auto read_bool = [&](const char* key, bool fallback) -> bool {
      String s = req.url.searchParams.get(key).str();
      if (s.size() == 0 && bodyPtr != nullptr && bodyPtr->contains(key)) {
        const auto& jv = (*bodyPtr)[key];
        if (jv.is_boolean()) {
          try { return jv.get<bool>(); } catch (...) {}
        }
        if (jv.is_number_integer()) {
          try { return jv.get<int>() != 0; } catch (...) {}
        }
        if (jv.is_string()) {
          try { s = jv.get<std::string>(); } catch (...) {}
        }
      }
      return parse_bool(s, fallback);
    };

    const float typical_p = read_float("typical_p", 0.0f);
    const float top_n_sigma = read_float("top_n_sigma", 0.0f);
    const float xtc_p = read_float("xtc_p", 0.0f);
    const float xtc_t = read_float("xtc_t", 0.0f);
    const int mirostat_mode = read_int("mirostat", 0);
    const float mirostat_tau = read_float("mirostat_tau", 0.0f);
    const float mirostat_eta = read_float("mirostat_eta", 0.0f);
    const int mirostat_m = read_int("mirostat_m", 0);
    const bool dynamic_temp_enabled = read_bool("dynamic_temperature", false);
    const float dynatemp_delta = read_float("dynatemp_delta", 0.0f);
    const float dynatemp_exponent = read_float("dynatemp_exponent", 0.0f);
    const bool causal_attn = read_bool("causal_attn", true);
    const bool warmup = read_bool("warmup", false);
    // Prompt tokens estimate
    int n_prompt_tokens = -llama_tokenize(model->vocab, prompt.c_str(), (int32_t)prompt.size(), nullptr, 0, true, true);
    if (n_prompt_tokens < 0) n_prompt_tokens = -n_prompt_tokens;

    auto samplingOverrides = parse_sampling_overrides(model, req, bodyPtr, this->options);

    // Tools / function-calling stub for non-stream
      if (bodyPtr != nullptr && bodyPtr->contains("tools")) {
        const auto& j = *bodyPtr;
        bool require_tool = false;
        String toolName = "";

        if (j.contains("tool_choice")) {
          const auto& tc = j["tool_choice"];

          if ((tc.is_string() && tc.get<std::string>() == "required") || tc.is_object()) {
            require_tool = true;
          }

          if (tc.is_string() && tc.get<std::string>() == "none") {
            require_tool = false;
          }
        }

        if (!require_tool && !(j.contains("tool_choice") && j["tool_choice"].is_object())) {
          // no tool selection requested
        } else {
          if (j["tool_choice"].is_object() && j["tool_choice"].contains("function") && j["tool_choice"]["function"].contains("name")) {
            try {
              toolName = j["tool_choice"]["function"]["name"].get<std::string>();
            } catch (...) {}
          }

          if (toolName.size() == 0) {
            for (const auto& t : j["tools"]) {
              try {
                if (t.contains("type") && t["type"].get<std::string>() == "function" && t.contains("function") && t["function"].contains("name")) {
                  toolName = t["function"]["name"].get<std::string>();
                  break;
                }
              } catch (...) {}
            }
          }

          if (toolName.size() > 0) {
            const auto created = (int64_t)(time(nullptr));
          const auto call_id = String("call_") + std::to_string(crypto::rand64());
          JSON::Object::Entries functionJson {{"name", toolName}, {"arguments", "{}"}};
          JSON::Array toolCalls; toolCalls.push(JSON::Object::Entries {{"index", 0}, {"id", call_id}, {"type", "function"}, {"function", functionJson}});
          JSON::Array choices; choices.push(JSON::Object::Entries {{"index", 0}, {"message", JSON::Object::Entries {{"role", "assistant"}, {"content", JSON::Null()}, {"tool_calls", toolCalls}}}, {"finish_reason", "tool_calls"}});
          const auto usage = JSON::Object::Entries {{"prompt_tokens", (int64_t)n_prompt_tokens}, {"completion_tokens", 0}, {"total_tokens", (int64_t)n_prompt_tokens}};
          const auto resp = JSON::Object::Entries {{"id", String("chatcmpl-") + std::to_string(crypto::rand64())}, {"object", "chat.completion"}, {"created", created}, {"model", model->name}, {"choices", choices}, {"usage", usage}};
          headers.set("content-type", "application/json; charset=utf-8");
          body = bytes::Buffer::from(JSON::Object(resp).str());
          statusCode = 200;
          g_route_metrics.recordStatus(routeTag, statusCode);
          return true;
        }
      }
    }

    // Create context (ephemeral)
    ai::llm::Context::Options copt;
    copt.size = std::max<size_t>(2048, (size_t)(n_prompt_tokens + max_tokens + 64));
    if (seed >= 0) {
      copt.dist = (uint32_t)seed;
    }

    auto ctx = llm.acquirePooledContext(llm.models.at(model->name), copt);
    if (ctx == nullptr || ctx->context == nullptr) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Failed to create context"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }

    // Configure sampler via options then rebuild sampler
    ctx->options.temp = temperature;
    ctx->options.topP = top_p;
    ctx->options.topK = top_k;
    ctx->options.minP = min_p;
    ctx->options.penaltyRepeat = samplingOverrides.repeatPenalty;
    ctx->options.penaltyLastN = samplingOverrides.repeatLastN;
    ctx->options.penaltyFreq = samplingOverrides.frequencyPenalty;
    ctx->options.penaltyPresent = samplingOverrides.presencePenalty;
    ctx->options.logitBias = samplingOverrides.logitBias;
    ctx->options.useTypical = (typical_p > 0.0f);
    ctx->options.typicalP = typical_p;
    ctx->options.useTopNSigma = (top_n_sigma > 0.0f);
    ctx->options.topNSigma = top_n_sigma;
    ctx->options.useXTCSampler = (xtc_p > 0.0f && xtc_t > 0.0f);
    ctx->options.xtcP = xtc_p;
    ctx->options.xtcT = xtc_t;
    ctx->options.useMirostat = (mirostat_mode != 0 && mirostat_tau > 0.0f && mirostat_eta > 0.0f && mirostat_m > 0);
    ctx->options.mirostatTau = mirostat_tau;
    ctx->options.mirostatEta = mirostat_eta;
    ctx->options.mirostatM = mirostat_m;
    ctx->options.useDynamicTemperature = (dynamic_temp_enabled && dynatemp_delta != 0.0f);
    ctx->options.tempExtDelta = dynatemp_delta;
    ctx->options.tempExtExponent = dynatemp_exponent;
    ctx->options.enableCausalAttention = causal_attn;
    ctx->options.enableWarmup = warmup;
    (void) ctx->rebuildSampler();

    // Ensure pooled context is released on all return paths
    bool __released_ctx = false;
    auto __release_ctx = [&]() {
      if (!__released_ctx) { llm.releasePooledContext(ctx); __released_ctx = true; }
    };
    struct __ScopeRelease { decltype(__release_ctx)& f; ~__ScopeRelease(){ f(); } } __scope{__release_ctx};

    // Collect stop sequences (query + JSON body)
    auto stops = parse_stop_params(req);
    if (bodyPtr != nullptr) {
      auto js = parse_stop_params_from_json(*bodyPtr);
      for (auto &s : js) {
        stops.push_back(s);
      }
    }
    auto ensureStop = [&](const String& s) {
      if (std::find(stops.begin(), stops.end(), s) == stops.end()) {
        stops.push_back(s);
      }
    };
    ensureStop("<|im_end|>");
    ensureStop("<|eot_id|>");
    ensureStop("<step>");
    ensureStop("</s>");
    if (std::find_if(stops.begin(), stops.end(), [](const String& s){ return s.find("user:") != String::npos || s.find("system:") != String::npos; }) == stops.end()) {
      ensureStop("\nuser:");
      ensureStop("\nsystem:");
      ensureStop("\nassistant:");
      ensureStop("\nUser:");
      ensureStop("\nSystem:");
      ensureStop("\nAssistant:");
    }

    ai::chat::Session session(ctx, {});

    size_t out_tokens = 0;
    Vector<bytes::Buffer> chunks;

    ai::chat::Session::ChatOptions chopt;
    using namespace std::chrono;
    chopt.deadlineAtMs = (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() + (uint64_t)max_decode_ms;
    chopt.antiprompts = stops;

    const bool ok = session.complete(chatMessages, chopt, [&](auto /*id*/, auto buf, auto /*eog*/) mutable {
      if (buf.size() > 0) {
        chunks.push_back(buf);
        out_tokens++;
      }
    });

    if (!ok) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Generation failed"}}}}).str());
      __release_ctx();
      return true;
    }

    auto content = bytes::Buffer::concat(chunks).str();
    String finish_reason = (out_tokens >= (size_t)max_tokens) ? String("length") : String("stop");
    if (stops.size() > 0) {
      size_t cut = String::npos;
      for (const auto& s : stops) {
        auto pos = content.find(s);
        if (pos != String::npos) {
          if (cut == String::npos || pos < cut) cut = pos;
        }
      }
      if (cut != String::npos) {
        content = content.substr(0, cut);
      }
    }
    const Vector<String> roleMarkers = {
      "system:", "user:", "assistant:",
      "System:", "User:", "Assistant:",
      "system ", "user ", "assistant "
    };
    size_t markerCut = String::npos;
    for (const auto& marker : roleMarkers) {
      size_t pos = content.find(marker);
      while (pos != String::npos) {
        bool valid = (pos == 0);
        if (!valid) {
          const char prev = content[pos - 1];
          if (prev == '\n' || prev == '\r' || prev == '#' || prev == ' ' || prev == '\t') {
            valid = true;
          }
        }
        if (valid) {
          if (markerCut == String::npos || pos < markerCut) markerCut = pos;
          break;
        }
        pos = content.find(marker, pos + marker.size());
      }
    }
    if (markerCut != String::npos) {
      content = content.substr(0, markerCut);
      content = string::trimRight(content, " \r\n\t#");
    }

    auto cappedContent = cap_text_to_tokens(model, content, (size_t)max_tokens);
    if (cappedContent.truncated) {
      content = cappedContent.text;
      finish_reason = "length";
    } else {
      content = cappedContent.text;
    }
    out_tokens = cappedContent.tokenCount;

    // Build OpenAI-like response
    const auto created = (int64_t) (time(nullptr));
    JSON::Array choices;
    choices.push(JSON::Object::Entries {
      {"index", 0},
      {"message", JSON::Object::Entries {{"role", "assistant"}, {"content", content}}},
      {"finish_reason", finish_reason}
    });

    const auto usage = JSON::Object::Entries {
      {"prompt_tokens", (int64_t) n_prompt_tokens},
      {"completion_tokens", (int64_t) out_tokens},
      {"total_tokens", (int64_t) (n_prompt_tokens + out_tokens)}
    };

    const auto resp = JSON::Object::Entries {
      {"id", String("chatcmpl-") + std::to_string(crypto::rand64())},
      {"object", "chat.completion"},
      {"created", created},
      {"model", model->name},
      {"choices", choices},
      {"usage", usage}
    };

    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(resp).str());
    statusCode = 200;
    g_route_metrics.recordStatus(routeTag, statusCode);
    // context released by scope guard
    maybe_log_kv_metrics("/v1/chat/completions", &llm);
    return true;
  }

  bool LlamaServer::streamChatCompletionsV1(
    serviceworker::Request& req,
    ai::llm::Manager& llm,
    const Function<bool(const String& event, const String& data, bool finished)>& emit
  ) {
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->vocab) {
      emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "No model loaded"}}}}).str(), true);
      return false;
    }
    maybe_log_kv_metrics("/v1/chat/completions?stream=true", &llm);
    const String routeTag = this->options.routePrefix + "/v1/chat/completions?stream=true";

    String prompt = req.url.searchParams.get("prompt").str();
    if (prompt.size() == 0 && req.body.size() > 0) {
      json j; if (parse_json_body(req, j)) {
        if (j.contains("messages")) {
          const auto p = render_chat_prompt_from_json(model, j["messages"]);
          if (p.size() > 0) prompt = p;
        }
        if (prompt.size() == 0 && j.contains("prompt") && j["prompt"].is_string()) {
          try { prompt = j["prompt"].get<std::string>(); } catch (...) {}
        }
      }
    }
    if (prompt.size() == 0 && req.body.size() > 0) {
      prompt = req.body.str();
    }
    if (prompt.size() == 0) {
      emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Missing 'prompt'"}}}}).str(), true);
      g_route_metrics.recordStatus(routeTag, 400);
      return false;
    }

    const size_t kMaxPromptBytes = this->options.maxPromptBytes; // 128 KiB default
    if (prompt.size() > kMaxPromptBytes) {
      emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Prompt too large"}}}}).str(), true);
      g_route_metrics.recordStatus(routeTag, 413);
      return false;
    }

    int max_tokens = [&]() {
      auto s = req.url.searchParams.get("max_tokens").str();
      if (s.size() == 0) s = req.url.searchParams.get("n_predict").str();
      if (s.size() == 0) {
        if (req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("max_tokens")) { try { s = std::to_string(j["max_tokens"].get<int>()); } catch (...) {} } }
      }
      if (s.size() == 0) return std::max(1, this->options.defaultMaxTokens);
      try { return std::max(1, std::stoi(s)); } catch (...) { return std::max(1, this->options.defaultMaxTokens); }
    }();
    max_tokens = std::min(max_tokens, this->options.hardMaxTokens);

    const float temperature = [&]() {
      auto s = req.url.searchParams.get("temperature").str();
      if (s.size() == 0) {
        if (req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("temperature")) { try { s = std::to_string(j["temperature"].get<double>()); } catch (...) {} } }
      }
      if (s.size() == 0) return this->options.defaultTemperature;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTemperature; }
    }();
    const float top_p = [&]() {
      auto s = req.url.searchParams.get("top_p").str();
      if (s.size() == 0) {
        if (req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("top_p")) { try { s = std::to_string(j["top_p"].get<double>()); } catch (...) {} } }
      }
      if (s.size() == 0) return this->options.defaultTopP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTopP; }
    }();
    const int top_k = [&]() {
      auto s = req.url.searchParams.get("top_k").str();
      if (s.size() == 0) {
        if (req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("top_k")) { try { s = std::to_string(j["top_k"].get<int>()); } catch (...) {} } }
      }
      if (s.size() == 0) return std::max(1, this->options.defaultTopK);
      try { return std::max(1, std::stoi(s)); } catch (...) { return std::max(1, this->options.defaultTopK); }
    }();
    const float min_p = [&]() {
      auto s = req.url.searchParams.get("min_p").str();
      if (s.size() == 0) {
        if (req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("min_p")) { try { s = std::to_string(j["min_p"].get<double>()); } catch (...) {} } }
      }
      if (s.size() == 0) return this->options.defaultMinP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultMinP; }
    }();

    const int seed = [&]() {
      auto s = req.url.searchParams.get("seed").str();
      if (s.size() == 0) {
        if (req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("seed")) { try { s = std::to_string(j["seed"].get<int>()); } catch (...) {} } }
      }
      if (s.size() == 0) return -1;
      try { return std::stoi(s); } catch (...) { return -1; }
    }();

    // Tools / function-calling stub: if request requires a tool, emit a stub call and finish
    json j;
    if (parse_json_body(req, j) && j.contains("tools")) {
      bool require_tool = false;
      String toolName = "";
      if (j.contains("tool_choice")) {
        const auto& tc = j["tool_choice"];
        if ((tc.is_string() && tc.get<std::string>() == "required") || tc.is_object()) {
          require_tool = true;
        }
      }
      if (!require_tool && j.contains("tool_choice")) {
        // explicit none disables tools
        if (j["tool_choice"].is_string() && j["tool_choice"].get<std::string>() == "none") {
          require_tool = false;
        }
      }
      if (require_tool || (j.contains("tool_choice") && j["tool_choice"].is_object())) {
        // pick named tool when specified, else the first function tool
        if (j["tool_choice"].is_object() && j["tool_choice"].contains("function") && j["tool_choice"]["function"].contains("name")) {
          try { toolName = j["tool_choice"]["function"]["name"].get<std::string>(); } catch (...) {}
        }
        if (toolName.size() == 0) {
          for (const auto& t : j["tools"]) {
            try {
              if (t.contains("type") && t["type"].get<std::string>() == "function" && t.contains("function") && t["function"].contains("name")) {
                toolName = t["function"]["name"].get<std::string>();
                break;
              }
            } catch (...) {}
          }
        }
        if (toolName.size() > 0) {
          const auto created = (int64_t)(time(nullptr));
          const auto stream_id = String("chatcmpl-") + std::to_string(crypto::rand64());
          const auto call_id = String("call_") + std::to_string(crypto::rand64());
          JSON::Object::Entries functionJson {{"name", toolName}, {"arguments", "{}"}};
          JSON::Array toolCalls; toolCalls.push(JSON::Object::Entries {{"index", 0}, {"id", call_id}, {"type", "function"}, {"function", functionJson}});
          JSON::Array choices; choices.push(JSON::Object::Entries {{"index", 0}, {"delta", JSON::Object::Entries {{"tool_calls", toolCalls}}}, {"finish_reason", "tool_calls"}});
          const auto chunk = JSON::Object::Entries {{"id", stream_id}, {"object", "chat.completion.chunk"}, {"created", created}, {"model", model->name}, {"choices", choices}};
          emit("message", JSON::Object(chunk).str(), false);
          emit("message", String("[DONE]"), true);
          return true;
        }
      }
    }

    // Create context
    int n_prompt_tokens = -llama_tokenize(model->vocab, prompt.c_str(), (int32_t)prompt.size(), nullptr, 0, true, true);
    if (n_prompt_tokens < 0) n_prompt_tokens = -n_prompt_tokens;

    ai::llm::Context::Options copt;
    copt.size = std::max<size_t>(2048, (size_t)(n_prompt_tokens + max_tokens + 64));
    if (seed >= 0) {
      copt.dist = (uint32_t)seed;
    }
    auto ctx = llm.acquirePooledContext(llm.models.at(model->name), copt);
    if (ctx == nullptr || ctx->context == nullptr) {
      emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Failed to create context"}}}}).str(), true);
      g_route_metrics.recordStatus(routeTag, 500);
      return false;
    }
    ctx->options.temp = temperature;
    ctx->options.topP = top_p;
    ctx->options.topK = top_k;
    ctx->options.minP = min_p;

    // Ensure pooled context is released on all return paths in this function
    bool __released_ctx = false;
    auto __release_ctx = [&]() { if (!__released_ctx) { llm.releasePooledContext(ctx); __released_ctx = true; } };
    struct __ScopeRelease { decltype(__release_ctx)& f; ~__ScopeRelease(){ f(); } } __scope{__release_ctx};

    // Tokenize prompt
    Vector<llama_token> toks((size_t)n_prompt_tokens);
    if (llama_tokenize(model->vocab, prompt.c_str(), (int32_t)prompt.size(), toks.data(), (int32_t)toks.size(), true, true) < 0) {
      emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Tokenization failed"}}}}).str(), true);
      __release_ctx();
      g_route_metrics.recordStatus(routeTag, 500);
      return false;
    }

    llama_batch batch = llama_batch_get_one(toks.data(), (int32_t)toks.size());
    size_t out_tokens = 0;
    const auto created = (int64_t)(time(nullptr));
    const auto stream_id = String("chatcmpl-") + std::to_string(crypto::rand64());
    auto stops = parse_stop_params(req);
    // Merge JSON-provided stop/stop_sequences if present
    do { json j; if (parse_json_body(req, j)) { auto js = parse_stop_params_from_json(j); for (auto &s : js) { stops.push_back(s); } } } while (0);
    String tail; tail.reserve(256);
    size_t max_stop = 0; for (const auto& s : stops) max_stop = std::max(max_stop, s.size());

    auto emit_delta = [&](const String& content, bool finish, const char* finish_reason) -> bool {
      JSON::Array choices;
      JSON::Object::Entries delta;
      if (content.size() > 0) {
        delta["content"] = content;
      } else {
        delta["content"] = "";
      }
      choices.push(JSON::Object::Entries {
        {"index", 0},
        {"delta", delta},
        {"finish_reason", finish_reason ? JSON::Any(finish_reason) : JSON::Null()}
      });
      const auto chunk = JSON::Object::Entries {
        {"id", stream_id},
        {"object", "chat.completion.chunk"},
        {"created", created},
        {"model", model->name},
        {"choices", choices}
      };
      const auto payload = JSON::Object(chunk).str();
      bool ok = emit("message", payload, false);
      if (!ok) return false;
      if (finish) {
        ok = emit("message", String("[DONE]"), true) && ok;
      }
      return ok;
    };

    // Prefill the model with prompt
    if (ctx->used() + (size_t)batch.n_tokens > ctx->size()) {
      emit_delta("", true, "length");
      // context released by scope guard
      g_route_metrics.recordStatus(routeTag, 200);
      return true;
    }
    if (llama_decode(ctx->context, batch)) {
      emit_delta("", true, "stop");
      // context released by scope guard
      g_route_metrics.recordStatus(routeTag, 500);
      return true;
    }
    ctx->usedTokens.fetch_add((size_t)batch.n_tokens, std::memory_order_acq_rel);

    // Generate loop with time budget
    const int max_decode_ms = [&]() {
      auto s = req.url.searchParams.get("max_decode_ms").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("max_decode_ms")) { try { s = std::to_string(j["max_decode_ms"].get<int>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultMaxDecodeMs;
      try { return std::max(1, std::stoi(s)); } catch (...) { return this->options.defaultMaxDecodeMs; }
    }();
    const auto t0 = std::chrono::steady_clock::now();
    llama_token token = 0;
    for (int i = 0; i < max_tokens; ++i) {
      const auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
      if (elapsed >= max_decode_ms) {
        emit_delta("", true, "timeout");
        // context released by scope guard
        g_route_metrics.recordStatus(routeTag, 408);
        return true;
      }
      token = llama_sampler_sample(ctx->sampler, ctx->context, -1);
      if (llama_vocab_is_eog(model->vocab, token)) {
        emit_delta("", true, "stop");
        // context released by scope guard
        g_route_metrics.recordStatus(routeTag, 200);
        return true;
      }
      static thread_local Vector<char> pieceBuf;
      if (pieceBuf.empty()) pieceBuf.resize(256);
      int n = llama_token_to_piece(model->vocab, token, pieceBuf.data(), (int32_t)pieceBuf.size(), 0, true);
      if (n < 0) {
        int tries = 0; size_t cap = pieceBuf.size();
        while (n < 0 && tries++ < 4 && cap < 8192) {
          cap *= 2; pieceBuf.resize(cap);
          n = llama_token_to_piece(model->vocab, token, pieceBuf.data(), (int32_t)pieceBuf.size(), 0, true);
        }
      }
      if (n > 0) {
        String delta(pieceBuf.data(), (size_t)n);
        tail += delta;
        if (max_stop > 0 && tail.size() > max_stop) tail = tail.substr(tail.size() - max_stop);
        if (!emit_delta(delta, false, nullptr)) {
          return false;
        }
        if (max_stop > 0 && tail_has_stop(tail, stops)) {
          emit_delta("", true, "stop");
          __release_ctx();
          g_route_metrics.recordStatus(routeTag, 200);
          return true;
        }
      }
      out_tokens++;
      batch = llama_batch_get_one(&token, 1);
      if (ctx->used() + (size_t)batch.n_tokens > ctx->size()) {
        emit_delta("", true, "length");
        __release_ctx();
        g_route_metrics.recordStatus(routeTag, 200);
        return true;
      }
      if (llama_decode(ctx->context, batch)) {
        emit_delta("", true, "stop");
        __release_ctx();
        g_route_metrics.recordStatus(routeTag, 500);
        return true;
      }
      ctx->usedTokens.fetch_add((size_t)batch.n_tokens, std::memory_order_acq_rel);
    }

    // Reached token limit
    emit_delta("", true, "length");
    __release_ctx();
    g_route_metrics.recordStatus(routeTag, 200);
    maybe_log_kv_metrics("/v1/chat/completions?stream=true", &llm);
    return true;
  }

  bool LlamaServer::streamCompletionsV1(
    serviceworker::Request& req,
    ai::llm::Manager& llm,
    const Function<bool(const String& event, const String& data, bool finished)>& emit
  ) {
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }

    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->vocab) {
      (void) emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "No model loaded"}}}}).str(), true);
      g_route_metrics.recordStatus(this->options.routePrefix + "/v1/completions?stream=true", 400);
      return false;
    }
    maybe_log_kv_metrics("/v1/completions?stream=true", &llm);
    const String routeTag = this->options.routePrefix + "/v1/completions?stream=true";

    String prompt = req.url.searchParams.get("prompt").str();
    if (prompt.size() == 0 && req.body.size() > 0) {
      json j; if (parse_json_body(req, j) && j.contains("prompt") && j["prompt"].is_string()) {
        try { prompt = j["prompt"].get<std::string>(); } catch (...) {}
      }
    }
    if (prompt.size() == 0 && req.body.size() > 0) {
      prompt = req.body.str();
    }
    if (prompt.size() == 0) {
      (void) emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Missing 'prompt'"}}}}).str(), true);
      g_route_metrics.recordStatus(routeTag, 400);
      return false;
    }

    const size_t kMaxPromptBytes = this->options.maxPromptBytes;
    if (prompt.size() > kMaxPromptBytes) {
      (void) emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Prompt too large"}}}}).str(), true);
      g_route_metrics.recordStatus(routeTag, 413);
      return false;
    }

    int max_tokens = [&]() {
      auto s = req.url.searchParams.get("max_tokens").str();
      if (s.size() == 0) s = req.url.searchParams.get("n_predict").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("max_tokens")) { try { s = std::to_string(j["max_tokens"].get<int>()); } catch (...) {} } }
      if (s.size() == 0) return std::max(1, this->options.defaultMaxTokens);
      try { return std::max(1, std::stoi(s)); } catch (...) { return std::max(1, this->options.defaultMaxTokens); }
    }();
    max_tokens = std::min(max_tokens, this->options.hardMaxTokens);

    const float temperature = [&]() {
      auto s = req.url.searchParams.get("temperature").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("temperature")) { try { s = std::to_string(j["temperature"].get<double>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultTemperature;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTemperature; }
    }();

    const float top_p = [&]() {
      auto s = req.url.searchParams.get("top_p").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("top_p")) { try { s = std::to_string(j["top_p"].get<double>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultTopP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTopP; }
    }();

    const int top_k = [&]() {
      auto s = req.url.searchParams.get("top_k").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("top_k")) { try { s = std::to_string(j["top_k"].get<int>()); } catch (...) {} } }
      if (s.size() == 0) return std::max(1, this->options.defaultTopK);
      try { return std::max(1, std::stoi(s)); } catch (...) { return std::max(1, this->options.defaultTopK); }
    }();

    const float min_p = [&]() {
      auto s = req.url.searchParams.get("min_p").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("min_p")) { try { s = std::to_string(j["min_p"].get<double>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultMinP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultMinP; }
    }();

    const int seed = [&]() {
      auto s = req.url.searchParams.get("seed").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("seed")) { try { s = std::to_string(j["seed"].get<int>()); } catch (...) {} } }
      if (s.size() == 0) return -1;
      try { return std::stoi(s); } catch (...) { return -1; }
    }();

    auto read_float = [&](const char* key, float fallback) -> float {
      String s = req.url.searchParams.get(key).str();
      if (s.size() == 0 && req.body.size() > 0) {
        json j;
        if (parse_json_body(req, j) && j.contains(key) && j[key].is_number()) {
          try { return (float)j[key].get<double>(); } catch (...) {}
        }
      }
      if (s.size() == 0) return fallback;
      try { return (float)std::stod(s); } catch (...) { return fallback; }
    };

    auto read_int = [&](const char* key, int fallback) -> int {
      String s = req.url.searchParams.get(key).str();
      if (s.size() == 0 && req.body.size() > 0) {
        json j;
        if (parse_json_body(req, j) && j.contains(key) && j[key].is_number_integer()) {
          try { return (int)j[key].get<int64_t>(); } catch (...) {}
        }
      }
      if (s.size() == 0) return fallback;
      try { return std::stoi(s); } catch (...) { return fallback; }
    };

    auto read_bool = [&](const char* key, bool fallback) -> bool {
      String s = req.url.searchParams.get(key).str();
      if (s.size() == 0 && req.body.size() > 0) {
        json j;
        if (parse_json_body(req, j) && j.contains(key)) {
          const auto& jv = j[key];
          if (jv.is_boolean()) {
            try { return jv.get<bool>(); } catch (...) {}
          }
          if (jv.is_number_integer()) {
            try { return jv.get<int>() != 0; } catch (...) {}
          }
          if (jv.is_string()) {
            try { s = jv.get<std::string>(); } catch (...) {}
          }
        }
      }
      return parse_bool(s, fallback);
    };

    const float typical_p = read_float("typical_p", 0.0f);
    const float top_n_sigma = read_float("top_n_sigma", 0.0f);
    const float xtc_p = read_float("xtc_p", 0.0f);
    const float xtc_t = read_float("xtc_t", 0.0f);
    const int mirostat_mode = read_int("mirostat", 0);
    const float mirostat_tau = read_float("mirostat_tau", 0.0f);
    const float mirostat_eta = read_float("mirostat_eta", 0.0f);
    const int mirostat_m = read_int("mirostat_m", 0);
    const bool dynamic_temp_enabled = read_bool("dynamic_temperature", false);
    const float dynatemp_delta = read_float("dynatemp_delta", 0.0f);
    const float dynatemp_exponent = read_float("dynatemp_exponent", 0.0f);
    const bool causal_attn = read_bool("causal_attn", true);
    const bool warmup = read_bool("warmup", false);

    // Prepare context
    int n_prompt_tokens = -llama_tokenize(model->vocab, prompt.c_str(), (int32_t)prompt.size(), nullptr, 0, true, true);
    if (n_prompt_tokens < 0) n_prompt_tokens = -n_prompt_tokens;
    ai::llm::Context::Options copt; copt.size = std::max<size_t>(2048, (size_t)(n_prompt_tokens + max_tokens + 64));
    if (seed >= 0) {
      copt.dist = (uint32_t)seed;
    }
    auto ctx = llm.acquirePooledContext(llm.models.at(model->name), copt);
    if (!ctx || !ctx->context) {
      (void) emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Failed to create context"}}}}).str(), true);
      g_route_metrics.recordStatus(routeTag, 500);
      return false;
    }
    ctx->options.temp = temperature;
    ctx->options.topP = top_p;
    ctx->options.topK = top_k;
    ctx->options.minP = min_p;
    ctx->options.useTypical = (typical_p > 0.0f);
    ctx->options.typicalP = typical_p;
    ctx->options.useTopNSigma = (top_n_sigma > 0.0f);
    ctx->options.topNSigma = top_n_sigma;
    ctx->options.useXTCSampler = (xtc_p > 0.0f && xtc_t > 0.0f);
    ctx->options.xtcP = xtc_p;
    ctx->options.xtcT = xtc_t;
    ctx->options.useMirostat = (mirostat_mode != 0 && mirostat_tau > 0.0f && mirostat_eta > 0.0f && mirostat_m > 0);
    ctx->options.mirostatTau = mirostat_tau;
    ctx->options.mirostatEta = mirostat_eta;
    ctx->options.mirostatM = mirostat_m;
    ctx->options.useDynamicTemperature = (dynamic_temp_enabled && dynatemp_delta != 0.0f);
    ctx->options.tempExtDelta = dynatemp_delta;
    ctx->options.tempExtExponent = dynatemp_exponent;
    ctx->options.enableCausalAttention = causal_attn;
    ctx->options.enableWarmup = warmup;

    // Tokenize and prefill
    Vector<llama_token> toks((size_t)n_prompt_tokens);
    if (llama_tokenize(model->vocab, prompt.c_str(), (int32_t)prompt.size(), toks.data(), (int32_t)toks.size(), true, true) < 0) {
      (void) emit("message", JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Tokenization failed"}}}}).str(), true);
      // context released by scope guard
      g_route_metrics.recordStatus(routeTag, 500);
      return false;
    }

    llama_batch batch = llama_batch_get_one(toks.data(), (int32_t)toks.size());
    const auto created = (int64_t)(time(nullptr));
    const auto stream_id = String("cmpl-") + std::to_string(crypto::rand64());
    // optional stop sequences
    auto stops = parse_stop_params(req);
    do { json j; if (parse_json_body(req, j)) { auto js = parse_stop_params_from_json(j); for (auto &s : js) { stops.push_back(s); } } } while (0);
    String tail; tail.reserve(256);
    size_t max_stop = 0; for (const auto& s : stops) max_stop = std::max(max_stop, s.size());

    auto emit_delta = [&](const String& content, bool finish, const char* finish_reason) {
      JSON::Array choices;
      JSON::Object::Entries delta;
      if (content.size() > 0) {
        delta["text"] = content;
      } else {
        delta["text"] = "";
      }
      choices.push(JSON::Object::Entries {{"index", 0}, {"text", content}, {"finish_reason", finish_reason ? JSON::Any(finish_reason) : JSON::Null()}});
      const auto chunk = JSON::Object::Entries {{"id", stream_id}, {"object", "text_completion.chunk"}, {"created", created}, {"model", model->name}, {"choices", choices}};
      const auto payload = JSON::Object(chunk).str();
      if (!emit("message", payload, false)) {
        return false;
      }
      if (finish) {
        (void) emit("message", String("[DONE]"), true);
      }
      return true;
    };

    // Scope release guard for streamCompletionsV1
    bool __released_ctx2 = false;
    auto __release_ctx2 = [&]() { if (!__released_ctx2) { llm.releasePooledContext(ctx); __released_ctx2 = true; } };
    struct __ScopeRelease2 { decltype(__release_ctx2)& f; ~__ScopeRelease2(){ f(); } } __scope2{__release_ctx2};

    if (ctx->used() + (size_t)batch.n_tokens > ctx->size()) {
      emit_delta("", true, "length");
      __release_ctx2();
      g_route_metrics.recordStatus(routeTag, 200);
      return true;
    }
    if (llama_decode(ctx->context, batch)) {
      emit_delta("", true, "stop");
      __release_ctx2();
      g_route_metrics.recordStatus(routeTag, 500);
      return true;
    }

    // Generate loop with time budget
    const int max_decode_ms = [&]() {
      auto s = req.url.searchParams.get("max_decode_ms").str();
      if (s.size() == 0 && req.body.size() > 0) {
        json j; if (parse_json_body(req, j) && j.contains("max_decode_ms")) { try { s = std::to_string(j["max_decode_ms"].get<int>()); } catch (...) {} }
      }
      if (s.size() == 0) return this->options.defaultMaxDecodeMs;
      try { return std::max(1, std::stoi(s)); } catch (...) { return this->options.defaultMaxDecodeMs; }
    }();
    const auto t0 = std::chrono::steady_clock::now();
    llama_token token = 0;
    for (int i = 0; i < max_tokens; ++i) {
      const auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
      if (elapsed >= max_decode_ms) {
        emit_delta("", true, "timeout");
        // context released by scope guard
        g_route_metrics.recordStatus(routeTag, 408);
        return true;
      }
      token = llama_sampler_sample(ctx->sampler, ctx->context, -1);
      if (llama_vocab_is_eog(model->vocab, token)) {
        emit_delta("", true, "stop");
        // context released by scope guard
        g_route_metrics.recordStatus(routeTag, 200);
        return true;
      }
      static thread_local Vector<char> pieceBuf2;
      if (pieceBuf2.empty()) pieceBuf2.resize(256);
      int n = llama_token_to_piece(model->vocab, token, pieceBuf2.data(), (int32_t)pieceBuf2.size(), 0, true);
      if (n < 0) {
        int tries = 0; size_t cap = pieceBuf2.size();
        while (n < 0 && tries++ < 4 && cap < 8192) {
          cap *= 2; pieceBuf2.resize(cap);
          n = llama_token_to_piece(model->vocab, token, pieceBuf2.data(), (int32_t)pieceBuf2.size(), 0, true);
        }
      }
      if (n > 0) {
        String delta(pieceBuf2.data(), (size_t)n);
        tail += delta;
        if (max_stop > 0 && tail.size() > max_stop) tail = tail.substr(tail.size() - max_stop);
        if (!emit_delta(delta, false, nullptr)) {
          return false;
        }
        if (max_stop > 0 && tail_has_stop(tail, stops)) {
          emit_delta("", true, "stop");
          g_route_metrics.recordStatus(routeTag, 200);
          __release_ctx2();
          return true;
        }
      }
      batch = llama_batch_get_one(&token, 1);
      if (ctx->used() + (size_t)batch.n_tokens > ctx->size()) {
        emit_delta("", true, "length");
        __release_ctx2();
        g_route_metrics.recordStatus(routeTag, 200);
        return true;
      }
      if (llama_decode(ctx->context, batch)) {
        emit_delta("", true, "stop");
        __release_ctx2();
        g_route_metrics.recordStatus(routeTag, 500);
        return true;
      }
      ctx->usedTokens.fetch_add((size_t)batch.n_tokens, std::memory_order_acq_rel);
    }

    emit_delta("", true, "length");
    __release_ctx2();
    g_route_metrics.recordStatus(routeTag, 200);
    return true;
  }

  bool LlamaServer::handleEmbeddingsV1(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    const String routeTag = this->options.routePrefix + "/v1/embeddings";
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }
    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->vocab) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "No model loaded"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    String input = req.url.searchParams.get("input").str();
    Vector<String> inputs;
    if (req.body.size() > 0) {
      json j;
      if (parse_json_body(req, j) && j.contains("input")) {
        if (j["input"].is_string()) {
          inputs.push_back(j["input"].get<std::string>());
        } else if (j["input"].is_array()) {
          for (const auto &it : j["input"]) {
            if (it.is_string()) {
              inputs.push_back(it.get<std::string>());
            }
          }
        }
      }
    }
    if (inputs.empty()) {
      if (input.size() == 0 && req.body.size() > 0) {
        input = req.body.str();
      }
      if (input.size() > 0) {
        inputs.push_back(input);
      }
    }
    if (inputs.empty()) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Missing 'input'"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    // Total bytes guard across inputs
    size_t totalBytes = 0; for (const auto &s : inputs) totalBytes += s.size();
    if (totalBytes > this->options.embedMaxTotalBytes) {
      statusCode = 413;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Input too large"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    // Cap number of inputs to prevent abuse
    if (inputs.size() > 16) inputs.resize(16);

    // Prepare a context once; we reset between inputs
    ai::llm::Context::Options copt;
    // estimate size by first input length if present
    size_t estTokens = inputs.empty() ? 256 : (size_t)std::max(256, -llama_tokenize(model->vocab, inputs[0].c_str(), (int32_t)inputs[0].size(), nullptr, 0, true, true));
    copt.size = std::max<size_t>(2048, estTokens + 64);
    auto ctx = llm.acquirePooledContext(llm.models.at(model->name), copt);
    if (!ctx || !ctx->context) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Failed to create context"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    // Ensure pooled context release on all paths in embeddings handler
    bool __released_ctx_emb = false;
    auto __release_ctx_emb = [&]() { if (!__released_ctx_emb) { llm.releasePooledContext(ctx); __released_ctx_emb = true; } };
    struct __ScopeReleaseEmb { decltype(__release_ctx_emb)& f; ~__ScopeReleaseEmb(){ f(); } } __scopeEmb{__release_ctx_emb};

    // Enable embeddings output (ensure set after (re)initialization)
    llama_set_embeddings(ctx->context, true);
    JSON::Array data;
    int64_t total_tokens = 0;
    for (size_t idx = 0; idx < inputs.size(); ++idx) {
      const auto &text = inputs[idx];
      const int n_tokens = -llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), nullptr, 0, true, true);
    if (n_tokens <= 0) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Tokenization failed"}}}}).str());
      __release_ctx_emb();
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
      Vector<llama_token> toks((size_t)n_tokens);
      if (llama_tokenize(model->vocab, text.c_str(), (int32_t)text.size(), toks.data(), (int32_t)toks.size(), true, true) < 0) {
        statusCode = 500;
        headers.set("content-type", "application/json; charset=utf-8");
        body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Tokenization failed"}}}}).str());
        __release_ctx_emb();
        g_route_metrics.recordStatus(routeTag, statusCode);
        return true;
      }
      llama_batch batch = llama_batch_get_one(toks.data(), (int32_t)toks.size());
      if (ctx->used() + (size_t)batch.n_tokens > ctx->size()) {
        statusCode = 400;
        headers.set("content-type", "application/json; charset=utf-8");
        body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Context too small"}}}}).str());
        __release_ctx_emb();
        g_route_metrics.recordStatus(routeTag, statusCode);
        return true;
      }
      if (llama_decode(ctx->context, batch)) {
        statusCode = 500;
        headers.set("content-type", "application/json; charset=utf-8");
        body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Decode failed"}}}}).str());
        __release_ctx_emb();
        g_route_metrics.recordStatus(routeTag, statusCode);
        return true;
      }
      ctx->usedTokens.fetch_add((size_t)batch.n_tokens, std::memory_order_acq_rel);
      const int32_t n_embd = llama_model_n_embd(model->model);
      float* emb = llama_get_embeddings_ith(ctx->context, -1);
      if (!emb || n_embd <= 0) {
        statusCode = 500;
        headers.set("content-type", "application/json; charset=utf-8");
        body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Embeddings unavailable"}}}}).str());
        __release_ctx_emb();
        g_route_metrics.recordStatus(routeTag, statusCode);
        return true;
      }
      JSON::Array arr;
      for (int i = 0; i < n_embd; ++i) arr.push((double)emb[i]);
      data.push(JSON::Object::Entries {{"object", "embedding"}, {"index", (int64_t)idx}, {"embedding", arr}});
      total_tokens += (int64_t)n_tokens;
      (void) ctx->reset();
      // After reset, re-enable embeddings flag for next input
      llama_set_embeddings(ctx->context, true);
    }
    const auto usage = JSON::Object::Entries {{"prompt_tokens", total_tokens}, {"total_tokens", total_tokens}};
    const auto resp = JSON::Object::Entries {{"object", "list"}, {"data", data}, {"model", model->name}, {"usage", usage}};
    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(resp).str());
    statusCode = 200;
    // context released by scope guard
    maybe_log_kv_metrics("/v1/embeddings", &llm);
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }

  bool LlamaServer::handleCompletionsV1(
    serviceworker::Request& req,
    int& statusCode,
    http::Headers& headers,
    bytes::Buffer& body,
    ai::llm::Manager& llm
  ) {
    // Treat like chat with a raw prompt, returning choices[].text
    if (!llm.isInitialized.load(std::memory_order_acquire)) {
      llm.init();
    }
    const String routeTag = this->options.routePrefix + "/v1/completions";
    const auto modelName = req.url.searchParams.get("model").str();
    auto* model = select_model(llm, modelName, this->options.defaultModelName);
    if (!model || !model->vocab) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "No model loaded"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    String prompt = req.url.searchParams.get("prompt").str();
    if (prompt.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("prompt") && j["prompt"].is_string()) { prompt = j["prompt"].get<std::string>(); } }
    if (prompt.size() == 0 && req.body.size() > 0) { prompt = req.body.str(); }
    if (prompt.size() == 0) {
      statusCode = 400;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Missing 'prompt'"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    if (prompt.size() > this->options.maxPromptBytes) {
      statusCode = 413;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Prompt too large"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    const int max_tokens = [&]() {
      auto s = req.url.searchParams.get("max_tokens").str();
      if (s.size() == 0) s = req.url.searchParams.get("n_predict").str();
      if (s.size() == 0 && req.body.size() > 0) {
        json j;
        if (parse_json_body(req, j) && j.contains("max_tokens")) {
          try { s = std::to_string(j["max_tokens"].get<int>()); } catch (...) {}
        }
      }
      if (s.size() == 0) return 128;
      try { return std::max(1, std::stoi(s)); } catch (...) { return 128; }
    }();
    int capped_max = std::min(max_tokens, this->options.hardMaxTokens);

    const int max_decode_ms = [&]() {
      auto s = req.url.searchParams.get("max_decode_ms").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("max_decode_ms")) { try { s = std::to_string(j["max_decode_ms"].get<int>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultMaxDecodeMs;
      try { return std::max(1, std::stoi(s)); } catch (...) { return this->options.defaultMaxDecodeMs; }
    }();

    const float temperature = [&]() {
      auto s = req.url.searchParams.get("temperature").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("temperature")) { try { s = std::to_string(j["temperature"].get<double>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultTemperature;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTemperature; }
    }();

    const float top_p = [&]() {
      auto s = req.url.searchParams.get("top_p").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("top_p")) { try { s = std::to_string(j["top_p"].get<double>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultTopP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultTopP; }
    }();

    const int top_k = [&]() {
      auto s = req.url.searchParams.get("top_k").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("top_k")) { try { s = std::to_string(j["top_k"].get<int>()); } catch (...) {} } }
      if (s.size() == 0) return std::max(1, this->options.defaultTopK);
      try { return std::max(1, std::stoi(s)); } catch (...) { return std::max(1, this->options.defaultTopK); }
    }();

    const float min_p = [&]() {
      auto s = req.url.searchParams.get("min_p").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("min_p")) { try { s = std::to_string(j["min_p"].get<double>()); } catch (...) {} } }
      if (s.size() == 0) return this->options.defaultMinP;
      try { return (float)std::stod(s); } catch (...) { return this->options.defaultMinP; }
    }();

    const int seed = [&]() {
      auto s = req.url.searchParams.get("seed").str();
      if (s.size() == 0 && req.body.size() > 0) { json j; if (parse_json_body(req, j) && j.contains("seed")) { try { s = std::to_string(j["seed"].get<int>()); } catch (...) {} } }
      if (s.size() == 0) return -1;
      try { return std::stoi(s); } catch (...) { return -1; }
    }();

    // Context and session
    int n_prompt_tokens = -llama_tokenize(model->vocab, prompt.c_str(), (int32_t)prompt.size(), nullptr, 0, true, true);
    if (n_prompt_tokens < 0) n_prompt_tokens = -n_prompt_tokens;
    ai::llm::Context::Options copt; copt.size = std::max<size_t>(2048, (size_t)(n_prompt_tokens + capped_max + 64));
    if (seed >= 0) {
      copt.dist = (uint32_t)seed;
    }
    if (seed >= 0) {
      copt.dist = (uint32_t)seed;
    }
    auto ctx = llm.acquirePooledContext(llm.models.at(model->name), copt);
    if (!ctx || !ctx->context) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Failed to create context"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    // Ensure pooled context is released on all paths
    bool __released_ctx_txt = false;
    auto __release_ctx_txt = [&]() { if (!__released_ctx_txt) { llm.releasePooledContext(ctx); __released_ctx_txt = true; } };
    struct __ScopeReleaseTxt { decltype(__release_ctx_txt)& f; ~__ScopeReleaseTxt(){ f(); } } __scopeTxt{__release_ctx_txt};

    // apply sampling controls
    ctx->options.temp = temperature;
    ctx->options.topP = top_p;
    ctx->options.topK = top_k;
    ctx->options.minP = min_p;
    (void) ctx->rebuildSampler();
    ai::chat::Session session(ctx, {});
    Vector<bytes::Buffer> chunks;
    size_t out_tokens = 0;
    ai::chat::Session::GenerateOptions gopt;
    using namespace std::chrono;
    gopt.deadlineAtMs = (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() + (uint64_t)max_decode_ms;
    const bool ok = session.generate(prompt, gopt, [&](auto buf, auto eog) mutable {
      if (buf.size() > 0) { chunks.push_back(buf); out_tokens++; }
      (void)eog;
    });
    if (!ok) {
      statusCode = 500;
      headers.set("content-type", "application/json; charset=utf-8");
      body = bytes::Buffer::from(JSON::Object(JSON::Object::Entries {{"error", JSON::Object::Entries {{"message", "Generation failed"}}}}).str());
      g_route_metrics.recordStatus(routeTag, statusCode);
      return true;
    }
    const auto created = (int64_t)(time(nullptr));
    auto text = bytes::Buffer::concat(chunks).str();
    // Optional stop sequences: trim output at earliest stop occurrence
    auto stops = parse_stop_params(req);
    do { json j; if (parse_json_body(req, j)) { auto js = parse_stop_params_from_json(j); for (auto &s : js) { stops.push_back(s); } } } while (0);
    const char* finish_reason_over = nullptr;
    if (!stops.empty()) {
      size_t cut = String::npos;
      for (const auto& s : stops) {
        auto pos = text.find(s);
        if (pos != String::npos) { if (cut == String::npos || pos < cut) cut = pos; }
      }
      if (cut != String::npos) { text = text.substr(0, cut); finish_reason_over = "stop"; }
    }
    auto cappedText = cap_text_to_tokens(model, text, (size_t)capped_max);
    if (cappedText.truncated) {
      text = cappedText.text;
      finish_reason_over = "length";
    } else {
      text = cappedText.text;
    }
    out_tokens = cappedText.tokenCount;
    const uint64_t nowMs3 = (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    const char* finish_reason3 = finish_reason_over ? finish_reason_over : ((nowMs3 >= gopt.deadlineAtMs && gopt.deadlineAtMs > 0) ? "timeout" : "stop");
    JSON::Array choices; choices.push(JSON::Object::Entries {{"index", 0}, {"text", text}, {"finish_reason", finish_reason3}});
    const auto usage = JSON::Object::Entries {{"prompt_tokens", (int64_t)n_prompt_tokens}, {"completion_tokens", (int64_t)out_tokens}, {"total_tokens", (int64_t)(n_prompt_tokens + out_tokens)}};
    const auto resp = JSON::Object::Entries {{"id", String("cmpl-") + std::to_string(crypto::rand64())}, {"object", "text_completion"}, {"created", created}, {"model", model->name}, {"choices", choices}, {"usage", usage}};
    headers.set("content-type", "application/json; charset=utf-8");
    body = bytes::Buffer::from(JSON::Object(resp).str());
    statusCode = 200;
    // context released by scope guard
    maybe_log_kv_metrics("/v1/completions", &llm);
    g_route_metrics.recordStatus(routeTag, statusCode);
    return true;
  }
}
