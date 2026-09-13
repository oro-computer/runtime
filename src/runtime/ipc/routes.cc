#include "../../extension/extension.hh"

#if ORO_RUNTIME_PLATFORM_APPLE
#include "../../cli.hh"
#endif

#include "../serviceworker.hh"
#include "../filesystem.hh"
#include "../sqlite.hh"
#include "../bridge.hh"
#include "../window.hh"
#include "../http.hh"
#include "../bytes.hh"
#include "../json.hh"
#include "../url.hh"
#include "../scheme.hh"
#include "../core/services/usb.hh"
#include "../core/services/bluetooth.hh"
#include "../core/services/dbus.hh"
#include "../core/services/mcp.hh"
#include "../core/services/asn1.hh"
#include "../core/services/ipfs.hh"
#include "../core/services/secure_storage.hh"
#include "../core/services/cdp.hh"
#include "../core/services/tls.hh"
#include "../core/services/xpc.hh"
#include "../core/services/tar.hh"
#include "../core/services/hci.hh"
#include "../core/services/zlib.hh"
#include "../app.hh"
#include "../env.hh"
#include "../ini.hh"
#include "../io.hh"
#include "../iroh.hh"
#include "../ann.hh"
#include "../semver.hh"
#include "../webview/cookies.hh"

#include "../ipc.hh"
#include "../deps.hh"

#include <cstring>
#include <optional>
#include <utility>
#include <chrono>
#include <thread>
#include <limits>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <regex>
#include <cmath>
#include <ctime>

namespace io = oro::runtime::io;
namespace env = oro::runtime::env;
namespace INI = oro::runtime::INI;
namespace iroh = oro::runtime::iroh;

using namespace oro::runtime;
using namespace oro::runtime::ipc;
using namespace oro::runtime::javascript;

using oro::extension::Extension;
using oro::runtime::app::App;
using oro::runtime::config::getUserConfig;
using oro::runtime::url::encodeURIComponent;
using oro::runtime::string::replace;
using oro::runtime::string::trim;
using oro::runtime::string::split;
using oro::runtime::string::splitc;
using oro::runtime::string::toLowerCase;
#if ORO_RUNTIME_PLATFORM_WINDOWS
using oro::runtime::string::formatWindowsError;
#endif
using oro::runtime::crypto::rand64;
using oro::runtime::Vector;

extern int LLAMA_BUILD_NUMBER;

namespace {
  bool parseBoolValue(const String& raw, bool fallback = false) {
    if (raw.size() == 0) return fallback;
    const auto s = toLowerCase(trim(raw));
    if (s == "1" || s == "true" || s == "on" || s == "yes") return true;
    if (s == "0" || s == "false" || s == "off" || s == "no") return false;
    return fallback;
  }

  bool parseUint64 (const String& input, uint64_t& out) {
    if (input.empty()) return false;
    if (!std::all_of(input.begin(), input.end(), [](unsigned char value) {
      return std::isdigit(value) != 0;
    })) {
      return false;
    }
    try {
      out = std::stoull(input);
      return true;
    } catch (...) {
      return false;
    }
  }

  bool decodeBase64String (const String& value, Vector<uint8_t>& out) {
    auto decoded = bytes::base64::decode(value);
    if (!value.empty() && decoded.empty()) {
      return false;
    }
    out.assign(decoded.begin(), decoded.end());
    return true;
  }

  inline bool isWhitespace (char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
  }

  Vector<String> splitWhitespace (const String& value) {
    Vector<String> tokens;
    String token;

    for (const auto ch : value) {
      if (isWhitespace(ch)) {
        if (!token.empty()) {
          tokens.push_back(token);
          token.clear();
        }
        continue;
      }
      token.push_back(ch);
    }

    if (!token.empty()) {
      tokens.push_back(token);
    }

    return tokens;
  }

  String normaliseSha256PinTokenValue (const String& rawToken) {
    auto value = trim(rawToken);
    if (value.empty()) {
      return "";
    }

    const auto lower = toLowerCase(value);
    if (lower.rfind("sha256/", 0) == 0) {
      value = value.substr(String("sha256/").size());
    }

    // Normalise Base64URL -> Base64.
    for (auto& ch : value) {
      if (ch == '-') {
        ch = '+';
      } else if (ch == '_') {
        ch = '/';
      }
    }

    // Ensure proper padding for decoding.
    const auto remainder = value.size() % 4;
    if (remainder != 0) {
      const auto pad = 4 - remainder;
      value.append(pad, '=');
    }

    const auto decoded = bytes::base64::decode(value);
    if (!value.empty() && decoded.empty()) {
      return "";
    }

    if (decoded.size() != 32) {
      return "";
    }

    Vector<uint8_t> digest(decoded.begin(), decoded.end());
    return bytes::base64::encode(digest);
  }

  bool validateTlsPinsConfigValue (const String& raw, String& outMessage) {
    if (trim(raw).empty()) {
      return true;
    }

    const auto lines = split(raw, '\n');
    size_t lineNumber = 0;
    for (auto line : lines) {
      ++lineNumber;
      line = trim(line);

      if (line.empty()) {
        continue;
      }

      // Strip trailing comments.
      const auto comment = line.find_first_of("#;");
      if (comment != String::npos) {
        line = trim(line.substr(0, comment));
      }

      if (line.empty()) {
        continue;
      }

      const auto tokens = splitWhitespace(line);
      if (tokens.empty()) {
        continue;
      }

      const auto first = trim(tokens[0]);
      const auto lowerFirst = toLowerCase(first);
      if (lowerFirst.rfind("sha256/", 0) == 0 || !normaliseSha256PinTokenValue(first).empty()) {
        outMessage = "Pins entries must begin with a host, not a pin token (line " +
          std::to_string(lineNumber) + ")";
        return false;
      }

      const auto host = webview::normaliseTlsPinEndpoint(first);
      if (host.empty()) {
        outMessage = "Invalid host in pins entry (line " + std::to_string(lineNumber) + ")";
        return false;
      }

      if (tokens.size() < 2) {
        outMessage = "Pins entry for '" + host + "' must include at least one sha256 pin token (line " +
          std::to_string(lineNumber) + ")";
        return false;
      }

      for (size_t i = 1; i < tokens.size(); ++i) {
        const auto token = trim(tokens[i]);
        if (normaliseSha256PinTokenValue(token).empty()) {
          outMessage = "Invalid sha256 pin token for '" + host + "' (line " + std::to_string(lineNumber) + ")";
          return false;
        }
      }
    }

    return true;
  }

  String mergeTlsPinsConfigValue (const String& existing, const String& incoming) {
    const bool hadExistingNewline = !existing.empty() && existing.back() == '\n';
    const bool hadIncomingNewline = !incoming.empty() && incoming.back() == '\n';

    // Preserve potentially-invalid existing configs to avoid changing security semantics.
    // The incoming value is always validated before reaching this merge.
    if (!trim(existing).empty()) {
      String validateMessage;
      if (!validateTlsPinsConfigValue(existing, validateMessage)) {
        String combined = existing;
        const auto pins = trim(incoming);
        if (!pins.empty()) {
          if (!combined.empty() && combined.back() != '\n') {
            combined += "\n";
          }
          combined += incoming;
        }
        return combined;
      }
    }

    UnorderedSet<String> seen;
    Vector<String> outLines;

    const auto addPinsFromValue = [&](const String& raw) {
      if (trim(raw).empty()) {
        return;
      }

      const auto lines = split(raw, '\n');
      for (auto line : lines) {
        line = trim(line);

        if (line.empty()) {
          continue;
        }

        // Strip trailing comments.
        const auto comment = line.find_first_of("#;");
        if (comment != String::npos) {
          line = trim(line.substr(0, comment));
        }

        if (line.empty()) {
          continue;
        }

        const auto tokens = splitWhitespace(line);
        if (tokens.size() < 2) {
          continue;
        }

        const auto firstToken = trim(tokens[0]);
        const auto lowerFirst = toLowerCase(firstToken);
        if (lowerFirst.rfind("sha256/", 0) == 0 || !normaliseSha256PinTokenValue(firstToken).empty()) {
          continue;
        }

        const auto host = webview::normaliseTlsPinEndpoint(firstToken);
        if (host.empty()) {
          continue;
        }

        for (size_t i = 1; i < tokens.size(); ++i) {
          const auto pin = normaliseSha256PinTokenValue(tokens[i]);
          if (pin.empty()) {
            continue;
          }

          String key = host;
          key.push_back('\n');
          key += pin;

          if (seen.contains(key)) {
            continue;
          }
          seen.emplace(key);
          outLines.push_back(host + " sha256/" + pin);
        }
      }
    };

    addPinsFromValue(existing);
    addPinsFromValue(incoming);

    String merged;
    for (size_t i = 0; i < outLines.size(); ++i) {
      if (i > 0) {
        merged += "\n";
      }
      merged += outLines[i];
    }

    if ((hadExistingNewline || hadIncomingNewline) && !merged.empty() && merged.back() != '\n') {
      merged += "\n";
    }

    return merged;
  }
}

#define REQUIRE_AND_GET_MESSAGE_VALUE(var, name, parse, ...)                   \
  try {                                                                        \
    var = parse(message.get(name, ##__VA_ARGS__));                             \
  } catch (...) {                                                              \
    return reply(Result::Err { message, JSON::Object::Entries {                \
      {"message", "Invalid '" name "' given in parameters"}                    \
    }});                                                                       \
  }

#define RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)                     \
  [message, reply](auto seq, auto json, auto queuedResponse) {                 \
    auto result = Result { seq, message, json, queuedResponse };               \
    normalizeResultSourceFromPayload(result);                                  \
    reply(std::move(result));                                                   \
  }

static inline String extractSourceFromPayload (const JSON::Any& value) {
  if (value.type == JSON::Type::Object) {
    const auto object = value.as<JSON::Object>();
    if (object.has("source")) {
      const auto source = object.get("source");
      if (source.type == JSON::Type::String) {
        return source.as<JSON::String>().data;
      }
    }
  }

  return "";
}

static inline void normalizeResultSourceFromPayload (Result& result) {
  const auto source = extractSourceFromPayload(result.value);
  if (source.size() == 0) {
    return;
  }

  if (source == "mcp.tool.invoke") {
    result.source = "mcp.server.invokeTool";
  } else {
    result.source = source;
  }
}

static bool trySendResultOverConduit (Router* router, Result& result) {
  if (router == nullptr) {
    return false;
  }

  const auto runtime = router->bridge.getRuntime();
  if (runtime == nullptr) {
    return false;
  }

  auto conduitParam = result.message.get("__conduit_id");
  if (conduitParam.size() == 0) {
    conduitParam = result.message.get("conduit-id");
  }

  uint64_t conduitId = 0;
  bool hasConduitId = false;

  if (conduitParam.size() > 0) {
    try {
      conduitId = std::stoull(conduitParam);
      hasConduitId = true;
    } catch (...) {
      return false;
    }
  } else if (result.message.client.id > 0) {
    conduitId = static_cast<uint64_t>(result.message.client.id);
    hasConduitId = true;
  } else {
    const auto fallbackParam = result.message.get("id");
    if (fallbackParam.size() == 0) {
      return false;
    }
    try {
      conduitId = std::stoull(fallbackParam);
      hasConduitId = true;
    } catch (...) {
      return false;
    }
  }

  if (!hasConduitId) {
    return false;
  }

  auto& conduitService = runtime->services.conduit;
  if (!conduitService.has(conduitId)) {
    return false;
  }

  auto client = conduitService.get(conduitId);
  if (client == nullptr) {
    return false;
  }

  oro::runtime::core::services::Conduit::Message::Options options;

  if (result.source.size() > 0) {
    options.emplace("route", result.source);
  } else if (result.message.name.size() > 0) {
    options.emplace("route", result.message.name);
  }

  if (result.seq != "-1" && result.token.size() > 0) {
    options.emplace("token", result.token);
    options.emplace("ipc-token", result.token);
  }

  auto payload = result.queuedResponse.body;
  size_t length = static_cast<size_t>(result.queuedResponse.length);
  SharedPointer<unsigned char[]> allocated = nullptr;

  if (payload == nullptr || length == 0) {
    const auto data = result.str();
    length = data.size();
    const auto size = std::max<size_t>(length, 1);
    allocated = std::make_shared<unsigned char[]>(size);
    if (length > 0) {
      std::memcpy(allocated.get(), data.c_str(), length);
    }
    payload = allocated;
  }

  const auto sent = client->send(options, payload, length);
  return sent;
}

static inline void replyConduitAware (
  Router* router,
  const Router::ReplyCallback& reply,
  Result&& result
) {
  normalizeResultSourceFromPayload(result);

  if (!trySendResultOverConduit(router, result)) {
    reply(std::move(result));
  }
}

static JSON::Any validateMessageParameters (
  const Message& message,
  const Vector<String> names
) {
  for (const auto& name : names) {
    if (!message.has(name) || message.get(name).size() == 0) {
      return JSON::Object::Entries {
        {"message", "Expecting '" + name + "' in parameters"}
      };
    }
  }

  return nullptr;
}

namespace {
  constexpr int64_t kNanosecondsPerSecond = 1000000000LL;
  constexpr int kClockRealtimeId = 0;
  constexpr int kClockMonotonicId = 1;
  constexpr int kTimerAbstimeFlag = 1;
  constexpr uint64_t kMaxDiagnosticStreamItems = 256;
  constexpr uint64_t kMaxDiagnosticStreamInterval = 1000;
  constexpr uint64_t kMaxDiagnosticChunkBytes = 64 * 1024;
  constexpr uint64_t kMaxDiagnosticStreamBytes = 4 * 1024 * 1024;

  bool parseDiagnosticStreamParameter (
    const Message& message,
    const String& name,
    uint64_t fallback,
    uint64_t minimum,
    uint64_t maximum,
    uint64_t& value,
    String& error
  ) {
    value = fallback;
    if (!message.has(name)) {
      return true;
    }

    if (
      !parseUint64(message.get(name), value) ||
      value < minimum ||
      value > maximum
    ) {
      error = "'" + name + "' must be between " +
        std::to_string(minimum) + " and " + std::to_string(maximum);
      return false;
    }

    return true;
  }

  std::optional<std::chrono::nanoseconds> durationFromTimespec (
    int64_t seconds,
    int64_t nanoseconds
  ) {
    if (seconds < 0 || nanoseconds < 0 || nanoseconds >= kNanosecondsPerSecond) {
      return std::nullopt;
    }

    const auto max = std::numeric_limits<int64_t>::max();
    if (seconds > max / kNanosecondsPerSecond) {
      return std::nullopt;
    }

    const auto nanosFromSeconds = seconds * kNanosecondsPerSecond;
    if (nanosFromSeconds > max - nanoseconds) {
      return std::nullopt;
    }

    return std::chrono::nanoseconds(nanosFromSeconds + nanoseconds);
  }

  std::chrono::nanoseconds monotonicNow () {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()
    );
  }

  std::chrono::nanoseconds realtimeNow () {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()
    );
  }
}

static inline bool tlsServiceEnabled (Router* router) {
  return router->bridge.getRuntime()->services.tls.enabled.load();
}

static inline bool tcpServiceEnabled (Router* router) {
  return router->bridge.getRuntime()->services.tcp.enabled.load();
}

static inline bool updateServiceEnabled (Router* router) {
  return router->bridge.getRuntime()->services.update.enabled.load();
}

static Vector<String> parseDelimitedList (const String& input) {
  Vector<String> result;
  if (input.empty()) return result;

  for (auto token : split(input, ',')) {
    token = trim(token);
    if (!token.empty()) {
      result.push_back(token);
    }
  }

  return result;
}

static inline String bluetoothSubscriptionKey (
  const String& deviceId,
  const String& serviceId,
  const String& characteristicId
) {
  String key = deviceId;
  key += "|";
  key += serviceId;
  key += "|";
  key += characteristicId;
  return key;
}

