#include "ann.hh"
#include "string.hh"
#include "debug.hh"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <system_error>
#include <stdexcept>

#include <cranium/cranium.h>

using oro::runtime::string::toLowerCase;

namespace {
  using namespace oro::runtime;

  static Activation resolveActivation(const String& raw) {
    auto normalized = raw;
    if (normalized.empty()) {
      normalized = "linear";
    } else {
      normalized = toLowerCase(normalized);
    }
    return getFunctionByName(normalized.c_str());
  }

  static String activationToString(Activation fn) {
    if (fn == nullptr) {
      return "linear";
    }
    return String(getFunctionName(fn));
  }

  static LOSS_FUNCTION toInternalLoss(ann::LossFunction loss) {
    switch (loss) {
      case ann::LossFunction::MeanSquaredError:
        return MEAN_SQUARED_ERROR;
      case ann::LossFunction::CrossEntropy:
      default:
        return CROSS_ENTROPY_LOSS;
    }
  }

  struct DataSetHolder {
    ::DataSet* handle = nullptr;
    size_t rows = 0;
    size_t cols = 0;

    DataSetHolder () = default;
    DataSetHolder (const ann::DataView& view) {
      this->reset(view);
    }

    DataSetHolder (const DataSetHolder&) = delete;
    DataSetHolder& operator = (const DataSetHolder&) = delete;

    DataSetHolder (DataSetHolder&& other) noexcept {
      this->handle = other.handle;
      this->rows = other.rows;
      this->cols = other.cols;
      other.handle = nullptr;
      other.rows = 0;
      other.cols = 0;
    }

    DataSetHolder& operator = (DataSetHolder&& other) noexcept {
      if (this != &other) {
        this->reset();
        this->handle = other.handle;
        this->rows = other.rows;
        this->cols = other.cols;
        other.handle = nullptr;
        other.rows = 0;
        other.cols = 0;
      }
      return *this;
    }

    ~DataSetHolder () {
      this->reset();
    }

    void reset () {
      if (this->handle != nullptr) {
        ::destroyDataSet(this->handle);
        this->handle = nullptr;
        this->rows = 0;
        this->cols = 0;
      }
    }

    void reset (const ann::DataView& view) {
      this->reset();
      if (view.data == nullptr || view.rows == 0 || view.cols == 0) {
        return;
      }

      this->rows = view.rows;
      this->cols = view.cols;

      auto** rowPointers = static_cast<float**>(std::malloc(sizeof(float*) * view.rows));
      if (rowPointers == nullptr) {
        throw std::bad_alloc();
      }

      std::memset(rowPointers, 0, sizeof(float*) * view.rows);

      try {
        for (size_t row = 0; row < view.rows; ++row) {
          auto* rowData = static_cast<float*>(std::malloc(sizeof(float) * view.cols));
          if (rowData == nullptr) {
            throw std::bad_alloc();
          }
          std::memcpy(rowData, view.data + (row * view.cols), sizeof(float) * view.cols);
          rowPointers[row] = rowData;
        }
      } catch (...) {
        if (rowPointers != nullptr) {
          for (size_t row = 0; row < view.rows; ++row) {
            if (rowPointers[row] != nullptr) {
              std::free(rowPointers[row]);
            }
          }
          std::free(rowPointers);
        }
        throw;
      }

      this->handle = ::createDataSet(view.rows, view.cols, rowPointers);
    }
  };
}

namespace oro::runtime::ann {
  struct Model::Impl {
    Options options;
    Network_* network = nullptr;
    Vector<LayerConfig> hiddenLayers;
    size_t input = 0;
    size_t output = 0;
    Mutex mutex;
  };

  LossFunction lossFunctionFromString (const String& raw) {
    auto normalized = toLowerCase(raw);
    if (normalized == "mse" || normalized == "meansquarederror" || normalized == "mean_square_error") {
      return LossFunction::MeanSquaredError;
    }
    return LossFunction::CrossEntropy;
  }

