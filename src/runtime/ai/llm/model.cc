#include "../../filesystem.hh"
#include "../../config.hh"
#include "../../env.hh"
#include "../../cwd.hh"
#include "../llm.hh"

using oro::runtime::config::getUserConfig;

namespace oro::runtime::ai::llm {

  static bool is_clip_or_mmproj_candidate(const Path& path) {
    // Heuristic: CLIP/mmproj files are not valid main LLaMA models.
    // Avoid attempting to load filenames ending in ".mmproj.gguf" as the
    // primary model to prevent llama.cpp from emitting CLIP-specific errors.
    const auto s = path.string();
    const std::string suffix = ".mmproj.gguf";
    if (s.size() >= suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return true;
    }
    return false;
  }
  Model::Model (const Options& options)
    : name(options.name),
      params(llama_model_default_params()),
      options(options) {
    // Apply options and config overrides
    this->params.n_gpu_layers = this->options.gpuLayerCount;
    do {
      static const auto uc = getUserConfig();
      try {
        if (uc.contains("ai_llm_gpu_layer_count") && uc.at("ai_llm_gpu_layer_count").size() > 0) {
          this->params.n_gpu_layers = std::stoi(uc.at("ai_llm_gpu_layer_count"));
        }
      } catch (...) {}
    } while (0);
  }

  Model::~Model() {
    if (this->model) {
      llama_model_free(this->model);
      this->model = nullptr;
    }
  }

  bool Model::load () {
    Lock lock(this->mutex);

    if (this->isLoaded.load(std::memory_order_acquire)) {
      return true;
    }

    if (this->name.size() == 0) {
      return false;
    }

    if (this->model == nullptr) {
      if (filesystem::Resource::isFile(this->name)) {
        Path candidate(this->name);
        if (!is_clip_or_mmproj_candidate(candidate)) {
          this->model = llama_model_load_from_file(this->name.c_str(), this->params);
          this->filename = this->name;
        }
      }
    }

    if (this->model == nullptr) {
      auto rootModelDirectory = env::get("ORO_AI_LLM_MODEL_PATH");
      if (rootModelDirectory.size() == 0) {
        rootModelDirectory = env::get("ORO_RUNTIME_AI_LLM_MODEL_PATH");
      }
      if (rootModelDirectory.size() > 0) {
        if (filesystem::Resource::isDirectory(rootModelDirectory)) {
          const auto filename = Path(rootModelDirectory) / this->name;
          if (filesystem::Resource::isFile(filename) && !is_clip_or_mmproj_candidate(filename)) {
            this->model = llama_model_load_from_file(filename.string().c_str(), this->params);
            this->filename = filename;
          }
        }
      }
    }

    if (this->model == nullptr && this->options.directory.size() > 0) {
      if (filesystem::Resource::isDirectory(this->options.directory)) {
        const auto filename = Path(this->options.directory) / this->name;
        if (filesystem::Resource::isFile(filename) && !is_clip_or_mmproj_candidate(filename)) {
          this->model = llama_model_load_from_file(filename.string().c_str(), this->params);
          this->filename = filename;
        }
      }
    }

    if (this->model == nullptr) {
      static auto userConfig = getUserConfig();
      if (!userConfig["ai_llm_model_path"].empty()) {
        const auto directory = userConfig["ai_llm_model_path"];
        const auto filename = Path(directory) / this->name;
        if (filesystem::Resource::isFile(filename) && !is_clip_or_mmproj_candidate(filename)) {
          this->model = llama_model_load_from_file(filename.string().c_str(), this->params);
          this->filename = filename;
        }
      }
    }

    if (this->model == nullptr) {
      const auto filename = Path(getcwd()) / this->name;
      if (filesystem::Resource::isFile(filename) && !is_clip_or_mmproj_candidate(filename)) {
        this->model = llama_model_load_from_file(filename.string().c_str(), this->params);
        this->filename = filename;
      }
    }

    if (this->model != nullptr) {
      this->vocab = llama_model_get_vocab(this->model);
      if (this->vocab != nullptr) {
        this->isLoaded = true;
        return true;
      }
    }

    this->filename.clear();
    return false;
  }

  bool Model::loaded () const {
    return this->isLoaded.load(std::memory_order_acquire);
  }

  JSON::Object Model::json () const {
    return JSON::Object::Entries {
      {"id", std::to_string(this->id)},
      {"name", this->name},
      {"loaded", this->loaded()},
      {"filename", this->filename},
      {"options", JSON::Object::Entries {
        {"directory", this->options.directory},
        {"gpuLayerCount", this->options.gpuLayerCount}
      }}
    };
  }
}
