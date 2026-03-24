#include <llama.h>

#include "../../debug.hh"
#include "../../string.hh"

#include "../llm.hh"
#include "../chat.hh"

using oro::runtime::string::trim;

namespace oro::runtime::ai::chat {
  Session::Session (
    SharedPointer<llm::Context> context,
    const Options& options
  ) : context(context),
      antiprompts(options.antiprompts),
      id(options.id)
  {}

  Session::~Session () {}

  bool Session::generate (
    const String& prompt,
    const GenerateOptions& options,
    const GenerateStreamCallback& callback
  ) {
    Lock lock(this->mutex);

    if (this->context == nullptr || this->context->model == nullptr) {
      return false;
    }

    const auto tokenCount = -llama_tokenize(
      this->context->model->vocab,
      prompt.c_str(),
      prompt.size(),
      nullptr,
      0,
      true,
      true
    );

    Vector<llama_token> tokens(tokenCount);
    const auto status = llama_tokenize(
      this->context->model->vocab,
      prompt.c_str(),
      prompt.size(),
      tokens.data(),
      tokens.size(),
      this->context->used() == 0,
      true
    );

    if (status < 0) {
      return false;
    }

    // stop strings or reverse prompts (stop and wait for user input)
    Vector<String> antiprompts = this->antiprompts;
    bool isDetectingAntiprompt = false;
    size_t antipromptMaxSize = 0;
    bytes::BufferQueue generation;
    llama_batch batch;
    llama_token token;

    for (const auto& antiprompt :  options.antiprompts) {
      antiprompts.push_back(antiprompt);
    }

    for (const auto& antiprompt :  antiprompts) {
      if (antiprompt.size() > antipromptMaxSize) {
        antipromptMaxSize = antiprompt.size();
      }
    }

    batch = llama_batch_get_one(tokens.data(), tokens.size());

    auto nowMs = [](){ using namespace std::chrono; return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); };
    while (!options.signal.aborted()) {
      if (options.deadlineAtMs > 0 && nowMs() >= options.deadlineAtMs) {
        if (callback != nullptr) {
          callback(bytes::Buffer(0), true);
        }
        return true;
      }
      if (this->context->used() + batch.n_tokens > this->context->size()) {
        return false;
      }

      if (llama_decode(this->context->context, batch)) {
        debug("llama_decode failed");
        return false;
      }
      // track used tokens locally on the context to avoid relying on removed API
      this->context->usedTokens.fetch_add(batch.n_tokens, std::memory_order_acq_rel);

      token = llama_sampler_sample(
        this->context->sampler,
        this->context->context,
        -1
      );

      if (llama_vocab_is_eog(this->context->model->vocab, token)) {
        callback(bytes::Buffer(0), true);
        return true;
      }

      static thread_local Vector<char> pieceBuf;
      if (pieceBuf.empty()) pieceBuf.resize(256);
      int size = llama_token_to_piece(
        this->context->model->vocab,
        token,
        pieceBuf.data(),
        (int32_t)pieceBuf.size(),
        0,
        true
      );
      if (size < 0) {
        int tries = 0; size_t cap = pieceBuf.size();
        while (size < 0 && tries++ < 4 && cap < 8192) {
          cap *= 2; pieceBuf.resize(cap);
          size = llama_token_to_piece(
            this->context->model->vocab,
            token,
            pieceBuf.data(),
            (int32_t)pieceBuf.size(),
            0,
            true
          );
        }
      }
      if (size == 0) {
        callback(bytes::Buffer(0), true);
        return true;
      }
      if (size < 0) {
        debug("llama_token_to_piece failed");
        return false;
      }

      for (int i = 0; i < size; ++i) {
        generation.push(pieceBuf[i]);
      }

      if (generation.size() > 0) {
        // - enumerate each antiprompt
        // - look at tail generation buffer
        // - if generation tail expands to the prefix
        //   of an antiprompt, signal `isDetectingAntiprompt = true`
        for (size_t i = generation.size() - 1; i >= 0; --i) {
          if (generation.size() - i > antipromptMaxSize) {
            break;
          }

          const auto tail = generation.slice(i, generation.size());
          if (tail.size() == 0) {
            continue;
          }

          isDetectingAntiprompt = false;
          for (const auto& antiprompt : antiprompts) {
            if (bytes::Buffer::compare(tail, antiprompt) == 0) {
              callback(bytes::Buffer(0), true);
              return true;
            }

            bool startsWith = false;
            for (size_t j = 0; j < tail.size(); ++j) {
              if (antiprompt[j] == tail[j]) {
                startsWith = true;
              } else {
                startsWith = false;
                break;
              }
            }

            if (startsWith) {
              isDetectingAntiprompt = true;
              break;
            } else {
              isDetectingAntiprompt = false;
            }
          }

          if (isDetectingAntiprompt) {
            break;
          }
        }
      }

      batch = llama_batch_get_one(&token, 1);

      if (!isDetectingAntiprompt && callback != nullptr) {
        const auto buffer = bytes::Buffer::from(pieceBuf.data(), (size_t)size);
        const auto eog = buffer.size() == 0 || (buffer.size() == 1 && buffer[0] == 0);
        callback(std::move(buffer), false);
      }
    }

