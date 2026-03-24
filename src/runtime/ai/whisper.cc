#include "whisper.hh"

#include "../debug.hh"
#include "../config.hh"
#include "../env.hh"
#include "../cwd.hh"

#include <chrono>
#include <cmath>
#include <thread>

using oro::runtime::config::getUserConfig;

namespace oro::runtime::ai::whisper {
  namespace {
    static size_t hardware_thread_count() {
      auto count = std::thread::hardware_concurrency();
      if (count == 0) return 4;
      return static_cast<size_t>(count);
    }

    static Path joinIfRelative(const Path& base, const String& name) {
      if (name.empty()) return {};
      Path candidate(name);
      if (candidate.is_absolute()) return candidate;
      if (base.empty()) return candidate;
      return base / candidate;
    }
  }

  Model::Model(const Options& opts)
    : name(opts.name),
      options(opts),
      ctxParams(whisper_context_default_params()) {
    ctxParams.use_gpu = opts.useGPU;
    ctxParams.gpu_device = opts.gpuDevice;
  }

  Model::~Model() {
    Lock lock(this->mutex);
    for (auto* state : this->statePool) {
      if (state != nullptr) {
        whisper_free_state(state);
      }
    }
    this->statePool.clear();
    if (this->ctx != nullptr) {
      whisper_free(this->ctx);
      this->ctx = nullptr;
    }
  }

  bool Model::tryResolveFilename(const Path& candidate) {
    if (candidate.empty()) return false;
    if (!filesystem::Resource::isFile(candidate)) return false;

    const auto resolved = filesystem::Resource::resolve(candidate);
    if (resolved.empty() || !filesystem::Resource::isFile(resolved)) {
      return false;
    }

    auto* ctxCandidate = whisper_init_from_file_with_params(resolved.string().c_str(), this->ctxParams);
    if (ctxCandidate == nullptr) {
      debug("whisper: failed to load model '%s'", resolved.string().c_str());
      return false;
    }

    this->ctx = ctxCandidate;
    this->filename = resolved.string();
    return true;
  }

  bool Model::load() {
    Lock lock(this->mutex);

    if (this->ctx != nullptr) {
      return true;
    }

    if (this->name.empty()) {
      return false;
    }

    // direct path
    if (filesystem::Resource::isFile(this->name)) {
      if (this->tryResolveFilename(this->name)) {
        return true;
      }
    }

    // options directory
    if (!this->options.directory.empty()) {
      if (this->tryResolveFilename(joinIfRelative(this->options.directory, this->name))) {
        return true;
      }
    }

    // env override
    do {
      auto envPath = env::get("ORO_AI_WHISPER_MODEL_PATH");
      if (envPath.empty()) {
        envPath = env::get("ORO_RUNTIME_AI_WHISPER_MODEL_PATH");
      }
      if (!envPath.empty()) {
        if (this->tryResolveFilename(Path(envPath) / this->name)) {
          return true;
        }
      }
    } while (0);

    // config override
    do {
      static const auto uc = getUserConfig();
      try {
        if (uc.contains("ai_whisper_model_path") && uc.at("ai_whisper_model_path").size() > 0) {
          const auto configured = uc.at("ai_whisper_model_path");
          if (this->tryResolveFilename(Path(configured) / this->name)) {
            return true;
          }
        }
      } catch (...) {}
    } while (0);

    // relative to cwd
    if (this->tryResolveFilename(Path(getcwd()) / this->name)) {
      return true;
    }

    // search bundled resources
    do {
      auto resource = filesystem::Resource::resolve(this->name);
      if (!resource.empty() && filesystem::Resource::isFile(resource)) {
        if (this->tryResolveFilename(resource)) {
          return true;
        }
      }
    } while (0);

    return false;
  }

  bool Model::loaded() const {
    return this->ctx != nullptr;
  }

  JSON::Object Model::json() const {
    return JSON::Object::Entries {
      {"id", std::to_string(this->id)},
      {"name", this->name},
      {"loaded", this->loaded()},
      {"filename", this->filename},
      {"options", JSON::Object::Entries {
        {"directory", this->options.directory},
        {"threadCount", this->options.threadCount},
        {"statePoolLimit", this->options.statePoolLimit},
        {"useGPU", this->options.useGPU},
        {"gpuDevice", this->options.gpuDevice}
      }}
    };
  }

