#ifndef ORO_RUNTIME_MCP_PROTOCOL_H
#define ORO_RUNTIME_MCP_PROTOCOL_H

#include "../core.hh"
#include "../json.hh"
#include "../string.hh"

#include <optional>

namespace oro::runtime::mcp {
  inline constexpr const char* kProtocolVersion = "2025-06-18";
  inline constexpr const char* kJsonRpcVersion = "2.0";

  enum class ErrorCode : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
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