    return true;
  }

  bool Session::chat (
    const String& prompt,
    const ChatOptions& options,
    const ChatStreamCallback& callback
  ) {
    Vector<bytes::Buffer> generations;
    const auto messageId = crypto::rand64();
    // use the model's default chat template (pass nullptr for name)
    auto chatTemplate = llama_model_chat_template(this->context->model->model, nullptr);

    this->messages.push_back({
      "user",
      prompt,
      messageId
    });

    this->history.push_back(this->messages.back().data());

    debug("template: %s", chatTemplate);

    for (const auto& message : this->messages) {
      debug("%s", message.json().str().c_str());
    }

    auto size = llama_chat_apply_template(
      chatTemplate,
      this->history.data(),
      this->history.size(),
      true,
      this->tokens.data(),
      this->tokens.size()
    );

    if (size > static_cast<int>(this->tokens.size())) {
      this->tokens.resize(size);
      size = llama_chat_apply_template(
        chatTemplate,
        this->history.data(),
        this->history.size(),
        true,
        this->tokens.data(),
        this->tokens.size()
      );
    }

    if (size < 0) {
      debug("llama_chat_apply_template failed");
      return false;
    }

    String input(
      this->tokens.begin() + this->tokenCount,
      this->tokens.begin() + size
    );

    debug("input: %s", input.c_str());
    const auto generated = this->generate(input, options, [
      messageId,
      &callback,
      &generations
    ] (auto result, auto eog) mutable {
        generations.push_back(result);
        if (callback != nullptr) {
          callback(messageId, result, eog);
        }
      }
    );

    if (!generated) {
      return false;
    }

    this->messages.push_back({
      "assistant",
      bytes::Buffer::concat(generations).str(),
      messageId
    });

    this->history.push_back(this->messages.back().data());

    this->tokenCount = llama_chat_apply_template(
      chatTemplate,
      this->history.data(),
      this->history.size(),
      false,
      nullptr,
      0
    );

    if (this->tokenCount < 0) {
      return false;
    }

    return true;
  }

  bool Session::complete (
    const Vector<Message>& messages,
    const ChatOptions& options,
    const ChatStreamCallback& callback
  ) {
    Lock lock(this->mutex);

    if (this->context == nullptr || this->context->model == nullptr) {
      return false;
    }

    this->messages = messages;
    this->history.clear();
    this->history.reserve(this->messages.size());
    for (const auto& message : this->messages) {
      this->history.push_back(message.data());
    }

    auto chatTemplate = llama_model_chat_template(this->context->model->model, nullptr);
    String input;

    if (chatTemplate != nullptr) {
      auto size = llama_chat_apply_template(
        chatTemplate,
        this->history.data(),
        this->history.size(),
        true,
        nullptr,
        0
      );

      if (size <= 0) {
        return false;
      }

      if (static_cast<size_t>(size) > this->tokens.size()) {
        this->tokens.resize(size);
      }

      size = llama_chat_apply_template(
        chatTemplate,
        this->history.data(),
        this->history.size(),
        true,
        this->tokens.data(),
        (int32_t)this->tokens.size()
      );

      if (size < 0) {
        return false;
      }

      input.assign(this->tokens.begin(), this->tokens.begin() + size);
      this->tokenCount.store((size_t)size, std::memory_order_release);
    } else {
      // Fallback: join role + content lines.
      String joined;
      for (const auto& message : this->messages) {
        joined += message.role;
        joined += ": ";
        joined += message.content;
        joined += '\n';
      }

      input = joined;
      this->tokens.assign(input.begin(), input.end());
      this->tokenCount.store(this->tokens.size(), std::memory_order_release);
    }

    Vector<bytes::Buffer> generations;
    const auto messageId = crypto::rand64();

    const auto generated = this->generate(input, options, [
      messageId,
      &callback,
      &generations
    ] (auto result, auto eog) mutable {
        if (result.size() > 0) {
          generations.push_back(result);
        }
        if (callback != nullptr) {
          callback(messageId, result, eog);
        }
      }
    );

    if (!generated) {
      return false;
    }

    this->messages.push_back({
      "assistant",
      bytes::Buffer::concat(generations).str(),
      messageId
    });

    this->history.push_back(this->messages.back().data());

    if (chatTemplate != nullptr) {
      auto size = llama_chat_apply_template(
        chatTemplate,
        this->history.data(),
        this->history.size(),
        false,
        nullptr,
        0
      );

      if (size > 0) {
        this->tokenCount.store((size_t)size, std::memory_order_release);
      }
    }

    return true;
  }

  size_t Session::size () const {
    return this->tokenCount.load(std::memory_order_acquire);
  }

  JSON::Object Session::json () const {
    JSON::Array messages;
    for (const auto& message : this->messages) {
      messages.push(message.json());
    }
    return JSON::Object::Entries {
      {"id", std::to_string(this->id)},
      {"model", this->context->model->name},
      {"messages", messages}
    };
  }
}
