#include "../llm.hh"
#include "../../config.hh"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif
#include <chrono>

namespace oro::runtime::ai::llm {
  // KV-cache telemetry counters (definitions)
  std::atomic<uint64_t> kvClearAttempted{0};
  std::atomic<uint64_t> kvClearSucceeded{0};
  std::atomic<long long> kvClearLastAttemptMs{0};
  std::atomic<long long> kvClearLastSuccessMs{0};
  bool isKVClearAvailable() {
    using fn_t = void (*)(llama_context*);
    static fn_t fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
#if defined(_WIN32)
      HMODULE mod = GetModuleHandleW(nullptr);
      if (mod) fn = reinterpret_cast<fn_t>(GetProcAddress(mod, "llama_kv_cache_clear"));
      if (!fn) {
        HMODULE h = GetModuleHandleW(L"llama.dll");
        if (h) fn = reinterpret_cast<fn_t>(GetProcAddress(h, "llama_kv_cache_clear"));
      }
#else
      fn = reinterpret_cast<fn_t>(dlsym(RTLD_DEFAULT, "llama_kv_cache_clear"));
#endif
      resolved = true;
    }
    return fn != nullptr;
  }
  Context::Context (
    SharedPointer<Model> model,
    const Options& options
  ) : options(options),
      sampler(llama_sampler_chain_init(llama_sampler_chain_default_params())),
      params(llama_context_default_params()),
      model(model) {
    // Apply defaults and config overrides
    this->params.n_ctx = this->options.size;
    this->params.n_batch = this->options.size;
    this->params.n_ubatch = 512;
    do {
      static const auto uc = oro::runtime::config::getUserConfig();
      try {
        if (uc.contains("ai_llm_n_threads") && uc.at("ai_llm_n_threads").size() > 0) {
          this->params.n_threads = std::stoi(uc.at("ai_llm_n_threads"));
        }
      } catch (...) {}
      try {
        if (uc.contains("ai_llm_context_n_ubatch") && uc.at("ai_llm_context_n_ubatch").size() > 0) {
          this->params.n_ubatch = std::stoi(uc.at("ai_llm_context_n_ubatch"));
        }
      } catch (...) {}
      try {
        if (uc.contains("ai_llm_context_n_batch") && uc.at("ai_llm_context_n_batch").size() > 0) {
          this->params.n_batch = std::stoi(uc.at("ai_llm_context_n_batch"));
        }
      } catch (...) {}
      // Per-model overrides: ai_llm_model_<normalizedName>_(n_threads|n_batch|n_ubatch)
      // ini sections [a], [a.b.c] flatten to keys a_b and a_b_c_d = value
      // Normalize model names by replacing non-alnum with '_'
      if (this->model && this->model->name.size() > 0) {
        const auto n = oro::runtime::config::normalizeKeySegment(this->model->name);
        const auto prefix = String("ai_llm_model_") + n + String("_");
        try {
          const auto key = prefix + String("n_threads");
          if (uc.contains(key) && uc.at(key).size() > 0) {
            this->params.n_threads = std::stoi(uc.at(key));
          }
        } catch (...) {}
        try {
          const auto key = prefix + String("n_batch");
          if (uc.contains(key) && uc.at(key).size() > 0) {
            this->params.n_batch = std::stoi(uc.at(key));
          }
        } catch (...) {}
        try {
          const auto key = prefix + String("n_ubatch");
          if (uc.contains(key) && uc.at(key).size() > 0) {
            this->params.n_ubatch = std::stoi(uc.at(key));
          }
        } catch (...) {}
      }
    } while (0);

    if (this->model != nullptr) {
      this->context = llama_init_from_model(
        this->model->model,
        this->params
      );
      if (this->context != nullptr) {
        // Optional context-level toggles exposed by newer llama.cpp APIs
        llama_set_causal_attn(this->context, this->options.enableCausalAttention);
        llama_set_warmup(this->context, this->options.enableWarmup);
      }
    }

    this->configureSamplerChain();

    if (options.id > 0) {
      this->id = options.id;
    }
  }

  Context::~Context () {
    if (this->sampler) {
      llama_sampler_free(this->sampler);
      this->sampler = nullptr;
    }

    if (this->context) {
      llama_clear_adapter_lora(this->context);
      llama_free(this->context);
      this->context = nullptr;
    }
  }

  size_t Context::size () const {
    if (this->context != nullptr) {
      return llama_n_ctx(this->context);
    }

    return 0;
  }

  size_t Context::used () const {
    return this->usedTokens.load(std::memory_order_acquire);
  }

  JSON::Object Context::json () const {
    return JSON::Object::Entries {
      {"id", std::to_string(this->id)},
      {"size", this->size()},
      {"used", this->used()},
      {"model", this->model->json()},
      {"options", JSON::Object::Entries {
        {"size", this->options.size},
        {"minP", this->options.minP},
        {"temp", this->options.temp},
        {"topK", this->options.topK},
        {"topP", this->options.topP}
      }}
    };
  }

  bool Context::restore (const bytes::Buffer& buffer) {
    if (this->context == nullptr) {
      return false;
    }

    if (llama_state_set_data(this->context, buffer.data(), buffer.size()) > 0) {
      // mark context as non-empty so we don't re-insert BOS
      this->usedTokens.store(1, std::memory_order_release);
      return true;
    }

    return false;
  }

  const bytes::Buffer Context::dump () const {
    if (this->context == nullptr) {
      return bytes::Buffer(0);
    }

    const auto size = llama_state_get_size(this->context);
    auto data = std::make_shared<uint8_t[]>(size);
    llama_state_get_data(this->context, data.get(), size);
    return bytes::Buffer::from(data.get(), size);
  }

  bool Context::reset () {
    if (this->context == nullptr) {
      return false;
    }
    // Prefer clearing KV cache in-place when supported to avoid
    // reinitialization overhead. If unavailable, fall back to
    // fully reinitialize context and sampler.
    if (this->clearKV()) {
      this->usedTokens.store(0, std::memory_order_release);
      return true;
    }

    // Fallback: fully reinitialize context and sampler
    if (this->sampler) {
      llama_sampler_free(this->sampler);
      this->sampler = nullptr;
    }
    if (this->context) {
      llama_clear_adapter_lora(this->context);
      llama_free(this->context);
      this->context = nullptr;
    }
    // Preserve previous threading/batching preferences when reinitializing
    const int prev_threads = this->params.n_threads;
    const int prev_batch   = this->params.n_batch;
    const int prev_ubatch  = this->params.n_ubatch;
    this->params = llama_context_default_params();
    this->params.n_ctx   = this->options.size;
    this->params.n_batch = prev_batch > 0 ? prev_batch : (int)this->options.size;
    this->params.n_ubatch = prev_ubatch > 0 ? prev_ubatch : 512;
    if (prev_threads > 0) this->params.n_threads = prev_threads;
    if (this->model != nullptr) {
      this->context = llama_init_from_model(this->model->model, this->params);
      if (this->context != nullptr) {
        llama_set_causal_attn(this->context, this->options.enableCausalAttention);
        llama_set_warmup(this->context, this->options.enableWarmup);
      }
    }
    this->sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    this->configureSamplerChain();
    this->usedTokens.store(0, std::memory_order_release);
    return this->context != nullptr && this->sampler != nullptr;
  }

  bool Context::rebuildSampler () {
    if (this->context == nullptr) return false;
    if (this->sampler) {
      llama_sampler_free(this->sampler);
      this->sampler = nullptr;
    }
    this->sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (this->sampler == nullptr) return false;
    this->configureSamplerChain();
    return true;
  }

  // Runtime-resolved KV cache clear; avoids compile/link dependency
  // on a specific llama.cpp version.
  bool Context::clearKV () {
    if (this->context == nullptr) return false;

    using fn_t = void (*)(llama_context*);
    static fn_t fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
#if defined(_WIN32)
      // Try main module first (statically linked) then common DLL names
      HMODULE mod = GetModuleHandleW(nullptr);
      if (mod) fn = reinterpret_cast<fn_t>(GetProcAddress(mod, "llama_kv_cache_clear"));
      if (!fn) {
        HMODULE h = GetModuleHandleW(L"llama.dll");
        if (h) fn = reinterpret_cast<fn_t>(GetProcAddress(h, "llama_kv_cache_clear"));
      }
#else
      // Search the global namespace for dynamically linked symbols
      fn = reinterpret_cast<fn_t>(dlsym(RTLD_DEFAULT, "llama_kv_cache_clear"));
#endif
      resolved = true;
    }
    // Telemetry: record attempt timestamp
    kvClearAttempted.fetch_add(1);
    kvClearLastAttemptMs.store(
      (long long) std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
      ).count(),
      std::memory_order_relaxed
    );
    if (!fn) return false;
    // Attempt KV-cache clear
    fn(this->context);
    kvClearSucceeded.fetch_add(1);
    kvClearLastSuccessMs.store(
      (long long) std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
      ).count(),
      std::memory_order_relaxed
    );
    return true;
  }

  void Context::configureSamplerChain() {
    if (!this->sampler) return;

    bool hasSelector = false;

    if (options.minP > 0.0f) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_min_p(options.minP, 1));
    }

    if (options.penaltyRepeat != 1.0f || options.penaltyFreq != 0.0f || options.penaltyPresent != 0.0f) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_penalties(
        options.penaltyLastN,
        options.penaltyRepeat,
        options.penaltyFreq,
        options.penaltyPresent
      ));
    }

    if (!options.logitBias.empty() && this->model && this->model->vocab) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_logit_bias(
        llama_vocab_n_tokens(this->model->vocab),
        (int32_t)options.logitBias.size(),
        options.logitBias.data()
      ));
    }

    if (options.temp != 0) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_temp(options.temp));
    }

    if (options.topK > 0) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_top_k(options.topK));
    }

    if (options.topP > 0.0f) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_top_p(options.topP, 1));
    }

    if (options.dist > 0) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_dist(options.dist));
      hasSelector = true;
    }

    // Advanced samplers available in newer llama.cpp versions; all are optional
    if (options.useTypical && options.typicalP > 0.0f) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_typical(options.typicalP, 1));
    }

    if (options.useTopNSigma && options.topNSigma > 0.0f) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_top_n_sigma(options.topNSigma));
    }

    if (options.useXTCSampler && options.xtcP > 0.0f && options.xtcT > 0.0f) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_xtc(options.xtcP, options.xtcT, 1, options.dist));
    }

    if (options.useDynamicTemperature && options.temp > 0.0f && options.tempExtDelta != 0.0f) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_temp_ext(options.temp, options.tempExtDelta, options.tempExtExponent != 0.0f ? options.tempExtExponent : 1.0f));
    }

    if (options.useMirostat && this->model && this->model->vocab && options.mirostatTau > 0.0f && options.mirostatEta > 0.0f && options.mirostatM > 0) {
      const int32_t n_vocab = llama_vocab_n_tokens(this->model->vocab);
      if (n_vocab > 0) {
        llama_sampler_chain_add(this->sampler, llama_sampler_init_mirostat(
          n_vocab,
          options.dist,
          options.mirostatTau,
          options.mirostatEta,
          options.mirostatM
        ));
        hasSelector = true;
      }
    }

    if (!hasSelector) {
      llama_sampler_chain_add(this->sampler, llama_sampler_init_greedy());
    }
  }
}