  String lossFunctionToString (LossFunction loss) {
    switch (loss) {
      case LossFunction::MeanSquaredError:
        return "meanSquaredError";
      case LossFunction::CrossEntropy:
      default:
        return "crossEntropy";
    }
  }

  Model::Model (const Options& options)
    : id(crypto::rand64()),
      name(options.name),
      impl(std::make_unique<Impl>()) {
    if (options.inputSize == 0) {
      throw std::invalid_argument("ANN model inputSize must be greater than 0");
    }
    if (options.outputLayer.size == 0) {
      throw std::invalid_argument("ANN model outputLayer.size must be greater than 0");
    }

    this->impl->options = options;
    this->impl->hiddenLayers = options.hiddenLayers;
    this->impl->input = options.inputSize;
    this->impl->output = options.outputLayer.size;

    Vector<size_t> hiddenSizes;
    Vector<Activation> hiddenActivations;
    hiddenSizes.reserve(options.hiddenLayers.size());
    hiddenActivations.reserve(options.hiddenLayers.size());

    for (const auto& layer : options.hiddenLayers) {
      if (layer.size == 0) {
        throw std::invalid_argument("ANN hidden layer size must be greater than 0");
      }
      hiddenSizes.push_back(layer.size);
      hiddenActivations.push_back(resolveActivation(layer.activation.empty() ? "relu" : layer.activation));
    }

    auto outputActivation = resolveActivation(options.outputLayer.activation);
    auto* created = createNetwork(
      options.inputSize,
      hiddenSizes.size(),
      hiddenSizes.empty() ? nullptr : hiddenSizes.data(),
      hiddenActivations.empty() ? nullptr : hiddenActivations.data(),
      options.outputLayer.size,
      outputActivation
    );

    if (created == nullptr) {
      throw std::runtime_error("Failed to create ANN network");
    }

    this->impl->network = created;
  }

  Model::Model (const Options& options, Network_* existing)
    : id(crypto::rand64()),
      name(options.name),
      impl(std::make_unique<Impl>()) {
    if (existing == nullptr) {
      throw std::invalid_argument("Existing ANN network handle is null");
    }

    this->impl->options = options;
    this->impl->hiddenLayers = options.hiddenLayers;
    this->impl->input = options.inputSize;
    this->impl->output = options.outputLayer.size;
    this->impl->network = existing;
  }

  Model::~Model () {
    if (this->impl && this->impl->network != nullptr) {
      destroyNetwork(this->impl->network);
      this->impl->network = nullptr;
    }
  }

  const Model::Options& Model::options () const {
    return this->impl->options;
  }

  size_t Model::inputSize () const {
    return this->impl->input;
  }

  size_t Model::outputSize () const {
    return this->impl->output;
  }

  bool Model::save (const Path& path) const {
    if (!this->impl || this->impl->network == nullptr) {
      return false;
    }

    auto resolved = path;
    if (resolved.has_parent_path()) {
      std::error_code ec;
      oro::fs::create_directories(resolved.parent_path(), ec);
      if (ec) {
        debug("ann: failed to create directory '%s': %s", resolved.parent_path().string().c_str(), ec.message().c_str());
      }
    }

    const auto stringPath = resolved.string();
    Vector<char> writable(stringPath.begin(), stringPath.end());
    writable.push_back('\0');

    {
      Lock lock(this->impl->mutex);
      ::saveNetwork(this->impl->network, writable.data());
    }

    return true;
  }

