#include "protocol.hh"

#include "../crypto.hh"

#include <atomic>

namespace oro::runtime::mcp {
  namespace {
    std::atomic<uint64_t> gRequestCounter(1);

    JSON::Any ensureParamsObject(const JSON::Any& params) {
      if (params.isNull()) {
        return JSON::Object(JSON::Object::Entries{});
      }

      if (params.isObject() || params.isArray()) {
        return params;
      }

      // Wrap primitive parameters in an object for consistency.
      return JSON::Object(JSON::Object::Entries {{"value", params}});
    }
  }

  bool isModernProtocolVersion(const String& version) {
    return version == kProtocolVersion;
  }

  bool isMcp2025ProtocolVersion(const String& version) {
    return version == kMcp2025ProtocolVersion ||
      version == kEarlierMcp2025ProtocolVersion;
  }

  bool isSupportedProtocolVersion(const String& version) {
    return isModernProtocolVersion(version) || isMcp2025ProtocolVersion(version);
  }

  Vector<String> supportedProtocolVersions() {
    return {
      kProtocolVersion,
      kMcp2025ProtocolVersion,
      kEarlierMcp2025ProtocolVersion
    };
  }

  Error::Error(ErrorCode errorCode, const String& message)
    : code(errorCode),
      message(message)
  {}

  Error::Error(ErrorCode errorCode, const String& message, const JSON::Any& errorData)
    : code(errorCode),
      message(message),
      data(errorData)
  {}

  JSON::Object Error::toJSON() const {
    JSON::Object::Entries entries {
      {"code", JSON::Number(static_cast<int>(this->code))},
      {"message", JSON::String(this->message)}
    };

    if (!this->data.isNull()) {
      entries.insert({"data", this->data});
    }

    return JSON::Object(entries);
  }

  bool Request::isNotification() const {
    return this->id.isNull();
  }

  Request Request::create(const String& methodName, const JSON::Any& methodParams) {
    return Request::createWithId(nextRequestId(), methodName, methodParams);
  }

  Request Request::createWithId(const Id& requestId, const String& methodName, const JSON::Any& methodParams) {
    Request request;
    request.method = methodName;
    request.params = ensureParamsObject(methodParams);
    request.id = requestId;
    return request;
  }

  Request Request::createNotification(const String& methodName, const JSON::Any& methodParams) {
    Request request;
    request.method = methodName;
    request.params = ensureParamsObject(methodParams);
    request.id = JSON::Null();
    return request;
  }

  JSON::Object Request::toJSON() const {
    JSON::Object::Entries entries {
      {"jsonrpc", JSON::String(this->jsonrpc)},
      {"method", JSON::String(this->method)}
    };

    if (!this->params.isNull()) {
      entries.insert({"params", this->params});
    }

    if (!this->isNotification()) {
      entries.insert({"id", this->id});
    }

    return JSON::Object(entries);
  }

  bool Response::isError() const {
    return this->error.has_value();
  }

  Response Response::success(const Request::Id& responseId, const JSON::Any& responseResult) {
    Response response;
    response.id = responseId;
    response.result = responseResult.isNull()
      ? JSON::Object(JSON::Object::Entries{})
      : responseResult;
    return response;
  }

  Response Response::failure(const Request::Id& responseId, const Error& responseError) {
    Response response;
    response.id = responseId;
    response.error = responseError;
    return response;
  }

  JSON::Object Response::toJSON() const {
    JSON::Object::Entries entries {
      {"jsonrpc", JSON::String(this->jsonrpc)},
      {"id", this->id}
    };

    if (this->isError()) {
      entries.insert({"error", this->error->toJSON()});
    } else {
      entries.insert({
        "result",
        this->result.isNull()
          ? JSON::Object(JSON::Object::Entries{})
          : this->result
      });
    }

    return JSON::Object(entries);
  }

  uint64_t nextRequestNumericId() {
    auto value = gRequestCounter.fetch_add(1, std::memory_order_relaxed);
    if (value == 0) {
      value = gRequestCounter.fetch_add(1, std::memory_order_relaxed);
    }
    return value;
  }

  Request::Id nextRequestId() {
    const auto value = nextRequestNumericId();
    return JSON::String(std::to_string(value));
  }
}
