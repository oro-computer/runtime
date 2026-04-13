#ifndef ORO_RUNTIME_AI_WHISPER_H
#define ORO_RUNTIME_AI_WHISPER_H

#include <whisper.h>
#include "../concurrent.hh"
#include "../bytes.hh"
#include "../json.hh"
#include "../filesystem.hh"
#include "../crypto.hh"

namespace oro::runtime::ai::whisper {
  using types::Atomic;
  using types::Function;
  using types::Map;
  using types::Mutex;
  using types::Path;
  using types::SharedPointer;
  using types::String;
  using types::Vector;

  using ID = uint64_t;

  struct Segment {
    int index = 0;
    float start = 0.0f;
    float end = 0.0f;
    String text;
    float tokenProbability = 0.0f;
    // Optional alignment metadata from newer whisper.cpp
    float startDTW = 0.0f;
    float endDTW = 0.0f;
  };

  struct VADSegment {
    float start = 0.0f;
    float end = 0.0f;
  };

  struct Result {
    String text;
    Vector<Segment> segments;
    String language;
    double audioMs = 0.0;
    double processingMs = 0.0;
    int sourceSampleRate = WHISPER_SAMPLE_RATE;
    size_t inputSamples = 0;
    size_t outputSamples = 0;
    bool resampled = false;
    bool normalized = false;
    // When VAD is enabled, the effective segments processed may be a subset
    bool usedVAD = false;
    Vector<VADSegment> vadSegments;
  };

  struct ModelOptions {
    String name;
    String directory;
    size_t threadCount = 0;
    size_t statePoolLimit = 4;
    bool useGPU = false;
    int gpuDevice = 0;
  };

  struct TranscribeOptions {
    Vector<float> pcmf32;
    String language;
    bool translate = false;
    bool detectLanguage = false;
    bool enableTimestamps = true;
    bool enableWordTimestamps = false;
    bool diarize = false;
    size_t threadCount = 0;
    int maxSegmentLength = 0;
    float temperature = 0.0f;
    float temperatureIncrement = 0.4f;
    float entropyThreshold = 2.4f;
    float logProbThreshold = -1.0f;
    float noSpeechThreshold = 0.6f;
    size_t inputSamples = 0;
    size_t channels = 1;
    int inputSampleRate = WHISPER_SAMPLE_RATE;
    bool resampled = false;
    bool normalized = false;
    bool stream = false;
    // Advanced options backed by whisper.cpp v1.8.x APIs
    bool enableVAD = false;
    String vadModelPath;
  };

  class Model {
    public:
      using Options = ModelOptions;

      ID id = crypto::rand64();
      String name;
      String filename;
      Options options;

      Model (const Options&);
      ~Model ();

      Model (const Model&) = delete;
      Model (Model&&) = delete;
      Model& operator = (const Model&) = delete;
      Model& operator = (Model&&) = delete;

      bool load ();
      bool loaded () const;
      JSON::Object json () const;

      whisper_context* context () const;
      whisper_state* acquireState ();
      void releaseState (whisper_state*);

    private:
      whisper_context_params ctxParams;
      whisper_context* ctx = nullptr;
      mutable Mutex mutex;
      Vector<whisper_state*> statePool;
      Atomic<size_t> activeStates = 0;
      bool tryResolveFilename (const Path&);
  };

  class Manager {
    public:
      Manager ();
      ~Manager ();

      Manager (const Manager&) = delete;
      Manager (Manager&&) = delete;
      Manager& operator = (const Manager&) = delete;
      Manager& operator = (Manager&&) = delete;

      SharedPointer<Model> loadModel (const Model::Options&);
      SharedPointer<Model> getModel (const String& name);
      SharedPointer<Model> getModel (ID id);
      bool unloadModel (const String& name);
      bool unloadModel (ID id);
      Vector<JSON::Object> listModels ();

      void setQueueLimit (size_t limit);
      size_t queueLimit () const;

      struct TaskResult {
        bool ok = false;
        String error;
        Result result;
      };

      using TaskCallback = Function<void(const TaskResult&)>;
      using ProgressCallback = Function<void(const Segment&)>;

      void transcribe (SharedPointer<Model>, const TranscribeOptions&, const TaskCallback&, const ProgressCallback& progress = nullptr);

    private:
      concurrent::WorkerQueue queue;
      Mutex mutex;
      Map<String, SharedPointer<Model>> modelsByName;
      Map<ID, SharedPointer<Model>> modelsById;
  };
}

#endif
