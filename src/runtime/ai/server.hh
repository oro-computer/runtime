#ifndef ORO_RUNTIME_AI_SERVER_H
#define ORO_RUNTIME_AI_SERVER_H

#include "../json.hh"
#include "../bytes.hh"
#include "../platform.hh"
#include "../serviceworker.hh"
#include "llm.hh"
#include "../ai/chat.hh"
#include "../string.hh"

namespace oro::runtime::ai::server {
  using namespace oro::runtime;

  class LlamaServer {
    public:
      struct Options {
        using HealthMetricsProvider = Function<JSON::Any(const serviceworker::Request&)>;

        String routePrefix = "/ai/llama"; // relative path under oro://<bundle>
        String defaultModelName = "";      // optional preferred model
        int maxConcurrent = 4;              // simple concurrency guard
        // request defaults
        int defaultMaxTokens = 128;
        int defaultMaxDecodeMs = 15000;     // time budget for non-stream decode
        int hardMaxTokens = 2048;           // cap regardless of request value
        size_t maxPromptBytes = 131072;     // prompt size limit (bytes)
        size_t embedMaxTotalBytes = 262144; // embeddings: total bytes across inputs
        float defaultTemperature = 0.8f;
        float defaultTopP = 0.95f;
        int defaultTopK = 40;
        float defaultMinP = 0.05f;
        float defaultRepeatPenalty = 1.0f;
        int defaultRepeatLastN = 64;
        float defaultFrequencyPenalty = 0.0f;
        float defaultPresencePenalty = 0.0f;
        // rate limiting (per-process, coarse)
        int rateRPS = 0;    // requests per second (0 = disabled)
        int rateBurst = 0;  // maximum tokens/burst capacity
        bool enableMetrics = true;
        bool enableProps = true;
        bool enableRerank = true;
        HealthMetricsProvider healthMetricsProvider = nullptr;
      };

      LlamaServer() : options(Options{}) {}
      explicit LlamaServer(const Options& options)
        : options(options) {}

      void setHealthMetricsProvider(Options::HealthMetricsProvider provider) {
        this->options.healthMetricsProvider = std::move(provider);
      }

      bool matches(const String& path) const {
        if (options.routePrefix.size() == 0) return false;
        if (path.size() < options.routePrefix.size()) return false;
        return path.rfind(options.routePrefix, 0) == 0; // starts_with
      }

      // Handle a minimal set of endpoints for MVP.
      // Returns true if handled and fills out status, headers and body.
      bool handle(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleModelsV1(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleModelListAliases(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleChatCompletionsV1(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      // Streamed chat completions (OpenAI-style SSE). Returns true on successful start.
      bool streamChatCompletionsV1(
        serviceworker::Request& req,
        ai::llm::Manager& llm,
        const Function<bool(const String& event, const String& data, bool finished)>& emit
      );

      bool handleEmbeddingsV1(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleCompletionsV1(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool streamCompletionsV1(
        serviceworker::Request& req,
        ai::llm::Manager& llm,
        const Function<bool(const String& event, const String& data, bool finished)>& emit
      );

    private:
      Options options;

      bool handleHealth(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleTokenize(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleDetokenize(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleMetrics(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleProps(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handlePropsPost(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleRerank(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleApiShow(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleApplyTemplate(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleLoraAdapters(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );

      bool handleSlots(
        serviceworker::Request& req,
        int& statusCode,
        http::Headers& headers,
        bytes::Buffer& body,
        ai::llm::Manager& llm
      );
  };
}

#endif
