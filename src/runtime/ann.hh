#ifndef ORO_RUNTIME_ANN_H
#define ORO_RUNTIME_ANN_H

#include "string.hh"
#include "json.hh"
#include "filesystem.hh"
#include "crypto.hh"
#include "concurrent.hh"

struct Network_;

namespace oro::runtime::ann {
  using types::Map;
  using types::Mutex;
  using types::Path;
  using types::SharedPointer;
  using types::String;
  using types::UniquePointer;
  using types::Vector;

  using ID = uint64_t;

  struct LayerConfig {
    size_t size = 0;
    String activation;
  };

  struct ModelOptions {
    String name;
    size_t inputSize = 0;
    LayerConfig outputLayer;
    Vector<LayerConfig> hiddenLayers;
  };

  enum class LossFunction : uint8_t {
    CrossEntropy,
    MeanSquaredError
  };

  struct TrainingOptions {
    LossFunction lossFunction = LossFunction::CrossEntropy;
    size_t batchSize = 32;
    float learningRate = 0.01f;
    float searchTime = 0.001f;
    float regularizationStrength = 0.0f;
    float momentumFactor = 0.9f;
    int maxEpochs = 50;
    bool shuffle = true;
    bool verbose = false;
  };

  struct TrainingReport {
    float loss = 0.0f;
    float accuracy = 0.0f;
    size_t epochs = 0;
    double durationMs = 0.0;
  };

  struct InferenceResult {
    Vector<int> classes;
    Vector<float> logits;
    size_t rows = 0;
    size_t cols = 0;
  };

  struct DataView {
    const float* data = nullptr;
    size_t rows = 0;
    size_t cols = 0;
  };

  class Model {
    public:
      using Options = ModelOptions;
      using TrainOptions = TrainingOptions;
      using TrainReport = TrainingReport;
      using InferResult = InferenceResult;

      ID id = 0;
      String name;

      Model (const Options&);
      Model (const Model&) = delete;
      Model (Model&&) = delete;
      Model& operator = (const Model&) = delete;
      Model& operator = (Model&&) = delete;
      ~Model ();

      const Options& options () const;

      bool save (const Path&) const;
      TrainReport train (const TrainOptions&, const DataView& features, const DataView& labels);
      InferResult infer (const DataView& features);
      float accuracy (const DataView& features, const DataView& labels);
      JSON::Object json () const;

      size_t inputSize () const;
      size_t outputSize () const;

      static UniquePointer<Model> load (const Path&, const String& name = "");

    private:
      struct Impl;
      UniquePointer<Impl> impl;

      Model (const Options&, Network_* existing);
  };

  class Manager {
    public:
      Manager ();
      ~Manager ();

      SharedPointer<Model> create (const Model::Options&);
      SharedPointer<Model> load (const Path&, const String& name = "");
      SharedPointer<Model> get (ID) const;
      SharedPointer<Model> get (const String&) const;
      bool remove (ID);
      bool remove (const String&);
      Vector<JSON::Object> list () const;

    private:
      mutable Mutex mutex;
      Map<ID, SharedPointer<Model>> modelsById;
      Map<String, SharedPointer<Model>> modelsByName;
  };

  LossFunction lossFunctionFromString (const String&);
  String lossFunctionToString (LossFunction);
}

#endif
