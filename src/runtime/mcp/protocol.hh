#ifndef ORO_RUNTIME_MCP_PROTOCOL_H
#define ORO_RUNTIME_MCP_PROTOCOL_H

#include "../core.hh"
#include "../json.hh"
#include "../string.hh"

#include <optional>

namespace oro::runtime::mcp {
  using types::Atomic;
  using types::ConditionVariableAny;
  using types::Function;
  using types::Map;
  using types::Mutex;
  using types::SharedPointer;
  using types::String;
  using types::Vector;

  inline constexpr const char* kProtocolVersion = "2026-07-28";
  inline constexpr const char* kMcp2025ProtocolVersion = "2025-11-25";
  inline constexpr const char* kEarlierMcp2025ProtocolVersion = "2025-06-18";
  inline constexpr const char* kJsonRpcVersion = "2.0";

  bool isModernProtocolVersion(const String& version);
  bool isMcp2025ProtocolVersion(const String& version);
  bool isSupportedProtocolVersion(const String& version);
  Vector<String> supportedProtocolVersions();

  enum class ErrorCode : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    HeaderMismatch = -32020,
    MissingRequiredClientCapability = -32021,
    UnsupportedProtocolVersion = -32022,
    ServerErrorStart = -32000,
    ServerErrorEnd = -32099
  };

  struct Error {
    ErrorCode code = ErrorCode::InternalError;
    String message;
    JSON::Any data = JSON::Null();

    Error () = default;
    Error(ErrorCode code, const String& message);
    Error(ErrorCode code, const String& message, const JSON::Any& data);

    JSON::Object toJSON() const;
  };

  struct Request {
    using Id = JSON::Any;

    String method;
    JSON::Any params = JSON::Null();
    Id id = JSON::Null();
    String jsonrpc = kJsonRpcVersion;

    bool isNotification() const;

    static Request create(const String& method, const JSON::Any& params = JSON::Null());
    static Request createWithId(const Id& id, const String& method, const JSON::Any& params = JSON::Null());
    static Request createNotification(const String& method, const JSON::Any& params = JSON::Null());

    JSON::Object toJSON() const;
  };

  struct Response {
    Request::Id id = JSON::Null();
    JSON::Any result = JSON::Null();
    std::optional<Error> error;
    String jsonrpc = kJsonRpcVersion;

    bool isError() const;

    static Response success(const Request::Id& id, const JSON::Any& result = JSON::Null());
    static Response failure(const Request::Id& id, const Error& error);

    JSON::Object toJSON() const;
  };

  uint64_t nextRequestNumericId();
  Request::Id nextRequestId();
}

#endif