static void mapIPCRoutes (Router *router) {
  auto userConfig = router->bridge.getRuntime()->userConfig;

  #if ORO_RUNTIME_PLATFORM_APPLE
    auto bundleIdentifier = userConfig["meta_bundle_identifier"];
    auto ORO_RUNTIME_OS_LOG_BUNDLE = os_log_create(bundleIdentifier.c_str(), "oro.runtime");
  #endif

  /**
   * Loads a LLM model by name. A directory path where the model is located
   * can be given. The runtime will attempt to use the
   * `ORO_AI_LLM_MODEL_PATH` environment variable,
   * the `[ai.llm.model] path` user config value, or the application resources
   * directory when trying to load a model.
   * @param name
   * @param path
   * @param gpuLayerCount
   */
  router->map("ai.llm.model.load", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto options = core::services::AI::LLM::LoadModelOptions {};
    options.name = message.get("name");
    options.directory = message.get("directory");

    if (message.has("gpuLayerCount")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.gpuLayerCount, "gpuLayerCount", std::stoul);
    }

    app->runtime.services.ai.llm.loadModel(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.create", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    auto err = validateMessageParameters(message, {"inputSize", "outputSize"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    core::services::AI::ANN::CreateModelOptions options;
    options.name = message.get("name");

    size_t inputSize = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(inputSize, "inputSize", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    size_t outputSize = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(outputSize, "outputSize", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    options.inputSize = inputSize;
    options.outputLayer.size = outputSize;
    options.outputLayer.activation = message.get("outputActivation", "linear");

    if (message.has("hiddenLayers")) {
      const auto rawHidden = message.get("hiddenLayers");
      if (!rawHidden.empty()) {
        try {
          const auto parsedHidden = JSON::parse(rawHidden);
          if (parsedHidden.type == JSON::Type::Array) {
            const auto array = parsedHidden.template as<JSON::Array>();
            for (const auto& entry : array.value()) {
              if (entry.type != JSON::Type::Object) {
                continue;
              }
              const auto object = entry.template as<JSON::Object>();
              ann::LayerConfig config;
              if (object.has("size")) {
                const auto sizeValue = object.get("size");
                if (sizeValue.type == JSON::Type::Number) {
                  config.size = static_cast<size_t>(sizeValue.template as<JSON::Number>().value());
                } else if (sizeValue.type == JSON::Type::String) {
                  config.size = static_cast<size_t>(std::stoull(sizeValue.template as<JSON::String>().value()));
                }
              }
              if (config.size == 0) {
                continue;
              }
              if (object.has("activation")) {
                const auto activationValue = object.get("activation");
                if (activationValue.type == JSON::Type::String) {
                  config.activation = activationValue.template as<JSON::String>().value();
                }
              }
              options.hiddenLayers.push_back(config);
            }
          }
        } catch (...) {
          return reply(Result::Err {
            message,
            JSON::Object::Entries {{"message", "Invalid 'hiddenLayers' parameter"}}
          });
        }
      }
    }

    service.createModel(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.load", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    auto err = validateMessageParameters(message, {"path"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    core::services::AI::ANN::LoadModelOptions options;
    options.path = message.get("path");
    options.name = message.get("name");

    if (options.path.empty()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Model path is required"}}
      });
    }

    service.loadModel(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.save", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    auto err = validateMessageParameters(message, {"id", "path"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    if (!parseUint64(message.get("id"), id) || id == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid 'id' given in parameters"}}
      });
    }

    core::services::AI::ANN::SaveModelOptions options;
    options.id = id;
    options.path = message.get("path");

    if (options.path.empty()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Model path is required"}}
      });
    }

    service.saveModel(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.remove", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    core::services::AI::ANN::RemoveModelOptions options;
    uint64_t id = 0;
    if (parseUint64(message.get("id"), id) && id > 0) {
      options.id = id;
    }
    options.name = message.get("name");

    if (options.id == 0 && options.name.empty()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Model id or name is required"}}
      });
    }

    service.removeModel(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.list", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    service.listModels(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.train", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    auto err = validateMessageParameters(message, {"id", "rows", "featureCols", "labelCols"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    if (!parseUint64(message.get("id"), id) || id == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid 'id' given in parameters"}}
      });
    }

    size_t rows = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(rows, "rows", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    size_t featureCols = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(featureCols, "featureCols", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    size_t labelCols = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(labelCols, "labelCols", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    if (rows == 0 || featureCols == 0 || labelCols == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Rows and column counts must be greater than 0"}}
      });
    }

    const auto bufferSize = message.buffer.size();
    if (bufferSize == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Training data buffer is required"}}
      });
    }

    const size_t featureCount = rows * featureCols;
    const size_t labelCount = rows * labelCols;
    const size_t expectedBytes = (featureCount + labelCount) * sizeof(float);
    if (bufferSize < expectedBytes) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Training buffer size does not match provided dimensions"}}
      });
    }

    core::services::AI::ANN::TrainRequest request;
    request.id = id;
    request.rows = rows;
    request.featureColumns = featureCols;
    request.labelColumns = labelCols;

    request.features.resize(featureCount);
    request.labels.resize(labelCount);

    const auto* raw = reinterpret_cast<const unsigned char*>(message.buffer.data());
    if (raw == nullptr) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Training data buffer is unavailable"}}
      });
    }
    std::memcpy(
      request.features.data(),
      raw,
      featureCount * sizeof(float)
    );
    std::memcpy(
      request.labels.data(),
      raw + (featureCount * sizeof(float)),
      labelCount * sizeof(float)
    );

    ann::TrainingOptions options;
    const auto loss = message.get("loss");
    if (!loss.empty()) {
      options.lossFunction = ann::lossFunctionFromString(loss);
    }

    if (message.has("batchSize")) {
      try {
        options.batchSize = static_cast<size_t>(std::stoull(message.get("batchSize")));
      } catch (...) {}
    }

    if (message.has("learningRate")) {
      try {
        options.learningRate = std::stof(message.get("learningRate"));
      } catch (...) {}
    }

    if (message.has("searchTime")) {
      try {
        options.searchTime = std::stof(message.get("searchTime"));
      } catch (...) {}
    }

    if (message.has("regularizationStrength")) {
      try {
        options.regularizationStrength = std::stof(message.get("regularizationStrength"));
      } catch (...) {}
    }

    if (message.has("momentumFactor")) {
      try {
        options.momentumFactor = std::stof(message.get("momentumFactor"));
      } catch (...) {}
    }

    if (message.has("maxEpochs")) {
      try {
        options.maxEpochs = std::stoi(message.get("maxEpochs"));
      } catch (...) {}
    }

    if (message.has("shuffle")) {
      const auto value = message.get("shuffle");
      options.shuffle = (value == "1" || toLowerCase(value) == "true");
    }

    if (message.has("verbose")) {
      const auto value = message.get("verbose");
      options.verbose = (value == "1" || toLowerCase(value) == "true");
    }

    request.options = options;

    service.train(
      message.seq,
      request,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.predict", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    auto err = validateMessageParameters(message, {"id", "rows", "featureCols"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    if (!parseUint64(message.get("id"), id) || id == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid 'id' given in parameters"}}
      });
    }

    size_t rows = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(rows, "rows", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    size_t featureCols = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(featureCols, "featureCols", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    if (rows == 0 || featureCols == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Rows and feature column count must be greater than 0"}}
      });
    }

    const auto bufferSize = message.buffer.size();
    if (bufferSize == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Prediction data buffer is required"}}
      });
    }

    const size_t featureCount = rows * featureCols;
    const size_t expectedBytes = featureCount * sizeof(float);
    if (bufferSize < expectedBytes) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Prediction buffer size does not match provided dimensions"}}
      });
    }

    core::services::AI::ANN::PredictRequest request;
    request.id = id;
    request.rows = rows;
    request.featureColumns = featureCols;
    request.features.resize(featureCount);

    const auto* raw = reinterpret_cast<const unsigned char*>(message.buffer.data());
    if (raw == nullptr) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Prediction data buffer is unavailable"}}
      });
    }
    std::memcpy(
      request.features.data(),
      raw,
      featureCount * sizeof(float)
    );

    service.infer(
      message.seq,
      request,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.ann.model.accuracy", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ai.ann;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "ANN service is disabled"}}
      });
    }

    auto err = validateMessageParameters(message, {"id", "rows", "featureCols", "labelCols"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    if (!parseUint64(message.get("id"), id) || id == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid 'id' given in parameters"}}
      });
    }

    size_t rows = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(rows, "rows", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    size_t featureCols = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(featureCols, "featureCols", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    size_t labelCols = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(labelCols, "labelCols", [](const auto& value) {
      return static_cast<size_t>(std::stoull(value));
    });

    if (rows == 0 || featureCols == 0 || labelCols == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Rows and column counts must be greater than 0"}}
      });
    }

    const auto bufferSize = message.buffer.size();
    if (bufferSize == 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Evaluation data buffer is required"}}
      });
    }

    const size_t featureCount = rows * featureCols;
    const size_t labelCount = rows * labelCols;
    const size_t expectedBytes = (featureCount + labelCount) * sizeof(float);
    if (bufferSize < expectedBytes) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Evaluation buffer size does not match provided dimensions"}}
      });
    }

    core::services::AI::ANN::AccuracyRequest request;
    request.id = id;
    request.rows = rows;
    request.featureColumns = featureCols;
    request.labelColumns = labelCols;
    request.features.resize(featureCount);
    request.labels.resize(labelCount);

    const auto* raw = reinterpret_cast<const unsigned char*>(message.buffer.data());
    if (raw == nullptr) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Evaluation data buffer is unavailable"}}
      });
    }
    std::memcpy(
      request.features.data(),
      raw,
      featureCount * sizeof(float)
    );
    std::memcpy(
      request.labels.data(),
      raw + (featureCount * sizeof(float)),
      labelCount * sizeof(float)
    );

    service.evaluate(
      message.seq,
      request,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Whisper speech-to-text: load a model into memory.
   * Either `id` or `name` must be provided as an identifier.
   * @param id
   * @param name
   * @param directory
   * @param threadCount
   * @param statePoolLimit
   * @param useGPU
   * @param gpuDevice
   */
  router->map("ai.whisper.model.load", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    core::services::AI::Speech::LoadModelOptions options;

    uint64_t id = 0;
    if (parseUint64(message.get("id"), id) && id > 0) {
      options.id = id;
    }
    options.name = message.get("name");

    if (options.id == 0 && options.name.empty()) {
      const auto err = JSON::Object::Entries {
        {"message", "Model identifier (id or name) is required"}
      };
      return reply(Result::Err { message, err });
    }

    options.directory = message.get("directory");

    const auto threadCountRaw = message.get("threadCount");
    if (!threadCountRaw.empty()) {
      try {
        options.threadCount = static_cast<size_t>(std::stoull(threadCountRaw));
      } catch (...) {}
    }

    const auto statePoolLimitRaw = message.get("statePoolLimit");
    if (!statePoolLimitRaw.empty()) {
      try {
        options.statePoolLimit = static_cast<size_t>(std::stoull(statePoolLimitRaw));
      } catch (...) {}
    }

    const auto useGpuRaw = message.get("useGPU");
    if (!useGpuRaw.empty()) {
      options.useGPU = parseBoolValue(useGpuRaw, false);
    }

    const auto gpuDeviceRaw = message.get("gpuDevice");
    if (!gpuDeviceRaw.empty()) {
      try {
        options.gpuDevice = std::stoi(gpuDeviceRaw);
      } catch (...) {}
    }

    app->runtime.services.ai.speech.loadModel(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.whisper.model.unload", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    core::services::AI::Speech::UnloadModelOptions options;

    uint64_t id = 0;
    if (parseUint64(message.get("id"), id) && id > 0) {
      options.id = id;
    }
    options.name = message.get("name");

    if (options.id == 0 && options.name.empty()) {
      const auto err = JSON::Object::Entries {
        {"message", "Model identifier (id or name) is required"}
      };
      return reply(Result::Err { message, err });
    }

    app->runtime.services.ai.speech.unloadModel(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.whisper.model.list", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    app->runtime.services.ai.speech.listModels(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Transcribe PCM audio using a loaded Whisper model.
   * Audio samples are provided in the request body as either
   * 16-bit PCM (`format=pcm16`) or 32-bit float (`format=f32`).
   * @param id
   * @param name
   * @param format
   * @param sampleRate
   * @param channels
   * @param normalize
   * @param stream
   * @param language
   * @param translate
   * @param detectLanguage
   * @param timestamps
   * @param wordTimestamps
   * @param diarize
   * @param threadCount
   * @param maxSegmentLength
   * @param temperature
   * @param temperatureIncrement
   * @param entropyThreshold
   * @param logProbThreshold
   * @param noSpeechThreshold
   * @param enableVAD
   * @param vadModelPath
   * @param conduit
   */
  router->map("ai.whisper.transcribe", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();

    const auto bufferSize = message.buffer.size();
    if (bufferSize == 0 || message.buffer.data() == nullptr) {
      const auto err = JSON::Object::Entries {
        {"message", "Audio buffer is required for transcription"}
      };
      return reply(Result::Err { message, err });
    }

    core::services::AI::Speech::TranscribeRequest request;

    uint64_t id = 0;
    if (parseUint64(message.get("id"), id) && id > 0) {
      request.id = id;
    }
    request.name = message.get("name");

    auto format = toLowerCase(trim(message.get("format", "f32")));
    if (format.empty()) {
      format = "f32";
    }

    int sampleRate = 16000;
    {
      auto raw = trim(message.get("sampleRate"));
      if (!raw.empty()) {
        try {
          sampleRate = std::stoi(raw);
        } catch (...) {}
      }
      if (sampleRate <= 0) {
        sampleRate = 16000;
      }
    }

    size_t channels = 1;
    {
      auto raw = trim(message.get("channels"));
      if (!raw.empty()) {
        try {
          channels = static_cast<size_t>(std::stoull(raw));
        } catch (...) {}
      }
      if (channels == 0) {
        channels = 1;
      }
    }

    auto& options = request.options;
    options.inputSampleRate = sampleRate;
    options.channels = channels;

    const auto* rawBytes = reinterpret_cast<const unsigned char*>(message.buffer.data());
    constexpr int kTargetSampleRate = 16000;

    const size_t bytesPerSample = (format == "pcm16" || format == "s16") ? sizeof(int16_t) : sizeof(float);
    if (bufferSize < bytesPerSample) {
      const auto err = JSON::Object::Entries {
        {"message", "Audio buffer is too small for requested format"}
      };
      return reply(Result::Err { message, err });
    }

    const size_t totalSamples = bufferSize / bytesPerSample;
    const size_t frames = channels > 0 ? totalSamples / channels : totalSamples;
    if (frames == 0) {
      const auto err = JSON::Object::Entries {
        {"message", "Audio buffer is empty"}
      };
      return reply(Result::Err { message, err });
    }

    options.inputSamples = frames * channels;
    Vector<float> mono;
    mono.resize(frames);

    if (format == "pcm16" || format == "s16") {
      for (size_t i = 0; i < frames; ++i) {
        float acc = 0.0f;
        for (size_t ch = 0; ch < channels; ++ch) {
          const size_t index = (i * channels + ch) * sizeof(int16_t);
          int16_t sample = 0;
          std::memcpy(&sample, rawBytes + index, sizeof(int16_t));
          acc += static_cast<float>(sample) / 32768.0f;
        }
        mono[i] = acc / static_cast<float>(channels);
      }
    } else {
      for (size_t i = 0; i < frames; ++i) {
        float acc = 0.0f;
        for (size_t ch = 0; ch < channels; ++ch) {
          const size_t index = (i * channels + ch) * sizeof(float);
          float sample = 0.0f;
          std::memcpy(&sample, rawBytes + index, sizeof(float));
          acc += sample;
        }
        mono[i] = acc / static_cast<float>(channels);
      }
    }

    if (sampleRate != kTargetSampleRate) {
      const double ratio = static_cast<double>(kTargetSampleRate) / static_cast<double>(sampleRate);
      const size_t outSamples = static_cast<size_t>(std::ceil(static_cast<double>(frames) * ratio));
      Vector<float> resampled;
      resampled.resize(outSamples);

      for (size_t i = 0; i < outSamples; ++i) {
        const double srcIndex = static_cast<double>(i) / ratio;
        const size_t left = static_cast<size_t>(srcIndex);
        const size_t right = left + 1 < frames ? left + 1 : frames - 1;
        const double frac = srcIndex - static_cast<double>(left);
        const float leftVal = mono[left];
        const float rightVal = mono[right];
        resampled[i] = static_cast<float>((1.0 - frac) * leftVal + frac * rightVal);
      }

      options.pcmf32 = std::move(resampled);
      options.resampled = true;
    } else {
      options.pcmf32 = std::move(mono);
      options.resampled = false;
    }

    const auto normalizeRaw = message.get("normalize");
    bool normalize = false;
    if (!normalizeRaw.empty()) {
      normalize = parseBoolValue(normalizeRaw, false);
    }

    if (normalize) {
      float maxAbs = 0.0f;
      for (const auto value : options.pcmf32) {
        const float absVal = std::fabs(value);
        if (absVal > maxAbs) {
          maxAbs = absVal;
        }
      }
      if (maxAbs > 0.0f) {
        const float scale = 1.0f / maxAbs;
        for (auto& value : options.pcmf32) {
          value *= scale;
        }
      }
      options.normalized = true;
    }

    options.stream = parseBoolValue(message.get("stream"), false);
    options.language = message.get("language");
    options.translate = parseBoolValue(message.get("translate"), false);
    options.detectLanguage = parseBoolValue(message.get("detectLanguage"), false);

    const auto timestampsRaw = message.get("timestamps");
    if (!timestampsRaw.empty()) {
      options.enableTimestamps = parseBoolValue(timestampsRaw, true);
    }

    options.enableWordTimestamps = parseBoolValue(message.get("wordTimestamps"), false);
    options.diarize = parseBoolValue(message.get("diarize"), false);

    auto threadCountRaw = message.get("threadCount");
    if (!threadCountRaw.empty()) {
      try {
        options.threadCount = static_cast<size_t>(std::stoull(threadCountRaw));
      } catch (...) {}
    }

    auto maxSegmentLenRaw = message.get("maxSegmentLength");
    if (!maxSegmentLenRaw.empty()) {
      try {
        options.maxSegmentLength = std::stoi(maxSegmentLenRaw);
      } catch (...) {}
    }

    auto tempRaw = message.get("temperature");
    if (!tempRaw.empty()) {
      try {
        options.temperature = std::stof(tempRaw);
      } catch (...) {}
    }

    auto tempIncRaw = message.get("temperatureIncrement");
    if (!tempIncRaw.empty()) {
      try {
        options.temperatureIncrement = std::stof(tempIncRaw);
      } catch (...) {}
    }

    auto entropyRaw = message.get("entropyThreshold");
    if (!entropyRaw.empty()) {
      try {
        options.entropyThreshold = std::stof(entropyRaw);
      } catch (...) {}
    }

    auto logProbRaw = message.get("logProbThreshold");
    if (!logProbRaw.empty()) {
      try {
        options.logProbThreshold = std::stof(logProbRaw);
      } catch (...) {}
    }

    auto noSpeechRaw = message.get("noSpeechThreshold");
    if (!noSpeechRaw.empty()) {
      try {
        options.noSpeechThreshold = std::stof(noSpeechRaw);
      } catch (...) {}
    }

    options.enableVAD = parseBoolValue(message.get("enableVAD"), false);
    options.vadModelPath = message.get("vadModelPath");

    const auto conduitRaw = message.get("conduit");
    if (!conduitRaw.empty()) {
      try {
        request.conduitId = std::stoull(conduitRaw);
      } catch (...) {
        request.conduitId = 0;
      }
    }

    app->runtime.services.ai.speech.transcribe(
      message.seq,
      request,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.init", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    service.init(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.shutdown", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    service.shutdown(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.status", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    service.status(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.setLogLevel", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"level"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    auto levelValue = trim(message.get("level"));
    iroh::LogLevel level = iroh::LogLevel::Info;
    bool parsed = false;

    if (!levelValue.empty()) {
      if (iroh::fromString(levelValue, level)) {
        parsed = true;
      } else {
        try {
          const auto numeric = std::stoi(levelValue);
          if (numeric >= 0 && numeric <= static_cast<int>(iroh::LogLevel::Off)) {
            level = static_cast<iroh::LogLevel>(numeric);
            parsed = true;
          }
        } catch (...) {}
      }
    }

    if (!parsed) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid 'level' given in parameters"}}
      });
    }

    service.setLogLevel(
      message.seq,
      level,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.pathToKey", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    auto encoding = toLowerCase(message.get("encoding", "base64"));
    if (encoding != "base64") {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Unsupported encoding"}}
      });
    }

    const auto path = message.get("path");
    std::optional<String> prefix;
    if (message.has("prefix")) {
      prefix = message.get("prefix");
    }

    std::optional<String> root;
    if (message.has("root")) {
      root = message.get("root");
    }

    service.pathToKey(
      message.seq,
      path,
      prefix,
      root,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.keyToPath", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"key"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    auto encoding = toLowerCase(message.get("encoding", "base64"));
    if (encoding != "base64") {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Unsupported encoding"}}
      });
    }

    Vector<uint8_t> keyBytes;
    if (!decodeBase64String(message.get("key"), keyBytes)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid key encoding"}}
      });
    }

    std::optional<String> prefix;
    if (message.has("prefix")) {
      prefix = message.get("prefix");
    }

    std::optional<String> root;
    if (message.has("root")) {
      root = message.get("root");
    }

    service.keyToPath(
      message.seq,
      keyBytes,
      prefix,
      root,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.endpoint.create", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    if (!parseUint64(message.get("id"), endpointId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint id"}}
      });
    }

    core::services::Iroh::EndpointOptions options;

    if (message.has("secretKey")) {
      options.secretKey = message.get("secretKey");
    }

    const auto relayMode = toLowerCase(message.get("relayMode", ""));
    if (!relayMode.empty()) {
      if (relayMode == "disabled") {
        options.relayMode = iroh::RelayMode::Disabled;
      } else if (relayMode == "default") {
        options.relayMode = iroh::RelayMode::Default;
      } else {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Invalid relayMode value"}}
        });
      }
    }

    const auto discovery = toLowerCase(message.get("discovery", ""));
    if (!discovery.empty()) {
      if (discovery == "none") {
        options.discovery = iroh::DiscoveryConfig::None;
      } else if (discovery == "default") {
        options.discovery = iroh::DiscoveryConfig::Default;
      } else {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Invalid discovery value"}}
        });
      }
    }

    if (message.has("alpns")) {
      const auto raw = message.get("alpns");
      for (const auto& token : string::split(raw, ',')) {
        const auto value = trim(token);
        if (value.empty()) {
          continue;
        }
        Vector<uint8_t> alpn;
        if (!decodeBase64String(value, alpn)) {
          return reply(Result::Err {
            message,
            JSON::Object::Entries {{"message", "Invalid base64 in alpns"}}
          });
        }
        options.alpns.push_back(std::move(alpn));
      }
    }

    service.createEndpoint(
      message.seq,
      endpointId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.endpoint.destroy", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint id"}}
      });
    }

    service.destroyEndpoint(
      message.seq,
      endpointId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.endpoint.bind", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint id"}}
      });
    }

    core::services::Iroh::BindOptions options;
    if (message.has("ipv4")) {
      options.ipv4 = message.get("ipv4");
    }
    if (message.has("ipv6")) {
      options.ipv6 = message.get("ipv6");
    }

    service.bindEndpoint(
      message.seq,
      endpointId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.endpoint.homeRelay", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint id"}}
      });
    }

    service.getHomeRelay(
      message.seq,
      endpointId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.endpoint.nodeAddr", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint id"}}
      });
    }

    service.getNodeAddr(
      message.seq,
      endpointId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.connect", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId", "connectionId", "nodeAddr", "alpn"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId) ||
        !parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint or connection id"}}
      });
    }

    Vector<uint8_t> alpn;
    if (!decodeBase64String(message.get("alpn"), alpn)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid alpn"}}
      });
    }

    core::services::Iroh::ConnectOptions options;
    options.alpn = std::move(alpn);
    options.nodeAddr = message.get("nodeAddr");

    service.connect(
      message.seq,
      endpointId,
      connectionId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.accept", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId", "connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId) ||
        !parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint or connection id"}}
      });
    }

    core::services::Iroh::AcceptOptions options;
    if (message.has("expectedAlpn")) {
      Vector<uint8_t> expected;
      if (!decodeBase64String(message.get("expectedAlpn"), expected)) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Invalid expectedAlpn"}}
        });
      }
      options.expectedAlpn = std::move(expected);
    }

    service.accept(
      message.seq,
      endpointId,
      connectionId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.acceptAny", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId", "connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId) ||
        !parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint or connection id"}}
      });
    }

    service.acceptAny(
      message.seq,
      endpointId,
      connectionId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.endpoint.close", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint id"}}
      });
    }

    service.closeEndpoint(
      message.seq,
      endpointId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.close", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection id"}}
      });
    }

    service.closeConnection(
      message.seq,
      connectionId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.waitClosed", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection id"}}
      });
    }

    service.waitConnectionClosed(
      message.seq,
      connectionId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.stats", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection id"}}
      });
    }

    service.connectionStats(
      message.seq,
      connectionId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.datagram.write", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId", "data"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection id"}}
      });
    }

    Vector<uint8_t> payload;
    if (!decodeBase64String(message.get("data"), payload)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid datagram payload"}}
      });
    }

    core::services::Iroh::DatagramOptions options;
    if (message.has("timeoutMs")) {
      uint64_t timeout = 0;
      if (!parseUint64(message.get("timeoutMs"), timeout)) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Invalid timeout"}}
        });
      }
      options.timeoutMs = timeout;
    }

    service.writeDatagram(
      message.seq,
      connectionId,
      payload,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.datagram.read", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection id"}}
      });
    }

    core::services::Iroh::DatagramOptions options;
    if (message.has("timeoutMs")) {
      uint64_t timeout = 0;
      if (!parseUint64(message.get("timeoutMs"), timeout)) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Invalid timeout"}}
        });
      }
      options.timeoutMs = timeout;
    }

    service.readDatagram(
      message.seq,
      connectionId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connectionType.watch", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"endpointId", "nodeId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID endpointId = 0;
    if (!parseUint64(message.get("endpointId"), endpointId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid endpoint id"}}
      });
    }

    const auto nodeId = message.get("nodeId");
    const auto watchValue = toLowerCase(message.get("watch", "true"));
    const bool enable = watchValue != "false" && watchValue != "0";

    service.watchConnectionType(
      message.seq,
      endpointId,
      nodeId,
      enable,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.openBi", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId", "sendStreamId", "recvStreamId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    core::services::Iroh::ID sendId = 0;
    core::services::Iroh::ID recvId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId) ||
        !parseUint64(message.get("sendStreamId"), sendId) ||
        !parseUint64(message.get("recvStreamId"), recvId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection or stream id"}}
      });
    }

    service.openBidirectionalStream(
      message.seq,
      connectionId,
      sendId,
      recvId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.openUni", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId", "sendStreamId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    core::services::Iroh::ID sendId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId) ||
        !parseUint64(message.get("sendStreamId"), sendId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection or stream id"}}
      });
    }

    service.openUnidirectionalStream(
      message.seq,
      connectionId,
      sendId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.acceptBi", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId", "sendStreamId", "recvStreamId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    core::services::Iroh::ID sendId = 0;
    core::services::Iroh::ID recvId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId) ||
        !parseUint64(message.get("sendStreamId"), sendId) ||
        !parseUint64(message.get("recvStreamId"), recvId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection or stream id"}}
      });
    }

    service.acceptBidirectionalStream(
      message.seq,
      connectionId,
      sendId,
      recvId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.connection.acceptUni", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId", "recvStreamId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID connectionId = 0;
    core::services::Iroh::ID recvId = 0;
    if (!parseUint64(message.get("connectionId"), connectionId) ||
        !parseUint64(message.get("recvStreamId"), recvId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid connection or stream id"}}
      });
    }

    service.acceptUnidirectionalStream(
      message.seq,
      connectionId,
      recvId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.stream.write", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"streamId", "data"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID streamId = 0;
    if (!parseUint64(message.get("streamId"), streamId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid stream id"}}
      });
    }

    Vector<uint8_t> payload;
    if (!decodeBase64String(message.get("data"), payload)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid payload"}}
      });
    }

    core::services::Iroh::StreamWriteOptions options;
    if (message.has("timeoutMs")) {
      uint64_t timeout = 0;
      if (!parseUint64(message.get("timeoutMs"), timeout)) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Invalid timeout"}}
        });
      }
      options.timeoutMs = timeout;
    }

    service.sendStreamWrite(
      message.seq,
      streamId,
      payload,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.stream.finish", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"streamId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID streamId = 0;
    if (!parseUint64(message.get("streamId"), streamId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid stream id"}}
      });
    }

    service.sendStreamFinish(
      message.seq,
      streamId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.stream.read", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"streamId", "length"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID streamId = 0;
    if (!parseUint64(message.get("streamId"), streamId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid stream id"}}
      });
    }

    uint64_t length = 0;
    if (!parseUint64(message.get("length"), length)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid length"}}
      });
    }

    core::services::Iroh::StreamReadOptions options;
    options.length = static_cast<size_t>(length);
    if (message.has("timeoutMs")) {
      uint64_t timeout = 0;
      if (!parseUint64(message.get("timeoutMs"), timeout)) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Invalid timeout"}}
        });
      }
      options.timeoutMs = timeout;
    }

    service.recvStreamRead(
      message.seq,
      streamId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("iroh.stream.readToEnd", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"streamId", "sizeLimit", "timeoutMs"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.iroh;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Iroh service is disabled"}}
      });
    }

    core::services::Iroh::ID streamId = 0;
    if (!parseUint64(message.get("streamId"), streamId)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid stream id"}}
      });
    }

    uint64_t sizeLimit = 0;
    uint64_t timeout = 0;
    if (!parseUint64(message.get("sizeLimit"), sizeLimit) ||
        !parseUint64(message.get("timeoutMs"), timeout)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid sizeLimit or timeout"}}
      });
    }

    core::services::Iroh::StreamReadToEndOptions options;
    options.sizeLimit = static_cast<size_t>(sizeLimit);
    options.timeoutMs = timeout;

    service.recvStreamReadToEnd(
      message.seq,
      streamId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.start", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    core::services::IPFS::StartOptions options;
    if (message.has("repoPath")) {
      options.repoPath = message.get("repoPath");
    }
    if (message.has("port")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.port, "port", std::stoi);
    }

    service.nodeStart(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.stop", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    service.nodeStop(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.status", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    service.nodeStatus(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.add", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    const auto path = message.get("path");
    service.addFile(
      message.seq,
      path,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.get", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"cid", "destination"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    const auto cid = message.get("cid");
    const auto destination = message.get("destination");
    const bool pinResult = toLowerCase(message.get("pin", "false")) == "true";

    service.fetch(
      message.seq,
      cid,
      destination,
      pinResult,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.pin", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"cid"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    const auto cid = message.get("cid");
    service.pin(
      message.seq,
      cid,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.unpin", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"cid"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    const auto cid = message.get("cid");
    service.unpin(
      message.seq,
      cid,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.gc", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    service.garbageCollect(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.peerId", [=](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    service.peerId(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.addPeer", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"address"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    const auto address = message.get("address");
    service.addPeer(
      message.seq,
      address,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ipfs.removePeer", [=](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"address"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto& service = router->bridge.getRuntime()->services.ipfs;
    if (!service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "IPFS service is disabled"}}
      });
    }

    const auto address = message.get("address");
    service.removePeer(
      message.seq,
      address,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("ai.llm.model.list", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    app->runtime.services.ai.llm.listModels(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Creates a new LLM Context for a given model with optional parameters.
   * @param model
   * @param id
   * @param size
   * @param minP
   * @param temp
   * @param topK
   */
  router->map("ai.llm.context.create", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"model"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto options = core::services::AI::LLM::CreateContextOptions {};
    if (message.has("size")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.size, "size", std::stoul);
    }

    if (message.has("minP")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.minP, "minP", std::stof);
    }

    if (message.has("temp")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.temp, "temp", std::stof);
    }

    if (message.has("topK")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.topK, "topK", std::stoi);
    }

    if (message.has("id")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.id, "id", std::stoull);
    }

    SharedPointer<ai::llm::Model> model = nullptr;
    const auto modelParam = message.get("model");
    ai::llm::ID modelId = 0;

    try {
      modelId = std::stoull(modelParam);
    } catch (...) {}

    {
      Lock lock(app->runtime.services.ai.llm.manager.mutex);
      if (modelId > 0) {
        for (const auto& entry : app->runtime.services.ai.llm.manager.models) {
          if (entry.second->id == modelId) {
            model = entry.second;
            break;
          }
        }
      } else if (modelParam.size() > 0) {
        const auto& models = app->runtime.services.ai.llm.manager.models;
        const auto it = models.find(modelParam);
        if (it != models.end()) {
          model = it->second;
        }
      }
    }

    if (model == nullptr) {
      const auto json = JSON::Object::Entries {
        {"source", "ai.llm.context.create"},
        {"err", JSON::Object::Entries {
          {"message", "Model is not loaded or does not exist"},
        }}
      };

      return reply(Result { message.seq, message, json });
    }

    options.model.id = model->id;
    options.model.name = model->name;
    options.model.directory = model->options.directory;
    options.model.gpuLayerCount = model->options.gpuLayerCount;
    app->runtime.services.ai.llm.createContext(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * @param id
   */
  router->map("ai.llm.context.destroy", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.llm.destroyContext(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * @param id
   */
  router->map("ai.llm.context.info", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.llm.getContextStats(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns usage statistics for an LLM context.
   * @param id
   */
  router->map("ai.llm.context.stats", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.llm.getContextStats(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Adds an ai chat session message
   * @param id
   * @param prompt
   */
  router->map("ai.chat.session.message", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = message.buffer.size() > 0
      ? validateMessageParameters(message, {"id"})
      : validateMessageParameters(message, {"id", "prompt"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto prompt = trim(message.get("prompt", message.buffer.str()));
    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.chat.message(
      message.seq,
      id,
      { prompt },
      [=](auto seq, auto json, auto queuedResponse) {
        if (seq == "-1" && app->runtime.services.conduit.has(id)) {
          auto client = app->runtime.services.conduit.get(id);
          client->send(
            {
              {"source", message.name},
              {"eog", json
                .template as<JSON::Object>()
                .get("data")
                .template as<JSON::Object>()
                .get("eog")
                .template as<JSON::Boolean>()
                .str()
              }
            },
            queuedResponse.body,
            queuedResponse.length
          );
          return;
        }

        reply(Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Ephemeral chat session prompt generation
   * @param id
   * @param prompt
   */
  router->map("ai.chat.session.generate", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = message.buffer.size() > 0
      ? validateMessageParameters(message, {"id"})
      : validateMessageParameters(message, {"id", "prompt"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto prompt = trim(message.get("prompt", message.buffer.str()));
    const auto antiprompts = split(trim(message.get("antiprompts")), '\x01');

    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.chat.generate(
      message.seq,
      id,
      { prompt, antiprompts },
      [=](auto seq, auto json, auto queuedResponse) {
        if (seq == "-1" && app->runtime.services.conduit.has(id)) {
          auto client = app->runtime.services.conduit.get(id);
          client->send(
            {
              {"source", message.name},
              {"complete", json
                .template as<JSON::Object>()
                .get("data")
                .template as<JSON::Object>()
                .get("eog")
                .template as<JSON::Boolean>()
                .str()
              }
            },
            queuedResponse.body,
            queuedResponse.length
          );
          return;
        }

        reply(Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * List an ai chat session history
   * @param id
   */
  router->map("ai.chat.session.history", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.chat.history(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * list ai chat sessions
   */
  router->map("ai.chat.list", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    app->runtime.services.ai.chat.list(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   */
  router->map("ai.chat.completions", [](auto message, auto router, auto reply) {
    // auto app = App::sharedApplication();
  });

  /**
   * @param model
   * @param name
   * @param directory
   * @param id
   */
  router->map("ai.llm.lora.load", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    const bool idOnly = message.has("id") && !message.has("name");
    const auto err = idOnly
      ? validateMessageParameters(message, {"id"})
      : validateMessageParameters(message, {"model", "name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID loraId = 0;
    if (message.has("id")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(loraId, "id", std::stoull);
    }

    if (idOnly) {
      const auto lora = app->runtime.services.ai.llm.manager.loadLoRA(loraId);

      if (lora == nullptr) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"type", "NotFoundError"},
          {"message", "LoRA is not loaded"}
        }});
      }

      const auto json = JSON::Object::Entries {
        {"data", lora->json()}
      };

      return reply(Result { message.seq, message, json });
    }

    auto options = core::services::AI::LLM::LoadLoRAOptions {};
    options.id = loraId;
    options.name = message.get("name");
    options.directory = message.get("directory");

    SharedPointer<ai::llm::Model> model = nullptr;
    const auto modelParam = message.get("model");
    ai::llm::ID modelId = 0;

    try {
      modelId = std::stoull(modelParam);
    } catch (...) {}

    if (modelId > 0) {
      Lock lock(app->runtime.services.ai.llm.manager.mutex);
      for (const auto& entry : app->runtime.services.ai.llm.manager.models) {
        if (entry.second->id == modelId) {
          model = entry.second;
          break;
        }
      }
    }

    if (model == nullptr && modelParam.size() > 0) {
      Lock lock(app->runtime.services.ai.llm.manager.mutex);
      const auto& models = app->runtime.services.ai.llm.manager.models;
      const auto it = models.find(modelParam);
      if (it != models.end()) {
        model = it->second;
      }
    }

    if (model == nullptr) {
      const auto json = JSON::Object::Entries {
        {"source", "ai.llm.model.load"},
        {"err", JSON::Object::Entries {
          {"message", "Model is not loaded or does not exist"},
        }}
      };

      return reply(Result { message.seq, message, json });
    }

    options.model.id = model->id;
    options.model.name = model->name;
    options.model.directory = model->options.directory;
    options.model.gpuLayerCount = model->options.gpuLayerCount;

    app->runtime.services.ai.llm.loadLoRA(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * @param id
   * @param context
   * @param scale
   */
  router->map("ai.llm.lora.attach", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id", "context"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto options = ai::llm::LoRA::AttachOptions {};

    ai::llm::ID id = 0;
    ai::llm::ID contextId = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(contextId, "context", std::stoull);

    if (message.has("scale")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.scale, "scale", std::stof);
    }

    app->runtime.services.ai.llm.attachLoRa(
      message.seq,
      id,
      contextId,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * @param id
   * @param context
   */
  router->map("ai.llm.lora.detach", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id", "context"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID id = 0;
    ai::llm::ID contextId = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(contextId, "context", std::stoull);

    app->runtime.services.ai.llm.detachLoRa(
      message.seq,
      id,
      contextId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * @param id
   */
  router->map("ai.llm.context.dump", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.llm.dumpContextState(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * @param id
   */
  router->map("ai.llm.context.restore", [](auto message, auto router, auto reply) {
    auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    ai::llm::ID id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    app->runtime.services.ai.llm.restoreContextState(
      message.seq,
      id,
      message.buffer,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Attemps to exit the application
   * @param value The exit code
   */
  router->map("application.exit", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    const auto err = validateMessageParameters(message, {"value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    int exitCode;
    REQUIRE_AND_GET_MESSAGE_VALUE(exitCode, "value", std::stoi);

  #if ORO_RUNTIME_PLATFORM_APPLE
    // from cli
    if (app->launchSource == App::LaunchSource::Tool) {
      debug("__EXIT_SIGNAL__=%d", exitCode);
      oro::cli::notify();
    }
  #endif

    const auto window = app->runtime.windowManager.getWindow(0);

    if (window == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    window->exit(exitCode);

    reply(Result::Data { message, JSON::Object {} });
  });

  /**
   * Get the screen size available to the application
   */
  router->map("application.getScreenSize", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    const auto window = app->runtime.windowManager.getWindow(0);

    if (window == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    const auto screenSize = window->getScreenSize();
    const JSON::Object json = JSON::Object::Entries {
      { "width", screenSize.width },
      { "height", screenSize.height }
    };

    reply(Result::Data { message, json });
  });

  /**
   * Get all active application windows
   * @param value - A list of window indexes to filter on
   */
  router->map("application.getWindows", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    const auto window = app->runtime.windowManager.getWindow(0);

    if (window == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    const auto requested = split(message.value, ',');
    Vector<int> indices;

    if (requested.size() == 0) {
      for (const auto& window : app->runtime.windowManager.windows) {
        if (window != nullptr) {
          indices.push_back(window->index);
        }
      }
    } else {
      for (const auto& value : requested) {
        try {
          indices.push_back(std::stoi(value));
        } catch (...) {
          return reply(Result::Err { message, "Invalid index given" });
        }
      }
    }

    const auto json  = app->runtime.windowManager.json(indices);
    reply(Result::Data { message, json });
  });

  /**
   * Set the application tray menu
   * @param value - The DSL for the system tray menu
   */
  router->map("application.setTrayMenu", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const auto app = App::sharedApplication();
    const auto err = validateMessageParameters(message, {"value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    app->dispatch([=]() {
      const auto window = app->runtime.windowManager.getWindow(0);

      if (window == nullptr) {
        return reply(Result::Err { message, "Application is invalid state" });
      }

      window->setTrayMenu(message.value);
      reply(Result::Data { message, JSON::Object {} });
    });
  #else
    reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Application Tray Menu is not supported"}
      }
    });
  #endif
  });

  /**
   * Set the application system menu
   * @param value - The DSL for the system tray menu
   */
  router->map("application.setSystemMenu", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const auto app = App::sharedApplication();
    const auto err = validateMessageParameters(message, {"value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    app->dispatch([=]() {
      const auto window = app->runtime.windowManager.getWindow(0);

      if (window == nullptr) {
        return reply(Result::Err { message, "Application is invalid state" });
      }

      window->setSystemMenu(message.value);
      reply(Result::Data { message, JSON::Object {} });
    });
  #else
    reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Application System Menu is not supported"}
      }
    });
  #endif
  });

  /**
   * Get the current WebView TLS pins configuration.
   */
  router->map("application.getWebviewTlsPins", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    const auto& userConfig = app->runtime.userConfig;
    String value = "";
    if (userConfig.contains("webview_tls_pins")) {
      value = userConfig.at("webview_tls_pins");
    } else if (userConfig.contains("webview.tls_pins")) {
      value = userConfig.at("webview.tls_pins");
    }

    reply(Result::Data {
      message,
      JSON::Object::Entries {
        {"value", value}
      }
    });
  });

  /**
   * Set or extend WebView TLS pins at runtime.
   * @param value - Newline-separated list of '<host> sha256/<base64>' entries.
   * @param mode  - Optional; 'append' (default) or 'replace'.
   */
  router->map("application.setWebviewTlsPins", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    const bool hasValue = message.has("value");
    const bool hasBody = message.buffer.size() > 0;
    if (!hasValue && !hasBody) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Expecting 'value' in parameters or request body"}
        }
      });
    }

    auto& runtime = app->runtime;
    const auto pins = hasValue ? message.value : message.buffer.str();
    String validateMessage;
    if (!validateTlsPinsConfigValue(pins, validateMessage)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "WEBVIEW_TLS_PINS_INVALID"},
        {"message", validateMessage}
      }});
    }
    const auto mode = toLowerCase(trim(message.get("mode", "append")));
    const bool replace = (mode == "replace");
    String newValue;

    // Update global runtime config used by Android and other consumers.
    {
      auto& globalConfig = runtime.userConfig;
      if (replace) {
        newValue = pins;
      } else {
        const String combined = globalConfig.contains("webview_tls_pins")
          ? globalConfig["webview_tls_pins"]
          : (globalConfig.contains("webview.tls_pins")
              ? globalConfig["webview.tls_pins"]
              : "");
        newValue = mergeTlsPinsConfigValue(combined, pins);
      }

      globalConfig["webview_tls_pins"] = newValue;
      globalConfig["webview.tls_pins"] = newValue;
    }

    // Ensure default window config used for new windows sees the updated pins.
    {
      Lock lock(runtime.windowManager.mutex);
      runtime.windowManager.options.userConfig["webview_tls_pins"] = newValue;
      runtime.windowManager.options.userConfig["webview.tls_pins"] = newValue;
    }

    // Update all active bridge configs so platform WebViews see the pins via bridge.userConfig.
    {
      Lock lock(runtime.bridgeManager.mutex);
      for (auto& entry : runtime.bridgeManager.entries) {
        if (!entry) {
          continue;
        }
        entry->userConfig["webview_tls_pins"] = newValue;
        entry->userConfig["webview.tls_pins"] = newValue;
      }
    }

  #if ORO_RUNTIME_PLATFORM_LINUX
    // On WebKitGTK, TLS pinning is applied via a host allow-list. Prime that
    // allow-list for endpoints that include explicit ports so subresource
    // requests (fetch/XHR) can succeed without requiring a navigation retry.
    app->dispatch([app]() {
      auto& runtime = app->runtime;
      Vector<SharedPointer<window::Manager::ManagedWindow>> windows;
      {
        Lock lock(runtime.windowManager.mutex);
        for (const auto& window : runtime.windowManager.windows) {
          if (window != nullptr) {
            windows.push_back(window);
          }
        }
      }

      for (const auto& window : windows) {
        window->prewarmWebviewTlsPins();
      }
    });
  #endif

    reply(Result::Data {
      message,
      JSON::Object::Entries {
        {"ok", true},
        {"value", newValue}
      }
    });
  });

  /**
   * Get cookies for a URL as a `Cookie` header value ("a=b; c=d").
   * @param url
   */
  router->map("cookies.get", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"url"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (
      router->bridge.userConfig.contains("permissions_allow_cookies") &&
      router->bridge.userConfig.at("permissions_allow_cookies") == "false"
    ) {
      auto err = JSON::Object::Entries {
        {"message", "Cookies API is not allowed"}
      };

      return reply(Result::Err { message, err });
    }

    const auto url = message.get("url");

    webview::cookies::get(router->bridge, url, [message, reply](const auto& value, const auto& error) mutable {
      if (!error.empty()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", error}
        }});
      }

      reply(Result::Data { message, JSON::Object::Entries {
        {"value", value}
      }});
    });
  });

  /**
   * Set a cookie for a URL from a `Set-Cookie` header value.
   * @param url
   * @param cookie - a Set-Cookie header value
   */
  router->map("cookies.set", [](auto message, auto router, auto reply) {
    const bool hasURL = message.has("url");
    const bool hasCookie = message.has("cookie");
    const bool hasBody = message.buffer.size() > 0;

    if (
      router->bridge.userConfig.contains("permissions_allow_cookies") &&
      router->bridge.userConfig.at("permissions_allow_cookies") == "false"
    ) {
      auto err = JSON::Object::Entries {
        {"message", "Cookies API is not allowed"}
      };

      return reply(Result::Err { message, err });
    }

    if (!hasURL) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Expecting 'url' in parameters"}
      }});
    }

    if (!hasCookie && !hasBody) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Expecting 'cookie' in parameters or request body"}
      }});
    }

    const auto url = message.get("url");
    const auto cookie = hasCookie ? message.get("cookie") : message.buffer.str();

    webview::cookies::set(router->bridge, url, cookie, [message, reply](bool ok, const auto& error) mutable {
      if (!ok || !error.empty()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", error.empty() ? "Failed to set cookie" : error}
        }});
      }

      reply(Result::Data { message, JSON::Object::Entries {
        {"ok", true}
      }});
    });
  });

  /**
   * Remove cookies matching `name` for a URL.
   * @param url
   * @param name
   */
  router->map("cookies.remove", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"url", "name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (
      router->bridge.userConfig.contains("permissions_allow_cookies") &&
      router->bridge.userConfig.at("permissions_allow_cookies") == "false"
    ) {
      auto err = JSON::Object::Entries {
        {"message", "Cookies API is not allowed"}
      };

      return reply(Result::Err { message, err });
    }

    const auto url = message.get("url");
    const auto name = message.get("name");

    webview::cookies::remove(router->bridge, url, name, [message, reply](bool ok, const auto& error) mutable {
      if (!ok || !error.empty()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", error.empty() ? "Failed to remove cookie" : error}
        }});
      }

      reply(Result::Data { message, JSON::Object::Entries {
        {"ok", true}
      }});
    });
  });

  /**
   * Clear all cookies for the current WebView data store.
   */
  router->map("cookies.clear", [](auto message, auto router, auto reply) {
    if (
      router->bridge.userConfig.contains("permissions_allow_cookies") &&
      router->bridge.userConfig.at("permissions_allow_cookies") == "false"
    ) {
      auto err = JSON::Object::Entries {
        {"message", "Cookies API is not allowed"}
      };

      return reply(Result::Err { message, err });
    }

    webview::cookies::clear(router->bridge, [message, reply](bool ok, const auto& error) mutable {
      if (!ok || !error.empty()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", error.empty() ? "Failed to clear cookies" : error}
        }});
      }

      reply(Result::Data { message, JSON::Object::Entries {
        {"ok", true}
      }});
    });
  });

  /**
   * Set the application system menu item enabled state
   * @param enabled - true or false
   * @param indexMain
   * @param indexSub
   */
  router->map("application.setSystemMenuItemEnabled", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const auto app = App::sharedApplication();
    const auto err = validateMessageParameters(message, {"enabled", "indexMain", "indexSub"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    app->dispatch([=]() {
      const auto window = app->runtime.windowManager.getWindow(0);

      if (window == nullptr) {
        return reply(Result::Err { message, "Application is invalid state" });
      }

      const auto enabled = message.get("enabled") == "true";
      int indexMain;
      int indexSub;

      REQUIRE_AND_GET_MESSAGE_VALUE(indexMain, "indexMain", std::stoi);
      REQUIRE_AND_GET_MESSAGE_VALUE(indexSub, "indexSub", std::stoi);

      window->setSystemMenuItemEnabled(enabled, indexMain, indexSub);

      reply(Result::Data { message, JSON::Object {} });
    });
  #else
    reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Application System Menu is not supported"}
      }
    });
  #endif
  });

  /**
   * Starts a bluetooth service
   * @param serviceId
   */
  router->map("bluetooth.start", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"serviceId"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (router->bridge.userConfig["permissions_allow_bluetooth"] == "false") {
      auto err = JSON::Object::Entries {
        {"message", "Bluetooth is not allowed"}
      };

      return reply(Result::Err { message, err });
    }

    const auto errMsg = JSON::Object::Entries {{
      {"message", "bluetooth.start route is not supported in this build"}
    }};
    reply(Result::Err { message, JSON::Object(errMsg) });
  });

  /**
   * Subscribes to a characteristic for a service.
   * @param serviceId
   * @param characteristicId
   */
  router->map("bluetooth.subscribe", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {
      "characteristicId",
      "serviceId"
    });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (router->bridge.userConfig["permissions_allow_bluetooth"] == "false") {
      auto err = JSON::Object::Entries {
        {"message", "Bluetooth is not allowed"}
      };

      return reply(Result::Err { message, err });
    }

    const auto errMsg = JSON::Object::Entries {{
      {"message", "bluetooth.subscribe route is not supported in this build"}
    }};
    reply(Result::Err { message, JSON::Object(errMsg) });
  });

  /**
   * Publishes data to a characteristic for a service.
   * @param serviceId
   * @param characteristicId
   */
  router->map("bluetooth.publish", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {
      "characteristicId",
      "serviceId"
    });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (router->bridge.userConfig["permissions_allow_bluetooth"] == "false") {
      auto err = JSON::Object::Entries {
        {"message", "Bluetooth is not allowed"}
      };

      return reply(Result::Err { message, err });
    }

    auto bytes = reinterpret_cast<char*>(message.buffer.data());
    auto size = message.buffer.size();

    if (bytes == nullptr) {
      bytes = message.value.data();
      size = message.value.size();
    }

    const auto errMsg = JSON::Object::Entries {{
      {"message", "bluetooth.publish route is not supported in this build"}
    }};
    reply(Result::Err { message, JSON::Object(errMsg) });
  });

  router->map("dbus.availability", [](auto message, auto router, auto reply) {
    const auto info = router->bridge.getRuntime()->services.dbus.availability();
    reply(Result { message.seq, message, JSON::Object::Entries {{
      {"source", String("dbus.availability")},
      {"data", info}
    }}, QueuedResponse{} });
  });

  router->map("dbus.connect", [](auto message, auto router, auto reply) {
    JSON::Any options = nullptr;
    if (message.has("options")) {
      options = JSON::Raw(message.get("options"));
    }

    router->bridge.getRuntime()->services.dbus.connect(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.disconnect", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.dbus.disconnect(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.requestName", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "name"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    uint32_t flags = 0;
    if (message.has("flags")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(flags, "flags", std::stoul);
    }

    router->bridge.getRuntime()->services.dbus.requestName(
      message.seq,
      id,
      message.get("name"),
      flags,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.releaseName", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "name"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.dbus.releaseName(
      message.seq,
      id,
      message.get("name"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.call", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "member"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    core::services::DBus::CallOptions options;
    options.connectionId = id;
    options.destination = message.get("destination");
    options.path = message.get("path");
    options.interfaceName = message.get("interface");
    options.member = message.get("member");
    options.signature = message.get("signature");
    if (message.has("timeout")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(options.timeoutMs, "timeout", std::stoi);
    }
    options.noReply = message.get("noReply", "false") == "true";
    if (message.has("body")) {
      options.body = JSON::Raw(message.get("body"));
    }

    router->bridge.getRuntime()->services.dbus.call(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.signal", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path", "name"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    core::services::DBus::SignalOptions options;
    options.connectionId = id;
    options.path = message.get("path");
    options.interfaceName = message.get("interface");
    options.name = message.get("name");
    options.signature = message.get("signature");
    if (message.has("body")) {
      options.body = JSON::Raw(message.get("body"));
    }

    router->bridge.getRuntime()->services.dbus.emitSignal(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.addMatch", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "rule"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    core::services::DBus::MatchRule rule;
    rule.connectionId = id;
    rule.rule = message.get("rule");

    router->bridge.getRuntime()->services.dbus.addMatch(
      message.seq,
      rule,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.removeMatch", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"matchId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t matchId;
    REQUIRE_AND_GET_MESSAGE_VALUE(matchId, "matchId", std::stoull);

    router->bridge.getRuntime()->services.dbus.removeMatch(
      message.seq,
      matchId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.exportObject", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    core::services::DBus::ExportOptions options;
    options.connectionId = id;
    options.path = message.get("path");
    options.interfaceName = message.get("interface");

    JSON::Any definition = nullptr;
    if (message.has("definition")) {
      definition = JSON::Raw(message.get("definition"));
    }

    router->bridge.getRuntime()->services.dbus.exportObject(
      message.seq,
      options,
      definition,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.unexportObject", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"exportId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t exportId;
    REQUIRE_AND_GET_MESSAGE_VALUE(exportId, "exportId", std::stoull);

    router->bridge.getRuntime()->services.dbus.unexportObject(
      message.seq,
      exportId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("dbus.respond", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"callId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t callId;
    REQUIRE_AND_GET_MESSAGE_VALUE(callId, "callId", std::stoull);
    bool isError = message.get("error", "false") == "true";
    const auto name = message.get("name");
    const auto signature = message.get("signature");
    JSON::Any body = nullptr;
    if (message.has("body")) {
      body = JSON::Raw(message.get("body"));
    }

    router->bridge.getRuntime()->services.dbus.respond(
      message.seq,
      callId,
      isError,
      name,
      signature,
      body,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("xpc.availability", [](auto message, auto router, auto reply) {
    const auto info = router->bridge.getRuntime()->services.xpc.availability();
    reply(Result { message.seq, message, info, QueuedResponse{} });
  });

  router->map("xpc.connect", [](auto message, auto router, auto reply) {
    JSON::Any options = nullptr;
    if (message.has("options")) {
      options = JSON::Raw(message.get("options"));
    }

    router->bridge.getRuntime()->services.xpc.connect(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("xpc.disconnect", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "connectionId", std::stoull);

    router->bridge.getRuntime()->services.xpc.disconnect(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("xpc.send", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId", "message"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "connectionId", std::stoull);

    bool expectReply = false;
    if (message.has("expectReply")) {
      const auto& value = message.get("expectReply");
      expectReply = value == "true" || value == "1";
    }

    uint64_t timeoutMs = 0;
    if (message.has("timeout")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(timeoutMs, "timeout", std::stoull);
    }

    JSON::Any payload = nullptr;
    if (message.has("message")) {
      payload = JSON::Raw(message.get("message"));
    }

    router->bridge.getRuntime()->services.xpc.send(
      message.seq,
      id,
      payload,
      expectReply,
      timeoutMs,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  auto handleXpcRespond = [](auto message, auto router, auto reply, bool isError) {
    auto err = validateMessageParameters(message, {"messageId"});
    if (err.type != JSON::Type::Null) {
      reply(Result::Err { message, err });
      return;
    }

    uint64_t messageId = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(messageId, "messageId", std::stoull);

    JSON::Any payload = nullptr;
    if (message.has("message")) {
      payload = JSON::Raw(message.get("message"));
    }

    router->bridge.getRuntime()->services.xpc.respond(
      message.seq,
      messageId,
      isError,
      payload,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  };

  router->map("xpc.respond", [handleXpcRespond](auto message, auto router, auto reply) {
    handleXpcRespond(message, router, reply, false);
  });

  router->map("xpc.respondError", [handleXpcRespond](auto message, auto router, auto reply) {
    handleXpcRespond(message, router, reply, true);
  });

  router->map("xpc.suspend", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "connectionId", std::stoull);

    router->bridge.getRuntime()->services.xpc.suspend(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("xpc.resume", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"connectionId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "connectionId", std::stoull);

    router->bridge.getRuntime()->services.xpc.resume(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hid.getDevices", [](auto message, auto router, auto reply) {
    auto& svc = router->bridge.getRuntime()->services.hid;
    svc.getDevices(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.requestDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"options"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });

    const auto rawOptions = message.get("options");
    JSON::Any optionsJSON;

    try {
      optionsJSON = JSON::parse(rawOptions);
    } catch (const JSON::Error& parseError) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", parseError.name.size() > 0 ? parseError.name : String("SyntaxError")},
            {"message", parseError.message.size() > 0 ? parseError.message : String("Invalid 'options' JSON for hid.requestDevice")},
            {"code", parseError.code},
            {"location", parseError.location}
          }}
        }
      });
    } catch (const std::exception& ex) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", String("Error")},
            {"message", String("Invalid 'options' JSON for hid.requestDevice")},
            {"detail", String(ex.what())}
          }}
        }
      });
    } catch (...) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", String("Error")},
            {"message", String("Invalid 'options' JSON for hid.requestDevice")}
          }}
        }
      });
    }

    auto& svc = router->bridge.getRuntime()->services.hid;
    svc.requestDevice(
      message.seq,
      optionsJSON,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.chooseDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    core::services::HID::DeviceSelection selection;
    selection.deviceId = message.get("deviceId");
    router->bridge.getRuntime()->services.hid.chooseDevice(
      message.seq,
      selection,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.cancelRequest", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.hid.cancelRequest(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.forgetDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    router->bridge.getRuntime()->services.hid.forgetDevice(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.device.open", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    router->bridge.getRuntime()->services.hid.open(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.device.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    router->bridge.getRuntime()->services.hid.close(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.device.sendReport", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "reportId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t reportId = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(reportId, "reportId", std::stoul);
    auto* data = reinterpret_cast<unsigned char*>(message.buffer.data());
    size_t size = message.buffer.size();
    bytes::Buffer payload(size);
    if (data && size > 0) {
      memcpy(payload.data(), data, size);
    }
    router->bridge.getRuntime()->services.hid.sendReport(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(reportId & 0xFFu),
      payload,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.device.sendFeatureReport", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "reportId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t reportId = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(reportId, "reportId", std::stoul);
    auto* data = reinterpret_cast<unsigned char*>(message.buffer.data());
    size_t size = message.buffer.size();
    bytes::Buffer payload(size);
    if (data && size > 0) {
      memcpy(payload.data(), data, size);
    }
    router->bridge.getRuntime()->services.hid.sendFeatureReport(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(reportId & 0xFFu),
      payload,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hid.device.receiveFeatureReport", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "reportId", "length"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t reportId = 0;
    uint32_t length = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(reportId, "reportId", std::stoul);
    REQUIRE_AND_GET_MESSAGE_VALUE(length, "length", std::stoul);
    router->bridge.getRuntime()->services.hid.receiveFeatureReport(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(reportId & 0xFFu),
      static_cast<uint16_t>(length & 0xFFFFu),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("hci.listAdapters", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.hci.listAdapters(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hci.getAdapterInfo", [](auto message, auto router, auto reply) {
    uint32_t devId = 0;
    if (message.contains("devId")) {
      try {
        devId = static_cast<uint32_t>(std::stoul(message.get("devId")));
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'devId' given in parameters"}
        }});
      }
    }

    router->bridge.getRuntime()->services.hci.getAdapter(
      message.seq,
      static_cast<uint16_t>(devId & 0xFFFFu),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hci.setAdapterState", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"up"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint32_t devId = 0;
    if (message.contains("devId")) {
      try {
        devId = static_cast<uint32_t>(std::stoul(message.get("devId")));
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'devId' given in parameters"}
        }});
      }
    }

    const auto upStr = message.get("up");
    const bool up = (upStr == "true" || upStr == "1");

    router->bridge.getRuntime()->services.hci.setAdapterState(
      message.seq,
      static_cast<uint16_t>(devId & 0xFFFFu),
      up,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hci.open", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    uint16_t devId = 0;
    if (message.contains("devId")) {
      try {
        devId = static_cast<uint16_t>(std::stoul(message.get("devId")));
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'devId' given in parameters"}
        }});
      }
    }

    router->bridge.getRuntime()->services.hci.open(
      message.seq,
      id,
      devId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hci.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.hci.close(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hci.write", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.hci.write(
      message.seq,
      id,
      message.buffer.shared(),
      message.buffer.size(),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hci.readStart", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.hci.readStart(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("hci.readStop", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.hci.readStop(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("bluetooth.getAvailability", [](auto message, auto router, auto reply) {
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.getAvailability(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.getDevices", [](auto message, auto router, auto reply) {
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.getDevices(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.restartDiscovery", [](auto message, auto router, auto reply) {
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.restartDiscovery(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.requestDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"options"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.requestDevice(
      message.seq,
      JSON::Raw(message.get("options")),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.chooseDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.chooseDevice(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.cancelRequest", [](auto message, auto router, auto reply) {
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.cancelRequest(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.gatt.connect", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.gattConnect(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.gatt.disconnect", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.gattDisconnect(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.gatt.getPrimaryService", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "service"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    const String deviceId = message.get("deviceId");
    const String serviceId = core::services::Bluetooth::normalizeUUID(message.get("service"));
    runtime->services.bluetooth.gattGetPrimaryService(
      message.seq,
      deviceId,
      serviceId,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.gatt.getPrimaryServices", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    const String deviceId = message.get("deviceId");
    String serviceFilter;
    if (message.has("service") && message.get("service").size() > 0) {
      serviceFilter = core::services::Bluetooth::normalizeUUID(message.get("service"));
    }
    runtime->services.bluetooth.gattGetPrimaryServices(
      message.seq,
      deviceId,
      serviceFilter,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.service.getCharacteristic", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "service", "characteristic"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    const String deviceId = message.get("deviceId");
    const String serviceId = core::services::Bluetooth::normalizeUUID(message.get("service"));
    const String characteristicId = core::services::Bluetooth::normalizeUUID(message.get("characteristic"));
    runtime->services.bluetooth.serviceGetCharacteristic(
      message.seq,
      deviceId,
      serviceId,
      characteristicId,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.service.getCharacteristics", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "service"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    const String deviceId = message.get("deviceId");
    const String serviceId = core::services::Bluetooth::normalizeUUID(message.get("service"));
    String characteristicFilter;
    if (message.has("characteristic") && message.get("characteristic").size() > 0) {
      characteristicFilter = core::services::Bluetooth::normalizeUUID(message.get("characteristic"));
    }
    runtime->services.bluetooth.serviceGetCharacteristics(
      message.seq,
      deviceId,
      serviceId,
      characteristicFilter,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.characteristic.readValue", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "service", "characteristic"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    const String deviceId = message.get("deviceId");
    const String serviceId = core::services::Bluetooth::normalizeUUID(message.get("service"));
    const String characteristicId = core::services::Bluetooth::normalizeUUID(message.get("characteristic"));
    runtime->services.bluetooth.characteristicReadValue(
      message.seq,
      deviceId,
      serviceId,
      characteristicId,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.characteristic.writeValue", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "service", "characteristic"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    const String deviceId = message.get("deviceId");
    const String serviceId = core::services::Bluetooth::normalizeUUID(message.get("service"));
    const String characteristicId = core::services::Bluetooth::normalizeUUID(message.get("characteristic"));
    auto* data = reinterpret_cast<unsigned char*>(message.buffer.data());
    size_t size = message.buffer.size();
    bytes::Buffer payload(size);
    if (data && size > 0) {
      memcpy(payload.data(), data, size);
    }
    runtime->services.bluetooth.characteristicWriteValue(
      message.seq,
      deviceId,
      serviceId,
      characteristicId,
      payload,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.characteristic.startNotifications", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "service", "characteristic"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }

    const String deviceId = message.get("deviceId");
    const String serviceId = core::services::Bluetooth::normalizeUUID(message.get("service"));
    const String characteristicId = core::services::Bluetooth::normalizeUUID(message.get("characteristic"));
    const String key = bluetoothSubscriptionKey(deviceId, serviceId, characteristicId);
    auto* bridge = &router->bridge;

    {
      Lock lock(bridge->bluetoothMutex);
      auto& subs = bridge->bluetoothSubscriptions;
      auto it = subs.find(key);
      if (it != subs.end()) {
        it->second.refCount += 1;
        JSON::Object json = JSON::Object::Entries {{"data", JSON::Object {}}};
        return reply(Result::Data { message, json });
      }

      bridge::Bridge::BluetoothSubscription sub;
      sub.deviceId = deviceId;
      sub.serviceId = serviceId;
      sub.characteristicId = characteristicId;
      sub.refCount = 1;
      sub.pending = true;
      bridge->bluetoothSubscriptions.insert_or_assign(key, sub);
    }

    runtime->services.bluetooth.characteristicStartNotifications(
      message.seq,
      deviceId,
      serviceId,
      characteristicId,
      [reply, message, bridge, key, deviceId, serviceId, characteristicId](auto seq, auto json, auto queued) mutable {
        const bool hasError = (
          json.type == JSON::Type::Object &&
          json.template as<JSON::Object>().has("err")
        );

        if (hasError) {
          Lock lock(bridge->bluetoothMutex);
          bridge->bluetoothSubscriptions.erase(key);
          return reply(Result{seq, message, json, queued});
        }

        bool needObserver = false;
        {
          Lock lock(bridge->bluetoothMutex);
          auto it = bridge->bluetoothSubscriptions.find(key);
          if (it != bridge->bluetoothSubscriptions.end()) {
            it->second.pending = false;
            if (it->second.observerId == 0) {
              needObserver = true;
            }
          }
        }

        if (needObserver) {
          auto* runtime = bridge->getRuntime();
          if (runtime) {
            const uint64_t observerId = runtime->services.bluetooth.addCharacteristicObserver(
              deviceId,
              serviceId,
              characteristicId,
              [bridge](const JSON::Any& event) {
                auto* strong = bridge;
                if (!strong || !strong->active()) {
                  return;
                }
                strong->emit("bluetooth.characteristicvaluechanged", event);
              }
            );

            Lock lock(bridge->bluetoothMutex);
            auto it = bridge->bluetoothSubscriptions.find(key);
            if (it != bridge->bluetoothSubscriptions.end()) {
              it->second.observerId = observerId;
            }
          }
        }

        reply(Result{seq, message, json, queued});
      }
    );
  });

  router->map("bluetooth.characteristic.stopNotifications", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "service", "characteristic"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }

    const String deviceId = message.get("deviceId");
    const String serviceId = core::services::Bluetooth::normalizeUUID(message.get("service"));
    const String characteristicId = core::services::Bluetooth::normalizeUUID(message.get("characteristic"));
    const String key = bluetoothSubscriptionKey(deviceId, serviceId, characteristicId);
    auto* bridge = &router->bridge;
    uint64_t observerId = 0;

    {
      Lock lock(bridge->bluetoothMutex);
      auto& subs = bridge->bluetoothSubscriptions;
      auto it = subs.find(key);
      if (it == subs.end()) {
        JSON::Object json = JSON::Object::Entries {{
          {"err", JSON::Object::Entries {{
            {"type", "InvalidStateError"},
            {"message", String("No active bluetooth notifications for this characteristic")}
          }}}
        }};
        return reply(Result::Err { message, json });
      }

      if (it->second.refCount > 1) {
        it->second.refCount -= 1;
        JSON::Object json = JSON::Object::Entries {{"data", JSON::Object {}}};
        return reply(Result::Data { message, json });
      }

      observerId = it->second.observerId;
      subs.erase(it);
    }

    if (observerId != 0) {
      runtime->services.bluetooth.removeCharacteristicObserver(observerId);
    }

    runtime->services.bluetooth.characteristicStopNotifications(
      message.seq,
      deviceId,
      serviceId,
      characteristicId,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.device.watchAdvertisements", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.deviceWatchAdvertisements(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("bluetooth.device.forget", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* runtime = router->bridge.getRuntime();
    if (!runtime) {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "InternalError"},
          {"message", String("Runtime unavailable")}
        }}}
      }};
      return reply(Result::Err { message, json });
    }
    runtime->services.bluetooth.deviceForget(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.getDevices", [](auto message, auto router, auto reply) {
    auto& svc = router->bridge.getRuntime()->services.usb;
    svc.getDevices(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.requestDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"options"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });

    const auto rawOptions = message.get("options");
    JSON::Any optionsJSON;

    try {
      optionsJSON = JSON::parse(rawOptions);
    } catch (const JSON::Error& parseError) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", parseError.name.size() > 0 ? parseError.name : String("SyntaxError")},
            {"message", parseError.message.size() > 0 ? parseError.message : String("Invalid 'options' JSON for usb.requestDevice")},
            {"code", parseError.code},
            {"location", parseError.location}
          }}
        }
      });
    } catch (const std::exception& ex) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", String("Error")},
            {"message", String("Invalid 'options' JSON for usb.requestDevice")},
            {"detail", String(ex.what())}
          }}
        }
      });
    } catch (...) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", String("Error")},
            {"message", String("Invalid 'options' JSON for usb.requestDevice")}
          }}
        }
      });
    }

    auto& svc = router->bridge.getRuntime()->services.usb;
    svc.requestDevice(
      message.seq,
      optionsJSON,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.chooseDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    core::services::USB::DeviceSelection selection;
    selection.deviceId = message.get("deviceId");
    router->bridge.getRuntime()->services.usb.chooseDevice(
      message.seq,
      selection,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.cancelRequest", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.usb.cancelRequest(
      message.seq,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.forgetDevice", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    router->bridge.getRuntime()->services.usb.forgetDevice(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.device.open", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    router->bridge.getRuntime()->services.usb.open(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.device.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    router->bridge.getRuntime()->services.usb.close(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.device.selectConfiguration", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "configurationValue"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t configurationValue = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(configurationValue, "configurationValue", std::stoul);
    router->bridge.getRuntime()->services.usb.selectConfiguration(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(configurationValue & 0xFFu),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.device.claimInterface", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "interfaceNumber"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t interfaceNumber = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(interfaceNumber, "interfaceNumber", std::stoul);
    router->bridge.getRuntime()->services.usb.claimInterface(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(interfaceNumber & 0xFFu),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.device.releaseInterface", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "interfaceNumber"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t interfaceNumber = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(interfaceNumber, "interfaceNumber", std::stoul);
    router->bridge.getRuntime()->services.usb.releaseInterface(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(interfaceNumber & 0xFFu),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.device.selectAlternateInterface", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "interfaceNumber", "alternateSetting"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t interfaceNumber = 0;
    uint32_t alternateSetting = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(interfaceNumber, "interfaceNumber", std::stoul);
    REQUIRE_AND_GET_MESSAGE_VALUE(alternateSetting, "alternateSetting", std::stoul);
    router->bridge.getRuntime()->services.usb.selectAlternateInterface(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(interfaceNumber & 0xFFu),
      static_cast<uint8_t>(alternateSetting & 0xFFu),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.device.reset", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    router->bridge.getRuntime()->services.usb.reset(
      message.seq,
      message.get("deviceId"),
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.transfer.controlIn", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "setup", "length"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t length = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(length, "length", std::stoul);
    router->bridge.getRuntime()->services.usb.controlTransferIn(
      message.seq,
      message.get("deviceId"),
      JSON::Raw(message.get("setup")),
      length,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.transfer.controlOut", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "setup"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    auto* data = reinterpret_cast<unsigned char*>(message.buffer.data());
    size_t size = message.buffer.size();
    bytes::Buffer payload(size);
    if (data && size > 0) {
      memcpy(payload.data(), data, size);
    }
    router->bridge.getRuntime()->services.usb.controlTransferOut(
      message.seq,
      message.get("deviceId"),
      JSON::Raw(message.get("setup")),
      payload,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.transfer.in", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "endpointNumber", "length"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t endpointNumber = 0;
    uint32_t length = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(endpointNumber, "endpointNumber", std::stoul);
    REQUIRE_AND_GET_MESSAGE_VALUE(length, "length", std::stoul);
    router->bridge.getRuntime()->services.usb.transferIn(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(endpointNumber & 0xFFu),
      length,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.transfer.out", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "endpointNumber"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t endpointNumber = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(endpointNumber, "endpointNumber", std::stoul);
    auto* data = reinterpret_cast<unsigned char*>(message.buffer.data());
    size_t size = message.buffer.size();
    bytes::Buffer payload(size);
    if (data && size > 0) {
      memcpy(payload.data(), data, size);
    }
    router->bridge.getRuntime()->services.usb.transferOut(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(endpointNumber & 0xFFu),
      payload,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("usb.transfer.clearHalt", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"deviceId", "endpointNumber", "direction"});
    if (err.type != JSON::Type::Null) return reply(Result::Err { message, err });
    uint32_t endpointNumber = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(endpointNumber, "endpointNumber", std::stoul);
    const auto direction = message.get("direction");
    bool directionIn = false;
    if (direction == "in") {
      directionIn = true;
    } else if (direction == "out") {
      directionIn = false;
    } else {
      JSON::Object json = JSON::Object::Entries {{
        {"err", JSON::Object::Entries {{
          {"type", "TypeError"},
          {"message", "direction must be 'in' or 'out'"}
        }}}
      }};
      return reply(Result::Err { message, json });
    }

    router->bridge.getRuntime()->services.usb.clearHalt(
      message.seq,
      message.get("deviceId"),
      static_cast<uint8_t>(endpointNumber & 0xFFu),
      directionIn,
      [reply, message](auto seq, auto json, auto queued) { reply(Result{seq, message, json, queued}); }
    );
  });

  router->map("broadcast_channel.subscribe", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {
      "name",
      "origin"
    });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto name = message.get("name");
    const auto origin = message.get("origin");
    const auto subscription = router->bridge.getRuntime()->services.broadcastChannel.subscribe({
      name,
      origin,
      router->bridge.client,
      [message, router](const auto event) mutable {
        const auto app = App::sharedApplication();
        router->bridge.emit("broadcastchannelmessage", JSON::Object::Entries {
          {"source", message.name},
          {"data", event.json()},
          {"err", nullptr}
        });
      }
   });

    reply(Result::Data { message, subscription.json() });
  });

  router->map("broadcast_channel.unsubscribe", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, { "id" });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    if (!router->bridge.getRuntime()->services.broadcastChannel.unsubscribe(id)) {
      const auto err = JSON::Object::Entries {
        {"type", "NotFoundError"},
        {"message", "No subscription found" }
      };
      return reply(Result::Err { message, err });
    }

    reply(Result { message.seq, message, JSON::Object {} });
  });

  router->map("broadcast_channel.queuedResponseMessage", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {
      "origin",
      "token",
      "name",
      "data"
    });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto data = JSON::Raw(message.get("data"));
    const auto name = message.get("name");
    const auto token = message.get("token");
    const auto origin = message.get("origin");
    const auto messageToqueuedResponse = core::services::BroadcastChannel::Message {
      router->bridge.client,
      origin,
      token,
      name,
      data
    };

    const auto queuedResponseed = router->bridge.getRuntime()->services.broadcastChannel.postMessage(messageToqueuedResponse);

    if (!queuedResponseed) {
      const auto err = JSON::Object::Entries {
        {"type", "NotFoundError"},
        {"message", "No subscribers"}
      };

      return reply(Result::Err { message, err });
    }

    reply(Result::Data { message, messageToqueuedResponse.json() });
  });

  /**
   * Kills an already spawned child process.
   *
   * @param id
   * @param signal
   */
  router->map("child_process.kill", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_IOS
    auto err = JSON::Object::Entries {
      {"type", "NotSupportedError"},
      {"message", "Operation is not supported on this platform"}
    };

    return reply(Result::Err { message, err });
  #else
    auto err = validateMessageParameters(message, {"id", "signal"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    int signal;
    REQUIRE_AND_GET_MESSAGE_VALUE(signal, "signal", std::stoi);

    router->bridge.getRuntime()->services.process.kill(
      message.seq,
      id,
      signal,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  #endif
  });

  /**
   * Spawns a child process
   *
   * @param id
   * @param args (command, ...args)
   */
  router->map("child_process.spawn", [](auto message, auto router, auto reply) {
    #if ORO_RUNTIME_PLATFORM_IOS
      auto err = JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Operation is not supported on this platform"}
      };

      return reply(Result::Err { message, err });
    #else
      auto err = validateMessageParameters(message, {"args", "id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      auto args = splitc(message.get("args"), 0x0001);

      if (args.size() == 0 || args.at(0).size() == 0) {
        auto json = JSON::Object::Entries {
          {"source", "child_process.spawn"},
          {"err", JSON::Object::Entries {
            {"message", "Spawn requires at least one argument with a length greater than zero"},
          }}
        };

        return reply(Result { message.seq, message, json });
      }

      uint64_t id;
      REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

      Vector<String> env{};

      if (message.has("env")) {
        env = split(message.get("env"), 0x0001);
      }

      const auto options = oro::runtime::core::services::Process::SpawnOptions {
        .cwd = message.get("cwd", getcwd()),
        .env = env,
        .replaceEnvironment = message.has("env"),
        .allowStdin = message.get("stdin") != "false",
        .allowStdout = message.get("stdout") != "false",
        .allowStderr = message.get("stderr") != "false"
      };

      router->bridge.getRuntime()->services.process.spawn(
        message.seq,
        id,
        args,
        options,
        [message, reply](auto seq, auto json, auto queuedResponse) {
          reply(Result { seq, message, json, queuedResponse });
        }
      );
    #endif
  });

  router->map("child_process.exec", [](auto message, auto router, auto reply) {
    #if ORO_RUNTIME_PLATFORM_IOS
      auto err = JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Operation is not supported on this platform"}
      };

      return reply(Result::Err { message, err });
    #else
      auto err = validateMessageParameters(message, {"args", "id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      auto args = splitc(message.get("args"), 0x0001);

      if (args.size() == 0 || args.at(0).size() == 0) {
        auto json = JSON::Object::Entries {
          {"source", "child_process.exec"},
          {"err", JSON::Object::Entries {
            {"message", "Spawn requires at least one argument with a length greater than zero"},
          }}
        };

        return reply(Result { message.seq, message, json });
      }

      uint64_t id;
      REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

      uint64_t timeout = 0;
      int killSignal = 0;

      if (message.has("timeout")) {
        REQUIRE_AND_GET_MESSAGE_VALUE(timeout, "timeout", std::stoull);
      }

      if (message.has("killSignal")) {
        REQUIRE_AND_GET_MESSAGE_VALUE(killSignal, "killSignal", std::stoi);
      }

      Vector<String> env{};

      if (message.has("env")) {
        env = split(message.get("env"), 0x0001);
      }

      const auto options = oro::runtime::core::services::Process::ExecOptions {
        .cwd = message.get("cwd", getcwd()),
        .env = env,
        .replaceEnvironment = message.has("env"),
        .allowStdout = message.get("stdout") != "false",
        .allowStderr = message.get("stderr") != "false",
        .timeout = timeout,
        .killSignal = killSignal
      };

      router->bridge.getRuntime()->services.process.exec(
        message.seq,
        id,
        args,
        options,
        [message, reply](auto seq, auto json, auto queuedResponse) {
          reply(Result { seq, message, json, queuedResponse });
        }
      );
    #endif
  });

  /**
   * Writes to an already spawned child process.
   *
   * @param id
   */
  router->map("child_process.write", [](auto message, auto router, auto reply) {
    #if ORO_RUNTIME_PLATFORM_IOS
      auto err = JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Operation is not supported on this platform"}
      };

      return reply(Result::Err { message, err });
    #else
      auto err = validateMessageParameters(message, {"id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

      router->bridge.getRuntime()->services.process.write(
        message.seq,
        id,
        message.buffer.shared(),
        message.buffer.size(),
        RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
      );
    #endif
  });
  router->map("asn1.parse", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"source"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto* runtime = router->bridge.getRuntime();
    if (runtime == nullptr) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"type", "InvalidStateError"},
        {"message", "Runtime services unavailable"}
      }});
    }

    auto& service = runtime->services.asn1;
    if (!service.enabled.load()) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "ASN.1 service is disabled"}
      }});
    }

    core::services::ASN1::ParseOptions options;

    if (message.has("lexerDebug")) {
      const auto flag = toLowerCase(message.get("lexerDebug"));
      options.lexerDebug = (flag == "1" || flag == "true" || flag == "yes");
    }

    if (message.has("includeSourceText")) {
      const auto flag = toLowerCase(message.get("includeSourceText"));
      options.includeSourceText = (flag == "1" || flag == "true" || flag == "yes");
    }

    if (message.has("maxDepth")) {
      try {
        const auto depth = std::stoull(message.get("maxDepth"));
        if (depth > 0) {
          options.maxDepth = std::min<size_t>(depth, 1024);
        }
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"type", "TypeError"},
          {"message", "Expected 'maxDepth' to be a positive integer"}
        }});
      }
    }

    service.parse(
      message.seq,
      message.get("source"),
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("asn1.parseFile", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto* runtime = router->bridge.getRuntime();
    if (runtime == nullptr) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"type", "InvalidStateError"},
        {"message", "Runtime services unavailable"}
      }});
    }

    auto& service = runtime->services.asn1;
    if (!service.enabled.load()) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "ASN.1 service is disabled"}
      }});
    }

    core::services::ASN1::ParseOptions options;

    if (message.has("lexerDebug")) {
      const auto flag = toLowerCase(message.get("lexerDebug"));
      options.lexerDebug = (flag == "1" || flag == "true" || flag == "yes");
    }

    if (message.has("includeSourceText")) {
      const auto flag = toLowerCase(message.get("includeSourceText"));
      options.includeSourceText = (flag == "1" || flag == "true" || flag == "yes");
    }

    if (message.has("maxDepth")) {
      try {
        const auto depth = std::stoull(message.get("maxDepth"));
        if (depth > 0) {
          options.maxDepth = std::min<size_t>(depth, 1024);
        }
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"type", "TypeError"},
          {"message", "Expected 'maxDepth' to be a positive integer"}
        }});
      }
    }

    service.parseFile(
      message.seq,
      message.get("path"),
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.open", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    int flags = oro::runtime::sqlite::kDefaultOpenFlags;
    if (message.has("flags")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(flags, "flags", std::stoi);
    }

    router->bridge.getRuntime()->services.sqlite.open(
      message.seq,
      id,
      message.get("path"),
      flags,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.sqlite.close(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.exec", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "sql"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    const auto mode = message.get("mode", "object");
    const bool arrayMode = mode == "array";
    const auto params = message.get("params", "[]");

    router->bridge.getRuntime()->services.sqlite.exec(
      message.seq,
      id,
      message.get("sql"),
      arrayMode,
      params,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.prepare", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "sql"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t databaseId;
    REQUIRE_AND_GET_MESSAGE_VALUE(databaseId, "id", std::stoull);

    router->bridge.getRuntime()->services.sqlite.prepare(
      message.seq,
      databaseId,
      message.get("sql"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.statement.bind", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t statementId;
    REQUIRE_AND_GET_MESSAGE_VALUE(statementId, "id", std::stoull);

    const auto params = message.get("params", "[]");

    router->bridge.getRuntime()->services.sqlite.bind(
      message.seq,
      statementId,
      params,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.statement.step", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t statementId;
    REQUIRE_AND_GET_MESSAGE_VALUE(statementId, "id", std::stoull);

    size_t limit = 0;
    if (message.has("limit")) {
      try {
        limit = static_cast<size_t>(std::stoull(message.get("limit")));
      } catch (const std::exception&) {
        JSON::Object::Entries typeErr {
          {"type", "TypeError"},
          {"message", "limit must be a positive integer"}
        };
        return reply(Result::Err { message, typeErr });
      }
    }

    const auto mode = message.get("mode", "object");
    const bool arrayMode = mode == "array";

    router->bridge.getRuntime()->services.sqlite.step(
      message.seq,
      statementId,
      limit,
      arrayMode,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.statement.reset", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t statementId;
    REQUIRE_AND_GET_MESSAGE_VALUE(statementId, "id", std::stoull);

    router->bridge.getRuntime()->services.sqlite.reset(
      message.seq,
      statementId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("sqlite.statement.finalize", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t statementId;
    REQUIRE_AND_GET_MESSAGE_VALUE(statementId, "id", std::stoull);

    router->bridge.getRuntime()->services.sqlite.finalize(
      message.seq,
      statementId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("secureStorage.set", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"key"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const bool hasBuffer = message.buffer.size() > 0;
    const bool hasValueParam = message.has("value");

    if (!hasBuffer && !hasValueParam) {
      return reply(Result::Err { message, JSON::Object::Entries {{
        "message", "Expecting 'value' parameter or binary payload"
      }} });
    }

    bytes::Buffer value;

    if (hasBuffer) {
      value = bytes::Buffer::from(message.buffer);
    } else {
      auto encodingName = message.get("encoding", "utf8");
      core::services::SecureStorage::Encoding encoding;
      if (!core::services::SecureStorage::parseEncoding(encodingName, encoding)) {
        return reply(Result::Err { message, JSON::Object::Entries {{
          "message", "Unsupported encoding"
        }} });
      }

      String decodeError;
      if (!core::services::SecureStorage::decodeValue(message.get("value"), encoding, value, decodeError)) {
        if (decodeError.empty()) {
          decodeError = "Failed to decode value";
        }

        return reply(Result::Err { message, JSON::Object::Entries {{
          "message", decodeError
        }} });
      }
    }

    router->bridge.getRuntime()->services.secureStorage.set(
      message.seq,
      message.get("scope"),
      message.get("key"),
      std::move(value),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("secureStorage.get", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"key"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto encodingName = message.get("encoding", "base64");
    core::services::SecureStorage::Encoding encoding;

    if (!core::services::SecureStorage::parseEncoding(encodingName, encoding)) {
      return reply(Result::Err { message, JSON::Object::Entries {{
        "message", "Unsupported encoding"
      }} });
    }

    router->bridge.getRuntime()->services.secureStorage.get(
      message.seq,
      message.get("scope"),
      message.get("key"),
      encoding,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("secureStorage.remove", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"key"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.secureStorage.remove(
      message.seq,
      message.get("scope"),
      message.get("key"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("secureStorage.clear", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.secureStorage.clear(
      message.seq,
      message.get("scope"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("secureStorage.keys", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.secureStorage.keys(
      message.seq,
      message.get("scope"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });



  /**
   * Query diagnostics information about the runtime core.
   */
  router->map("diagnostics.query", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.diagnostics.query(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Reports the streaming behavior of the active webview backend.
   */
  router->map("diagnostics.capabilities", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_WINDOWS
    constexpr bool incremental = false;
  #else
    constexpr bool incremental = true;
  #endif
    return reply(Result::Data {
      message,
      JSON::Object::Entries {
        {"streaming", JSON::Object::Entries {
          {"sseIncremental", incremental},
          {"chunkedIncremental", incremental}
        }}
      }
    });
  });

  /**
   * Emits a bounded diagnostic server-sent event stream.
   */
  router->map("diagnostics.stream.sse", [](auto message, auto router, auto reply) {
    if (!message.isHTTP) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Diagnostic streams require an HTTP transport"}}
      });
    }

    uint64_t count = 0;
    uint64_t interval = 0;
    String parameterError;
    if (
      !parseDiagnosticStreamParameter(
        message,
        "count",
        8,
        1,
        kMaxDiagnosticStreamItems,
        count,
        parameterError
      ) ||
      !parseDiagnosticStreamParameter(
        message,
        "interval",
        25,
        1,
        kMaxDiagnosticStreamInterval,
        interval,
        parameterError
      )
    ) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", parameterError}}
      });
    }

    auto eventName = message.get("name", "message");
    const auto invalidName = std::find_if(eventName.begin(), eventName.end(), [](unsigned char character) {
      return !std::isalnum(character) && character != '-' && character != '_' && character != '.';
    });
    if (eventName.empty() || eventName.size() > 64 || invalidName != eventName.end()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{
          "message",
          "'name' must contain 1 to 64 letters, digits, hyphens, underscores, or periods"
        }}
      });
    }

    auto stream = std::make_shared<QueuedResponse::EventStreamCallback>(
      [](const char*, const unsigned char*, bool) {
        return false;
      }
    );
    auto response = QueuedResponse {};
    response.headers.set("content-type", "text/event-stream; charset=utf-8");
    response.headers.set("cache-control", "no-store");
    response.eventStreamCallback = stream;
    auto emitted = std::make_shared<uint64_t>(0);
    auto runtime = router->bridge.getRuntime();
    response.streamStartCallback = [runtime, stream, emitted, count, eventName, interval] {
      runtime->services.timers.setInterval(
        interval,
        [stream, emitted, count, eventName](auto cancel) {
          const auto data = std::to_string(*emitted);
          *emitted += 1;
          const bool finished = *emitted >= count;
          const bool sent = (*stream)(
            eventName.c_str(),
            reinterpret_cast<const unsigned char*>(data.c_str()),
            finished
          );
          if (!sent || finished) {
            cancel();
          }
        }
      );
    };
    reply(Result::Data { message, JSON::Object {}, response });
  });

  /**
   * Emits a bounded diagnostic binary stream.
   */
  router->map("diagnostics.stream.chunks", [](auto message, auto router, auto reply) {
    if (!message.isHTTP) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Diagnostic streams require an HTTP transport"}}
      });
    }

    uint64_t chunks = 0;
    uint64_t chunkSize = 0;
    uint64_t interval = 0;
    String parameterError;
    if (
      !parseDiagnosticStreamParameter(
        message,
        "chunks",
        8,
        1,
        kMaxDiagnosticStreamItems,
        chunks,
        parameterError
      ) ||
      !parseDiagnosticStreamParameter(
        message,
        "chunkSize",
        1024,
        1,
        kMaxDiagnosticChunkBytes,
        chunkSize,
        parameterError
      ) ||
      !parseDiagnosticStreamParameter(
        message,
        "interval",
        25,
        1,
        kMaxDiagnosticStreamInterval,
        interval,
        parameterError
      )
    ) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", parameterError}}
      });
    }

    if (chunks * chunkSize > kMaxDiagnosticStreamBytes) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Diagnostic chunk streams are limited to 4194304 bytes"}}
      });
    }

    auto stream = std::make_shared<QueuedResponse::ChunkStreamCallback>(
      [](const unsigned char*, size_t, bool) {
        return false;
      }
    );
    auto response = QueuedResponse {};
    response.headers.set("content-type", "application/octet-stream");
    response.headers.set("transfer-encoding", "chunked");
    response.chunkStreamCallback = stream;
    auto emitted = std::make_shared<uint64_t>(0);
    auto chunk = std::make_shared<Vector<unsigned char>>(chunkSize, 0x5a);
    auto runtime = router->bridge.getRuntime();
    response.streamStartCallback = [runtime, stream, emitted, chunks, chunk, interval] {
      runtime->services.timers.setInterval(
        interval,
        [stream, emitted, chunks, chunk](auto cancel) {
          *emitted += 1;
          const bool finished = *emitted >= chunks;
          const bool sent = (*stream)(chunk->data(), chunk->size(), finished);
          if (!sent || finished) {
            cancel();
          }
        }
      );
    };
    reply(Result::Data { message, JSON::Object {}, response });
  });

  /**
   * Look up an IP address by `hostname`.
   * @param hostname Host name to lookup
   * @param family IP address family to resolve [default = 0 (AF_UNSPEC)]
   * @see getaddrinfo(3)
   */
  router->map("dns.lookup", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"hostname"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int family = 0;
    if (message.has("family")) {
      auto value = message.get("family");
      auto lower = toLowerCase(value);
      if (lower == "ipv4") {
        family = 4;
      } else if (lower == "ipv6") {
        family = 6;
      } else {
        try {
          family = std::stoi(value);
        } catch (...) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'family' given in parameters"}
          }});
        }
      }
    }

    bool all = false;
    if (message.has("all")) {
      auto value = toLowerCase(message.get("all"));
      if (value == "true" || value == "1") {
        all = true;
      } else if (value == "false" || value == "0") {
        all = false;
      } else {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'all' given in parameters"}
        }});
      }
    }

    bool verbatim = false;
    if (message.has("verbatim")) {
      auto value = toLowerCase(message.get("verbatim"));
      if (value == "true" || value == "1") {
        verbatim = true;
      } else if (value == "false" || value == "0") {
        verbatim = false;
      } else {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'verbatim' given in parameters"}
        }});
      }
    }

    int hints = 0;
    if (message.has("hints")) {
      try {
        hints = std::stoi(message.get("hints"));
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'hints' given in parameters"}
        }});
      }
    }

    router->bridge.getRuntime()->services.dns.lookup(
      message.seq,
      oro::runtime::core::services::DNS::LookupOptions { message.get("hostname"), family, all, verbatim, hints },
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("extension.stats", [](auto message, auto router, auto reply) {
    auto extensions = Extension::all();
    auto name = message.get("name");

    if (name.size() > 0) {
      auto type = Extension::getExtensionType(name);
      auto path = Extension::getExtensionPath(name);
      auto json = JSON::Object::Entries {
        {"source", "extension.stats"},
        {"data", JSON::Object::Entries {
          {"abi", ORO_RUNTIME_EXTENSION_ABI_VERSION},
          {"name", name},
          {"type", type},
        #if ORO_RUNTIME_PLATFORM_ANDROID
          {"path", filesystem::Resource::getResourcePath(path).string()}
        #else
          // `path` is absolute to the location of the resources
          {"path", String("/") + std::filesystem::relative(path, getcwd()).string()}
        #endif
        }}
      };

      reply(Result { message.seq, message, json });
    } else {
      int loaded = 0;

      for (const auto& tuple : extensions) {
        if (tuple.second != nullptr) {
          loaded++;
        }
      }

      auto json = JSON::Object::Entries {
        {"source", "extension.stats"},
        {"data", JSON::Object::Entries {
          {"abi", ORO_RUNTIME_EXTENSION_ABI_VERSION},
          {"loaded", loaded}
        }}
      };

      reply(Result { message.seq, message, json });
    }
  });

  /**
   * Query for type of extension ('shared', 'wasm32', 'unknown')
   * @param name
   */
  router->map("extension.type", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto name = message.get("name");
    auto type = Extension::getExtensionType(name);
    auto json = JSON::Object::Entries {
      {"source", "extension.type"},
      {"data", JSON::Object::Entries {
        {"name", name},
        {"type", type}
      }}
    };

    reply(Result { message.seq, message, json });
  });

  /**
   * Load a named native extension.
   * @param name
   * @param allow
   */
  router->map("extension.load", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto name = message.get("name");

    if (!Extension::load(name)) {
      #if ORO_RUNTIME_PLATFORM_WINDOWS
      auto error = formatWindowsError(GetLastError(), "bridge");
      #else
      auto err = dlerror();
      auto error = String(err ? err : "Unknown error");
      #endif

      std::cout << "Load extension error: " << error << std::endl;

      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Failed to load extension: '" + name + "': " + error}
      }});
    }

    auto extension = Extension::get(name);
    auto allowed = split(message.get("allow"), ',');
    auto context = Extension::getContext(name);
    auto ctx = context->memory.template alloc<Extension::Context>(context, router);

    for (const auto& value : allowed) {
      auto policy = trim(value);
      ctx->setPolicy(policy, true);
    }

    Extension::setRouterContext(name, router, ctx);

    /// init context
    if (!Extension::initialize(ctx, name, nullptr)) {
      if (ctx->state == Extension::Context::State::Error) {
        auto json = JSON::Object::Entries {
          {"source", "extension.load"},
          {"extension", name},
          {"err", JSON::Object::Entries {
            {"code", ctx->error.code},
            {"name", ctx->error.name},
            {"message", ctx->error.message},
            {"location", ctx->error.location},
          }}
        };

        reply(Result { message.seq, message, json });
      } else {
        auto json = JSON::Object::Entries {
          {"source", "extension.load"},
          {"extension", name},
          {"err", JSON::Object::Entries {
            {"message", "Failed to initialize extension: '" + name + "'"},
          }}
        };

        reply(Result { message.seq, message, json });
      }
    } else {
      auto json = JSON::Object::Entries {
        {"source", "extension.load"},
        {"data", JSON::Object::Entries {
         {"abi", (uint64_t) extension->abi},
         {"name", extension->name},
         {"version", extension->version},
         {"description", extension->description}
        }}
      };

      reply(Result { message.seq, message, json });
    }
  });

  /**
   * Unload a named native extension.
   * @param name
   */
  router->map("extension.unload", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto name = message.get("name");

    if (!Extension::isLoaded(name)) {
      return reply(Result::Err { message, JSON::Object::Entries {
      #if ORO_RUNTIME_PLATFORM_WINDOWS
        {"message", "Extension '" + name + "' is not loaded"}
      #else
        {"message", "Extension '" + name + "' is not loaded" + String(dlerror())}
      #endif
      }});
    }

    auto extension = Extension::get(name);
    auto ctx = Extension::getRouterContext(name, router);

    if (Extension::unload(ctx, name, extension->contexts.size() == 1)) {
      Extension::removeRouterContext(name, router);
      auto json = JSON::Object::Entries {
        {"source", "extension.unload"},
        {"extension", name},
        {"data", JSON::Object::Entries {}}
      };
      return reply(Result { message.seq, message, json });
    }

    if (ctx->state == Extension::Context::State::Error) {
      auto json = JSON::Object::Entries {
        {"source", "extension.unload"},
        {"extension", name},
        {"err", JSON::Object::Entries {
          {"code", ctx->error.code},
          {"name", ctx->error.name},
          {"message", ctx->error.message},
          {"location", ctx->error.location},
        }}
      };

      reply(Result { message.seq, message, json });
    } else {
      auto json = JSON::Object::Entries {
        {"source", "extension.unload"},
        {"extension", name},
        {"err", JSON::Object::Entries {
          {"message", "Failed to unload extension: '" + name + "'"},
        }}
      };

      reply(Result { message.seq, message, json });
    }
  });

  /**
   * Checks if current user can access file at `path` with `mode`.
   * @param path
   * @param mode
   * @see access(2)
   */
  router->map("fs.access", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "mode"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int mode = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(mode, "mode", std::stoi);

    router->bridge.getRuntime()->services.fs.access(
      message.seq,
      message.get("path"),
      mode,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Opens a tar archive at `path`.
   * @param path
   * @param writable
   * @param mmap
   */
  router->map("tar.open", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto path = message.get("path");
    const auto writable = message.get("writable") == "true";
    const auto mmap = message.get("mmap") == "true";

    uint32_t uid = 0;
    uint32_t gid = 0;
    uint64_t mtime = 0;
    oro::runtime::String uname;
    oro::runtime::String gname;
    bool hasUid = false;
    bool hasGid = false;
    bool hasUname = false;
    bool hasGname = false;
    bool hasMtime = false;

    const auto uidStr = message.get("uid");
    if (!uidStr.empty()) {
      try {
        const auto parsed = static_cast<uint64_t>(std::stoull(uidStr));
        if (parsed > std::numeric_limits<uint32_t>::max()) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'uid' given in parameters; expected a 32-bit unsigned integer"}
          }});
        }
        uid = static_cast<uint32_t>(parsed);
        hasUid = true;
      } catch (...) {}
    }

    const auto gidStr = message.get("gid");
    if (!gidStr.empty()) {
      try {
        const auto parsed = static_cast<uint64_t>(std::stoull(gidStr));
        if (parsed > std::numeric_limits<uint32_t>::max()) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'gid' given in parameters; expected a 32-bit unsigned integer"}
          }});
        }
        gid = static_cast<uint32_t>(parsed);
        hasGid = true;
      } catch (...) {}
    }

    const auto mtimeStr = message.get("mtime");
    if (!mtimeStr.empty()) {
      try {
        mtime = static_cast<uint64_t>(std::stoull(mtimeStr));
        hasMtime = true;
      } catch (...) {}
    }

    const auto unameStr = message.get("uname");
    if (!unameStr.empty()) {
      uname = unameStr;
      hasUname = true;
    }

    const auto gnameStr = message.get("gname");
    if (!gnameStr.empty()) {
      gname = gnameStr;
      hasGname = true;
    }

    router->bridge.getRuntime()->services.tar.open(
      message.seq,
      path,
      writable,
      mmap,
      uid,
      gid,
      uname,
      gname,
      mtime,
      hasUid,
      hasGid,
      hasUname,
      hasGname,
      hasMtime,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Opens a tar archive from an in-memory buffer for read-only access.
   * Expects binary data in the message buffer.
   */
  router->map("tar.openBuffer", [](auto message, auto router, auto reply) {
    if (message.buffer.data() == nullptr || message.buffer.size() == 0) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Expecting non-empty tar buffer in message payload"}
      }});
    }

    router->bridge.getRuntime()->services.tar.openFromBuffer(
      message.seq,
      message.buffer.shared(),
      message.buffer.size(),
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Creates a new in-memory tar archive for writing.
   * Global metadata (uid, gid, uname, gname, mtime) is optional.
   */
  router->map("tar.createBuffer", [](auto message, auto router, auto reply) {
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint64_t mtime = 0;
    String uname;
    String gname;
    bool hasUid = false;
    bool hasGid = false;
    bool hasUname = false;
    bool hasGname = false;
    bool hasMtime = false;

    const auto uidStr = message.get("uid");
    if (!uidStr.empty()) {
      try {
        const auto parsed = static_cast<uint64_t>(std::stoull(uidStr));
        if (parsed > std::numeric_limits<uint32_t>::max()) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'uid' given in parameters; expected a 32-bit unsigned integer"}
          }});
        }
        uid = static_cast<uint32_t>(parsed);
        hasUid = true;
      } catch (...) {}
    }

    const auto gidStr = message.get("gid");
    if (!gidStr.empty()) {
      try {
        const auto parsed = static_cast<uint64_t>(std::stoull(gidStr));
        if (parsed > std::numeric_limits<uint32_t>::max()) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'gid' given in parameters; expected a 32-bit unsigned integer"}
          }});
        }
        gid = static_cast<uint32_t>(parsed);
        hasGid = true;
      } catch (...) {}
    }

    const auto mtimeStr = message.get("mtime");
    if (!mtimeStr.empty()) {
      try {
        mtime = static_cast<uint64_t>(std::stoull(mtimeStr));
        hasMtime = true;
      } catch (...) {}
    }

    const auto unameStr = message.get("uname");
    if (!unameStr.empty()) {
      uname = unameStr;
      hasUname = true;
    }

    const auto gnameStr = message.get("gname");
    if (!gnameStr.empty()) {
      gname = gnameStr;
      hasGname = true;
    }

    router->bridge.getRuntime()->services.tar.createInMemory(
      message.seq,
      uid,
      gid,
      uname,
      gname,
      mtime,
      hasUid,
      hasGid,
      hasUname,
      hasGname,
      hasMtime,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Closes an open tar archive descriptor.
   * @param id
   */
  router->map("tar.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tar.close(
      message.seq,
      id,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Lists entries in an open tar archive.
   * @param id
   */
  router->map("tar.list", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tar.listEntries(
      message.seq,
      id,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Returns metadata for a single entry path in a tar archive.
   * @param id
   * @param path
   */
  router->map("tar.stat", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tar.statEntry(
      message.seq,
      id,
      message.get("path"),
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Reads up to `size` bytes from a tar archive entry at `path` starting at `offset`.
   * @param id
   * @param path
   * @param offset
   * @param size
   */
  router->map("tar.read", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path", "offset", "size"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    uint64_t offset = 0;
    uint32_t size = 0;

    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(offset, "offset", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(size, "size", std::stoul);

    router->bridge.getRuntime()->services.tar.readEntry(
      message.seq,
      id,
      message.get("path"),
      offset,
      size,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Begins writing a new entry into a tar archive.
   * @param id
   * @param path
   * @param size
   * @param mode
   * @param mtime
   * @param type
   */
  router->map("tar.write.begin", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path", "size"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    uint64_t size = 0;
    uint32_t mode = 0;
    uint64_t mtime = 0;
    bool hasMtime = false;
    uint32_t devmajor = 0;
    uint32_t devminor = 0;
    uint64_t sparseSize = 0;
    uint32_t uid = 0;
    uint32_t gid = 0;
    bool hasUid = false;
    bool hasGid = false;
    bool hasUname = false;
    bool hasGname = false;
    String uname;
    String gname;

    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(size, "size", std::stoull);

    if (message.has("mode")) {
      uint64_t parsed = 0;
      REQUIRE_AND_GET_MESSAGE_VALUE(parsed, "mode", std::stoull);
      if (parsed > std::numeric_limits<uint32_t>::max()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'mode' given in parameters; expected a 32-bit unsigned integer"}
        }});
      }
      mode = static_cast<uint32_t>(parsed);
    } else {
      mode = 0644;
    }

    if (message.has("mtime")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(mtime, "mtime", std::stoull);
      hasMtime = true;
    }

    if (message.has("uid")) {
      uint64_t parsed = 0;
      REQUIRE_AND_GET_MESSAGE_VALUE(parsed, "uid", std::stoull);
      if (parsed > std::numeric_limits<uint32_t>::max()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'uid' given in parameters; expected a 32-bit unsigned integer"}
        }});
      }
      uid = static_cast<uint32_t>(parsed);
      hasUid = true;
    }

    if (message.has("gid")) {
      uint64_t parsed = 0;
      REQUIRE_AND_GET_MESSAGE_VALUE(parsed, "gid", std::stoull);
      if (parsed > std::numeric_limits<uint32_t>::max()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'gid' given in parameters; expected a 32-bit unsigned integer"}
        }});
      }
      gid = static_cast<uint32_t>(parsed);
      hasGid = true;
    }

    if (message.has("uname")) {
      uname = message.get("uname");
      hasUname = true;
    }

    if (message.has("gname")) {
      gname = message.get("gname");
      hasGname = true;
    }

    if (message.has("devmajor")) {
      uint64_t parsed = 0;
      REQUIRE_AND_GET_MESSAGE_VALUE(parsed, "devmajor", std::stoull);
      if (parsed > std::numeric_limits<uint32_t>::max()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'devmajor' given in parameters; expected a 32-bit unsigned integer"}
        }});
      }
      devmajor = static_cast<uint32_t>(parsed);
    }

    if (message.has("devminor")) {
      uint64_t parsed = 0;
      REQUIRE_AND_GET_MESSAGE_VALUE(parsed, "devminor", std::stoull);
      if (parsed > std::numeric_limits<uint32_t>::max()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'devminor' given in parameters; expected a 32-bit unsigned integer"}
        }});
      }
      devminor = static_cast<uint32_t>(parsed);
    }

    const auto typeParam = message.get("type", "0");
    char type = '0';
    if (!typeParam.empty()) {
      type = typeParam[0];
    }

    if (message.has("sparseSize")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(sparseSize, "sparseSize", std::stoull);
    }

    router->bridge.getRuntime()->services.tar.beginWriteEntry(
      message.seq,
      id,
      message.get("path"),
      message.get("linkpath"),
      devmajor,
      devminor,
      size,
      mode,
      mtime,
      hasMtime,
      type,
      uid,
      gid,
      uname,
      gname,
      hasUid,
      hasGid,
      hasUname,
      hasGname,
      sparseSize,
      message.get("sparse"),
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Writes data for the current entry in a tar archive.
   * Expects binary data in `message.buffer`.
   * @param id
   * @param offset (ignored, reserved)
   */
  router->map("tar.write.data", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    auto& service = router->bridge.getRuntime()->services.tar;

    service.writeEntryData(
      message.seq,
      id,
      message.buffer.shared(),
      message.buffer.size(),
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Finalizes a tar archive, writing terminating blocks and flushing the sink.
   * @param id
   */
  router->map("tar.finalize", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tar.finalize(
      message.seq,
      id,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Returns the finalized in-memory tar archive as a binary payload.
   * @param id
   */
  router->map("tar.buffer", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tar.getBuffer(
      message.seq,
      id,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Reports zlib availability on the current runtime.
   * @return { available: boolean }
   */
  router->map("zlib.capabilities", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.zlib;
    const bool available = service.available() && service.enabled.load();

    const JSON::Object data = JSON::Object::Entries {
      {"available", available}
    };

    reply(Result::Data { message, data });
  });

  /**
   * Compresses a buffer using zlib/deflate. Expects binary data in the
   * request body and returns an `application/octet-stream` response.
   * @param format 'zlib' | 'gzip' | 'raw' (default: 'zlib')
   * @param level compression level (0-9, default: zlib default)
   */
  router->map("zlib.deflate", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.zlib;
    if (!service.available() || !service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "zlib service is disabled or unavailable"}
        }
      });
    }

    core::services::Zlib::CompressOptions options;

    if (message.has("level")) {
      int level = 0;
      REQUIRE_AND_GET_MESSAGE_VALUE(level, "level", std::stoi);
      options.level = level;
    }

    const auto format = toLowerCase(message.get("format", "zlib"));
    if (format == "gzip") {
      options.gzip = true;
    } else if (format == "raw") {
      options.raw = true;
    }

    service.deflate(
      message.seq,
      message.buffer.shared(),
      message.buffer.size(),
      options,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Decompresses a buffer using zlib/inflate. Expects binary data in the
   * request body and returns an `application/octet-stream` response.
   * @param format 'zlib' | 'gzip' | 'raw' (default: 'zlib')
   */
  router->map("zlib.inflate", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.zlib;
    if (!service.available() || !service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "zlib service is disabled or unavailable"}
        }
      });
    }

    core::services::Zlib::CompressOptions options;

    const auto format = toLowerCase(message.get("format", "zlib"));
    if (format == "gzip") {
      options.gzip = true;
    } else if (format == "raw") {
      options.raw = true;
    }

    service.inflate(
      message.seq,
      message.buffer.shared(),
      message.buffer.size(),
      options,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Opens a stateful zlib stream for incremental compression or
   * decompression.
   * @param mode 'deflate' | 'inflate' (default: 'deflate')
   * @param format 'zlib' | 'gzip' | 'raw' (default: 'zlib')
   * @param level compression level for deflate (0-9, default: zlib default)
   */
  router->map("zlib.stream.open", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.zlib;
    if (!service.available() || !service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "zlib service is disabled or unavailable"}
        }
      });
    }

    core::services::Zlib::StreamOptions options;

    const auto mode = toLowerCase(message.get("mode", "deflate"));
    if (mode == "inflate") {
      options.inflate = true;
    }

    const auto format = toLowerCase(message.get("format", "zlib"));
    if (format == "gzip") {
      options.gzip = true;
    } else if (format == "raw") {
      options.raw = true;
    }

    if (message.has("level")) {
      int level = 0;
      REQUIRE_AND_GET_MESSAGE_VALUE(level, "level", std::stoi);
      options.level = level;
    }

    service.openStream(
      message.seq,
      options,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Writes a chunk into a stateful zlib stream opened via `zlib.stream.open`.
   * Expects binary data in the request body and returns a compressed or
   * decompressed chunk as `application/octet-stream`.
   * @param id stream id returned from `zlib.stream.open`
   * @param finish boolean indicating this is the final chunk
   */
  router->map("zlib.stream.write", [](auto message, auto router, auto reply) {
    auto& service = router->bridge.getRuntime()->services.zlib;
    if (!service.available() || !service.enabled.load()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "zlib service is disabled or unavailable"}
        }
      });
    }

    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    const auto finishRaw = toLowerCase(message.get("finish", "false"));
    const bool finish = finishRaw == "true" || finishRaw == "1" || finishRaw == "yes";

    service.writeStream(
      message.seq,
      id,
      message.buffer.shared(),
      message.buffer.size(),
      finish,
      [message, router, reply](auto seq, auto json, auto queuedResponse) {
        replyConduitAware(router, reply, Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * SemVer utilities exposed to JavaScript. These routes provide a thin,
   * synchronous wrapper around the native semver implementation so that
   * consumers can parse, compare, and check ranges without re-implementing
   * the SemVer 2.0.0 rules in JavaScript.
   */
  router->map("semver.parse", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"version"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto raw = message.get("version");
    oro::runtime::semver::Version v;
    oro::runtime::String parseError;

    if (!oro::runtime::semver::parse(raw, v, &parseError)) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid semantic version: " + parseError}}
      });
    }

    JSON::Array::Entries prerelease;
    prerelease.reserve(v.prerelease.size());
    for (const auto& value : v.prerelease) {
      prerelease.emplace_back(value);
    }

    JSON::Array::Entries build;
    build.reserve(v.build.size());
    for (const auto& value : v.build) {
      build.emplace_back(value);
    }

    const JSON::Object data = JSON::Object::Entries {
      { "version", v.str() },
      { "major", v.major },
      { "minor", v.minor },
      { "patch", v.patch },
      { "prerelease", prerelease },
      { "build", build }
    };

    reply(Result::Data { message, data });
  });

  router->map("semver.compare", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"a", "b"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto a = message.get("a");
    auto b = message.get("b");

    bool ok = false;
    auto cmp = oro::runtime::semver::compare(a, b, &ok);
    if (!ok) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid semantic version(s) given to compare"}}
      });
    }

    int result = 0;
    if (cmp == oro::runtime::semver::Compare::Less) {
      result = -1;
    } else if (cmp == oro::runtime::semver::Compare::Greater) {
      result = 1;
    } else {
      result = 0;
    }

    const JSON::Object data = JSON::Object::Entries {
      { "result", result }
    };

    reply(Result::Data { message, data });
  });

  router->map("semver.satisfies", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"version", "range"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto version = message.get("version");
    const auto range = message.get("range");
    oro::runtime::String parseError;

    const bool ok = oro::runtime::semver::satisfies(version, range, &parseError);
    if (!ok && !parseError.empty()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid semantic version or range: " + parseError}}
      });
    }

    const JSON::Object data = JSON::Object::Entries {
      { "result", ok }
    };

    reply(Result::Data { message, data });
  });

  router->map("semver.inc", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"version", "release"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto version = message.get("version");
    const auto release = message.get("release");
    const auto preid = message.get("preid");

    oro::runtime::semver::ReleaseType type;
    if (release == "major") {
      type = oro::runtime::semver::ReleaseType::Major;
    } else if (release == "minor") {
      type = oro::runtime::semver::ReleaseType::Minor;
    } else if (release == "patch") {
      type = oro::runtime::semver::ReleaseType::Patch;
    } else if (release == "premajor") {
      type = oro::runtime::semver::ReleaseType::Premajor;
    } else if (release == "preminor") {
      type = oro::runtime::semver::ReleaseType::Preminor;
    } else if (release == "prepatch") {
      type = oro::runtime::semver::ReleaseType::Prepatch;
    } else if (release == "prerelease") {
      type = oro::runtime::semver::ReleaseType::Prerelease;
    } else {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid release type for semver.inc"}}
      });
    }

    const auto next = oro::runtime::semver::inc(version, type, preid);
    if (next.empty()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Failed to increment semantic version"}}
      });
    }

    const JSON::Object data = JSON::Object::Entries {
      { "version", next }
    };

    reply(Result::Data { message, data });
  });

  /**
   * Returns a mapping of file system constants.
   */
  router->map("fs.constants", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.fs.constants(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  /**
   * Changes `mode` of file at `path`.
   * @param path
   * @param mode
   * @see chmod(2)
   */
  router->map("fs.chmod", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "mode"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int mode = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(mode, "mode", std::stoi);

    router->bridge.getRuntime()->services.fs.chmod(
      message.seq,
      message.get("path"),
      mode,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Changes `mode` of a symbolic link at `path` without following it.
   * @param path
   * @param mode
   * @see lchmod(2)
   */
  router->map("fs.lchmod", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "mode"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int mode = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(mode, "mode", std::stoi);

    router->bridge.getRuntime()->services.fs.lchmod(
      message.seq,
      message.get("path"),
      mode,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Changes uid and gid of file at `path`.
   * @param path
   * @param uid
   * @param gid
   * @see chown(2)
   */
  router->map("fs.chown", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "uid", "gid"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int uid = 0;
    int gid = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(uid, "uid", std::stoi);
    REQUIRE_AND_GET_MESSAGE_VALUE(gid, "gid", std::stoi);

    router->bridge.getRuntime()->services.fs.chown(
      message.seq,
      message.get("path"),
      static_cast<uv_uid_t>(uid),
      static_cast<uv_gid_t>(gid),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Changes uid and gid of symbolic link at `path`.
   * @param path
   * @param uid
   * @param gid
   * @see lchown(2)
   */
  router->map("fs.lchown", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "uid", "gid"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int uid = 0;
    int gid = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(uid, "uid", std::stoi);
    REQUIRE_AND_GET_MESSAGE_VALUE(gid, "gid", std::stoi);

    router->bridge.getRuntime()->services.fs.lchown(
      message.seq,
      message.get("path"),
      static_cast<uv_uid_t>(uid),
      static_cast<uv_gid_t>(gid),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Closes underlying file descriptor handle.
   * @param id
   * @see close(2)
   */
  router->map("fs.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.close(message.seq, id, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  /**
   * Closes underlying directory descriptor handle.
   * @param id
   * @see closedir(3)
   */
  router->map("fs.closedir", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.closedir(message.seq, id, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  /**
   * Closes an open file or directory descriptor handle.
   * @param id
   * @see close(2)
   * @see closedir(3)
   */
  router->map("fs.closeOpenDescriptor", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.closeOpenDescriptor(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Closes all open file and directory descriptors, optionally preserving
   * explicitly retrained descriptors.
   * @param preserveRetained (default: true)
   * @see close(2)
   * @see closedir(3)
   */
  router->map("fs.closeOpenDescriptors", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.fs.closeOpenDescriptor(
      message.seq,
      message.get("preserveRetained") != "false",
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Copy file at path `src` to path `dest`.
   * @param src
   * @param dest
   * @param flags
   * @see copyfile(3)
   */
  router->map("fs.copyFile", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"src", "dest", "flags"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int flags = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(flags, "flags", std::stoi);

    router->bridge.getRuntime()->services.fs.copyFile(
      message.seq,
      message.get("src"),
      message.get("dest"),
      flags,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Creates a link at `dest`
   * @param src
   * @param dest
   * @see link(2)
   */
  router->map("fs.link", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"src", "dest"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.link(
      message.seq,
      message.get("src"),
      message.get("dest"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Creates a symlink at `dest`
   * @param src
   * @param dest
   * @param flags
   * @see symlink(2)
   */
  router->map("fs.symlink", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"src", "dest", "flags"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int flags = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(flags, "flags", std::stoi);

    router->bridge.getRuntime()->services.fs.symlink(
      message.seq,
      message.get("src"),
      message.get("dest"),
      flags,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Computes stats for an open file descriptor.
   * @param id
   * @see stat(2)
   * @see fstat(2)
   */
  router->map("fs.fstat", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.fstat(message.seq, id, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  /**
   * Synchronize a file's in-core state with storage device
   * @param id
   * @see fsync(2)
   */
  router->map("fs.fsync", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.fsync(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Synchronizes a file's data with the storage device.
   * @param id
   * @see fdatasync(2)
   */
  router->map("fs.fdatasync", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.fdatasync(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Truncates opened file
   * @param id
   * @param offset
   * @see ftruncate(2)
   */
  router->map("fs.ftruncate", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "offset"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    int64_t offset;
    REQUIRE_AND_GET_MESSAGE_VALUE(offset, "offset", std::stoll);

    router->bridge.getRuntime()->services.fs.ftruncate(
      message.seq,
      id,
      offset,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns all open file or directory descriptors.
   */
  router->map("fs.getOpenDescriptors", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.fs.getOpenDescriptors(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Computes stats for a symbolic link at `path`.
   * @param path
   * @see stat(2)
   * @see lstat(2)
   */
  router->map("fs.lstat", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.lstat(
      message.seq,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Creates a directory at `path` with an optional mode and an optional recursive flag.
   * @param path
   * @param mode
   * @param recursive
   * @see mkdir(2)
   */
  router->map("fs.mkdir", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "mode"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int mode = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(mode, "mode", std::stoi);

    router->bridge.getRuntime()->services.fs.mkdir(
      message.seq,
      message.get("path"),
      mode,
      message.get("recursive") == "true",
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Creates a uniquely named temporary directory from `prefix`.
   * @param prefix
   * @see mkdtemp(3)
   */
  router->map("fs.mkdtemp", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"prefix"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.mkdtemp(
      message.seq,
      message.get("prefix"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });


  /**
   * Opens a file descriptor at `path` for `id` with `flags` and `mode`
   * @param id
   * @param path
   * @param flags
   * @param mode
   * @see open(2)
   */
  router->map("fs.open", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {
      "id",
      "path",
      "flags",
      "mode"
    });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    int mode = 0;
    int flags = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(mode, "mode", std::stoi);
    REQUIRE_AND_GET_MESSAGE_VALUE(flags, "flags", std::stoi);

    router->bridge.getRuntime()->services.fs.open(
      message.seq,
      id,
      message.get("path"),
      flags,
      mode,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Opens a directory descriptor at `path` for `id` with `flags` and `mode`
   * @param id
   * @param path
   * @see opendir(3)
   */
  router->map("fs.opendir", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.opendir(
      message.seq,
      id,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Reads `size` bytes at `offset` from the underlying file descriptor.
   * @param id
   * @param size
   * @param offset
   * @see read(2)
   */
  router->map("fs.read", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "size", "offset"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    int size = 0;
    int offset = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(size, "size", std::stoi);
    REQUIRE_AND_GET_MESSAGE_VALUE(offset, "offset", std::stoi);

    router->bridge.getRuntime()->services.fs.read(
      message.seq,
      id,
      size,
      offset,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Reads next `entries` of from the underlying directory descriptor.
   * @param id
   * @param entries (default: 256)
   */
  router->map("fs.readdir", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    int entries = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(entries, "entries", std::stoi);

    router->bridge.getRuntime()->services.fs.readdir(
      message.seq,
      id,
      entries,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Read value of a symbolic link at 'path'
   * @param path
   * @see readlink(2)
   */
  router->map("fs.readlink", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.readlink(
      message.seq,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Get the realpath at 'path'
   * @param path
   * @see realpath(2)
   */
  router->map("fs.realpath", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.realpath(
      message.seq,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Marks a file or directory descriptor as retained.
   * @param id
   */
  router->map("fs.retainOpenDescriptor", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.retainOpenDescriptor(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Renames file at path `src` to path `dest`.
   * @param src
   * @param dest
   * @see rename(2)
   */
  router->map("fs.rename", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"src", "dest"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.rename(
      message.seq,
      message.get("src"),
      message.get("dest"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Removes file at `path`.
   * @param path
   * @see rmdir(2)
   */
  router->map("fs.rmdir", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.rmdir(
      message.seq,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Computes stats for a file at `path`.
   * @param path
   * @see stat(2)
   */
  router->map("fs.stat", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.stat(
      message.seq,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Updates access and modification times for a file at `path`.
   * @param path
   * @param atime
   * @param mtime
   * @see utimes(2)
   */
  router->map("fs.utimes", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "atime", "mtime"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    double atime = 0;
    double mtime = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(atime, "atime", std::stod);
    REQUIRE_AND_GET_MESSAGE_VALUE(mtime, "mtime", std::stod);

    router->bridge.getRuntime()->services.fs.utimes(
      message.seq,
      message.get("path"),
      atime,
      mtime,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Updates access and modification times for an open file descriptor.
   * @param id
   * @param atime
   * @param mtime
   * @see futimes(2)
   */
  router->map("fs.futimes", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "atime", "mtime"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    double atime = 0;
    double mtime = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(atime, "atime", std::stod);
    REQUIRE_AND_GET_MESSAGE_VALUE(mtime, "mtime", std::stod);

    router->bridge.getRuntime()->services.fs.futimes(
      message.seq,
      id,
      atime,
      mtime,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Updates access and modification times for a symbolic link.
   * @param path
   * @param atime
   * @param mtime
   * @see lutimes(2)
   */
  router->map("fs.lutimes", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path", "atime", "mtime"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    double atime = 0;
    double mtime = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(atime, "atime", std::stod);
    REQUIRE_AND_GET_MESSAGE_VALUE(mtime, "mtime", std::stod);

    router->bridge.getRuntime()->services.fs.lutimes(
      message.seq,
      message.get("path"),
      atime,
      mtime,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Stops a already started watcher
   */
  router->map("fs.stopWatch", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.stopWatch(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Removes a file or empty directory at `path`.
   * @param path
   * @see unlink(2)
   */
  router->map("fs.unlink", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.fs.unlink(
      message.seq,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * TODO
   */
  router->map("fs.watch", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "path"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.fs.watch(
      message.seq,
      id,
      message.get("path"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Writes buffer at `message.buffer.bytes` of size `message.buffers.size`
   * at `offset` for an opened file handle.
   * @param id Handle ID for an open file descriptor
   * @param offset The offset to start writing at
   * @see write(2)
   */
  router->map("fs.write", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "offset"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    int64_t offset = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(offset, "offset", std::stoll);

    if (message.buffer.data() == nullptr || message.buffer.size() == 0) {
      const auto json = JSON::Object::Entries {
        {"source", "fs.write"},
        {"data", JSON::Object::Entries {
          {"id", message.get("id")},
          {"result", 0}
        }}
      };
      return reply(Result::Data { message, json });
    }

    router->bridge.getRuntime()->services.fs.write(
      message.seq,
      id,
      message.buffer.shared(),
      message.buffer.size(),
      offset,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("geolocation.getCurrentPosition", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.geolocation.getCurrentPosition(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("geolocation.watchPosition", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoi);

    router->bridge.getRuntime()->services.geolocation.watchPosition(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("geolocation.clearWatch", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoi);
    router->bridge.getRuntime()->services.geolocation.clearWatch(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );

    reply(Result { message.seq, message, JSON::Object{} });
  });

  router->map("otp.credentials.get", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"origin"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.otp.get(
      message.seq,
      message.map(),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * A private API for artifically setting the current cached CWD value.
   * This is only useful on platforms that need to set this value from an
   * external source, like Android or ChromeOS.
   */
  router->map("internal.setcwd", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    setcwd(message.value);
    reply(Result { message.seq, message, JSON::Object{} });
  });

  /**
   * A private API for starting the Runtime `oro::runtime::core::services::Conduit`, if it isn't running.
   */
  router->map("internal.conduit.start", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.conduit.start([=]() {
      if (router->bridge.getRuntime()->services.conduit.isActive()) {
        reply(Result::Data {
          message,
          JSON::Object::Entries {
            {"isActive", true},
            {"port", router->bridge.getRuntime()->services.conduit.port.load()}
          }
        });
      } else {
        const auto err = JSON::Object::Entries {{ "message", "Failed to start Conduit"}};
        reply(Result::Err { message, err });
      }
    });
  });

  /**
   * A private API for stopping the Runtime `oro::runtime::core::services::Conduit`, if it is running.
   */
  router->map("internal.conduit.stop", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.conduit.stop();
    reply(Result { message.seq, message, JSON::Object{} });
  });

  /**
   * A private API for getting the status of the Runtime `oro::runtime::core::services::Conduit.
   */
  router->map("internal.conduit.status", [](auto message, auto router, auto reply) {
    reply(Result::Data {
      message,
      JSON::Object::Entries {
        {"sharedKey", router->bridge.getRuntime()->services.conduit.sharedKey},
        {"isActive", router->bridge.getRuntime()->services.conduit.isActive()},
        {"port", router->bridge.getRuntime()->services.conduit.port.load()}
      }
    });
  });

  /**
   * A private API for setting the shared key of the Runtime `oro::runtime::core::services::Conduit.
   */
  router->map("internal.conduit.setSharedKey", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"sharedKey"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto sharedKey = message.get("sharedKey");

    if (sharedKey.size() < 8) {
      const auto err = JSON::Object::Entries {
        {"message", "Invalid shared key length. Must be at least 8 bytes"}
      };

      return reply(Result::Err { message, err });
    }

    router->bridge.getRuntime()->services.conduit.sharedKey = sharedKey;

    reply(Result::Data {
      message,
      JSON::Object::Entries {
        {"sharedKey", sharedKey}
      }
    });
  });

  /**
   * A private API for getting the shared key of the Runtime `oro::runtime::core::services::Conduit.
   */
  router->map("internal.conduit.getSharedKey", [](auto message, auto router, auto reply) {
    reply(Result::Data {
      message,
      JSON::Object::Entries {
        {"sharedKey", router->bridge.getRuntime()->services.conduit.sharedKey}
      }
    });
  });

  /**
   * Log `value to stdout` with platform dependent logger.
   * @param value
   */
  router->map("log", [=](auto message, auto router, auto reply) {
    auto value = message.value.c_str();
    #if ORO_RUNTIME_PLATFORM_APPLE
      NSLog(@"%s", value);
      os_log_with_type(ORO_RUNTIME_OS_LOG_BUNDLE, OS_LOG_TYPE_INFO, "%{public}s", value);
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      __android_log_print(ANDROID_LOG_DEBUG, "", "%s", value);
    #else
      printf("%s\n", value);
    #endif
  });

  router->map("mime.lookup", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, { "value" });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    auto resource = filesystem::Resource(message.value, filesystem::Resource::Options {
      .cache = false
    });

    reply(Result { message.seq, message, JSON::Object::Entries {
      {"url", resource.url.str()},
      {"type", resource.mimeType()}
    }});
  });

  router->map("notification.show", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {
      "id",
      "title"
    });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    oro::runtime::core::services::Notifications::ShowOptions options;
    options.id = message.get("id");
    options.title = message.get("title", "Notification");
    options.tag = message.get("tag");
    options.lang = message.get("lang");
    options.silent = message.get("silent") == "true";
    options.icon = message.get("icon");
    options.image = message.get("image");
    options.body = message.get("body");
    options.channel = replace(
      message.get("channel", "default"),
      "default",
      router->bridge.userConfig["meta_bundle_identifier"]
    );
    options.category = message.get("category");
    options.vibrate = message.get("vibrate");
    options.badge = message.get("badge");
    options.dir = message.get("dir");
    options.data = message.get("data");
    options.actions = message.get("actions");
    options.timestamp = message.get("timestamp");
    options.renotify = message.get("renotify") == "true";
    options.requireInteraction = message.get("requireInteraction") == "true";

    router->bridge.getRuntime()->services.notifications.show(options, [=] (const auto result) {
      if (result.error.size() > 0) {
        const auto err = JSON::Object::Entries {{ "message", result.error }};
        return reply(Result::Err { message, err });
      }

      const auto data = JSON::Object::Entries {{"id", result.notification.identifier}};
      reply(Result::Data { message, data });
    });
  });

  router->map("notification.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, { "id" });

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }


    const auto notification = oro::runtime::core::services::Notifications::Notification {
      message.get("id"),
      message.get("tag")
    };

    router->bridge.getRuntime()->services.notifications.close(notification);

    reply(Result { message.seq, message, JSON::Object::Entries {
      {"id", notification.identifier}
    }});
  });

  router->map("notification.list", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.notifications.list([=](const auto notifications) {
      JSON::Array entries;
      for (const auto& notification : notifications) {
        entries.push(notification.json());
      }

      reply(Result::Data { message.seq, entries, });
    });
  });

  /**
   * Read or modify the `SEND_BUFFER` or `RECV_BUFFER` for a peer socket.
   * @param id Handle ID for the buffer to read/modify
   * @param size If given, the size to set in the buffer [default = 0]
   * @param buffer The buffer to read/modify (SEND_BUFFER, RECV_BUFFER) [default = 0 (SEND_BUFFER)]
   */
  router->map("os.bufferSize", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    int buffer = 0;
    int size = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(buffer, "buffer", std::stoi, "0");
    REQUIRE_AND_GET_MESSAGE_VALUE(size, "size", std::stoi, "0");

    router->bridge.getRuntime()->services.os.bufferSize(
      message.seq,
      id,
      size,
      buffer,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns a mapping of operating  system constants.
   */
  router->map("os.constants", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.constants(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  /**
   * Returns a mapping of network interfaces.
   */
  router->map("os.networkInterfaces", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.networkInterfaces(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  /**
   * Returns an array of CPUs available to the process.
   */
  router->map("os.cpus", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.cpus(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  router->map("os.rusage", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.rusage(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  router->map("os.uptime", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.uptime(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  router->map("os.uname", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.uname(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  router->map("os.hrtime", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.hrtime(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  router->map("os.availableMemory", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.os.availableMemory(message.seq, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  router->map("os.paths", [](auto message, auto router, auto reply) {
    static auto userConfig = getUserConfig();

    if (userConfig["meta_bundle_identifier"] != router->bridge.userConfig["meta_bundle_identifier"]) {
      const auto json = filesystem::Resource::getWellKnownPaths(router->bridge.userConfig["meta_bundle_identifier"]).json();
      return reply(Result::Data { message, json });
    } else {
      const auto json = filesystem::Resource::getWellKnownPaths().json();
      return reply(Result::Data { message, json });
    }
  });

  router->map("permissions.query", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    return router->bridge.getRuntime()->services.permissions.query(
      message.seq,
      message.get("name"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("permissions.request", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"name"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    return router->bridge.getRuntime()->services.permissions.request(
      message.seq,
      message.get("name"),
      message.dump(),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("application.update.check", [](auto message, auto router, auto reply) {
    if (!updateServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "UPDATE_DISABLED"},
        {"message", "Application update service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"manifestUrl"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    oro::runtime::core::services::Update::ManifestCheckOptions manifestOptions;
    manifestOptions.manifestUrl = message.get("manifestUrl");
    manifestOptions.signatureUrl = message.get("signatureUrl");
    manifestOptions.expectedAppId = message.get("expectedAppId");

    if (message.has("maxManifestBytes")) {
      try {
        manifestOptions.maxManifestBytes = std::stoull(message.get("maxManifestBytes"));
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'maxManifestBytes' given in parameters"}
        }});
      }
    }

    if (message.has("headers")) {
      const auto raw = message.get("headers");
      if (raw.size() > 0) {
        try {
          auto any = JSON::parse(raw);
          if (any.type == JSON::Type::Object) {
            auto& obj = any.template as<JSON::Object>();
            const auto entries = obj.value();
            for (const auto& entry : entries) {
              const auto& name = entry.first;
              const auto& valueAny = entry.second;
              if (valueAny.type == JSON::Type::String) {
                manifestOptions.headers.emplace_back(name, valueAny.template as<JSON::String>().data);
              } else {
                manifestOptions.headers.emplace_back(name, valueAny.str());
              }
            }
          } else {
            return reply(Result::Err { message, JSON::Object::Entries {
              {"message", "Invalid 'headers' given in parameters; expected JSON object"}
            }});
          }
        } catch (const JSON::Error&) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'headers' JSON given in parameters"}
          }});
        } catch (const std::exception&) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'headers' JSON given in parameters"}
          }});
        }
      }
    }

    // Public keys: either a single `publicKey` or a JSON-encoded
    // `publicKeys` array. The service requires at least one key.
    const auto singleKey = message.get("publicKey");
    if (singleKey.size() > 0) {
      manifestOptions.publicKeys.push_back(singleKey);
    }

    if (message.has("publicKeys")) {
      const auto raw = message.get("publicKeys");
      if (raw.size() > 0) {
        try {
          auto any = JSON::parse(raw);
          if (any.type == JSON::Type::Array) {
            auto& arr = any.template as<JSON::Array>();
            for (const auto& entry : arr) {
              if (entry.type == JSON::Type::String) {
                manifestOptions.publicKeys.push_back(entry.template as<JSON::String>().data);
              } else {
                manifestOptions.publicKeys.push_back(entry.str());
              }
            }
          } else {
            return reply(Result::Err { message, JSON::Object::Entries {
              {"message", "Invalid 'publicKeys' given in parameters; expected JSON array"}
            }});
          }
        } catch (const JSON::Error&) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'publicKeys' JSON given in parameters"}
          }});
        } catch (const std::exception&) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'publicKeys' JSON given in parameters"}
          }});
        }
      }
    }

    oro::runtime::core::services::Update::SelectionOptions selectionOptions;
    {
      static auto userConfig = getUserConfig();

      const auto channelOverride = message.get("channel");
      const auto channelConfig = userConfig.contains("update_channel")
        ? userConfig.at("update_channel")
        : String("");
      selectionOptions.channel = channelOverride.size() > 0
        ? channelOverride
        : (channelConfig.size() > 0 ? channelConfig : String("stable"));

      selectionOptions.currentVersion = message.get("currentVersion");
      selectionOptions.platform = message.get("platform");
      selectionOptions.arch = message.get("arch");
      selectionOptions.runtimeVersion = message.get("runtimeVersion");
    }

    oro::runtime::core::services::Update::DownloadOptions downloadOptions;
    {
      static auto userConfig = getUserConfig();

      String maxArtifactBytesValue;
      if (message.has("maxArtifactBytes")) {
        maxArtifactBytesValue = message.get("maxArtifactBytes");
      } else if (userConfig.contains("update_max_artifact_bytes")) {
        maxArtifactBytesValue = userConfig.at("update_max_artifact_bytes");
      }

      if (maxArtifactBytesValue.size() > 0) {
        try {
          downloadOptions.maxArtifactBytes = std::stoull(maxArtifactBytesValue);
        } catch (...) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'maxArtifactBytes' given in parameters"}
          }});
        }
      }

      String maxManifestBytesValue;
      if (message.has("maxManifestBytes")) {
        maxManifestBytesValue = message.get("maxManifestBytes");
      } else if (userConfig.contains("update_max_manifest_bytes")) {
        maxManifestBytesValue = userConfig.at("update_max_manifest_bytes");
      }

      if (maxManifestBytesValue.size() > 0) {
        try {
          manifestOptions.maxManifestBytes = std::stoull(maxManifestBytesValue);
        } catch (...) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'maxManifestBytes' given in parameters"}
          }});
        }
      }
    }

    router->bridge.getRuntime()->services.update.check(
      message.seq,
      manifestOptions,
      selectionOptions,
      downloadOptions,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  router->map("application.update.download", [](auto message, auto router, auto reply) {
    if (!updateServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "UPDATE_DISABLED"},
        {"message", "Application update service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"artifactUrl", "hash", "hashAlgorithm"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t expectedLength = 0;
    if (message.has("length")) {
      const auto raw = message.get("length");
      if (raw.size() > 0) {
        try {
          expectedLength = std::stoull(raw);
        } catch (...) {
          return reply(Result::Err { message, JSON::Object::Entries {
            {"message", "Invalid 'length' given in parameters"}
          }});
        }
      }
    }

    oro::runtime::core::services::Update::DownloadOptions options;
    if (message.has("maxArtifactBytes")) {
      try {
        options.maxArtifactBytes =
          std::stoull(message.get("maxArtifactBytes"));
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'maxArtifactBytes' given in parameters"}
        }});
      }
    }

    router->bridge.getRuntime()->services.update.download(
      message.seq,
      message.get("artifactUrl"),
      message.get("hashAlgorithm"),
      message.get("hash"),
      expectedLength,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Simply returns `pong`.
   */
  router->map("ping", [](auto message, ipc::Router* router, auto reply) {
    auto result = Result { message.seq, message };
    result.data = "pong";
    reply(result);
  });

  /**
   * Start the CDP (Chrome DevTools Protocol) server.
   * @param hostname Optional; defaults to 127.0.0.1
   * @param port Optional; 0 means random port
   */
  router->map("cdp.listen", [](auto message, auto router, auto reply) {
    oro::runtime::core::services::CDP::ListenOptions options;
    if (message.has("hostname")) {
      options.hostname = message.get("hostname");
    }

    if (message.has("port")) {
      try {
        options.port = std::stoi(message.get("port"));
      } catch (...) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"message", "Invalid 'port' given in parameters"}
        }});
      }
    }

    router->bridge.getRuntime()->services.cdp.listen(
      message.seq,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Stop the CDP (Chrome DevTools Protocol) server.
   */
  router->map("cdp.close", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.cdp.close(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Get CDP server status.
   */
  router->map("cdp.status", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.cdp.getStatus(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Internal: forward Runtime.addBinding binding calls to the CDP server.
   * @param name The binding name.
   * @param payload The binding payload.
   * @param executionContextId Optional; execution context id.
   */
  router->map("cdp.bindingCalled", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"name", "payload"});
    const auto app = App::sharedApplication();
    const auto window = app ? app->runtime.windowManager.getWindowForBridge(&router->bridge) : nullptr;

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    if (!window) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Window not found"}
      }});
    }

    int executionContextId = 0;
    if (message.has("executionContextId")) {
      try {
        executionContextId = std::stoi(message.get("executionContextId"));
      } catch (...) {
        executionContextId = 0;
      }
    }

    router->bridge.getRuntime()->services.cdp.onBindingCalled(
      window->index,
      message.get("name"),
      message.get("payload"),
      executionContextId
    );

    reply(Result::Data { message, JSON::Object::Entries {} });
  });

  /**
   * Internal: forward CDP events (e.g. Network.*, Runtime.*, Page.*) from the
   * window to the CDP server.
   * @param event JSON stringified event: { method, params }
   */
  router->map("cdp.networkEvent", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"event"});
    const auto app = App::sharedApplication();
    const auto window = app ? app->runtime.windowManager.getWindowForBridge(&router->bridge) : nullptr;

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    if (!window) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Window not found"}
      }});
    }

    const auto eventText = message.get("event");
    if (eventText.size() > (2ULL * 1024ULL * 1024ULL)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Event too large"}
      }});
    }

    JSON::Any parsed = JSON::Object::Entries {};
    try {
      parsed = JSON::parse(eventText);
    } catch (...) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Invalid 'event' JSON"}
      }});
    }

    router->bridge.getRuntime()->services.cdp.onNetworkEvent(window->index, parsed);
    reply(Result::Data { message, JSON::Object::Entries {} });
  });

  /**
   * Handles platform events.
   * @param value The event name [domcontentloaded]
   * @param data Optional data associated with the platform event.
   */
  router->map("platform.event", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"value"});
    const auto app = App::sharedApplication();
    const auto window = app->runtime.windowManager.getWindowForBridge(&router->bridge);

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    if (message.value == "readystatechange") {
      const auto err = validateMessageParameters(message, {"state"});

      if (err.type != JSON::Type::Null) {
        return reply(Result { message.seq, message, err });
      }

      const auto state = message.get("state");

      if (state == "loading") {
        window->readyState = Window::ReadyState::Loading;
      } else if (state == "interactive") {
        window->readyState = Window::ReadyState::Interactive;
      } else if (state == "complete") {
        window->readyState = Window::ReadyState::Complete;
      }

      window->onReadyStateChange(window->readyState);
      if (window) {
        router->bridge.getRuntime()->services.cdp.onWindowReadyStateChanged(window->index, state);
      }
    } else if (message.value == "domcontentloaded") {
      if (window) {
        router->bridge.getRuntime()->services.cdp.onWindowDOMContentLoaded(window->index);
      }
    }

    const auto frameType = message.get("runtime-frame-type");
    const auto frameSource = message.get("runtime-frame-source");
    auto userConfig = router->bridge.userConfig;

    if (frameType == "top-level" && frameSource != "serviceworker") {
      if (message.value == "beforeruntimeinit") {
        if (window) {
          router->bridge.getRuntime()->services.cdp.onWindowBeforeRuntimeInit(window->index);
        }
      }

      if (message.value == "load") {
        const auto href = message.get("location.href");
        if (href.size() > 0) {
          router->bridge.navigator.location.set(href);
          router->bridge.navigator.location.workers.clear();
          auto tmp = href;
          tmp = replace(tmp, "oro://", "");
          tmp = replace(tmp, "https://", "");
          tmp = replace(tmp, userConfig["meta_bundle_identifier"], "");
          const auto parsed = URL::Components::parse(tmp);
          router->bridge.navigator.location.pathname = parsed.pathname;
          router->bridge.navigator.location.query = parsed.query;

          if (window) {
            router->bridge.getRuntime()->services.cdp.onWindowNavigated(window->index, href);
          }
        }
      }

      if (router->bridge.navigator.serviceWorkerServer) {
        if (router->bridge.userConfig["webview_service_worker_mode"] == "hybrid") {
          if (router->bridge.navigator.location.size() > 0 && message.value == "beforeruntimeinit") {
            router->bridge.navigator.serviceWorkerServer->container.reset();
            router->bridge.navigator.serviceWorkerServer->container.isReady = false;
          } else if (message.value == "runtimeinit") {
            router->bridge.navigator.serviceWorkerServer->container.isReady = true;
          }
        }
      }
    }

    if (message.value == "load" && frameType == "worker") {
      const auto workerLocation = message.get("runtime-worker-location");
      const auto href = message.get("location.href");
      if (href.size() > 0 && workerLocation.size() > 0) {
        router->bridge.navigator.location.workers[href] = workerLocation;
      }
    }

    router->bridge.getRuntime()->services.platform.event(
      message.seq,
      message.value,
      message.get("data"),
      frameType,
      frameSource,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Reveal a file in the native operating system file system explorer.
   * @param value
   */
  router->map("platform.revealFile", [](auto message, auto router, auto reply) mutable {
    auto err = validateMessageParameters(message, {"value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    router->bridge.getRuntime()->services.platform.revealFile(
      message.seq,
      message.get("value"),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Requests a URL to be opened externally.
   * @param value
   */
  router->map("platform.openExternal", [](auto message, auto router, auto reply) mutable {
    const auto applicationProtocol = router->bridge.userConfig["meta_application_protocol"];
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    if (applicationProtocol.size() > 0 && message.value.starts_with(applicationProtocol + ":")) {
      JSON::Object json = JSON::Object::Entries {
        { "url", message.value }
      };

      const auto window = app->runtime.windowManager.getWindowForBridge(&router->bridge);

      if (window) {
        window->handleApplicationURL(message.value);
      }

      reply(Result {
        message.seq,
        message,
        JSON::Object::Entries {
          {"data", json}
        }
      });
      return;
    }

    router->bridge.getRuntime()->services.platform.openExternal(
      message.seq,
      message.value,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Return Oro Runtime primordials.
   */
  router->map("platform.primordials", [](
    const ipc::Message& message,
    ipc::Router* router,
    ipc::Router::ReplyCallback reply
  ) {
    std::regex platform_pattern("^mac$", std::regex_constants::icase);
    auto platformRes = std::regex_replace(platform.os, platform_pattern, "darwin");
    auto arch = std::regex_replace(platform.arch, std::regex("x86_64"), "x64");
    arch = std::regex_replace(arch, std::regex("x86"), "ia32");
    arch = std::regex_replace(arch, std::regex("arm(?!64).*"), "arm");
    String sqliteVersion;
    if (const char* value = sqlite3_libversion()) {
      sqliteVersion = value;
    }

#if ORO_RUNTIME_HAS_LIBUSB && !ORO_RUNTIME_PLATFORM_IOS
    String libusbVersion;
    if (const libusb_version* info = libusb_get_version()) {
      libusbVersion = std::to_string(info->major);
      libusbVersion += ".";
      libusbVersion += std::to_string(info->minor);
      libusbVersion += ".";
      libusbVersion += std::to_string(info->micro);
      if (info->nano != 0) {
        libusbVersion += ".";
        libusbVersion += std::to_string(info->nano);
      }
      if (info->rc != nullptr && info->rc[0] != '\0') {
        libusbVersion += "-";
        libusbVersion += info->rc;
      }
    }
#endif

#if ORO_RUNTIME_HAS_SODIUM
    String libsodiumVersion;
    if (const char* value = sodium_version_string()) {
      libsodiumVersion = value;
    }
#endif

#if ORO_RUNTIME_HAS_MBEDTLS && defined(MBEDTLS_VERSION_C)
    String mbedtlsVersion;
    {
      char buffer[64] = {0};
      mbedtls_version_get_string_full(buffer);
      mbedtlsVersion = buffer;
    }
#endif

    const auto irohVersion = iroh::Library::shared().version();

#if ORO_RUNTIME_HAS_WHISPER
    String whisperVersion;
    if (const char* value = whisper_version()) {
      whisperVersion = value;
    }
#endif

#if ORO_RUNTIME_HAS_CPPHTTPLIB
    String cppHttplibVersion = CPPHTTPLIB_VERSION;
#endif

#if ORO_RUNTIME_HAS_NLOHMANN_JSON
    String nlohmannJsonVersion = std::to_string(NLOHMANN_JSON_VERSION_MAJOR);
    nlohmannJsonVersion += ".";
    nlohmannJsonVersion += std::to_string(NLOHMANN_JSON_VERSION_MINOR);
    nlohmannJsonVersion += ".";
    nlohmannJsonVersion += std::to_string(NLOHMANN_JSON_VERSION_PATCH);
#endif

    JSON::Object::Entries data;
    data["arch"] = arch;
    data["cwd"] = getcwd();
    data["platform"] = platformRes;

    JSON::Object::Entries runtimeVersion;
    runtimeVersion["full"] = version::VERSION_FULL_STRING;
    runtimeVersion["short"] = version::VERSION_STRING;
    runtimeVersion["hash"] = version::VERSION_HASH_STRING;
    data["version"] = runtimeVersion;

    JSON::Object::Entries uvInfo;
    uvInfo["version"] = uv_version_string();
    data["uv"] = uvInfo;

    JSON::Object::Entries llamaInfo;
    llamaInfo["version"] = String("0.0.") + std::to_string(LLAMA_BUILD_NUMBER);
    data["llama"] = llamaInfo;

    if (!sqliteVersion.empty()) {
      JSON::Object::Entries sqliteInfo;
      sqliteInfo["version"] = sqliteVersion;
      data["sqlite"] = sqliteInfo;
    }

#if ORO_RUNTIME_HAS_CPPHTTPLIB
    if (!cppHttplibVersion.empty()) {
      JSON::Object::Entries cppHttplibInfo;
      cppHttplibInfo["version"] = cppHttplibVersion;
      data["cpp_httplib"] = cppHttplibInfo;
    }
#endif

#if ORO_RUNTIME_HAS_NLOHMANN_JSON
    if (!nlohmannJsonVersion.empty()) {
      JSON::Object::Entries nlohmannInfo;
      nlohmannInfo["version"] = nlohmannJsonVersion;
      data["nlohmann_json"] = nlohmannInfo;
    }
#endif

    if (!irohVersion.empty()) {
      JSON::Object::Entries irohInfo;
      irohInfo["version"] = irohVersion;
      data["iroh"] = irohInfo;
    }

#if ORO_RUNTIME_HAS_WHISPER
    if (!whisperVersion.empty()) {
      JSON::Object::Entries whisperInfo;
      whisperInfo["version"] = whisperVersion;
      data["whisper"] = whisperInfo;
    }
#endif

#if ORO_RUNTIME_HAS_LIBUSB && !ORO_RUNTIME_PLATFORM_IOS
    if (!libusbVersion.empty()) {
      JSON::Object::Entries libusbInfo;
      libusbInfo["version"] = libusbVersion;
      data["libusb"] = libusbInfo;
    }
#endif

#if ORO_RUNTIME_HAS_SODIUM
    if (!libsodiumVersion.empty()) {
      JSON::Object::Entries libsodiumInfo;
      libsodiumInfo["version"] = libsodiumVersion;
      data["libsodium"] = libsodiumInfo;
    }
#endif

#if ORO_RUNTIME_HAS_MBEDTLS && defined(MBEDTLS_VERSION_C)
    if (!mbedtlsVersion.empty()) {
      JSON::Object::Entries mbedtlsInfo;
      mbedtlsInfo["version"] = mbedtlsVersion;
      data["mbedtls"] = mbedtlsInfo;
    }
#endif

    String hostOperatingSystem;
    #if ORO_RUNTIME_PLATFORM_APPLE
      #if ORO_RUNTIME_PLATFORM_IOS_SIMULATOR
        hostOperatingSystem = "iphonesimulator";
      #elif ORO_RUNTIME_PLATFORM_IOS
        hostOperatingSystem = "iphoneos";
      #else
        hostOperatingSystem = "macosx";
      #endif
    #elif ORO_RUNTIME_PLATFORM_ANDROID
      hostOperatingSystem = router->bridge.context.android.isEmulator ? "android-emulator" : "android";
    #elif ORO_RUNTIME_PLATFORM_WINDOWS
      hostOperatingSystem = "win32";
    #elif ORO_RUNTIME_PLATFORM_LINUX
      hostOperatingSystem = "linux";
    #elif ORO_RUNTIME_PLATFORM_UNIX
      hostOperatingSystem = "unix";
    #else
      hostOperatingSystem = "unknown";
    #endif
    data["host-operating-system"] = hostOperatingSystem;

    JSON::Object::Entries json;
    json["source"] = "platform.primordials";
    json["data"] = data;
    reply(Result { message.seq, message, json });
  });

  /**
   * Returns pending queuedResponse data typically returned in the response of an
   * `ipc://queuedResponse` IPC call intercepted by an XHR request.
   * @param id The id of the queuedResponse data.
   */
  router->map("queuedResponse", false, [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    if (!router->bridge.getRuntime()->queuedResponses.contains(id)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"id", std::to_string(id)},
        {"type", "NotFoundError"},
        {"message", "A 'QueuedResponse' was not found for the given 'id' in parameters"}
      }});
    }

    auto result = Result { message.seq, message };
    result.queuedResponse = router->bridge.getRuntime()->queuedResponses[id];
    reply(result);
    router->bridge.getRuntime()->queuedResponses.erase(id);
  });

  /**
   * Registers a custom protocol handler scheme. Custom protocols MUST be handled in service workers.
   * @param scheme
   * @param data
   */
  router->map("protocol.register", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"scheme"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto scheme = message.get("scheme");
    const auto data = message.get("data");

    if (data.size() > 0 && router->bridge.navigator.serviceWorkerServer->container.protocols.hasHandler(scheme)) {
      router->bridge.navigator.serviceWorkerServer->container.protocols.setHandlerData(scheme, { data });
    } else {
      router->bridge.navigator.serviceWorkerServer->container.protocols.registerHandler(scheme, { data });
    }

    reply(Result { message.seq, message });
  });

  /**
   * Unregister a custom protocol handler scheme.
   * @param scheme
   */
  router->map("protocol.unregister", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"scheme"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto scheme = message.get("scheme");

    if (!router->bridge.navigator.serviceWorkerServer->container.protocols.hasHandler(scheme)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Protocol handler scheme is not registered."}
      }});
    }

    router->bridge.navigator.serviceWorkerServer->container.protocols.unregisterHandler(scheme);

    reply(Result { message.seq, message });
  });

  /**
   * Gets protocol handler data
   * @param scheme
   */
  router->map("protocol.getData", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"scheme"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto scheme = message.get("scheme");

    if (!router->bridge.navigator.serviceWorkerServer->container.protocols.hasHandler(scheme)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Protocol handler scheme is not registered."}
      }});
    }

    const auto data = router->bridge.navigator.serviceWorkerServer->container.protocols.getHandlerData(scheme);

    reply(Result { message.seq, message, JSON::Raw(data.json) });
  });

  /**
   * Sets protocol handler data
   * @param scheme
   * @param data
   */
  router->map("protocol.setData", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"scheme", "data"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto scheme = message.get("scheme");
    const auto data = message.get("data");

    if (!router->bridge.navigator.serviceWorkerServer->container.protocols.hasHandler(scheme)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Protocol handler scheme is not registered."}
      }});
    }

    router->bridge.navigator.serviceWorkerServer->container.protocols.setHandlerData(scheme, { data });

    reply(Result { message.seq, message });
  });

  /**
   * Gets service worker registration info by scheme
   * @param scheme
   */
  router->map("protocol.getServiceWorkerRegistration", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"scheme"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto scheme = message.get("scheme");

    for (const auto& entry : router->bridge.navigator.serviceWorkerServer->container.registrations) {
      const auto& registration = entry.second;
      if (registration.options.scheme == scheme) {
        auto json = JSON::Object::Entries {
          {"source", message.name},
          {"data", JSON::Object::Entries {
            {"id", registration.id},
            {"scope", registration.options.scope},
            {"origin", registration.origin.name()},
            {"scheme", scheme},
            {"scriptURL", registration.options.scriptURL}
          }}
        };
        return reply(Result { message.seq, message, json });
      }
    }

    reply(Result { message.seq, message, JSON::Null() });
  });

  /**
   * Sets an evironment variable
   * @param key
   * @param value
   */
  router->map("process.env.set", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"key", "value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto key = message.get("key");
    const auto value = message.get("value");
    env::set(key, value);
    reply(Result {
      message.seq,
      message,
      JSON::Object::Entries {
        {"key", key},
        {"value", value}
      }
    });
  });

  /**
   * Gets an evironment variable
   * @param key
   */
  router->map("process.env.get", [](auto message, auto router, auto reply) {
    const auto err = validateMessageParameters(message, {"key"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto key = message.get("key");
    const auto value = env::get(key);
    reply(Result {
      message.seq,
      message,
      JSON::Object::Entries {
        {"key", key},
        {"value", value}
      }
    });
  });

  /**
   * Prints incoming message value to stdout.
   * @param value
   */
  router->map("stdout", [=](auto message, auto router, auto reply) {
    if (message.value.size() > 0) {
      #if ORO_RUNTIME_PLATFORM_APPLE
        const auto seq = ++router->bridge.getRuntime()->counters.logSeq;
        auto msg = String(std::to_string(seq) + "::::" + message.value.c_str());
        os_log_with_type(ORO_RUNTIME_OS_LOG_BUNDLE, OS_LOG_TYPE_INFO, "%{public}s", msg.c_str());

        const auto logSocketEnv = env::get("ORO_LOG_SOCKET");
        if (logSocketEnv.size() > 0) {
          oro::runtime::core::services::UDP::SendOptions options;
          options.size = 2;
          options.address = "0.0.0.0";
          options.port = std::stoi(logSocketEnv);
          options.ephemeral = true;
          options.bytes.reset(new unsigned char[3]{ '+', 'N', '\0' });
          router->bridge.getRuntime()->services.udp.send("-1", 0, options, [](auto seq, auto json, auto queuedResponse) {});
        }
      #endif
      io::write(message.value, false);
    } else if (message.buffer.size() > 0) {
      io::write(message.buffer.str(), false);
    }

    reply(Result { message.seq, message });
  });

  /**
   * Prints incoming message value to stderr.
   * @param value
   */
  router->map("stderr", [=](auto message, auto router, auto reply) {
    if (message.get("debug") == "true") {
      if (message.value.size() > 0) {
        debug("%s", message.value.c_str());
      }
    } else if (message.value.size() > 0) {
      #if ORO_RUNTIME_PLATFORM_APPLE
        const auto seq = ++router->bridge.getRuntime()->counters.logSeq;
        auto msg = String(std::to_string(seq) + "::::" + message.value.c_str());
        os_log_with_type(ORO_RUNTIME_OS_LOG_BUNDLE, OS_LOG_TYPE_ERROR, "%{public}s", msg.c_str());

        const auto logSocketEnv = env::get("ORO_LOG_SOCKET");
        if (logSocketEnv.size() > 0) {
          oro::runtime::core::services::UDP::SendOptions options;
          options.size = 2;
          options.address = "0.0.0.0";
          options.port = std::stoi(logSocketEnv);
          options.ephemeral = true;
          options.bytes.reset(new unsigned char[3]{ '+', 'N', '\0' });
          router->bridge.getRuntime()->services.udp.send("-1", 0, options, [](auto seq, auto json, auto queuedResponse) {});
        }
      #endif
      io::write(message.value, true);
    } else if (message.buffer.size() > 0) {
      io::write(message.str(), true);
    }

    reply(Result { message.seq, message });
  });

  /**
   * Registers a service worker script for a given scope.
   * @param scriptURL
   * @param scope
   */
  router->map("serviceWorker.register", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"scriptURL", "scope"});
    auto app = App::sharedApplication();

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    auto options = serviceworker::Registration::Options {
      .type = serviceworker::Registration::Options::Type::Module,
      .scriptURL = message.get("scriptURL"),
      .scope = message.get("scope"),
      .scheme = message.get("scheme", "oro"),
      .serializedWorkerArgs = encodeURIComponent(message.get("__runtime_worker_args", message.get("serializedWorkerArgs")))
    };

    if (message.get("priority") == "high") {
      options.priority = serviceworker::Registration::Priority::High;
    } else if (message.get("priority") == "low") {
      options.priority = serviceworker::Registration::Priority::Low;
    }

    const auto url = URL(options.scriptURL);
    const auto origin = webview::Origin(url.str());

    auto serviceWorkerServer = app->runtime.serviceWorkerManager.get(origin.name());
    if (!serviceWorkerServer) {
      if (message.has("__runtime_user_config")) {
        auto userConfig = INI::parse(message.get("__runtime_user_config"));
        serviceWorkerServer = static_cast<Bridge&>(router->bridge).getRuntime()->serviceWorkerManager.init(
          origin.name(),
          serviceworker::Server::Options {
            origin.name(),
            userConfig
          }
        );
      } else {
        serviceWorkerServer = router->bridge.navigator.serviceWorkerServer;
      }
    }

    const auto registration = serviceWorkerServer->container.registerServiceWorker(options);
    const auto json = JSON::Object {
      JSON::Object::Entries {
        {"registration", registration.json()}
      }
    };

    reply(Result::Data { message, json });
  });

  /**
   * Resets the service worker container state.
   */
  router->map("serviceWorker.reset", [](auto message, auto router, auto reply) {
    router->bridge.navigator.serviceWorkerServer->container.reset();
    reply(Result::Data { message, JSON::Object {}});
  });

  /**
   * Unregisters a service worker for given scoep.
   * @param scope
   */
  router->map("serviceWorker.unregister", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    router->bridge.navigator.serviceWorkerServer->container.unregisterServiceWorker(id);

    return reply(Result::Data { message, JSON::Object {} });
  });

  /**
   * Gets registration information for a service worker scope.
   * @param scope
   */
  router->map("serviceWorker.getRegistration", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"scope"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    const auto scope = message.get("scope");
    const auto origin = webview::Origin(router->bridge.navigator.location.str());
    for (const auto& entry : router->bridge.navigator.serviceWorkerServer->container.registrations) {
      const auto& registration = entry.second;
      if (scheme::isRuntimeScheme(registration.options.scheme) && registration.origin.name() == origin.name() && scope.starts_with(registration.options.scope)) {
        auto json = JSON::Object {
          JSON::Object::Entries {
            {"registration", registration.json()},
            {"client", JSON::Object::Entries {
              {"id", std::to_string(router->bridge.client.id)}
            }}
          }
        };

        return reply(Result::Data { message, json });
      }
    }

    return reply(Result::Data { message, JSON::Object {} });
  });

  /**
   * Gets all service worker scope registrations.
   */
  router->map("serviceWorker.getRegistrations", [](auto message, auto router, auto reply) {
    const auto origin = webview::Origin(message.get("origin"));
    auto serviceWorkerServer = router->bridge.getRuntime()->serviceWorkerManager.get(origin.name());

    if (!serviceWorkerServer) {
      serviceWorkerServer = router->bridge.navigator.serviceWorkerServer;
    }

    auto json = JSON::Array::Entries {};
    for (const auto& entry : serviceWorkerServer->container.registrations) {
      const auto& registration = entry.second;
      json.push_back(registration.json());
    }
    return reply(Result::Data { message, json });
  });

  /**
   * @param method
   * @param scheme
   * @param hostname
   * @param pathname
   * @param query
   */
  router->map("serviceWorker.fetch", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto fetch = serviceworker::Request();
    fetch.method = message.get("method", "GET");
    const auto fetchScheme = message.get("scheme", "oro");
    fetch.scheme = fetchScheme;
    fetch.url.scheme = fetchScheme;
    fetch.url.hostname = message.get("hostname");
    fetch.url.pathname = message.get("pathname", "/");
    fetch.url.search = "?" + message.get("query", "");
    fetch.url.searchParams.set(message.get("query", ""));
    fetch.headers = message.get("headers", "");
    fetch.body = message.buffer;
    fetch.client = router->bridge.client;

    if (scheme::isRuntimeScheme(fetch.scheme) && fetch.url.hostname.size() == 0) {
      fetch.url.hostname = router->bridge.userConfig["meta_bundle_identifier"];
    }

    if (fetch.method == "OPTIONS") {
      const auto response = serviceworker::Response(204);
      return reply(Result {
        message.seq,
        message,
        JSON::Object {},
        QueuedResponse {
          rand64(),
          0,
          response.body.shared(),
          response.body.size(),
          response.headers.str()
        }
      });
    }

    if (fetch.scheme == "npm") {
      static auto userConfig = oro::runtime::config::getUserConfig();
      const auto bundleIdentifier = userConfig["meta_bundle_identifier"];
      if (fetch.url.hostname.size() > 0) {
        fetch.url.pathname = "/" + fetch.url.hostname;
      }

      fetch.url.hostname = bundleIdentifier;
    }

    if (!fetch.headers.has("origin")) {
      fetch.headers.set("origin", router->bridge.navigator.location.origin);
    }

    const auto scope = router->bridge.navigator.serviceWorkerServer->container.protocols.getServiceWorkerScope(fetch.scheme);

    if (scope.size() > 0) {
      fetch.url.pathname = scope + fetch.url.pathname;
    }

    const auto options = serviceworker::Fetch::Options {
      router->bridge.client
    };

    auto origin = webview::Origin(fetch.url.str());
    origin.scheme = "oro";
    auto serviceWorkerServer = app->runtime.serviceWorkerManager.get(origin.name());
    if (!serviceWorkerServer) {
      serviceWorkerServer = router->bridge.navigator.serviceWorkerServer;
    }

    const auto fetched = serviceWorkerServer->fetch(fetch, options, [=] (auto res) mutable {
      if (res.statusCode == 0) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {
            {"message", "ServiceWorker request failed"}
          }
        });
      } else {
        reply(Result {
          message.seq,
          message,
          JSON::Object {},
          QueuedResponse {
            rand64(),
            0,
            res.body.buffer.shared(),
            res.body.buffer.size(),
            res.headers.str()
          }
        });
      }
    });

    if (!fetched) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Not found"},
          {"type", "NotFoundError"}
        }
      });
    }
  });

  /**
   * Informs container that a service worker will skip waiting.
   * @param id
   */
  router->map("serviceWorker.skipWaiting", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.navigator.serviceWorkerServer->container.skipWaiting(id);

    reply(Result::Data { message, JSON::Object {}});
  });

  /**
   * Updates service worker controller state.
   * @param id
   * @param state
   */
  router->map("serviceWorker.updateState", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "state"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    const auto workerURL = message.get("workerURL");
    const auto scriptURL = message.get("scriptURL");

    if (workerURL.size() > 0 && scriptURL.size() > 0) {
      router->bridge.navigator.location.workers[workerURL] = scriptURL;
    }

    router->bridge.navigator.serviceWorkerServer->container.updateState(id, message.get("state"));
    reply(Result::Data { message, JSON::Object {}});
  });

  /**
   * Sets storage for a service worker.
   * @param id
   * @param key
   * @param value
   */
  router->map("serviceWorker.storage.set", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "key", "value"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    for (auto& entry : router->bridge.navigator.serviceWorkerServer->container.registrations) {
      if (entry.second.id == id) {
        auto& registration = entry.second;
        registration.storage.set(message.get("key"), message.get("value"));
        return reply(Result::Data { message, JSON::Object {}});
      }
    }

    return reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"message", "Not found"},
        {"type", "NotFoundError"}
      }
    });
  });

  /**
   * Gets a storage value for a service worker.
   * @param id
   * @param key
   */
  router->map("serviceWorker.storage.get", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "key"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    for (auto& entry : router->bridge.navigator.serviceWorkerServer->container.registrations) {
      if (entry.second.id == id) {
        auto& registration = entry.second;
        return reply(Result::Data {
          message,
          JSON::Object::Entries {
            {"value", registration.storage.get(message.get("key"))}
          }
        });
      }
    }

    return reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"message", "Not found"},
        {"type", "NotFoundError"}
      }
    });
  });

  /**
   * Remoes a storage value for a service worker.
   * @param id
   * @param key
   */
  router->map("serviceWorker.storage.remove", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "key"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    for (auto& entry : router->bridge.navigator.serviceWorkerServer->container.registrations) {
      if (entry.second.id == id) {
        auto& registration = entry.second;
        registration.storage.remove(message.get("key"));
        return reply(Result::Data {message, JSON::Object {}});
      }
    }

    return reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"message", "Not found"},
        {"type", "NotFoundError"}
      }
    });
  });

  /**
   * Clears all storage values for a service worker.
   * @param id
   */
  router->map("serviceWorker.storage.clear", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    for (auto& entry : router->bridge.navigator.serviceWorkerServer->container.registrations) {
      if (entry.second.id == id) {
        auto& registration = entry.second;
        registration.storage.clear();
        return reply(Result::Data { message, JSON::Object {} });
      }
    }

    return reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"message", "Not found"},
        {"type", "NotFoundError"}
      }
    });
  });

  /**
   * Gets all storage values for a service worker.
   * @param id
   */
  router->map("serviceWorker.storage", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    for (auto& entry : router->bridge.navigator.serviceWorkerServer->container.registrations) {
      if (entry.second.id == id) {
        auto& registration = entry.second;
        return reply(Result::Data { message, registration.storage.json() });
      }
    }

    return reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"message", "Not found"},
        {"type", "NotFoundError"}
      }
    });
  });

  router->map("timers.setTimeout", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"timeout"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint32_t timeout;
    REQUIRE_AND_GET_MESSAGE_VALUE(timeout, "timeout", std::stoul);
    const auto wait = message.get("wait") == "true";
    const oro::runtime::core::services::Timers::ID id = router->bridge.getRuntime()->services.timers.setTimeout(timeout, [=]() {
      if (wait) {
        reply(Result::Data { message, JSON::Object::Entries {{"id", std::to_string(id) }}});
      }
    });

    if (!wait) {
      reply(Result::Data { message, JSON::Object::Entries {{"id", std::to_string(id) }}});
    }
  });

  router->map("timers.clearTimeout", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    router->bridge.getRuntime()->services.timers.clearTimeout(id);

    reply(Result::Data { message, JSON::Object::Entries {{"id", std::to_string(id) }}});
  });

  router->map("timers.nanosleep", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"seconds", "nanoseconds"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    (void)router;

    int64_t seconds = 0;
    int64_t nanoseconds = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(seconds, "seconds", std::stoll);
    REQUIRE_AND_GET_MESSAGE_VALUE(nanoseconds, "nanoseconds", std::stoll);

    const auto duration = durationFromTimespec(seconds, nanoseconds);

    if (!duration.has_value()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Invalid timespec"}
        }
      });
    }

    if (duration.value() > std::chrono::nanoseconds::zero()) {
      std::this_thread::sleep_for(duration.value());
    }

    reply(Result::Data { message, JSON::Object {} });
  });

  router->map("timers.clock_nanosleep", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"clockId", "flags", "seconds", "nanoseconds"});

    if (err.type != JSON::Type::Null) {
      return reply(Result { message.seq, message, err });
    }

    (void)router;

    int clockId = 0;
    int flags = 0;
    int64_t seconds = 0;
    int64_t nanoseconds = 0;

    REQUIRE_AND_GET_MESSAGE_VALUE(clockId, "clockId", std::stoi);
    REQUIRE_AND_GET_MESSAGE_VALUE(flags, "flags", std::stoi);
    REQUIRE_AND_GET_MESSAGE_VALUE(seconds, "seconds", std::stoll);
    REQUIRE_AND_GET_MESSAGE_VALUE(nanoseconds, "nanoseconds", std::stoll);

    if (clockId != kClockRealtimeId && clockId != kClockMonotonicId) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Unsupported clock"}}
      });
    }

    if ((flags & ~kTimerAbstimeFlag) != 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Unsupported flags"}}
      });
    }

    const auto requestDuration = durationFromTimespec(seconds, nanoseconds);

    if (!requestDuration.has_value()) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {{"message", "Invalid timespec"}}
      });
    }

    std::optional<std::chrono::nanoseconds> currentDuration;

    if ((flags & kTimerAbstimeFlag) != 0) {
      if (message.has("currentSeconds") && message.has("currentNanoseconds")) {
        try {
          const auto currentSecondsValue = std::stoll(message.get("currentSeconds"));
          const auto currentNanosecondsValue = std::stoll(message.get("currentNanoseconds"));
          currentDuration = durationFromTimespec(currentSecondsValue, currentNanosecondsValue);
          if (!currentDuration.has_value()) {
            return reply(Result::Err {
              message,
              JSON::Object::Entries {{"message", "Invalid reference timespec"}}
            });
          }
        } catch (...) {
          return reply(Result::Err {
            message,
            JSON::Object::Entries {{"message", "Invalid reference timespec"}}
          });
        }
      } else if (clockId == kClockMonotonicId) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {{"message", "Missing reference time for monotonic clock"}}
        });
      }
    }

    auto sleepDuration = std::chrono::nanoseconds::zero();

    if ((flags & kTimerAbstimeFlag) != 0) {
      if (currentDuration.has_value()) {
        if (requestDuration.value() > currentDuration.value()) {
          sleepDuration = requestDuration.value() - currentDuration.value();
        }
      } else {
        const auto now = clockId == kClockRealtimeId ? realtimeNow() : monotonicNow();
        if (requestDuration.value() > now) {
          sleepDuration = requestDuration.value() - now;
        }
      }
    } else {
      sleepDuration = requestDuration.value();
    }

    if (sleepDuration > std::chrono::nanoseconds::zero()) {
      std::this_thread::sleep_for(sleepDuration);
    }

    reply(Result::Data {
      message,
      JSON::Object::Entries {
        {"seconds", std::to_string(0)},
        {"nanoseconds", std::to_string(0)}
      }
    });
  });

  /**
   * Creates a TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.create", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.create(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Closes a TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.close", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.close(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Binds a TCP socket to a local address and port.
   * @param id Handle ID for the socket
   * @param port Local port, where zero requests an ephemeral port
   * @param address Local address (default: 0.0.0.0)
   */
  router->map("tcp.bind", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id", "port"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    int port = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(port, "port", std::stoi);

    if (port < 0 || port > 65535) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Invalid 'port' given in parameters"}
      }});
    }

    router->bridge.getRuntime()->services.tcp.bind(
      message.seq,
      id,
      message.get("address", "0.0.0.0"),
      port,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Starts listening for connections on a bound TCP socket.
   * @param id Handle ID for the socket
   * @param backlog Maximum pending connection backlog
   */
  router->map("tcp.listen", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id", "backlog"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    int backlog = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(backlog, "backlog", std::stoi);

    if (backlog < 0) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Invalid 'backlog' given in parameters"}
      }});
    }

    router->bridge.getRuntime()->services.tcp.listen(
      message.seq,
      id,
      backlog,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Accepts a pending connection from a TCP server socket.
   * @param serverId Handle ID for the server socket
   * @param clientId Handle ID to assign to the accepted socket
   */
  router->map("tcp.accept", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"serverId", "clientId"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t serverId = 0;
    uint64_t clientId = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(serverId, "serverId", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(clientId, "clientId", std::stoull);

    router->bridge.getRuntime()->services.tcp.accept(
      message.seq,
      serverId,
      clientId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Connects a TCP socket to a remote address and port.
   * @param id Handle ID for the socket
   * @param port Remote port
   * @param address Remote address (default: 127.0.0.1)
   */
  router->map("tcp.connect", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id", "port"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    int port = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(port, "port", std::stoi);

    if (port <= 0 || port > 65535) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Invalid 'port' given in parameters"}
      }});
    }

    router->bridge.getRuntime()->services.tcp.connect(
      message.seq,
      id,
      message.get("address", "127.0.0.1"),
      port,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Writes buffered bytes to a connected TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.write", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.write(
      message.seq,
      id,
      message.buffer.shared(),
      message.buffer.size(),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Starts delivering bytes read from a TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.readStart", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.readStart(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Stops delivering bytes read from a TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.readStop", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.readStop(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Half-closes the writable side of a TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.shutdown", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.shutdown(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Enables or disables Nagle's algorithm for a TCP socket.
   * @param id Handle ID for the socket
   * @param on Whether TCP_NODELAY should be enabled
   */
  router->map("tcp.setNoDelay", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id", "on"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.setNoDelay(
      message.seq,
      id,
      parseBoolValue(message.get("on")),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Configures TCP keepalive for a socket.
   * @param id Handle ID for the socket
   * @param on Whether keepalive should be enabled
   * @param delay Initial keepalive delay in seconds (default: 0)
   */
  router->map("tcp.setKeepAlive", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id", "on"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    uint64_t delay = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(delay, "delay", std::stoull, "0");

    if (delay > std::numeric_limits<unsigned int>::max()) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"message", "Invalid 'delay' given in parameters"}
      }});
    }

    router->bridge.getRuntime()->services.tcp.setKeepAlive(
      message.seq,
      id,
      parseBoolValue(message.get("on")),
      static_cast<unsigned int>(delay),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns the local address of a TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.getSockName", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.getSockName(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns the remote address of a connected TCP socket.
   * @param id Handle ID for the socket
   */
  router->map("tcp.getPeerName", [](auto message, auto router, auto reply) {
    if (!tcpServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TCP_DISABLED"},
        {"message", "TCP service is disabled"}
      }});
    }

    const auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tcp.getPeerName(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Binds an UDP socket to a specified port, and optionally a host
   * address (default: 0.0.0.0).
   * @param id Handle ID of underlying socket
   * @param port Port to bind the UDP socket to
   * @param address The address to bind the UDP socket to (default: 0.0.0.0)
   * @param reuseAddr Reuse underlying UDP socket address (default: false)
   */
  router->map("udp.bind", [](auto message, auto router, auto reply) {
    oro::runtime::core::services::UDP::BindOptions options;
    auto err = validateMessageParameters(message, {"id", "port"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(options.port, "port", std::stoi);

    options.reuseAddr = message.get("reuseAddr") == "true";
    options.address = message.get("address", "0.0.0.0");

    router->bridge.getRuntime()->services.udp.bind(
      message.seq,
      id,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Close socket handle and underlying UDP socket.
   * @param id Handle ID of underlying socket
   */
  router->map("udp.close", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.udp.close(message.seq, id, RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply));
  });

  /**
   * Connects an UDP socket to a specified port, and optionally a host
   * address (default: 0.0.0.0).
   * @param id Handle ID of underlying socket
   * @param port Port to connect the UDP socket to
   * @param address The address to connect the UDP socket to (default: 0.0.0.0)
   */
  router->map("udp.connect", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "port"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    oro::runtime::core::services::UDP::ConnectOptions options;
    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(options.port, "port", std::stoi);

    options.address = message.get("address", "0.0.0.0");

    router->bridge.getRuntime()->services.udp.connect(
      message.seq,
      id,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Disconnects a connected socket handle and underlying UDP socket.
   * @param id Handle ID of underlying socket
   */
  router->map("udp.disconnect", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.udp.disconnect(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns connected peer socket address information.
   * @param id Handle ID of underlying socket
   */
  router->map("udp.getPeerName", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.udp.getPeerName(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns local socket address information.
   * @param id Handle ID of underlying socket
   */
  router->map("udp.getSockName", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.udp.getSockName(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Returns socket state information.
   * @param id Handle ID of underlying socket
   */
  router->map("udp.getState", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.udp.getState(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Initializes socket handle to start receiving data from the underlying
   * socket and route through the IPC bridge to the WebView.
   * @param id Handle ID of underlying socket
   */
  router->map("udp.readStart", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.udp.readStart(
      message.seq,
      id,
      [id, router, message, reply](auto seq, auto json, auto queuedResponse) {
        auto hasId = router->bridge.getRuntime()->services.conduit.has(id);

        // conduit must have garbage collected the client with that id or the ID is wrong
        if (seq == "-1" && hasId) {
          auto client = router->bridge.getRuntime()->services.conduit.get(id);

          if (client) {
            auto data = json["data"];
            oro::runtime::core::services::Conduit::Message::Options options = {
              { "port", data["port"].str() },
              { "address", data["address"].template as<JSON::String>().data }
            };

            client->send(options, queuedResponse.body, queuedResponse.length);
            return;
          }
        }

        reply(Result { seq, message, json, queuedResponse });
      }
    );
  });

  /**
   * Stops socket handle from receiving data from the underlying
   * socket and routing through the IPC bridge to the WebView.
   * @param id Handle ID of underlying socket
   */
  router->map("udp.readStop", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.udp.readStop(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Broadcasts a datagram on the socket. For connectionless sockets, the
   * destination port and address must be specified. Connected sockets, on the
   * other hand, will use their associated remote endpoint, so the port and
   * address arguments must not be set.
   * @param id Handle ID of underlying socket
   * @param port The port to send data to
   * @param size The size of the bytes to send
   * @param bytes A pointer to the bytes to send
   * @param address The address to send to (default: 0.0.0.0)
   * @param ephemeral Indicates that the socket handle, if created is ephemeral and should eventually be destroyed
   */
  router->map("udp.send", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "port"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    oro::runtime::core::services::UDP::SendOptions options;
    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(options.port, "port", std::stoi);

    options.ephemeral = message.get("ephemeral") == "true";
    options.address = message.get("address", "0.0.0.0");
    options.bytes = message.buffer.shared();
    options.size = message.buffer.size();


    router->bridge.getRuntime()->services.udp.send(
      message.seq,
      id,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Enable or disable SO_BROADCAST on the socket.
   * @param id Handle ID of underlying socket
   * @param on Whether broadcast should be enabled
   */
  router->map("udp.setBroadcast", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "on"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto onValue = message.get("on");
    const bool on = onValue == "true" || onValue == "1";

    router->bridge.getRuntime()->services.udp.setBroadcast(
      message.seq,
      id,
      on,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Set unicast TTL for outgoing packets.
   * @param id Handle ID of underlying socket
   * @param ttl Time-to-live value
   */
  router->map("udp.setTTL", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "ttl"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    int ttl;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(ttl, "ttl", std::stoi);

    router->bridge.getRuntime()->services.udp.setTTL(
      message.seq,
      id,
      ttl,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Set multicast TTL for outgoing multicast packets.
   * @param id Handle ID of underlying socket
   * @param ttl Time-to-live value
   */
  router->map("udp.setMulticastTTL", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "ttl"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    int ttl;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(ttl, "ttl", std::stoi);

    router->bridge.getRuntime()->services.udp.setMulticastTTL(
      message.seq,
      id,
      ttl,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Enable or disable multicast loopback for outgoing packets.
   * @param id Handle ID of underlying socket
   * @param on Whether loopback should be enabled
   */
  router->map("udp.setMulticastLoopback", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "on"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto onValue = message.get("on");
    const bool on = onValue == "true" || onValue == "1";

    router->bridge.getRuntime()->services.udp.setMulticastLoopback(
      message.seq,
      id,
      on,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Set the default network interface for multicast packets.
   * @param id Handle ID of underlying socket
   * @param interface Interface name or address
   */
  router->map("udp.setMulticastInterface", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto iface = message.get("interface");

    router->bridge.getRuntime()->services.udp.setMulticastInterface(
      message.seq,
      id,
      iface,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Configure the socket as IPv6-only.
   * @param id Handle ID of underlying socket
   * @param on Whether IPv6-only should be enabled
   */
  router->map("udp.setIPv6Only", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "on"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto onValue = message.get("on");
    const bool on = onValue == "true" || onValue == "1";

    router->bridge.getRuntime()->services.udp.setIPv6Only(
      message.seq,
      id,
      on,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Join a multicast group.
   * @param id Handle ID of underlying socket
   * @param address Multicast group address
   * @param interface Optional interface name or address
   */
  router->map("udp.addMembership", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "address"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto address = message.get("address");
    const auto iface = message.get("interface");

    router->bridge.getRuntime()->services.udp.addMembership(
      message.seq,
      id,
      address,
      iface,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Leave a multicast group.
   * @param id Handle ID of underlying socket
   * @param address Multicast group address
   * @param interface Optional interface name or address
   */
  router->map("udp.dropMembership", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "address"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto address = message.get("address");
    const auto iface = message.get("interface");

    router->bridge.getRuntime()->services.udp.dropMembership(
      message.seq,
      id,
      address,
      iface,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Join a source-specific multicast group (if supported).
   * @param id Handle ID of underlying socket
   * @param address Multicast group address
   * @param source Source address
   * @param interface Optional interface name or address
   */
  router->map("udp.addSourceSpecificMembership", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "address", "source"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto address = message.get("address");
    const auto source = message.get("source");
    const auto iface = message.get("interface");

    router->bridge.getRuntime()->services.udp.addSourceSpecificMembership(
      message.seq,
      id,
      address,
      source,
      iface,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Leave a source-specific multicast group (if supported).
   * @param id Handle ID of underlying socket
   * @param address Multicast group address
   * @param source Source address
   * @param interface Optional interface name or address
   */
  router->map("udp.dropSourceSpecificMembership", [](auto message, auto router, auto reply) {
    auto err = validateMessageParameters(message, {"id", "address", "source"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    const auto address = message.get("address");
    const auto source = message.get("source");
    const auto iface = message.get("interface");

    router->bridge.getRuntime()->services.udp.dropSourceSpecificMembership(
      message.seq,
      id,
      address,
      source,
      iface,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Query UDP service capabilities.
   */
  router->map("udp.capabilities", [](auto message, auto router, auto reply) {
    router->bridge.getRuntime()->services.udp.capabilities(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Get the current runtime TLS pins configuration.
   */
  router->map("tls.getPins", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    router->bridge.getRuntime()->services.tls.getPins(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Get the active runtime TLS provider name.
   */
  router->map("tls.getProvider", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    router->bridge.getRuntime()->services.tls.getProvider(
      message.seq,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Set TLS certificate pins for runtime TLS client connections.
   * @param value - Newline-separated list of '<host> sha256/<base64>' entries.
   * @param mode  - Optional; 'append' (default) or 'replace'.
   */
  router->map("tls.setPins", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    const bool hasValue = message.has("value");
    const bool hasBody = message.buffer.size() > 0;
    if (!hasValue && !hasBody) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Expecting 'value' in parameters or request body"}
        }
      });
    }

    auto runtime = router->bridge.getRuntime();
    if (runtime == nullptr) {
      return reply(Result::Err { message, "Runtime is invalid state" });
    }

    const auto value = hasValue ? message.value : message.buffer.str();
    String validateMessage;
    if (!validateTlsPinsConfigValue(value, validateMessage)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_PINS_INVALID"},
        {"message", validateMessage}
      }});
    }
    const auto mode = toLowerCase(trim(message.get("mode", "append")));
    const bool replacePins = (mode == "replace");
    String nextValue;

    if (replacePins) {
      nextValue = value;
    } else {
      const String combined = runtime->userConfig.contains("tls_pins")
        ? runtime->userConfig["tls_pins"]
        : (runtime->userConfig.contains("tls.pins")
            ? runtime->userConfig["tls.pins"]
            : "");
      nextValue = mergeTlsPinsConfigValue(combined, value);
    }

    runtime->userConfig["tls_pins"] = nextValue;
    runtime->userConfig["tls.pins"] = nextValue;

    runtime->services.tls.setPins(
      message.seq,
      nextValue,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Establishes a TLS client connection over an underlying TCP socket.
   * @param id Handle id for the TLS socket
   * @param host Remote host name
   * @param port Remote port
   */
  router->map("tls.connect", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id", "host", "port"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    oro::runtime::core::services::TLS::ConnectOptions options;
    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(options.port, "port", std::stoi);

    options.host = message.get("host");
    options.servername = message.get("servername", message.get("serverName"));

    options.host = webview::normaliseTlsPinHost(options.host);
    if (options.host.empty()) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_HOST_INVALID"},
        {"message", "Invalid 'host' given in parameters"}
      }});
    }

    if (!options.servername.empty()) {
      const auto servername = webview::normaliseTlsPinHost(options.servername);
      if (servername.empty()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"code", "TLS_SERVERNAME_INVALID"},
          {"message", "Invalid 'servername' given in parameters"}
        }});
      }
      options.servername = servername;
    }

    if (options.port <= 0 || options.port > 65535) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_PORT_INVALID"},
        {"message", "Invalid 'port' given in parameters"}
      }});
    }

    const auto rejectValue = message.get("rejectUnauthorized");
    options.rejectUnauthorized = true;
    if (rejectValue.size() > 0) {
      options.rejectUnauthorized = rejectValue != "false" && rejectValue != "0";
    }
    options.ca = message.get("ca");
    options.cert = message.get("cert");
    options.key = message.get("key");
    options.keyPassphrase = message.get("keyPassphrase", message.get("passphrase"));
    options.alpn = parseDelimitedList(message.get("alpn"));
    options.minVersion = message.get("minVersion");
    options.maxVersion = message.get("maxVersion");
    options.ciphers = parseDelimitedList(message.get("ciphers"));
    options.pins = message.get("pins");
    if (options.pins.empty() && message.buffer.size() > 0) {
      options.pins = message.buffer.str();
    }

    const auto pinsMode = toLowerCase(trim(message.get("pinsMode", "append")));
    const bool replacePins = (pinsMode == "replace");
    options.pinsMode = replacePins
      ? oro::runtime::core::services::TLS::PinsMode::Replace
      : oro::runtime::core::services::TLS::PinsMode::Append;

    const bool pinsProvided = !trim(options.pins).empty();
    if (replacePins && !pinsProvided) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_PINS_INVALID"},
        {"message", "pinsMode 'replace' requires non-empty pins"}
      }});
    }

    if (pinsProvided) {
      String validateMessage;
      if (!validateTlsPinsConfigValue(options.pins, validateMessage)) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"code", "TLS_PINS_INVALID"},
          {"message", validateMessage}
        }});
      }
    }

    if (replacePins && pinsProvided) {
      const auto pinHost = options.servername.empty()
        ? options.host
        : options.servername;
      const auto parsed = webview::parseTlsPinConfig(options.pins, true);

      String endpointKey = pinHost;
      if (!pinHost.empty() && options.port > 0) {
        if (pinHost.find(':') != String::npos) {
          endpointKey = "[" + pinHost + "]:" + std::to_string(options.port);
        } else {
          endpointKey = pinHost + ":" + std::to_string(options.port);
        }
      }
      endpointKey = webview::normaliseTlsPinEndpoint(endpointKey);

      const auto hostKey = webview::normaliseTlsPinHost(pinHost);

      auto it = parsed.find(endpointKey);
      if (it == parsed.end() && !hostKey.empty()) {
        it = parsed.find(hostKey);
      }
      if (it == parsed.end()) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"code", "TLS_PINS_INVALID"},
          {"message", "pinsMode 'replace' requires pins for this host"}
        }});
      }
      bool anyPins = false;
      for (const auto& pin : it->second) {
        if (pin.algorithm == "sha256" && !pin.value.empty()) {
          anyPins = true;
          break;
        }
      }
      if (!anyPins) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"code", "TLS_PINS_INVALID"},
          {"message", "pinsMode 'replace' requires at least one sha256 pin token for this host"}
        }});
      }
    }

    router->bridge.getRuntime()->services.tls.connect(
      message.seq,
      id,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Writes TLS application data to an active client or server session.
   * @param id Handle id for the TLS socket
   */
  router->map("tls.write", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.write(
      message.seq,
      id,
      message.buffer.shared(),
      message.buffer.size(),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Starts delivering decrypted TLS data via `tls.read` events.
   * @param id Handle id for the TLS socket
   */
  router->map("tls.readStart", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.readStart(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Stops delivering decrypted TLS data via `tls.read` events.
   * @param id Handle id for the TLS socket
   */
  router->map("tls.readStop", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.readStop(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Initiates a TLS shutdown sequence (half-close).
   * @param id Handle id for the TLS socket
   */
  router->map("tls.shutdown", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.shutdown(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Closes a TLS session and releases underlying resources.
   * @param id Handle id for the TLS socket
   */
  router->map("tls.close", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.close(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Creates a TLS server handle that wraps an underlying TCP listener.
   * @param id Server handle id
   */
  router->map("tls.server.create", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    oro::runtime::core::services::TLS::ServerOptions options;
    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    options.cert = message.get("cert");
    options.key = message.get("key");
    options.keyPassphrase = message.get("keyPassphrase", message.get("passphrase"));
    options.ca = message.get("ca");
    const auto requestClientCert = message.get("requestClientCert");
    const auto requestCert = message.get("requestCert");
    options.requestClientCert = requestClientCert == "true" || requestCert == "true";
    options.alpn = parseDelimitedList(message.get("alpn"));
    options.minVersion = message.get("minVersion");
    options.maxVersion = message.get("maxVersion");
    options.ciphers = parseDelimitedList(message.get("ciphers"));

    router->bridge.getRuntime()->services.tls.serverCreate(
      message.seq,
      id,
      options,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Binds a TLS server to the given address and port.
   * @param id Server handle id
   * @param port Listen port
   */
  router->map("tls.server.bind", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id", "port"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    int port = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(port, "port", std::stoi);

    router->bridge.getRuntime()->services.tls.serverBind(
      message.seq,
      id,
      message.get("address", "0.0.0.0"),
      port,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Starts listening for TLS client connections.
   * @param id Server handle id
   */
  router->map("tls.server.listen", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);
    int backlog = 128;
    if (message.has("backlog")) {
      const auto backlogValue = message.get("backlog");
      if (!backlogValue.empty()) {
        try { backlog = std::stoi(backlogValue); } catch (...) {}
      }
    }

    router->bridge.getRuntime()->services.tls.serverListen(
      message.seq,
      id,
      backlog,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Accepts a pending TLS client connection and creates a session handle.
   * @param serverId Server handle id
   * @param clientId New client session id
   */
  router->map("tls.server.accept", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"serverId", "clientId"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t serverId = 0;
    uint64_t clientId = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(serverId, "serverId", std::stoull);
    REQUIRE_AND_GET_MESSAGE_VALUE(clientId, "clientId", std::stoull);

    router->bridge.getRuntime()->services.tls.serverAccept(
      message.seq,
      serverId,
      clientId,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Starts delivering decrypted TLS data for a server-side client session.
   * @param id Client session id
   */
  router->map("tls.server.readStart", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.serverReadStart(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Stops delivering decrypted TLS data for a server-side client session.
   * @param id Client session id
   */
  router->map("tls.server.readStop", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.serverReadStop(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Writes TLS application data for a server-side client session.
   * @param id Client session id
   */
  router->map("tls.server.write", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.serverWrite(
      message.seq,
      id,
      message.buffer.shared(),
      message.buffer.size(),
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Initiates a shutdown on a server-side TLS client session.
   * @param id Client session id
   */
  router->map("tls.server.shutdown", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.serverShutdown(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Closes a TLS server or server-side client handle.
   * @param id Server or client session id
   */
  router->map("tls.server.close", [](auto message, auto router, auto reply) {
    if (!tlsServiceEnabled(router)) {
      return reply(Result::Err { message, JSON::Object::Entries {
        {"code", "TLS_DISABLED"},
        {"message", "TLS service is disabled"}
      }});
    }

    auto err = validateMessageParameters(message, {"id"});

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    uint64_t id = 0;
    REQUIRE_AND_GET_MESSAGE_VALUE(id, "id", std::stoull);

    router->bridge.getRuntime()->services.tls.serverClose(
      message.seq,
      id,
      RESULT_CALLBACK_FROM_CORE_CALLBACK(message, reply)
    );
  });

  /**
   * Show the file system picker dialog
   * @param allowMultiple
   * @param allowFiles
   * @param allowDirs
   * @param type
   * @param contentTypeSpecs
   * @param defaultName
   * @param defaultPath
   * @param title
   */
  router->map("window.showFileSystemPicker", [](auto message, auto router, auto reply) {
    const auto allowMultiple = message.get("allowMultiple") == "true";
    const auto allowFiles = message.get("allowFiles") == "true";
    const auto allowDirs = message.get("allowDirs") == "true";
    const auto isSave = message.get("type") == "save";

    const auto contentTypeSpecs = message.get("contentTypeSpecs");
    const auto defaultName = message.get("defaultName");
    const auto defaultPath = message.get("defaultPath");
    const auto title = message.get("title", isSave ? "Save" : "Open");
    const auto app = App::sharedApplication();
    const auto window = app->runtime.windowManager.getWindowForBridge(&router->bridge);

    app->dispatch([=]() {
      window::Dialog* dialog = nullptr;

      if (window) {
        dialog = &window->dialog;
      } else {
        dialog = new window::Dialog();
      }

      const auto options = window::Dialog::FileSystemPickerOptions {
        .prefersDarkMode = message.get("prefersDarkMode") == "true",
        .directories = allowDirs,
        .multiple = allowMultiple,
        .files = allowFiles,
        .contentTypes = contentTypeSpecs,
        .defaultName = defaultName,
        .defaultPath = defaultPath,
        .title = title
      };

      const auto callback = [=](Vector<String> results) {
        JSON::Array paths;

        if (results.size() == 0) {
          const auto err = JSON::Object::Entries {{"type", "AbortError"}};
          return reply(Result::Err { message, err });
        }

        for (const auto& result : results) {
          paths.push(result);
        }

        const auto data = JSON::Object::Entries {
          {"paths", paths}
        };

        reply(Result::Data { message, data });
      };

      if (isSave) {
        if (!dialog->showSaveFilePicker(options, callback)) {
          const auto err = JSON::Object::Entries {{"type", "AbortError"}};
          reply(Result::Err { message, err });
        }
      } else {
        const auto result = (
          allowFiles && !allowDirs
            ? dialog->showOpenFilePicker(options, callback)
            : dialog->showDirectoryPicker(options, callback)
        );

        if (!result) {
          const auto err = JSON::Object::Entries {{"type", "AbortError"}};
          reply(Result::Err { message, err });
        }
      }

      if (!window) {
        delete dialog;
      }
    });
  });

  /**
   * Show the native share sheet for the target window.
   */
  router->map("window.share", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    const auto window = app->runtime.windowManager.getWindowForBridge(&router->bridge);

    if (!window) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"type", "NotSupportedError"},
          {"message", "No window available for sharing"}
        }
      });
    }

    const auto options = window::Dialog::ShareOptions {
      .title = message.get("title"),
      .text = message.get("text"),
      .url = message.get("url")
    };

    app->dispatch([=]() {
      const auto callback = [=](bool success, const String& reason) {
        if (success) {
          reply(Result::Data { message, JSON::Object::Entries {} });
        } else {
          if (reason.size() > 0) {
            reply(Result::Err {
              message,
              JSON::Object::Entries {
                {"type", "AbortError"},
                {"message", reason}
              }
            });
          } else {
            reply(Result::Err {
              message,
              JSON::Object::Entries {{"type", "AbortError"}}
            });
          }
        }
      };

      if (!window->dialog.share(options, callback)) {
        reply(Result::Err {
          message,
          JSON::Object::Entries {{"type", "NotSupportedError"}}
        });
      }
    });
  });

  /**
   * Closes a target window
   * @param targetWindowIndex
   */
  router->map("window.close", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);

    if (!window) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    auto data = window->json();
    app->dispatch([=]() mutable {
      app->runtime.windowManager.destroyWindow(targetWindowIndex);
      data["status"] = window::Manager::WindowStatus::WINDOW_CLOSED;
      reply(Result::Data { message, data });
    });
  });

  /**
   * Creates a new window
   * @param url
   * @param title
   * @param shouldExitApplicationOnClose
   * @param headless
   * @param radius
   * @param margin
   * @param height
   * @param width
   * @param minWidth
   * @param minHeight
   * @param maxWidth
   * @param maxHeight
   * @param resizable
   * @param frameless
   * @param closable
   * @param maximizable
   * @param minimizable
   * @param aspectRatio
   * @param titlebarStyle
   * @param windowControlOffsets
   * @param backgroundColorLight
   * @param backgroundColorDark
   * @param utility
   * @param userScript
   * @param userConfig
   * @param targetWindowIndex
   */
  router->map("window.create", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    const auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    if (targetWindowIndex == -1) {
      targetWindowIndex = app->runtime.windowManager.getRandomWindowIndex(
        message.get("reserved") == "true"
      );
    }

    if (
      targetWindowIndex >= ORO_RUNTIME_MAX_WINDOWS &&
      message.get("headless") != "true" &&
      message.get("debug") != "true"
    ) {
      static const auto maxWindows = std::to_string(ORO_RUNTIME_MAX_WINDOWS);
      return reply(Result::Err {
        message,
        "Cannot create widow with an index beyond " + maxWindows
      });
    }

    app->dispatch([=]() {
      if (
        app->runtime.windowManager.getWindow(targetWindowIndex) != nullptr &&
        app->runtime.windowManager.getWindowStatus(targetWindowIndex) != window::Manager::WindowStatus::WINDOW_NONE
      ) {
        return reply(Result::Err {
          message,
          "Window with index " + message.get("targetWindowIndex") + " already exists"
        });
      }

      if (message.get("unique") == "true" && message.has("url")) {
        const auto origin = webview::Origin(URL(message.get("url"), router->bridge.navigator.location.href()).str());
        Lock lock(app->runtime.windowManager.mutex);
        for (const auto& window : app->runtime.windowManager.windows) {
          if (window != nullptr) {
            const auto windowOrigin = webview::Origin(URL(message.get("url"), window->bridge->navigator.location.href()).str());
            const auto pathname = window->bridge->navigator.location.resolve(URL(message.get("url")).pathname).pathname;

            if (window->options.token == message.get("token")) {
              reply(Result::Data { message, window->json() });
              return;
            } else if (windowOrigin.name() == origin.name() && window->bridge->navigator.location.pathname == pathname) {
              reply(Result::Data { message, window->json() });
              return;
            }
          }
        }
      }

      const auto window = app->runtime.windowManager.getWindow(0);
      const auto screen = window->getScreenSize();
      auto options = Window::Options {};

      options.shouldExitApplicationOnClose = message.get("shouldExitApplicationOnClose") == "true" ? true : false;
      options.headless = app->runtime.userConfig["build_headless"] == "true";
      if (message.has("token")) {
        options.token = message.get("token");
      }

      if (message.get("headless") == "true") {
        options.headless = true;
      } else if (message.get("headless") == "false") {
        options.headless = false;
      }

      try {
        if (message.has("radius")) {
          options.radius = std::stof(message.get("radius"));
        }
      } catch (...) {}

      try {
        if (message.has("margin")) {
          options.margin = std::stof(message.get("margin"));
        }
      } catch (...) {}

      options.width = message.get("width").size()
        ? window->getSizeInPixels(message.get("width"), screen.width)
        : 0;

      options.height = message.get("height").size()
        ? window->getSizeInPixels(message.get("height"), screen.height)
        : 0;

      options.minWidth = message.get("minWidth").size()
        ? window->getSizeInPixels(message.get("minWidth"), screen.width)
        : 0;

      options.minHeight = message.get("minHeight").size()
        ? window->getSizeInPixels(message.get("minHeight"), screen.height)
        : 0;

      options.maxWidth = message.get("maxWidth").size()
        ? window->getSizeInPixels(message.get("maxWidth"), screen.width)
        : screen.width;

      options.maxHeight = message.get("maxHeight").size()
        ? window->getSizeInPixels(message.get("maxHeight"), screen.height)
        : screen.height;

      options.resizable = message.get("resizable") == "true" ? true : false;
      options.frameless = message.get("frameless") == "true" ? true : false;
      options.closable = message.get("closable") == "true" ? true : false;
      options.maximizable = message.get("maximizable") == "true" ? true : false;
      options.minimizable = message.get("minimizable") == "true" ? true : false;
      options.aspectRatio = message.get("aspectRatio");
      options.titlebarStyle = message.get("titlebarStyle");
      options.windowControlOffsets = message.get("windowControlOffsets");
      options.backgroundColorLight = message.get("backgroundColorLight");
      options.backgroundColorDark = message.get("backgroundColorDark");
      options.utility = message.get("utility") == "true" ? true : false;
      if (message.has("followSystemTheme")) {
        options.followSystemTheme = message.get("followSystemTheme") != "false";
      } else {
        options.followSystemTheme = app->runtime.windowManager.options.followSystemTheme;
      }

      if (message.has("preferDarkTheme")) {
        options.preferDarkTheme = message.get("preferDarkTheme") == "true";
      } else {
        options.preferDarkTheme = app->runtime.windowManager.options.preferDarkTheme;
      }
      options.debug = message.get("debug") == "true" ? true : false;
      options.index = targetWindowIndex;
      options.RUNTIME_PRIMORDIAL_OVERRIDES = message.get("__runtime_primordial_overrides__");
      options.userConfig = INI::parse(message.get("config"));
      options.userScript = message.get("userScript");
      options.resourcesDirectory = message.get("resourcesDirectory");
      options.shouldPreferServiceWorker = message.get("shouldPreferServiceWorker", "false") == "true";

      if (options.index >= ORO_RUNTIME_MAX_WINDOWS) {
        options.features.useGlobalCommonJS = false;
        options.features.useGlobalNodeJS = false;
      }

      auto createdWindow = app->runtime.windowManager.createWindow(options);
      if (createdWindow != nullptr) {
        if (message.has("title")) {
          createdWindow->setTitle(message.get("title"));
        }

        if (message.has("url")) {
          createdWindow->navigate(message.get("url"));
        }

      #if !ORO_RUNTIME_PLATFORM_ANDROID
        createdWindow->show();
      #endif

        reply(Result::Data { message, createdWindow->json() });
      } else {
        reply(Result::Err { message, JSON::Object::Entries {
          {"type", "BadRequestError"},
          {"message", "Failed to create window"}
        }});
      }
    });
  });

  /**
   * Gets the background color of a target window window
   * @param targetWindowIndex
   */
  router->map("window.getBackgroundColor", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    app->dispatch([=]() {
      const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
      const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

      if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {
            {"message", "Target window not found"},
            {"type", "NotFoundError"}
          }
        });
      }

      reply(Result::Data { message, window->getBackgroundColor() });
    });
  });

  /**
   * Gets the title of a target window
   * @param targetWindowIndex
   */
  router->map("window.getTitle", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    app->dispatch([=]() {
      const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
      const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

      if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {
            {"message", "Target window not found"},
            {"type", "NotFoundError"}
          }
        });
      }

      reply(Result::Data { message, window->getTitle() });
    });
  });

  /**
   * Gets the current state of a window
   * @param index
   */
  router->map("window", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"index"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int index;
    REQUIRE_AND_GET_MESSAGE_VALUE(index, "index", std::stoi);

    app->dispatch([=]() {
      const auto window = app->runtime.windowManager.getWindow(index);
      const auto windowStatus = app->runtime.windowManager.getWindowStatus(index);

      if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
        return reply(Result::Err {
          message,
          JSON::Object::Entries {
            {"message", "Target window not found"},
            {"type", "NotFoundError"}
          }
        });
      }

      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Hides a target window
   * @param targetWindowIndex
   */
  router->map("window.hide", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      auto options = window->options;
      if (options.userConfig["build_headless"] != "true") {
        window->hide();
      }
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Brings a target window to the foreground and focuses it.
   * @param targetWindowIndex
   */
  router->map("window.focus", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;
    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      window->focus();
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Removes focus from a target window.
   * @param targetWindowIndex
   */
  router->map("window.blur", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;
    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      window->blur();
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Maximize a target window
   * @param targetWindowIndex
   */
  router->map("window.maximize", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      auto options = window->options;
      if (options.userConfig["build_headless"] != "true") {
      #if ORO_RUNTIME_PLATFORM_DESKTOP
        window->maximize();
      #else
        const auto screen = window->getScreenSize();
        window->setSize(screen.width, screen.height);
        window->show();
      #endif
      }

      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Minimize a target window
   * @param targetWindowIndex
   */
  router->map("window.minimize", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
    #if ORO_RUNTIME_PLATFORM_DESKTOP
      window->minimize();
    #else
      window->hide();
    #endif
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Navigate a targetbnnb
   * @param targetWindowIndex
   */
  router->map("window.navigate", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex", "url"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    const auto requestedURL = message.get("url");
    const auto allowed = window->bridge->navigator.isNavigationRequestAllowed(
      window->bridge->navigator.location.str(),
      requestedURL
    );

    if (!allowed) {
      return reply(Result::Err { message, "Navigation to URL is not allowed" });
    }

    app->dispatch([=]() {
      window->navigate(requestedURL);
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Restore a target window
   * @param targetWindowIndex
   */
  router->map("window.restore", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
    #if ORO_RUNTIME_PLATFORM_DESKTOP
      window->restore();
    #else
      if (!window->options.headless) {
        window->show();
      }
    #endif
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * @param index
   * @param value
   */
  router->map("window.eval", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is in invalid state" });
    }

    int index = 0;
    if (message.has("index")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(index, "index", std::stoi);
    }

    const auto value = message.get("value");
    const auto window = app->runtime.windowManager.getWindow(index);

    if (!window) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    window->eval(value, [=](const auto result) {
      reply(Result::Data { message, result });
    });
  });

  /**
   * Send an event to another window
   * @param event
   * @param value
   * @param targetWindowIndex (DEPRECATED) use `index` instead
   * @param index
   */
  router->map("window.send", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is in invalid state" });
    }

    int targetWindowIndex = -1;

    const auto event = message.get("event");
    const auto value = message.get("value");

    try {
      targetWindowIndex = message.get("targetWindowIndex").size() >= 0
        ? std::stoi(message.get("targetWindowIndex"))
        : -1;
    } catch (...) {}

    if (targetWindowIndex < 0) {
      return reply(Result::Err { message, "Invalid target window index" });
    }

    const auto targetWindow = app->runtime.windowManager.getWindow(targetWindowIndex);

    if (!targetWindow) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    targetWindow->eval(getEmitToRenderProcessJavaScript(event, value));
    app->dispatch([=]() {
      reply(Result { message.seq, message });
    });
  });

  /**
   * Sets the background color
   * @param targetWindowIndex
   * @param red
   * @param green
   * @param blue
   * @param alpha
   *
   */
  router->map("window.setBackgroundColor", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;
    int red = 0;
    int green = 0;
    int blue = 0;
    float alpha = 1;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    if (message.has("red")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(red, "red", std::stoi);
    }

    if (message.has("green")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(green, "green", std::stoi);
    }

    if (message.has("blue")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(blue, "blue", std::stoi);
    }

    if (message.has("alpha")) {
      REQUIRE_AND_GET_MESSAGE_VALUE(alpha, "alpha", std::stof);
    }

    if (alpha > 1 || alpha < 0) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Invalid 'alpha' parameter given"},
          {"type", "RangeError"}
        }
      });
    }

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      window->setBackgroundColor(red, green, blue, alpha);
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Creates and displays a context menu at the current mouse position (desktop only)
   * @param value
   */
  router->map("window.setContextMenu", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"index", "value"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    const auto window = app->runtime.windowManager.getWindow(message.index);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(message.index);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      window->setContextMenu(message.seq, message.value);
      reply(Result::Data { message, JSON::Object {} });
    });
  #else
    reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Setting a window context menu is not supported"}
      }
    });
  #endif
  });

  /**
   * Sets the position  of a target window
   * @param targetWindowIndex
   * @param height
   * @param width
   */
  router->map("window.setPosition", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex", "x", "y"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    const auto screen = window->getScreenSize();
    const auto x = Window::getSizeInPixels(message.get("x"), screen.width);
    const auto y = Window::getSizeInPixels(message.get("y"), screen.height);

    app->dispatch([=]() {
      window->setPosition(x, y);
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Sets whether a target window should stay above other windows (desktop only).
   * @param targetWindowIndex
   * @param value
   */
  router->map("window.setAlwaysOnTop", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex", "value"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    const auto enabled = message.get("value") == "true";

    app->dispatch([=]() {
      window->setAlwaysOnTop(enabled);
      reply(Result::Data { message, window->json() });
    });
  #else
    reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Always-on-top is not supported on this platform"}
      }
    });
  #endif
  });

  /**
   * Returns whether a target window is configured to stay above other windows (desktop only).
   * @param targetWindowIndex
   */
  router->map("window.getAlwaysOnTop", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      reply(Result::Data { message, JSON::Boolean(window->isAlwaysOnTop()) });
    });
  #else
    reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Always-on-top is not supported on this platform"}
      }
    });
  #endif
  });

  /**
   * MCP service routes (initial scaffolding)
   */
  router->map("mcp.server.registerTool", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      replyConduitAware(router, reply, std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"name"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      replyConduitAware(router, reply, std::move(result));
      return;
    }

    mcp::ToolBuilder builder(message.get("name"));
    const auto title = message.get("title");
    if (!title.empty()) {
      builder.title(title);
    }
    const auto description = message.get("description");
    if (!description.empty()) {
      builder.description(description);
    }

    const auto annotations = message.get("annotations");
    if (!annotations.empty()) {
      try {
        const auto value = JSON::parse(annotations);
        if (!value.isObject()) {
          throw JSON::Error("tool annotations must be a JSON object");
        }
        builder.annotations(value);
      } catch (const JSON::Error& error) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", error.what()}
          }
        });
        replyConduitAware(router, reply, std::move(result));
        return;
      }
    }

    const auto icons = message.get("icons");
    if (!icons.empty()) {
      try {
        const auto value = JSON::parse(icons);
        if (!value.isArray()) {
          throw JSON::Error("tool icons must be a JSON array");
        }
        builder.icons(value);
      } catch (const JSON::Error& error) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", error.what()}
          }
        });
        replyConduitAware(router, reply, std::move(result));
        return;
      }
    }

    const auto outputSchema = message.get("outputSchema");
    if (!outputSchema.empty()) {
      try {
        const auto value = JSON::parse(outputSchema);
        if (!value.isObject()) {
          throw JSON::Error("tool outputSchema must be a JSON object");
        }
        builder.outputSchema(value);
      } catch (const JSON::Error& error) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", error.what()}
          }
        });
        replyConduitAware(router, reply, std::move(result));
        return;
      }
    }

    const auto metadata = message.get("metadata");
    if (!metadata.empty()) {
      try {
        builder.metadata(JSON::parse(metadata));
      } catch (const JSON::Error& error) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", error.what()}
          }
        });
        replyConduitAware(router, reply, std::move(result));
        return;
      }
    }

    auto tool = builder.build();
    if (message.has("inputSchema")) {
      const auto inputSchema = message.get("inputSchema");
      if (!inputSchema.empty()) {
        try {
          const auto parsedSchema = JSON::parse(inputSchema);
          if (!parsedSchema.isObject()) {
            Result result(Result::Err {
              message,
              JSON::Object::Entries {
                {"type", "TypeError"},
                {"message", "tool inputSchema must be a JSON object"}
              }
            });
            replyConduitAware(router, reply, std::move(result));
            return;
          }
          const auto parsedObject = parsedSchema.template as<JSON::Object>();
          if (!parsedObject.contains("type") ||
              !parsedObject.get("type").isString() ||
              parsedObject.get("type").template as<JSON::String>().value() != "object") {
            Result result(Result::Err {
              message,
              JSON::Object::Entries {
                {"type", "TypeError"},
                {"message", "tool inputSchema must declare type 'object' at its root"}
              }
            });
            replyConduitAware(router, reply, std::move(result));
            return;
          }
          tool.inputSchema = parsedObject;
        } catch (const JSON::Error& error) {
          Result result(Result::Err {
            message,
            JSON::Object::Entries {
              {"type", "TypeError"},
              {"message", error.what()}
            }
          });
          replyConduitAware(router, reply, std::move(result));
          return;
        }
      }
    }

    String schemaError;
    if (!tool.prepareSchemas(schemaError)) {
      Result result(Result::Err {
        message,
        JSON::Object::Entries {
          {"type", "TypeError"},
          {"message", schemaError}
        }
      });
      replyConduitAware(router, reply, std::move(result));
      return;
    }

    const auto callback = [router, reply, message](auto seq, auto json, auto queuedResponse) {
      Result result { seq, message, json, queuedResponse };
      replyConduitAware(router, reply, std::move(result));
    };
    const auto id = app->runtime.services.mcp.registerTool(tool, callback);

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"id", JSON::Number(static_cast<uint64_t>(id))}}
    });
    replyConduitAware(router, reply, std::move(result));
  });

  router->map("mcp.server.unregisterTool", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"name"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto removed = app->runtime.services.mcp.unregisterTool(message.get("name"));
    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"removed", JSON::Boolean(removed)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.listTools", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    const auto tools = app->runtime.services.mcp.listTools();
    JSON::Array payload;
    for (const auto& tool : tools) {
      payload.push(tool.toJSON());
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"tools", payload}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.registerResource", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"uri"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    mcp::ResourceDescriptor descriptor;
    descriptor.uri = message.get("uri");
    descriptor.name = message.get("name");
    if (descriptor.name.empty()) {
      descriptor.name = descriptor.uri;
    }
    descriptor.title = message.get("title");
    descriptor.description = message.get("description");
    const auto mimeType = message.get("mimeType");
    descriptor.mimeType = mimeType.empty() ? String("application/octet-stream") : mimeType;
    descriptor.subscribable = message.get("subscribable") == "true";
    const auto size = message.get("size");
    if (!size.empty()) {
      try {
        descriptor.size = std::stoull(size);
      } catch (...) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", "resource size must be a non-negative integer"}
          }
        });
        respond(std::move(result));
        return;
      }
    }
    const auto icons = message.get("icons");
    if (!icons.empty()) {
      try {
        descriptor.icons = JSON::parse(icons);
        if (!descriptor.icons.isArray()) {
          throw JSON::Error("resource icons must be a JSON array");
        }
      } catch (const JSON::Error& error) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", error.what()}
          }
        });
        respond(std::move(result));
        return;
      }
    }
    const auto annotations = message.get("annotations");
    if (!annotations.empty()) {
      try {
        descriptor.annotations = JSON::parse(annotations);
        if (!descriptor.annotations.isObject()) {
          throw JSON::Error("resource annotations must be a JSON object");
        }
      } catch (const JSON::Error& error) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", error.what()}
          }
        });
        respond(std::move(result));
        return;
      }
    }
    const auto metadata = message.get("metadata");
    if (!metadata.empty()) {
      try {
        descriptor.metadata = JSON::parse(metadata);
      } catch (const JSON::Error& error) {
        Result result(Result::Err {
          message,
          JSON::Object::Entries {
            {"type", "TypeError"},
            {"message", error.what()}
          }
        });
        respond(std::move(result));
        return;
      }
    }

    const auto callback = [router, reply, message](auto seq, auto json, auto queuedResponse) {
      Result result { seq, message, json, queuedResponse };
      replyConduitAware(router, reply, std::move(result));
    };
    const auto id = app->runtime.services.mcp.registerResource(descriptor, callback);
    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"id", JSON::Number(static_cast<uint64_t>(id))}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.unregisterResource", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"uri"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto removed = app->runtime.services.mcp.unregisterResource(message.get("uri"));
    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"removed", JSON::Boolean(removed)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.listResources", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    const auto resources = app->runtime.services.mcp.listResources();
    JSON::Array payload;
    for (const auto& resource : resources) {
      auto serialized = resource.toJSON();
      serialized.set("subscribable", JSON::Boolean(resource.subscribable));
      payload.push(serialized);
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"resources", payload}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.setAuthHandler", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    const auto callback = [router, reply, message](auto seq, auto json, auto queuedResponse) {
      Result result { seq, message, json, queuedResponse };
      replyConduitAware(router, reply, std::move(result));
    };
    app->runtime.services.mcp.setAuthorizationHandler(callback);

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"registered", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.clearAuthHandler", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    app->runtime.services.mcp.clearAuthorizationHandler();

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"cleared", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.resolveAuthorization", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"id", "allow"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto id = message.get("id");
    const auto allowValue = string::toLowerCase(message.get("allow"));
    const bool allow = allowValue == "1" || allowValue == "true" || allowValue == "yes";

    std::optional<int> statusOverride;
    if (message.has("status")) {
      try {
        statusOverride = std::stoi(message.get("status"));
      } catch (...) {}
    }

    std::optional<String> messageOverride;
    if (message.has("message")) {
      messageOverride = message.get("message");
    }

    if (!app->runtime.services.mcp.resolveAuthorization(id, allow, statusOverride, messageOverride)) {
      Result result(Result::Err { message, JSON::Object::Entries {
        {"type", "NotFoundError"},
        {"message", "Authorization request not found"}
      }});
      respond(std::move(result));
      return;
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"resolved", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.start", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    mcp::HTTPServer::Config cfg;
    if (message.has("host")) {
      cfg.host = message.get("host");
    }
    if (message.has("port")) {
      uint64_t port = 0;
      if (!parseUint64(message.get("port"), port) || port > 65535) {
        Result result(Result::Err {
          message,
          "port must be an integer from 0 through 65535"
        });
        respond(std::move(result));
        return;
      }
      cfg.port = static_cast<int>(port);
    }
    auto normalizeEndpoint = [](String value) {
      auto trim = [](String input) {
        auto begin = input.begin();
        while (begin != input.end() && std::isspace(static_cast<unsigned char>(*begin))) {
          ++begin;
        }
        auto end = input.end();
        while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
          --end;
        }
        return String(begin, end);
      };

      String trimmed = trim(std::move(value));
      if (trimmed.empty()) {
        return trimmed;
      }

      String path = trimmed;
      const auto schemePos = path.find("://");
      if (schemePos != String::npos) {
        const auto authorityStart = schemePos + 3;
        const auto pathStart = path.find('/', authorityStart);
        if (pathStart != String::npos) {
          path = path.substr(pathStart);
        } else {
          path = "/";
        }
      }

      const auto fragmentPos = path.find('#');
      if (fragmentPos != String::npos) {
        path = path.substr(0, fragmentPos);
      }

      const auto queryPos = path.find('?');
      if (queryPos != String::npos) {
        path = path.substr(0, queryPos);
      }

      path = trim(std::move(path));
      if (path.empty()) {
        return String("/");
      }

      if (path.front() != '/') {
        path.insert(path.begin(), '/');
      }

      while (path.size() > 1 && path[0] == '/' && path[1] == '/') {
        path.erase(path.begin() + 1);
      }

      while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
      }

      if (path.empty()) {
        return String("/");
      }

      return path;
    };

    if (message.has("endpoint")) {
      cfg.endpoint = normalizeEndpoint(message.get("endpoint"));
    } else {
      if (message.has("message")) {
        cfg.endpoint = normalizeEndpoint(message.get("message"));
      } else if (message.has("sse")) {
        cfg.endpoint = normalizeEndpoint(message.get("sse"));
      }
    }
    if (message.has("token")) {
      cfg.token = message.get("token");
    }
    if (message.has("retry")) {
      uint64_t retry = 0;
      if (!parseUint64(message.get("retry"), retry) ||
          retry == 0 ||
          retry > std::numeric_limits<uint32_t>::max()) {
        Result result(Result::Err { message, "retry must be a positive 32-bit integer" });
        respond(std::move(result));
        return;
      }
      cfg.retryMilliseconds = static_cast<uint32_t>(retry);
    }

    auto readPositiveSize = [&message](const char* key, size_t& value) {
      if (!message.has(key)) {
        return true;
      }
      uint64_t parsed = 0;
      if (!parseUint64(message.get(key), parsed) ||
          parsed == 0 ||
          parsed > std::numeric_limits<size_t>::max()) {
        return false;
      }
      value = static_cast<size_t>(parsed);
      return true;
    };

    uint64_t sessionTtlSeconds = cfg.sessionTtlSeconds;
    if (message.has("sessionTtlSeconds")) {
      if (!parseUint64(message.get("sessionTtlSeconds"), sessionTtlSeconds) ||
          sessionTtlSeconds == 0 ||
          sessionTtlSeconds > std::numeric_limits<uint32_t>::max()) {
        Result result(Result::Err { message, "sessionTtlSeconds must be a positive 32-bit integer" });
        respond(std::move(result));
        return;
      }
      cfg.sessionTtlSeconds = static_cast<uint32_t>(sessionTtlSeconds);
    }

    if (!readPositiveSize("maxRequestBytes", cfg.maxRequestBytes) ||
        !readPositiveSize("maxSessions", cfg.maxSessions) ||
        !readPositiveSize("maxQueuedEvents", cfg.maxQueuedEvents) ||
        !readPositiveSize("maxQueuedBytes", cfg.maxQueuedBytes)) {
      Result result(Result::Err {
        message,
        "MCP server limits must be positive integers supported by this platform"
      });
      respond(std::move(result));
      return;
    }

    if (message.has("replaceSseStreamOnReconnect")) {
      cfg.replaceSseStreamOnReconnect = parseBoolValue(
        message.get("replaceSseStreamOnReconnect")
      );
    }

