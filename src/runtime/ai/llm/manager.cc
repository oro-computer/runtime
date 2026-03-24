#include <llama.h>

#include "../../debug.hh"
#include "../llm.hh"
#include "../../config.hh"

namespace oro::runtime::ai::llm {
  bool Manager::init () {
    if (!this->isInitialized) {
      Lock lock(this->mutex);
      ggml_backend_load_all();
      this->isInitialized = true;
      llama_log_set([](enum ggml_log_level level, const char* message, void*) {
        if (level >= GGML_LOG_LEVEL_ERROR) {
          debug("%s", message);
        }
      }, nullptr);
      return true;
    }

    return false;
  }

  SharedPointer<Model> Manager::loadModel (const Model::Options& options) {
    Lock lock(this->mutex);

    if (options.id > 0) {
      for (const auto& entry : this->models) {
        if (entry.second->id == options.id) {
          return entry.second;
        }
      }
    }

    if (options.name.empty()) {
      return nullptr;
    }

    if (this->models.contains(options.name)) {
      return this->models.at(options.name);
    }

    auto model = std::make_shared<Model>(options);

    if (model->load()) {
      this->models.insert_or_assign(model->name, model);
      return model;
    }

    return nullptr;
  }

  bool Manager::unloadModel (ID id) {
    Lock lock(this->mutex);
    for (const auto& entry : this->models) {
      if (entry.second->id == id) {
        this->models.erase(entry.first);
        return true;
      }
    }

    return false;
  }

  bool Manager::unloadModel (const String& name) {
    Lock lock(this->mutex);
    if (this->models.contains(name)) {
      this->models.erase(name);
      return true;
    }

    return false;
  }

  SharedPointer<LoRA> Manager::loadLoRA (ID id) {
    Lock lock(this->mutex);

    if (id > 0 && this->loras.contains(id)) {
      return this->loras.at(id);
    }

    return nullptr;
  }

  SharedPointer<LoRA> Manager::loadLoRA (
    SharedPointer<Model> model,
    const LoRA::Options& options
  ) {
    Lock lock(this->mutex);
    if (options.id > 0 && this->loras.contains(options.id)) {
      return this->loras.at(options.id);
    }

    auto lora = std::make_shared<LoRA>(model, options);
    if (!lora->load()) {
      return nullptr;
    }
    this->loras.insert_or_assign(lora->id, lora);
    return lora;
  }

  bool Manager::unloadLora (ID id) {
    Lock lock(this->mutex);
    if (this->loras.contains(id)) {
      auto lora = this->loras.at(id);
      for (const auto& entry : this->contexts) {
        for (size_t i = 0; i < entry.second->loras.size(); ++i) {
          const auto item = entry.second->loras[i];
          if (item == lora) {
            entry.second->loras.erase(entry.second->loras.begin() + i);
            break;
          }
        }
      }

      this->loras.erase(id);
      return true;
    }

    return false;
  }

  SharedPointer<Context> Manager::createContext (
    SharedPointer<Model> model,
    const Context::Options& options
  ) {
    Lock lock(this->mutex);
    auto context = std::make_shared<Context>(model, options);
    this->contexts.insert_or_assign(context->id, context);
    return context;
  }

  SharedPointer<Context> Manager::getContext (ID id) {
    Lock lock(this->mutex);
    if (this->contexts.contains(id)) {
      return this->contexts.at(id);
    }

    return nullptr;
  }

  bool Manager::destroyContext (ID id) {
    Lock lock(this->mutex);
    if (this->contexts.contains(id)) {
      this->contexts.erase(id);
      return true;
    }

    return false;
  }

  static inline String poolKeyFor(SharedPointer<Model> model, size_t size) {
    return model ? (model->name + String("|") + std::to_string(size)) : String("|") + std::to_string(size);
  }

  SharedPointer<Context> Manager::acquirePooledContext (
    SharedPointer<Model> model,
    const Context::Options& options
  ) {
    Lock lock(this->mutex);
    const auto key = poolKeyFor(model, options.size);
    auto applyActive = [&](SharedPointer<Context> ctx) {
      if (!ctx || !ctx->model) return;
      const auto& modelName = ctx->model->name;
      Vector<std::pair<SharedPointer<LoRA>, float>> list;
      if (this->activeLoras.contains(modelName)) {
        list = this->activeLoras.at(modelName);
      }
      auto existing = ctx->loras;
      for (auto &l : existing) {
        if (l) {
          l->detach(ctx);
        }
      }
      ctx->loras.clear();
      for (const auto &entry : list) {
        if (!entry.first || entry.second == 0.0f) continue;
        LoRA::AttachOptions opt; opt.scale = entry.second;
        entry.first->attach(ctx, opt);
      }
    };
    if (this->contextPool.contains(key)) {
      auto &vec = this->contextPool.at(key);
      if (!vec.empty()) {
        auto ctx = vec.back();
        vec.pop_back();
        // reset if necessary
        if (ctx) {
          (void) ctx->reset();
          this->contexts.insert_or_assign(ctx->id, ctx);
          this->poolReused.fetch_add(1);
          applyActive(ctx);
          return ctx;
        }
      }
    }
    // None available: create new
    auto ctx = std::make_shared<Context>(model, options);
    this->contexts.insert_or_assign(ctx->id, ctx);
    this->poolCreated.fetch_add(1);
    applyActive(ctx);
    return ctx;
  }

