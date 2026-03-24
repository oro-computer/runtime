#ifndef ORO_RUNTIME_CORE_SERVICES_AI_H
#define ORO_RUNTIME_CORE_SERVICES_AI_H

#include "../../ai.hh"
#include "../../ann.hh"
#include "../../ai/server.hh"
#include "../../ipc.hh"
#include "../../core.hh"
#include "../../config.hh"

#include <atomic>
#include <memory>
#include <thread>

namespace httplib {
  class Server;
  struct Request;
  struct Response;
}

namespace oro::runtime::core::services {
  class AI : public core::Service {
    public:
      // Server-scoped stats (per origin + route prefix)
      struct ServerStats {
        // concurrency
        std::atomic<int> inflight{0};

        // rate limiting (token bucket)
        std::atomic<int> tokens{0};
        std::atomic<long long> lastRefillMs{0};

        // counters
        std::atomic<uint64_t> reqChatStream{0};
        std::atomic<uint64_t> reqChatNonStream{0};
        std::atomic<uint64_t> reqTextStream{0};
        std::atomic<uint64_t> reqTextNonStream{0};
        std::atomic<uint64_t> reqEmbeddings{0};
        std::atomic<uint64_t> rateLimited{0};
        std::atomic<uint64_t> tooLarge{0};
        std::atomic<uint64_t> errors{0};
        std::atomic<uint64_t> timeouts{0};

        // latency aggregates (ms)
        std::atomic<long long> latChatStreamSum{0};
        std::atomic<long long> latChatStreamCount{0};
        std::atomic<long long> latChatNonStreamSum{0};
        std::atomic<long long> latChatNonStreamCount{0};
        std::atomic<long long> latTextStreamSum{0};
        std::atomic<long long> latTextStreamCount{0};
        std::atomic<long long> latTextNonStreamSum{0};
        std::atomic<long long> latTextNonStreamCount{0};
        std::atomic<long long> latEmbeddingsSum{0};
        std::atomic<long long> latEmbeddingsCount{0};

        // Try to consume a token; rate/burst provided by caller (per-request options)
        bool tryConsumeToken(int rate, int burst) {
          if (rate <= 0 || burst <= 0) return true; // disabled
          auto nowMs = []() -> long long {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
          };
          // Refill once per second
          long long last = lastRefillMs.load(std::memory_order_relaxed);
          long long now = nowMs();
          if (now - last >= 1000) {
            lastRefillMs.store(now, std::memory_order_relaxed);
            tokens.store(burst, std::memory_order_relaxed);
          }
          while (true) {
            int t = tokens.load(std::memory_order_relaxed);
            if (t <= 0) return false;
            if (tokens.compare_exchange_weak(t, t - 1, std::memory_order_relaxed)) return true;
          }
        }
      };

      class LLM : public core::Service {
        public:
          struct LoadModelOptions : public ai::llm::Model::Options {};
          struct LoadLoRAOptions : public ai::llm::LoRA::Options {
            LoadModelOptions model;
          };

          struct CreateContextOptions : public ai::llm::Context::Options {
            LoadModelOptions model;
          };

          struct PrewarmPoolOptions {
            ai::llm::Model::Options model;
            size_t size = 2048;
            size_t count = 1;
          };

          ai::llm::Manager manager;

          LLM (const Options& options)
            : core::Service(options) {
            if (options.enabled) {
              this->manager.init();
              this->queue.limit = 8;
              // Optional queue limit from userConfig
              auto uc = oro::runtime::config::getUserConfig();
              if (uc.contains("ai_llm_queue_limit") && uc.at("ai_llm_queue_limit").size() > 0) {
                try { this->queue.limit = (size_t) std::stoull(uc.at("ai_llm_queue_limit")); } catch (...) {}
              }
            }
          }

          void loadModel (
            const ipc::Message::Seq&,
            const LoadModelOptions&,
            const Callback&
          );

          void listModels (
            const ipc::Message::Seq&,
            const Callback&
          );

          void unloadModel (
            const ipc::Message::Seq&,
            const String& name,
            const ai::llm::ID id,
            const Callback&
          );

          void loadLoRA (
            const ipc::Message::Seq&,
            const LoadLoRAOptions&,
            const Callback&
          );

          void attachLoRa (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const ai::llm::ID,
            const ai::llm::LoRA::AttachOptions&,
            const Callback&
          );

          void detachLoRa (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const ai::llm::ID,
            const Callback&
          );

          void unloadLoRa (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const Callback&
          );

          void createContext (
            const ipc::Message::Seq&,
            const CreateContextOptions&,
            const Callback&
          );

          void destroyContext (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const Callback&
          );

          void getContextStats (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const Callback&
          );

          void dumpContextState (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const Callback&
          );

          void restoreContextState (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const bytes::Buffer&,
            const Callback&
          );

          void prewarmPool (
            const ipc::Message::Seq&,
            const PrewarmPoolOptions&,
            const Callback&
          );
      };

      class Speech : public core::Service {
        public:
          struct LoadModelOptions : public ai::whisper::ModelOptions {
            ai::whisper::ID id = 0;
          };

          struct UnloadModelOptions {
            ai::whisper::ID id = 0;
            String name;
          };

          struct TranscribeRequest {
            ai::whisper::ID id = 0;
            String name;
            ai::whisper::TranscribeOptions options;
            uint64_t conduitId = 0;
          };

          ai::whisper::Manager manager;

          Speech (const Options& options)
            : core::Service(options) {
            if (options.enabled) {
              this->queue.limit = 4;
              auto uc = oro::runtime::config::getUserConfig();
              if (uc.contains("ai_whisper_queue_limit") && uc.at("ai_whisper_queue_limit").size() > 0) {
                try {
                  const auto limit = (size_t) std::stoull(uc.at("ai_whisper_queue_limit"));
                  if (limit > 0) {
                    this->queue.limit = limit;
                    this->manager.setQueueLimit(limit);
                  }
                } catch (...) {}
              }
            }
          }