  whisper_context* Model::context() const {
    return this->ctx;
  }

  whisper_state* Model::acquireState() {
    Lock lock(this->mutex);
    whisper_state* state = nullptr;
    if (!this->statePool.empty()) {
      state = this->statePool.back();
      this->statePool.pop_back();
    }
    if (state == nullptr && this->ctx != nullptr) {
      state = whisper_init_state(this->ctx);
    }
    if (state != nullptr) {
      this->activeStates.fetch_add(1, std::memory_order_relaxed);
    }
    return state;
  }

  void Model::releaseState(whisper_state* state) {
    if (state == nullptr) return;
    this->activeStates.fetch_sub(1, std::memory_order_relaxed);
    Lock lock(this->mutex);
    if (this->statePool.size() < this->options.statePoolLimit) {
      this->statePool.push_back(state);
    } else {
      whisper_free_state(state);
    }
  }

  Manager::Manager()
    : queue(concurrent::WorkerQueue::Options { .limit = 4 })
  {}

  Manager::~Manager() {
    this->queue.destroy();
    Lock lock(this->mutex);
    this->modelsById.clear();
    this->modelsByName.clear();
  }

  void Manager::setQueueLimit(size_t limit) {
    if (limit == 0) {
      limit = 1;
    }
    this->queue.limit.store(limit, std::memory_order_relaxed);
  }

  size_t Manager::queueLimit() const {
    return this->queue.limit.load(std::memory_order_relaxed);
  }

  SharedPointer<Model> Manager::loadModel(const Model::Options& options) {
    auto model = this->getModel(options.name);
    if (model && model->loaded()) {
      return model;
    }

    auto next = std::make_shared<Model>(options);
    if (!next->load()) {
      debug("whisper: failed to load model '%s'", options.name.c_str());
      return nullptr;
    }

    {
      Lock lock(this->mutex);
      this->modelsByName.insert_or_assign(next->name, next);
      this->modelsById.insert_or_assign(next->id, next);
    }

    return next;
  }

  SharedPointer<Model> Manager::getModel(const String& name) {
    if (name.empty()) return nullptr;
    Lock lock(this->mutex);
    if (this->modelsByName.contains(name)) {
      return this->modelsByName.at(name);
    }
    return nullptr;
  }

  SharedPointer<Model> Manager::getModel(ID id) {
    if (id == 0) return nullptr;
    Lock lock(this->mutex);
    if (this->modelsById.contains(id)) {
      return this->modelsById.at(id);
    }
    return nullptr;
  }

  bool Manager::unloadModel(const String& name) {
    if (name.empty()) return false;
    Lock lock(this->mutex);
    if (!this->modelsByName.contains(name)) {
      return false;
    }
    auto model = this->modelsByName.at(name);
    this->modelsByName.erase(name);
    if (model) {
      this->modelsById.erase(model->id);
    }
    return true;
  }

  bool Manager::unloadModel(ID id) {
    if (id == 0) return false;
    Lock lock(this->mutex);
    if (!this->modelsById.contains(id)) {
      return false;
    }
    auto model = this->modelsById.at(id);
    this->modelsById.erase(id);
    if (model) {
      this->modelsByName.erase(model->name);
    }
    return true;
  }

  Vector<JSON::Object> Manager::listModels() {
    Vector<JSON::Object> list;
    Lock lock(this->mutex);
    for (const auto& entry : this->modelsByName) {
      if (entry.second) {
        list.push_back(entry.second->json());
      }
    }
    return list;
  }