#if __has_include(<nlohmann/json.hpp>)
    if (message.has("oauth")) {
      const auto raw = message.get("oauth");
      if (!raw.empty()) {
        try {
          const auto parsed = nlohmann::json::parse(raw);
          if (!parsed.is_object()) {
            throw std::invalid_argument("oauth must be a JSON object");
          }
          if (parsed.contains("enabled")) {
            if (!parsed["enabled"].is_boolean()) {
              throw std::invalid_argument("oauth.enabled must be a boolean");
            }
            cfg.oauth.enabled = parsed["enabled"].template get<bool>();
          } else if (!parsed.empty()) {
            cfg.oauth.enabled = true;
          }

          if (cfg.oauth.enabled) {
            const auto readString = [&parsed](const char* key, String& output) {
              if (!parsed.contains(key)) {
                return;
              }
              if (!parsed[key].is_string()) {
                throw std::invalid_argument(String("oauth.") + key + " must be a string");
              }
              output = parsed[key].template get<String>();
              if (output.empty()) {
                throw std::invalid_argument(String("oauth.") + key + " must not be empty");
              }
            };
            readString("issuer", cfg.oauth.issuer);
            readString("resource", cfg.oauth.resource);
            readString("defaultClientId", cfg.oauth.defaultClientId);
            readString("defaultScope", cfg.oauth.defaultScope);

            String path;
            readString("authorizePath", path);
            if (!path.empty()) {
              cfg.oauth.authorizePath = normalizeEndpoint(path);
            }
            path.clear();
            readString("tokenPath", path);
            if (!path.empty()) {
              cfg.oauth.tokenPath = normalizeEndpoint(path);
            }
            path.clear();
            readString("metadataPath", path);
            if (!path.empty()) {
              cfg.oauth.metadataPath = normalizeEndpoint(path);
            }

            if (parsed.contains("redirectUris")) {
              if (!parsed["redirectUris"].is_array()) {
                throw std::invalid_argument("oauth.redirectUris must be an array");
              }
              for (const auto& redirectUri : parsed["redirectUris"]) {
                if (!redirectUri.is_string() || redirectUri.template get<String>().empty()) {
                  throw std::invalid_argument(
                    "oauth.redirectUris must contain only non-empty strings"
                  );
                }
                cfg.oauth.redirectUris.push_back(redirectUri.template get<String>());
              }
            }

            const auto readLifetime = [&parsed](const char* key, uint32_t& output) {
              if (!parsed.contains(key)) {
                return;
              }
              const auto& value = parsed[key];
              uint64_t seconds = 0;
              if (value.is_number_unsigned()) {
                seconds = value.template get<uint64_t>();
              } else if (value.is_number_integer()) {
                const auto signedSeconds = value.template get<int64_t>();
                if (signedSeconds > 0) {
                  seconds = static_cast<uint64_t>(signedSeconds);
                }
              }
              if (seconds == 0 || seconds > std::numeric_limits<uint32_t>::max()) {
                throw std::invalid_argument(
                  String("oauth.") + key + " must be a positive 32-bit integer"
                );
              }
              output = static_cast<uint32_t>(seconds);
            };
            readLifetime("codeLifetimeSeconds", cfg.oauth.codeLifetimeSeconds);
            readLifetime("tokenLifetimeSeconds", cfg.oauth.tokenLifetimeSeconds);

            if (parsed.contains("screen")) {
              if (!parsed["screen"].is_object()) {
                throw std::invalid_argument("oauth.screen must be an object");
              }
              const auto& screen = parsed["screen"];
              const auto readScreenString = [&screen](const char* key, String& output) {
                if (!screen.contains(key)) {
                  return;
                }
                if (!screen[key].is_string() || screen[key].template get<String>().empty()) {
                  throw std::invalid_argument(
                    String("oauth.screen.") + key + " must be a non-empty string"
                  );
                }
                output = screen[key].template get<String>();
              };
              readScreenString("html", cfg.oauth.screenHtml);
              readScreenString("file", cfg.oauth.screenFile);
              if (!cfg.oauth.screenHtml.empty() && !cfg.oauth.screenFile.empty()) {
                throw std::invalid_argument(
                  "oauth.screen must specify either html or file, not both"
                );
              }
            }
          }
        } catch (const std::exception& error) {
          Result result(Result::Err {
            message,
            JSON::Object::Entries {
              {"type", "TypeError"},
              {"message", error.what()}
            }
          });
          respond(std::move(result));
          return;
        }
      }
    }