          void loadModel (
            const ipc::Message::Seq&,
            const LoadModelOptions&,
            const Callback&
          );

          void unloadModel (
            const ipc::Message::Seq&,
            const UnloadModelOptions&,
            const Callback&
          );

          void listModels (
            const ipc::Message::Seq&,
            const Callback&
          );

          void transcribe (
            const ipc::Message::Seq&,
            const TranscribeRequest&,
            const Callback&
          );
      };

      class ANN : public core::Service {
        public:
          struct CreateModelOptions : public ann::Model::Options {};

          struct LoadModelOptions {
            String path;
            String name;
          };

          struct RemoveModelOptions {
            ann::ID id = 0;
            String name;
          };

          struct SaveModelOptions {
            ann::ID id = 0;
            String path;
          };

          struct TrainRequest {
            ann::ID id = 0;
            ann::TrainingOptions options;
            size_t rows = 0;
            size_t featureColumns = 0;
            size_t labelColumns = 0;
            Vector<float> features;
            Vector<float> labels;
          };

          struct PredictRequest {
            ann::ID id = 0;
            size_t rows = 0;
            size_t featureColumns = 0;
            Vector<float> features;
          };

          struct AccuracyRequest {
            ann::ID id = 0;
            size_t rows = 0;
            size_t featureColumns = 0;
            size_t labelColumns = 0;
            Vector<float> features;
            Vector<float> labels;
          };

          ann::Manager manager;

          ANN (const Options& options)
            : core::Service(options) {
            if (options.enabled) {
              this->queue.limit = 4;
            }
          }

          void createModel (
            const ipc::Message::Seq&,
            const CreateModelOptions&,
            const Callback&
          );

          void loadModel (
            const ipc::Message::Seq&,
            const LoadModelOptions&,
            const Callback&
          );

          void removeModel (
            const ipc::Message::Seq&,
            const RemoveModelOptions&,
            const Callback&
          );

          void saveModel (
            const ipc::Message::Seq&,
            const SaveModelOptions&,
            const Callback&
          );

          void listModels (
            const ipc::Message::Seq&,
            const Callback&
          );

          void train (
            const ipc::Message::Seq&,
            const TrainRequest&,
            const Callback&
          );

          void infer (
            const ipc::Message::Seq&,
            const PredictRequest&,
            const Callback&
          );

          void evaluate (
            const ipc::Message::Seq&,
            const AccuracyRequest&,
            const Callback&
          );
      };

      class Chat : public core::Service {
        public:
          struct GenerateOptions {
            String prompt;
            Vector<String> antiprompts;
          };

          Mutex mutex;
          Map<ai::chat::ID, SharedPointer<ai::chat::Session>> sessions;

          Chat (const Options& options)
            : core::Service(options) {
            if (options.enabled) {
              this->queue.limit = 8;
              auto uc = oro::runtime::config::getUserConfig();
              if (uc.contains("ai_chat_queue_limit") && uc.at("ai_chat_queue_limit").size() > 0) {
                try { this->queue.limit = (size_t) std::stoull(uc.at("ai_chat_queue_limit")); } catch (...) {}
              }
            }
          }

          void list (
            const ipc::Message::Seq&,
            const Callback&
          );

          void history (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const Callback&
          );

          void generate (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const GenerateOptions&,
            const Callback&
          );

          void message  (
            const ipc::Message::Seq&,
            const ai::llm::ID,
            const GenerateOptions&,
            const Callback&
          );
      };

      // Per-origin + prefix stats
      Mutex serverStatsMutex;
      Map<String, SharedPointer<ServerStats>> serverStats;
      Map<String, SharedPointer<ServerStats>> serverWindowStats;

      ai::server::LlamaServer::Options serverOptions;
      ai::server::LlamaServer server;

      // Get or create stats for a given origin/prefix key
      SharedPointer<ServerStats> getServerStats(const String& origin, const String& prefix) {
        const auto key = origin + String("|") + (prefix.size() > 0 ? prefix : String("/ai/llama"));
        Lock lock(serverStatsMutex);
        if (serverStats.contains(key)) {
          return serverStats.at(key);
        }
        auto stats = std::make_shared<ServerStats>();
        serverStats.insert_or_assign(key, stats);
        return stats;
      }

      // Get or create per-window stats (origin|prefix|clientId)
      SharedPointer<ServerStats> getServerStatsForWindow(const String& origin, const String& prefix, uint64_t clientId) {
        const auto key = origin + String("|") + (prefix.size() > 0 ? prefix : String("/ai/llama")) + String("|") + std::to_string(clientId);
        Lock lock(serverStatsMutex);
        if (serverWindowStats.contains(key)) {
          return serverWindowStats.at(key);
        }
        auto stats = std::make_shared<ServerStats>();
        serverWindowStats.insert_or_assign(key, stats);
        return stats;
      }

      LLM llm;
      Speech speech;
      ANN ann;
      Chat chat;

      AI (const Options& options);
      bool start () override;
      bool stop () override;

    private:
      bool startHttpServer ();
      void stopHttpServer ();
      bool handleHttpRequest (const httplib::Request&, httplib::Response&);

      String httpRoutePrefix;
      String httpHost = "127.0.0.1";
      int httpPort = 0;
      bool httpAllowCORS = false;
      String httpSharedKey = "";
      bool httpUseTLS = false;

      std::shared_ptr<httplib::Server> httpServer;
      std::thread httpServerThread;
      std::atomic<bool> httpServerRunning { false };
      std::atomic<uint64_t> httpClientCounter { 1 };
  };
}
#endif