  void Manager::transcribe(SharedPointer<Model> model, const TranscribeOptions& options, const TaskCallback& callback, const ProgressCallback& progress) {
    if (!model || !model->loaded()) {
      TaskResult result;
      result.ok = false;
      result.error = "Model not loaded";
      callback(result);
      return;
    }

    if (options.pcmf32.empty()) {
      TaskResult result;
      result.ok = false;
      result.error = "Audio buffer empty";
      callback(result);
      return;
    }

    auto sharedResult = std::make_shared<TaskResult>();
    auto sharedModel = model;
    auto opts = options;
    auto progressCopy = progress;

    this->queue.push(
      [sharedResult, sharedModel, opts, progressCopy]() {
        auto state = sharedModel->acquireState();
        if (state == nullptr) {
          sharedResult->ok = false;
          sharedResult->error = "Unable to allocate whisper state";
          return;
        }

        struct StateGuard {
          SharedPointer<Model> model;
          whisper_state* state = nullptr;
          ~StateGuard() {
            if (model && state) {
              model->releaseState(state);
            }
          }
        } guard { sharedModel, state };

        auto params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
        const size_t threadCount = opts.threadCount > 0
          ? opts.threadCount
          : (sharedModel->options.threadCount > 0 ? sharedModel->options.threadCount : hardware_thread_count());

        params.n_threads = static_cast<int>(threadCount);
        params.translate = opts.translate;
        params.detect_language = opts.detectLanguage && (opts.language.empty());
        params.language = opts.language.empty() ? nullptr : opts.language.c_str();
        params.no_timestamps = !opts.enableTimestamps;
        params.token_timestamps = opts.enableWordTimestamps;
        params.tdrz_enable = opts.diarize;
        params.temperature = opts.temperature > 0.0f ? opts.temperature : 0.0f;
        params.temperature_inc = opts.temperatureIncrement;
        params.entropy_thold = opts.entropyThreshold;
        params.logprob_thold = opts.logProbThreshold;
        params.no_speech_thold = opts.noSpeechThreshold;
        params.print_progress = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        params.max_len = opts.maxSegmentLength;
        params.initial_prompt = nullptr;
        params.prompt_tokens = nullptr;
        params.prompt_n_tokens = 0;

        struct ProgressContext {
          const ProgressCallback* callback = nullptr;
        } progressContext { progressCopy ? &progressCopy : nullptr };

        if (progressCopy) {
          params.new_segment_callback = [](struct whisper_context* ctx, struct whisper_state* state, int n_new, void* user_data) {
            auto* context = static_cast<ProgressContext*>(user_data);
            if (context == nullptr || context->callback == nullptr) return;
            const auto& cb = *context->callback;
            if (!cb) return;

            const int total = whisper_full_n_segments_from_state(state);
            const int first = total - n_new;
            for (int i = first; i < total; ++i) {
              Segment segment;
              segment.index = i;
              segment.start = static_cast<float>(whisper_full_get_segment_t0_from_state(state, i)) * 0.01f;
              segment.end = static_cast<float>(whisper_full_get_segment_t1_from_state(state, i)) * 0.01f;
              if (const char* text = whisper_full_get_segment_text_from_state(state, i)) {
                segment.text = text;
              }

              const int tokens = whisper_full_n_tokens_from_state(state, i);
              if (tokens > 0) {
                double sum = 0.0;
                for (int t = 0; t < tokens; ++t) {
                  sum += whisper_full_get_token_p_from_state(state, i, t);
                }
                segment.tokenProbability = static_cast<float>(sum / tokens);
                // DTW-based alignment timing when available (use first token)
                whisper_token_data td = whisper_full_get_token_data_from_state(state, i, 0);
                if (td.t_dtw > 0) {
                  segment.startDTW = static_cast<float>(td.t_dtw) * 0.001f;
                }
              }

              cb(segment);
            }
          };
          params.new_segment_callback_user_data = &progressContext;
        }

        // Optional VAD: when enabled and a model path is provided, let whisper.cpp
        // derive speech segments and restrict transcription to those regions.
        whisper_vad_context* vadCtx = nullptr;
        whisper_vad_segments* vadSegments = nullptr;
        if (opts.enableVAD && !opts.vadModelPath.empty()) {
          auto vadCtxParams = whisper_vad_default_context_params();
          vadCtxParams.n_threads = (int)threadCount;
          vadCtxParams.use_gpu = sharedModel->options.useGPU;
          vadCtxParams.gpu_device = sharedModel->options.gpuDevice;
          vadCtx = whisper_vad_init_from_file_with_params(opts.vadModelPath.c_str(), vadCtxParams);
          if (vadCtx != nullptr) {
            auto vadParams = whisper_vad_default_params();
            vadSegments = whisper_vad_segments_from_samples(
              vadCtx,
              vadParams,
              opts.pcmf32.data(),
              (int)opts.pcmf32.size()
            );
          }
        }

        const auto start = std::chrono::steady_clock::now();
        const int rc = whisper_full_with_state(
          sharedModel->context(),
          state,
          params,
          opts.pcmf32.data(),
          static_cast<int>(opts.pcmf32.size())
        );
        const auto end = std::chrono::steady_clock::now();

        if (rc != 0) {
          if (vadSegments != nullptr) {
            whisper_vad_free_segments(vadSegments);
            vadSegments = nullptr;
          }
          if (vadCtx != nullptr) {
            whisper_vad_free(vadCtx);
            vadCtx = nullptr;
          }
          sharedResult->ok = false;
          sharedResult->error = "Whisper inference failed";
          return;
        }

        Result transcript;
        transcript.processingMs = std::chrono::duration<double, std::milli>(end - start).count();
        transcript.audioMs = (static_cast<double>(opts.pcmf32.size()) / static_cast<double>(WHISPER_SAMPLE_RATE)) * 1000.0;
        transcript.sourceSampleRate = opts.inputSampleRate;
        transcript.inputSamples = opts.inputSamples;
        transcript.outputSamples = opts.pcmf32.size();
        transcript.resampled = opts.resampled;
        transcript.normalized = opts.normalized;
        transcript.usedVAD = opts.enableVAD && !opts.vadModelPath.empty();

        if (vadSegments != nullptr) {
          const int nVadSegs = whisper_vad_segments_n_segments(vadSegments);
          if (nVadSegs > 0) {
            transcript.vadSegments.reserve((size_t)nVadSegs);
            for (int i = 0; i < nVadSegs; ++i) {
              VADSegment seg;
              seg.start = whisper_vad_segments_get_segment_t0(vadSegments, i);
              seg.end = whisper_vad_segments_get_segment_t1(vadSegments, i);
              transcript.vadSegments.push_back(seg);
            }
          }
          whisper_vad_free_segments(vadSegments);
          vadSegments = nullptr;
        }
        if (vadCtx != nullptr) {
          whisper_vad_free(vadCtx);
          vadCtx = nullptr;
        }

        if (params.detect_language || opts.language.empty()) {
          const int langId = whisper_full_lang_id_from_state(state);
          const char* lang = whisper_lang_str(langId);
          if (lang != nullptr) {
            transcript.language = lang;
          }
        } else {
          transcript.language = opts.language;
        }

        const int nSegments = whisper_full_n_segments_from_state(state);
        transcript.segments.reserve(static_cast<size_t>(nSegments));
        String aggregated;
        for (int i = 0; i < nSegments; ++i) {
          Segment segment;
          segment.index = i;
          segment.start = static_cast<float>(whisper_full_get_segment_t0_from_state(state, i)) * 0.01f;
          segment.end = static_cast<float>(whisper_full_get_segment_t1_from_state(state, i)) * 0.01f;
          const char* text = whisper_full_get_segment_text_from_state(state, i);
          if (text != nullptr) {
            segment.text = text;
            aggregated += segment.text;
          }

          const int tokens = whisper_full_n_tokens_from_state(state, i);
          if (tokens > 0) {
            double sum = 0.0;
            for (int t = 0; t < tokens; ++t) {
              sum += whisper_full_get_token_p_from_state(state, i, t);
            }
            segment.tokenProbability = static_cast<float>(sum / tokens);
            // Use the first token’s DTW-based timestamp if available
            whisper_token_data td = whisper_full_get_token_data_from_state(state, i, 0);
            if (td.t_dtw > 0) {
              segment.startDTW = static_cast<float>(td.t_dtw) * 0.001f;
            }
          }

          transcript.segments.push_back(std::move(segment));
        }
        transcript.text = aggregated;

        sharedResult->ok = true;
        sharedResult->result = std::move(transcript);
      },
      [callback, sharedResult]() {
        callback(*sharedResult);
      }
    );
  }
}