  Model::TrainReport Model::train (const TrainOptions& trainOptions, const DataView& features, const DataView& labels) {
    if (features.data == nullptr || labels.data == nullptr) {
      throw std::invalid_argument("ANN training requires feature and label data");
    }

    if (features.rows == 0 || features.cols == 0) {
      throw std::invalid_argument("ANN training feature dimensions must be greater than 0");
    }

    if (labels.rows == 0 || labels.cols == 0) {
      throw std::invalid_argument("ANN training label dimensions must be greater than 0");
    }

    if (features.rows != labels.rows) {
      throw std::invalid_argument("ANN training features and labels must have the same number of rows");
    }

    if (features.cols != this->impl->input) {
      throw std::invalid_argument("ANN training feature column count does not match model input size");
    }

    if (labels.cols != this->impl->output) {
      throw std::invalid_argument("ANN training label column count does not match model output size");
    }

    DataSetHolder featureSet(features);
    DataSetHolder labelSet(labels);

    TrainReport report;
    const auto start = std::chrono::steady_clock::now();

    {
      Lock lock(this->impl->mutex);
      const auto loss = toInternalLoss(trainOptions.lossFunction);
      const auto batchSize = trainOptions.batchSize == 0 ? features.rows : trainOptions.batchSize;
      ::batchGradientDescent(
        this->impl->network,
        featureSet.handle,
        labelSet.handle,
        loss,
        batchSize,
        trainOptions.learningRate,
        trainOptions.searchTime,
        trainOptions.regularizationStrength,
        trainOptions.momentumFactor,
        trainOptions.maxEpochs <= 0 ? 1 : trainOptions.maxEpochs,
        trainOptions.shuffle ? 1 : 0,
        trainOptions.verbose ? 1 : 0
      );

      ::forwardPassDataSet(this->impl->network, featureSet.handle);
      auto* predictions = ::getOuput(this->impl->network);
      if (predictions != nullptr) {
        if (trainOptions.lossFunction == LossFunction::CrossEntropy) {
          report.loss = ::crossEntropyLoss(this->impl->network, predictions, labelSet.handle, trainOptions.regularizationStrength);
        } else {
          report.loss = ::meanSquaredError(this->impl->network, predictions, labelSet.handle, trainOptions.regularizationStrength);
        }
      }
      report.accuracy = ::accuracy(this->impl->network, featureSet.handle, labelSet.handle);
    }

    const auto end = std::chrono::steady_clock::now();
    report.epochs = trainOptions.maxEpochs <= 0 ? 1 : static_cast<size_t>(trainOptions.maxEpochs);
    report.durationMs = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start).count();