#else
    (void)normalizeEndpoint;
    if (message.has("oauth")) {
      cfg.oauth.enabled = false;
    }
#endif

    if (!app->runtime.services.mcp.startServer(cfg)) {
      Result result(Result::Err { message, JSON::Object::Entries {{"message", "Failed to start MCP server"}} });
      respond(std::move(result));
      return;
    }

    const auto config = app->runtime.services.mcp.getServerConfig();

    Result result(Result::Data {
      message,
      JSON::Object::Entries {
        {"running", JSON::Boolean(true)},
        {"host", config.host},
        {"port", JSON::Number(config.port)},
        {"endpoint", config.endpoint},
        {"oauthAuthorizePath", config.oauth.authorizePath},
        {"oauthTokenPath", config.oauth.tokenPath},
        {"oauthMetadataPath", config.oauth.metadataPath},
        {"oauthProtectedResourceMetadataPath", config.oauth.protectedResourceMetadataPath}
      }
    });
    respond(std::move(result));
  });

  router->map("mcp.server.stop", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    app->runtime.services.mcp.stopServer();
    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"running", JSON::Boolean(false)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.status", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"running", JSON::Boolean(app->runtime.services.mcp.serverRunning())}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.invokeTool", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"name"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto callback = [router, reply, message](auto seq, auto json, auto queuedResponse) {
      Result result { seq, message, json, queuedResponse };
      replyConduitAware(router, reply, std::move(result));
    };
    const auto name = message.get("name");
    const auto sessionId = message.get("sessionId");
    const auto argumentsJson = message.get("arguments");

    if (!app->runtime.services.mcp.invokeTool(message.seq, name, sessionId, argumentsJson, false, callback)) {
      return;
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"accepted", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.resolveInvocation", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto id = message.get("id");
    const auto resultJson = message.get("result");

    if (!app->runtime.services.mcp.resolveInvocation(id, resultJson)) {
      Result result(Result::Err { message, JSON::Object::Entries {
        {"type", "NotFoundError"},
        {"message", "Invocation not found"}
      }});
      respond(std::move(result));
      return;
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"ok", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.rejectInvocation", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto id = message.get("id");
    int codeValue = static_cast<int>(mcp::ErrorCode::InternalError);
    if (message.has("code")) {
      try {
        codeValue = std::stoi(message.get("code"));
      } catch (...) {}
    }

    const auto errorMessage = message.get("message");
    mcp::Error error(static_cast<mcp::ErrorCode>(codeValue), errorMessage.empty() ? String("Invocation rejected") : errorMessage);

    const auto data = message.get("data");
    if (!data.empty()) {
      error.data = JSON::String(data);
    }

    if (!app->runtime.services.mcp.rejectInvocation(id, error)) {
      Result result(Result::Err { message, JSON::Object::Entries {
        {"type", "NotFoundError"},
        {"message", "Invocation not found"}
      }});
      respond(std::move(result));
      return;
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"ok", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.resolveResource", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto id = message.get("id");
    const auto resultJson = message.get("result");

    if (!app->runtime.services.mcp.resolveResourceRead(id, resultJson)) {
      Result result(Result::Err { message, JSON::Object::Entries {
        {"type", "NotFoundError"},
        {"message", "Resource request not found"}
      }});
      respond(std::move(result));
      return;
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"ok", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.rejectResource", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"id"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto id = message.get("id");
    int codeValue = static_cast<int>(mcp::ErrorCode::InternalError);
    if (message.has("code")) {
      try {
        codeValue = std::stoi(message.get("code"));
      } catch (...) {}
    }

    const auto errorMessage = message.get("message");
    mcp::Error error(static_cast<mcp::ErrorCode>(codeValue), errorMessage.empty() ? String("Resource read rejected") : errorMessage);

    const auto data = message.get("data");
    if (!data.empty()) {
      error.data = JSON::String(data);
    }

    if (!app->runtime.services.mcp.rejectResourceRead(id, error)) {
      Result result(Result::Err { message, JSON::Object::Entries {
        {"type", "NotFoundError"},
        {"message", "Resource request not found"}
      }});
      respond(std::move(result));
      return;
    }

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"ok", JSON::Boolean(true)}}
    });
    respond(std::move(result));
  });

  router->map("mcp.server.publishResource", [](auto message, auto router, auto reply) {
    const auto respond = [router, reply](Result&& result) {
      replyConduitAware(router, reply, std::move(result));
    };

    const auto app = App::sharedApplication();
    if (app == nullptr) {
      Result result(Result::Err { message, "Application is invalid state" });
      respond(std::move(result));
      return;
    }

    auto err = validateMessageParameters(message, {"uri", "result"});
    if (err.type != JSON::Type::Null) {
      Result result(Result::Err { message, err });
      respond(std::move(result));
      return;
    }

    const auto uri = message.get("uri");
    const auto resultJson = message.get("result");

    std::optional<String> sessionId;
    if (message.has("sessionId")) {
      const auto value = message.get("sessionId");
      if (!value.empty()) {
        sessionId = value;
      }
    }

    std::optional<String> subscriptionId;
    if (message.has("subscriptionId")) {
      const auto value = message.get("subscriptionId");
      if (!value.empty()) {
        subscriptionId = value;
      }
    }

    const bool delivered = app->runtime.services.mcp.publishResourceUpdate(uri, resultJson, sessionId, subscriptionId);

    Result result(Result::Data {
      message,
      JSON::Object::Entries {{"delivered", JSON::Boolean(delivered)}}
    });
    respond(std::move(result));
  });

  /**
   * Sets the size of a target window (desktop only)
   * @param targetWindowIndex
   * @param height
   * @param width
   */
  router->map("window.setSize", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex", "height", "width"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    const auto screen = window->getScreenSize();
    const auto width = window->getSizeInPixels(message.get("width"), screen.width);
    const auto height = window->getSizeInPixels(message.get("height"), screen.height);

    app->dispatch([=]() {
      window->setSize(width, height, 0);
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Sets the title of a target windo
   * @param targetWindowIndex
   * @param value
   */
  router->map("window.setTitle", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex", "value"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      window->setTitle(message.value);
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Shows a target window
   * @param targetWindowIndex
   */
  router->map("window.show", [](auto message, auto router, auto reply) {
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=]() {
      auto options = window->options;
      if (options.userConfig["build_headless"] != "true") {
        window->show();
      }
      reply(Result::Data { message, window->json() });
    });
  });

  /**
   * Shows the target window web inspector (desktop only)
   * @param targetWindowIndex
   */
  router->map("window.showInspector", [](auto message, auto router, auto reply) {
  #if ORO_RUNTIME_PLATFORM_DESKTOP
    const auto app = App::sharedApplication();
    auto err = validateMessageParameters(message, {"targetWindowIndex"});

    if (app == nullptr) {
      return reply(Result::Err { message, "Application is invalid state" });
    }

    if (err.type != JSON::Type::Null) {
      return reply(Result::Err { message, err });
    }

    int targetWindowIndex;

    REQUIRE_AND_GET_MESSAGE_VALUE(targetWindowIndex, "targetWindowIndex", std::stoi);

    const auto window = app->runtime.windowManager.getWindow(targetWindowIndex);
    const auto windowStatus = app->runtime.windowManager.getWindowStatus(targetWindowIndex);

    if (!window || windowStatus == window::Manager::WindowStatus::WINDOW_NONE) {
      return reply(Result::Err {
        message,
        JSON::Object::Entries {
          {"message", "Target window not found"},
          {"type", "NotFoundError"}
        }
      });
    }

    app->dispatch([=] () {
      window->showInspector();
      reply(Result::Data { message, window->json() });
    });
  #else
    reply(Result::Err {
      message,
      JSON::Object::Entries {
        {"type", "NotSupportedError"},
        {"message", "Showing the window inspector is not supported on this platform"}
      }
    });
  #endif
  });
}

namespace oro::runtime::ipc {
  void Router::mapRoutes () {
    mapIPCRoutes(this);
  }
}