  void Manager::releasePooledContext (SharedPointer<Context> ctx) {
    if (!ctx) return;
    Lock lock(this->mutex);
    const auto key = poolKeyFor(ctx->model, ctx->options.size);
    {
      auto attached = ctx->loras;
      for (auto &l : attached) {
        if (l) {
          l->detach(ctx);
        }
      }
      ctx->loras.clear();
    }
    // Ensure reset for reuse
    (void) ctx->reset();
    auto &vec = this->contextPool[key];
    if (vec.size() < this->poolCapacity) {
      vec.push_back(ctx);
    } else {
      // Drop: remove from contexts to reclaim
      this->contexts.erase(ctx->id);
      this->poolDropped.fetch_add(1);
    }
  }

  void Manager::prewarm (SharedPointer<Model> model, size_t size, size_t count) {
    if (!model) return;
    Lock lock(this->mutex);
    const auto key = poolKeyFor(model, size);
    auto &vec = this->contextPool[key];
    // Cap by poolCapacity and avoid overfilling
    size_t room = (this->poolCapacity > vec.size()) ? (this->poolCapacity - vec.size()) : 0;
    size_t n = std::min(count, room);
    for (size_t i = 0; i < n; ++i) {
      Context::Options opt; opt.size = size;
      auto ctx = std::make_shared<Context>(model, opt);
      vec.push_back(ctx);
      this->contexts.insert_or_assign(ctx->id, ctx);
      this->poolCreated.fetch_add(1);
    }
  }

  size_t Manager::drain (SharedPointer<Model> model, size_t size, size_t minKeep) {
    if (!model) return 0;
    Lock lock(this->mutex);
    const auto key = poolKeyFor(model, size);
    if (!this->contextPool.contains(key)) return 0;
    auto &vec = this->contextPool.at(key);
    if (minKeep >= vec.size()) return 0;
    size_t toDrop = vec.size() - minKeep;
    for (size_t i = 0; i < toDrop; ++i) {
      auto ctx = vec.back();
      vec.pop_back();
      this->contexts.erase(ctx->id);
      this->poolDropped.fetch_add(1);
    }
    return toDrop;
  }

  size_t Manager::drainAll (SharedPointer<Model> model, size_t minKeep) {
    if (!model) return 0;
    Lock lock(this->mutex);
    size_t dropped = 0;
    // Collect keys to avoid iterator invalidation while modifying
    Vector<String> keys;
    for (const auto &kv : this->contextPool) {
      // parse model|size key; naive check on model->name prefix
      const auto &k = kv.first;
      if (k.rfind(model->name + String("|"), 0) == 0) keys.push_back(k);
    }
    for (const auto &key : keys) {
      auto &vec = this->contextPool[key];
      if (minKeep >= vec.size()) continue;
      size_t toDrop = vec.size() - minKeep;
      for (size_t i = 0; i < toDrop; ++i) {
        auto ctx = vec.back();
        vec.pop_back();
        this->contexts.erase(ctx->id);
        this->poolDropped.fetch_add(1);
        dropped++;
      }
    }
    return dropped;
  }

  bool Manager::removePooledContext (ID id) {
    Lock lock(this->mutex);
    for (auto &kv : this->contextPool) {
      auto &vec = kv.second;
      for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (*it && (*it)->id == id) {
          auto ctx = *it;
          vec.erase(it);
          this->contexts.erase(id);
          this->poolDropped.fetch_add(1);
          return true;
        }
      }
    }
    return false;
  }

  Vector<std::pair<SharedPointer<LoRA>, float>> Manager::getActiveLoras (const String& modelName) {
    Lock lock(this->mutex);
    if (this->activeLoras.contains(modelName)) {
      return this->activeLoras.at(modelName);
    }
    return {};
  }

  void Manager::setActiveLoras (const String& modelName, const Vector<std::pair<SharedPointer<LoRA>, float>>& list) {
    Lock lock(this->mutex);
    Vector<std::pair<SharedPointer<LoRA>, float>> filtered;
    filtered.reserve(list.size());
    for (const auto& entry : list) {
      if (!entry.first) continue;
      if (entry.second == 0.0f) continue;
      filtered.push_back(entry);
    }
    this->activeLoras[modelName] = filtered;

    const auto prefix = modelName + String("|");
    for (auto &kv : this->contextPool) {
      if (kv.first.rfind(prefix, 0) != 0) continue;
      for (auto &ctx : kv.second) {
        if (!ctx) continue;
        auto existing = ctx->loras;
        for (auto &l : existing) {
          if (l) l->detach(ctx);
        }
        ctx->loras.clear();
        for (const auto& entry : filtered) {
          LoRA::AttachOptions opt; opt.scale = entry.second;
          entry.first->attach(ctx, opt);
        }
      }
    }
  }

  bool Manager::applyActiveLorasToContext (SharedPointer<Context> ctx) {
    if (!ctx || !ctx->model) return false;
    Lock lock(this->mutex);
    Vector<std::pair<SharedPointer<LoRA>, float>> list;
    const auto& modelName = ctx->model->name;
    if (this->activeLoras.contains(modelName)) {
      list = this->activeLoras.at(modelName);
    }
    auto existing = ctx->loras;
    for (auto &l : existing) {
      if (l) l->detach(ctx);
    }
    ctx->loras.clear();
    for (const auto& entry : list) {
      if (!entry.first) continue;
      LoRA::AttachOptions opt; opt.scale = entry.second;
      entry.first->attach(ctx, opt);
    }
    return true;
  }
}