    return report;
  }

  Model::InferResult Model::infer (const DataView& features) {
    if (features.data == nullptr || features.rows == 0 || features.cols == 0) {
      throw std::invalid_argument("ANN inference requires feature data");
    }

    if (features.cols != this->impl->input) {
      throw std::invalid_argument("ANN inference feature column count does not match model input size");
    }

    DataSetHolder featureSet(features);
    InferResult result;

    {
      Lock lock(this->impl->mutex);
      ::forwardPassDataSet(this->impl->network, featureSet.handle);
      Matrix* output = ::getOuput(this->impl->network);
      if (output != nullptr && output->rows > 0 && output->cols > 0) {
        result.rows = output->rows;
        result.cols = output->cols;
        result.logits.resize(output->rows * output->cols);
        std::memcpy(result.logits.data(), output->data, sizeof(float) * output->rows * output->cols);
      }

      int* prediction = ::predict(this->impl->network);
      if (prediction != nullptr) {
        result.classes.assign(prediction, prediction + featureSet.rows);
        std::free(prediction);
      }
    }

    return result;
  }

  float Model::accuracy (const DataView& features, const DataView& labels) {
    if (features.data == nullptr || labels.data == nullptr) {
      throw std::invalid_argument("ANN accuracy requires feature and label data");
    }

    if (features.rows != labels.rows) {
      throw std::invalid_argument("ANN accuracy features and labels must have matching rows");
    }

    if (features.cols != this->impl->input) {
      throw std::invalid_argument("ANN accuracy feature column count does not match model input size");
    }

    if (labels.cols != this->impl->output) {
      throw std::invalid_argument("ANN accuracy label column count does not match model output size");
    }

    DataSetHolder featureSet(features);
    DataSetHolder labelSet(labels);

    Lock lock(this->impl->mutex);
    return ::accuracy(this->impl->network, featureSet.handle, labelSet.handle);
  }

  JSON::Object Model::json () const {
    JSON::Array hidden;
    for (const auto& layer : this->impl->hiddenLayers) {
      hidden.push(JSON::Object::Entries {
        {"size", static_cast<int64_t>(layer.size)},
        {"activation", layer.activation.empty() ? "relu" : layer.activation}
      });
    }

    const auto outputActivation = activationToString(this->impl->network
      ? this->impl->network->layers[this->impl->network->numLayers - 1]->activation
      : resolveActivation(this->impl->options.outputLayer.activation)
    );

    return JSON::Object::Entries {
      {"id", std::to_string(this->id)},
      {"name", this->name},
      {"inputSize", static_cast<int64_t>(this->impl->input)},
      {"outputSize", static_cast<int64_t>(this->impl->output)},
      {"outputActivation", outputActivation},
      {"hiddenLayers", hidden}
    };
  }

  UniquePointer<Model> Model::load (const Path& path, const String& name) {
    const auto stringPath = path.string();
    if (stringPath.empty()) {
      return nullptr;
    }

    Vector<char> writable(stringPath.begin(), stringPath.end());
    writable.push_back('\0');

    Network_* handle = ::readNetwork(writable.data());
    if (handle == nullptr) {
      return nullptr;
    }

    Options options;
    options.name = name.size() > 0 ? name : path.filename().string();
    options.inputSize = handle->layers[0]->size;
    options.outputLayer.size = handle->layers[handle->numLayers - 1]->size;
    options.outputLayer.activation = activationToString(handle->layers[handle->numLayers - 1]->activation);

    if (handle->numLayers > 2) {
      for (size_t index = 1; index < handle->numLayers - 1; ++index) {
        LayerConfig layer;
        layer.size = handle->layers[index]->size;
        layer.activation = activationToString(handle->layers[index]->activation);
        options.hiddenLayers.push_back(layer);
      }
    }

    return UniquePointer<Model>(new Model(options, handle));
  }

  Manager::Manager () = default;
  Manager::~Manager () = default;

  SharedPointer<Model> Manager::create (const Model::Options& options) {
    auto model = std::make_shared<Model>(options);
    Lock lock(this->mutex);
    this->modelsById.insert_or_assign(model->id, model);
    if (!model->name.empty()) {
      this->modelsByName.insert_or_assign(model->name, model);
    }
    return model;
  }

  SharedPointer<Model> Manager::load (const Path& path, const String& name) {
    auto loaded = Model::load(path, name);
    if (!loaded) {
      return nullptr;
    }
    auto shared = SharedPointer<Model>(loaded.release());
    Lock lock(this->mutex);
    this->modelsById.insert_or_assign(shared->id, shared);
    if (!shared->name.empty()) {
      this->modelsByName.insert_or_assign(shared->name, shared);
    }
    return shared;
  }

  SharedPointer<Model> Manager::get (ID id) const {
    if (id == 0) {
      return nullptr;
    }
    Lock lock(this->mutex);
    if (!this->modelsById.contains(id)) {
      return nullptr;
    }
    return this->modelsById.at(id);
  }

  SharedPointer<Model> Manager::get (const String& name) const {
    if (name.empty()) {
      return nullptr;
    }
    Lock lock(this->mutex);
    if (!this->modelsByName.contains(name)) {
      return nullptr;
    }
    return this->modelsByName.at(name);
  }

  bool Manager::remove (ID id) {
    if (id == 0) {
      return false;
    }
    Lock lock(this->mutex);
    if (!this->modelsById.contains(id)) {
      return false;
    }
    auto model = this->modelsById.at(id);
    this->modelsById.erase(id);
    if (model && !model->name.empty()) {
      this->modelsByName.erase(model->name);
    }
    return true;
  }

  bool Manager::remove (const String& name) {
    if (name.empty()) {
      return false;
    }
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

  Vector<JSON::Object> Manager::list () const {
    Vector<JSON::Object> list;
    Lock lock(this->mutex);
    for (const auto& entry : this->modelsById) {
      if (entry.second) {
        list.push_back(entry.second->json());
      }
    }
    return list;
  }
}
