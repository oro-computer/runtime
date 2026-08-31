#include "../../config.hh"
#include "../../env.hh"
#include "../../cwd.hh"
#include "../../filesystem.hh"
#include "../llm.hh"

using oro::runtime::config::getUserConfig;

namespace oro::runtime::ai::llm {
  LoRA::LoRA (
    SharedPointer<Model> model,
    const Options& options
  ) : options(options),
      name(options.name) {
    this->model = model;
  }

  LoRA::~LoRA () {
    if (this->lora != nullptr) {
      llama_adapter_lora_free(this->lora);
      this->lora = nullptr;
    }
  }

  bool LoRA::load () {
    Lock lock(this->mutex);

    if (this->isLoaded.load(std::memory_order_acquire)) {
      return true;
    }

    if (this->name.size() == 0) {
      return false;
    }

    if (this->lora == nullptr) {
      if (filesystem::Resource::isFile(this->name)) {
        if (this->model != nullptr && this->model->model != nullptr) {
          this->lora = llama_adapter_lora_init(this->model->model, this->name.c_str());
        }
        this->filename = this->name;
      }
    }

    if (this->lora == nullptr) {
      // Environment override; prefer consistent naming: *_LORA_PATH
      auto rootLoRADirectory = env::get("ORO_AI_LLM_LORA_PATH");
      if (rootLoRADirectory.size() == 0) {
        rootLoRADirectory = env::get("ORO_RUNTIME_AI_LLM_LORA_PATH");
      }
      if (rootLoRADirectory.size() > 0) {
        if (filesystem::Resource::isDirectory(rootLoRADirectory)) {
          const auto filename = Path(rootLoRADirectory) / this->name;
          if (filesystem::Resource::isFile(filename)) {
            if (this->model != nullptr && this->model->model != nullptr) {
              this->lora = llama_adapter_lora_init(this->model->model, filename.string().c_str());
            }
            this->filename = filename;
          }
        }
      }
    }

    if (this->lora == nullptr && this->options.directory.size() > 0) {
      if (filesystem::Resource::isDirectory(this->options.directory)) {
        const auto filename = Path(this->options.directory) / this->name;
        if (filesystem::Resource::isFile(filename)) {
          if (this->model != nullptr && this->model->model != nullptr) {
            this->lora = llama_adapter_lora_init(this->model->model, filename.string().c_str());
          }
          this->filename = filename;
        }
      }
    }

    if (this->lora == nullptr) {
      static auto userConfig = getUserConfig();
      if (!userConfig["ai_llm_lora_path"].empty()) {
        const auto directory = userConfig["ai_llm_lora_path"];
        const auto filename = Path(directory) / this->name;
        if (filesystem::Resource::isFile(filename)) {
          if (this->model != nullptr && this->model->model != nullptr) {
            this->lora = llama_adapter_lora_init(this->model->model, filename.string().c_str());
          }
          this->filename = filename;
        }
      }
    }

    if (this->lora == nullptr) {
      const auto filename = Path(getcwd()) / this->name;
      if (filesystem::Resource::isFile(filename)) {
        if (this->model != nullptr && this->model->model != nullptr) {
          this->lora = llama_adapter_lora_init(this->model->model, filename.string().c_str());
        }
        this->filename = filename;
      }
    }

    if (this->lora != nullptr) {
      this->isLoaded.store(true, std::memory_order_release);
      return true;
    }

    this->filename.clear();
    return false;
  }

  bool LoRA::attach (
    SharedPointer<Context> context,
    const AttachOptions& options
  ) {
    if (context == nullptr || this->lora == nullptr) {
      return false;
    }

    ScopedLock lock(this->mutex, context->mutex);
    for (const auto& entry : context->loras) {
      if (entry.get() == this) {
        return false;
      }
    }

    const bool ok = 0 == llama_set_adapter_lora(
      context->context,
      this->lora,
      options.scale
    );

    if (ok) {
      context->loras.push_back(std::static_pointer_cast<LoRA>(shared_from_this()));
    }

    return ok;
  }

  bool LoRA::detach (SharedPointer<Context> context) {
    if (context == nullptr || this->lora == nullptr) {
      return false;
    }

    ScopedLock lock(this->mutex, context->mutex);
    for (const auto& entry : context->loras) {
      if (entry.get() == this) {
        const bool ok = 0 == llama_rm_adapter_lora(
          context->context,
          this->lora
        );
        if (ok) {
          for (size_t i = 0; i < context->loras.size(); ++i) {
            if (context->loras[i].get() == this) {
              context->loras.erase(context->loras.begin() + i);
              break;
            }
          }
        }
        return ok;
      }
    }

    return false;
  }

  bool LoRA::loaded () const {
    return this->isLoaded.load(std::memory_order_acquire);
  }

  JSON::Object LoRA::json () const {
    JSON::Object::Entries metaEntries;

    // Surface adapter GGUF metadata when available (newer llama.cpp)
    if (this->lora != nullptr) {
      char keyBuf[128];
      char valBuf[256];
      const int32_t n = llama_adapter_meta_count(this->lora);
      for (int32_t i = 0; i < n; ++i) {
        keyBuf[0] = '\0';
        valBuf[0] = '\0';
        if (llama_adapter_meta_key_by_index(this->lora, i, keyBuf, sizeof(keyBuf)) <= 0) {
          continue;
        }
        if (llama_adapter_meta_val_str_by_index(this->lora, i, valBuf, sizeof(valBuf)) <= 0) {
          continue;
        }
        metaEntries[String(keyBuf)] = String(valBuf);
      }

      // Expose alora invocation tokens when available
      const uint64_t n_inv = llama_adapter_get_alora_n_invocation_tokens(this->lora);
      const llama_token* inv = llama_adapter_get_alora_invocation_tokens(this->lora);
      if (n_inv > 0 && inv != nullptr) {
        JSON::Array arr;
        for (uint64_t i = 0; i < n_inv; ++i) {
          arr.push((int64_t)inv[i]);
        }
        metaEntries[String("alora.invocation_tokens")] = arr;
      }
    }

    return JSON::Object::Entries {
      {"id", std::to_string(this->id)},
      {"name", this->name},
      {"loaded", this->loaded()},
      {"filename", this->filename},
      {"options", JSON::Object::Entries {
        {"directory", this->options.directory}
      }},
      {"metadata", metaEntries}
    };
  }
}
