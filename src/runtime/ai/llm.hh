#ifndef ORO_RUNTIME_AI_LLM_H
#define ORO_RUNTIME_AI_LLM_H

#include <llama.h>

#include "../json.hh"
#include "../bytes.hh"
#include "../crypto.hh"

namespace oro::runtime::ai::llm {
  using types::Atomic;
  using types::Map;
  using types::Mutex;
  using types::Path;
  using types::SharedPointer;
  using types::String;
  using types::Vector;

  // forward
  class Context;

  using ID = uint64_t;

  class Model {
    public:
      struct Options {
        String name = "";
        String directory = "";
        size_t gpuLayerCount = 99;
        ai::llm::ID id = 0;
      };

      Mutex mutex;
      ID id = crypto::rand64();
      const Options options;
      String name = "";
      Path filename;
      Atomic<bool> isLoaded = false;

      // llamacpp
      llama_model_params params;
      llama_model* model = nullptr;
      const llama_vocab* vocab = nullptr;

      Model (const Options&);
      ~Model();
      Model (const Model&) = delete;
      Model (Model&&) = delete;
      Model& operator = (const Model&) = delete;
      Model& operator = (Model&&) = delete;

      virtual bool load ();
      bool loaded () const;
      JSON::Object json () const;
  };

  class LoRA : public std::enable_shared_from_this<LoRA> {
    public:
      struct Options {
        String name = "";
        String directory = "";
        ai::llm::ID id = 0;
      };

      struct AttachOptions {
        float scale = 0.5f;
      };

      Mutex mutex;
      ID id = crypto::rand64();
      const Options options;
      String name = "";
      Path filename;
      Atomic<bool> isLoaded = false;
      SharedPointer<Model> model = nullptr;

      // llamacpp
      llama_adapter_lora* lora = nullptr;

      LoRA (SharedPointer<Model>, const Options&);
      ~LoRA ();

      LoRA (const LoRA&) = delete;
      LoRA (LoRA&&) = delete;
      LoRA& operator = (const LoRA&) = delete;
      LoRA& operator = (LoRA&&) = delete;

      bool attach (SharedPointer<Context>, const AttachOptions&);
      bool detach (SharedPointer<Context>);

      virtual bool load ();
      bool loaded () const;
      JSON::Object json () const;
  };

  class Context {
    public:
      struct Options {
        size_t size = 2048;
        uint32_t dist = LLAMA_DEFAULT_SEED;
        float topP = 0.95;
        float minP = 0.05f;
        float temp = 0.80f;
        int topK = 40;
        ID id = 0;
        int penaltyLastN = 64;
        float penaltyRepeat = 1.0f;
        float penaltyFreq = 0.0f;
        float penaltyPresent = 0.0f;
        Vector<llama_logit_bias> logitBias;
        // Advanced sampling and decoding controls (optional)
        bool useTypical = false;
        float typicalP = 0.0f;
        bool useXTCSampler = false;
        float xtcP = 0.0f;
        float xtcT = 0.0f;
        bool useTopNSigma = false;
        float topNSigma = 0.0f;
        bool useMirostat = false;
        float mirostatTau = 0.0f;
        float mirostatEta = 0.0f;
        int mirostatM = 0;
        bool useDynamicTemperature = false;
        float tempExtDelta = 0.0f;
        float tempExtExponent = 0.0f;
        bool enableCausalAttention = true;
        bool enableWarmup = false;
      };

      Mutex mutex;
      ID id = crypto::rand64();
      SharedPointer<Model> model = nullptr;
      Vector<SharedPointer<LoRA>> loras;
      Options options;

      // llamacpp
      llama_context_params params;
      llama_context* context = nullptr;
      llama_sampler* sampler = nullptr;
      Atomic<size_t> usedTokens = 0; // tracks number of tokens submitted to decode

      Context (SharedPointer<Model>, const Options&);
      ~Context();
      Context (const Context&) = delete;
      Context (Context&&) = delete;
      Context& operator = (const Context&) = delete;
      Context& operator = (Context&&) = delete;

      size_t size () const;
      size_t used () const;
      JSON::Object json () const;
      bool restore (const bytes::Buffer&);
      const bytes::Buffer dump () const;
      // Reset the context for reuse (clear KV cache and counters).
      // Returns true on success.
      bool reset ();

      // Rebuild the sampler chain using current options without
      // resetting the KV cache/context. Returns true on success.
      bool rebuildSampler ();

      // Attempt to clear the llama KV cache in-place if supported by
      // the linked llama runtime. Returns true if cleared, false if
      // unsupported or failed. On false, callers should fall back to
      // full reinitialization.
      bool clearKV ();

    private:
      void configureSamplerChain();
  };

  class Manager {
    public:
      Mutex mutex;
      Atomic<bool> isInitialized = false;
      Map<String, SharedPointer<Model>> models;
      Map<ID, SharedPointer<Context>> contexts;
      Map<ID, SharedPointer<LoRA>> loras;
      Map<String, Vector<std::pair<SharedPointer<LoRA>, float>>> activeLoras;

      Manager () = default;
      Manager (const Manager&) = delete;
      Manager (Manager&&) = delete;
      Manager& operator = (const Manager&) = delete;
      Manager& operator = (Manager&&) = delete;

      bool init ();

      SharedPointer<Model> loadModel (const Model::Options&);
      bool unloadModel (ID);
      bool unloadModel (const String&);

      SharedPointer<LoRA> loadLoRA (ID);
      SharedPointer<LoRA> loadLoRA (SharedPointer<Model>, const LoRA::Options&);
      bool unloadLora (ID);

      SharedPointer<Context> createContext (SharedPointer<Model>, const Context::Options&);
      SharedPointer<Context> getContext (ID);
      bool destroyContext (ID);

      // Simple per-(model,size) context pool for reuse
      Map<String, Vector<SharedPointer<Context>>> contextPool;
      size_t poolCapacity = 2; // small default to limit memory
      std::atomic<uint64_t> poolReused{0};
      std::atomic<uint64_t> poolCreated{0};
      std::atomic<uint64_t> poolDropped{0};

      SharedPointer<Context> acquirePooledContext (SharedPointer<Model>, const Context::Options&);
      void releasePooledContext (SharedPointer<Context>);
      bool removePooledContext (ID);

      // Pre-create and stash N contexts for a given model and size.
      // Count is capped by poolCapacity and existing pool size.
      void prewarm (SharedPointer<Model>, size_t size, size_t count);

      // Drain the pool for a model+size, keeping at least minKeep.
      // Returns the number of contexts dropped.
      size_t drain (SharedPointer<Model>, size_t size, size_t minKeep);

      // Drain all sizes for a model, keeping at least minKeep per size.
      size_t drainAll (SharedPointer<Model>, size_t minKeep);

      Vector<std::pair<SharedPointer<LoRA>, float>> getActiveLoras (const String& modelName);
      void setActiveLoras (const String& modelName, const Vector<std::pair<SharedPointer<LoRA>, float>>& list);
      bool applyActiveLorasToContext (SharedPointer<Context> ctx);
  };

  // Global KV-cache clear telemetry (optional; depends on llama build)
  extern std::atomic<uint64_t> kvClearAttempted;
  extern std::atomic<uint64_t> kvClearSucceeded;
  extern std::atomic<long long> kvClearLastAttemptMs;
  extern std::atomic<long long> kvClearLastSuccessMs;

  // Returns true if llama_kv_cache_clear() symbol is available in the process.
  bool isKVClearAvailable();
}
#endif
