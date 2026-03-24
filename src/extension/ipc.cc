#include "extension.hh"

bool oapi_ipc_router_map (
  oapi_context_t* ctx,
  const char* name,
  oapi_ipc_router_message_callback_t callback,
  const void* data
) {
  if (
    ctx == nullptr ||
    ctx->router == nullptr ||
    ctx->state > oro::extension::Extension::Context::State::Init
  ) {
    return false;
  }

  if (!ctx->isAllowed("ipc_router_map")) {
    oapi_debug(ctx, "'ipc_router_map' is not allowed.");
    return false;
  }

  ctx->router->map(name, [ctx, data, callback](
    auto message,
    auto router,
    auto reply
  ) mutable {
    auto context = oapi_context_create(ctx, true);
    if (context == nullptr) {
      return;
    }

    context->data = data;
    context->internal = new oro::runtime::ipc::Router::ReplyCallback(reply);
    callback(
      context,
      (oapi_ipc_message_t*) &message,
      reinterpret_cast<const oapi_ipc_router_t*>(&router)
    );
  });

  return true;
}

bool oapi_ipc_router_unmap (oapi_context_t* ctx, const char* name) {
  if (ctx == nullptr || ctx->router == nullptr) {
    return false;
  }

  if (!ctx->isAllowed("ipc_router_unmap")) {
    oapi_debug(ctx, "'ipc_router_unmap' is not allowed.");
    return false;
  }

  ctx->router->unmap(name);

  return true;
}

uint64_t oapi_ipc_router_listen (
  oapi_context_t* ctx,
  const char* name,
  oapi_ipc_router_message_callback_t callback,
  const void* data
) {
  if (ctx == nullptr || ctx->router == nullptr || name == nullptr) {
    return 0;
  }

  if (!ctx->isAllowed("ipc_router_listen")) {
    oapi_debug(ctx, "'ipc_router_listen' is not allowed.");
    return 0;
  }

  return ctx->router->listen(name, [data, callback](
    auto message,
    auto router,
    auto reply
  ) mutable {
    oapi_context_t context;
    context.router = router;
    context.data = data;
    callback(
      &context,
      (oapi_ipc_message_t*)(&message),
      reinterpret_cast<const oapi_ipc_router_t*>(&router)
    );
  });
}

bool oapi_ipc_router_unlisten (
  oapi_context_t* ctx,
  const char* name,
  uint64_t token
) {
  if (ctx == nullptr || ctx->router == nullptr || name == nullptr) {
    return false;
  }

  if (!ctx->isAllowed("ipc_router_unlisten")) {
    oapi_debug(ctx, "'ipc_router_unlisten' is not allowed.");
    return false;
  }

  return ctx->router->unlisten(name, token);
}

bool oapi_ipc_reply_with_error (oapi_ipc_result_t* result, const char* error) {
  oapi_context* context = oapi_ipc_result_get_context(result);
  oapi_json_string_t* errorJson = oapi_json_string_create(context, error);
  oapi_json_object_t* errorObject = oapi_json_object_create(context);
  oapi_json_object_set(errorObject, "message", errorJson);
  oapi_ipc_result_set_json_error(result, oapi_json_any(errorObject));
  return oapi_ipc_reply(result);
}

bool oapi_ipc_reply (const oapi_ipc_result_t* result) {
  if (result == nullptr) return false;
  if (result->context == nullptr) return false;

  if (!result->context->isAllowed("ipc_router_reply")) {
    oapi_debug(result->context, "'ipc_router_reply' is not allowed.");
    return 0;
  }

  auto success = false;
  auto context = result->context;
  auto internal = context->internal;
  auto fn = reinterpret_cast<oro::runtime::ipc::Router::ReplyCallback*>(internal);

  if (fn != nullptr) {
    (*fn)(*reinterpret_cast<const oro::runtime::ipc::Result*>(result));
    success = true;
    delete fn;
    context->internal = nullptr;
  }

  // if retained, then then caller must eventually call `oapi_context_release()`
  if (context->release()) {
    delete context;
  }

  return success;
}

bool oapi_ipc_set_cancellation_handler (
  oapi_ipc_result_t* result,
  void (*handler)(void*),
  void* data
) {
  if (result == nullptr || result->message.cancel == nullptr) {
    return false;
  }

  if (result->context != nullptr && !result->context->isAllowed("ipc_router_set_cancellation_handler")) {
    oapi_debug(result->context, "'ipc_router_set_cancellation_handler' is not allowed.");
    return false;
  }

  auto message = result->message;
  *message.cancel = {
    .handler = handler,
    .data = data
  };
  return true;
}

bool oapi_ipc_send_chunk (
  oapi_ipc_result_t* result,
  const unsigned char* chunk,
  size_t chunk_size,
  bool finished
) {
  if (result == nullptr) {
    return false;
  }
  if (!result->message.isHTTP) {
    std::string error =
        "IPC method '" + result->message.name + "' must be invoked with HTTP";
    return oapi_ipc_reply_with_error(result, error.c_str());
  }
  auto send_chunk_ptr = result->queuedResponse.chunkStreamCallback;
  if (send_chunk_ptr == nullptr) {
    debug(
        "Cannot use 'oapi_ipc_send_chunk' before setting the \"Transfer-Encoding\""
        " header to \"chunked\"");
    return false;
  }
  bool success = (*send_chunk_ptr)(chunk, chunk_size, finished);
  if (finished) {
    auto context = result->context;
    if (context->release()) {
      delete context;
    }
  }
  return success;
}

bool oapi_ipc_send_event (
  oapi_ipc_result_t* result,
  const char* name,
  const unsigned char* data,
  bool finished
) {
  if (result == nullptr) {
    return false;
  }
  if (!result->message.isHTTP) {
    std::string error =
        "IPC method '" + result->message.name + "' must be invoked with HTTP";
    return oapi_ipc_reply_with_error(result, error.c_str());
  }
  auto send_event_ptr = result->queuedResponse.eventStreamCallback;
  if (send_event_ptr == nullptr) {
    debug(
        "Cannot use 'oapi_ipc_send_event' before setting the \"Content-Type\""
        " header to \"text/event-stream\"");
    return false;
  }
  bool success = (*send_event_ptr)(name, data, finished);
  if (finished) {
    auto context = result->context;
    if (context->release()) {
      delete context;
    }
  }
  return success;
}

bool oapi_ipc_send_bytes (
  oapi_context_t* ctx,
  oapi_ipc_message_t* message,
  unsigned int size,
  unsigned char* bytes,
  const char* headers
) {
  if (!ctx || !ctx->router || !bytes || !size) {
    return false;
  }

  auto queuedResponse = oro::runtime::QueuedResponse {
    .id = 0,
    .ttl = 0,
    .body = nullptr,
    .length = size,
    .headers = oro::runtime::String(headers ? headers : "")
  };

  if (bytes != nullptr && size > 0) {
    queuedResponse.body = std::make_shared<unsigned char[]>(size);
    memcpy(queuedResponse.body.get(), bytes, size);
  }

  if (message) {
    auto result = oro::runtime::ipc::Result(
      message->seq,
      *message,
      oro::runtime::JSON::null,
      queuedResponse
    );

    return ctx->router->bridge.send(result.seq, result.str(), result.queuedResponse);
  }

  auto result = oro::runtime::ipc::Result(oro::runtime::JSON::null);
  return ctx->router->bridge.send(result.seq, result.str(), queuedResponse);
}

bool oapi_ipc_send_bytes_with_result (
  oapi_context_t* ctx,
  oapi_ipc_result_t* result,
  unsigned int size,
  unsigned char* bytes,
  const char* headers
) {
  if (!ctx || !ctx->router || !bytes || !size || !result) {
    return false;
  }

  auto queuedResponse = oro::runtime::QueuedResponse {
    .id = 0,
    .ttl = 0,
    .body = nullptr,
    .length = size,
    .headers = oro::runtime::String(headers ? headers : "")
  };

  if (bytes != nullptr && size > 0) {
    queuedResponse.body = std::make_shared<unsigned char[]>(size);
    memcpy(queuedResponse.body.get(), bytes, size);
  }

  return ctx->router->bridge.send(result->seq, result->str(), queuedResponse);
}

bool oapi_ipc_send_json (
  oapi_context_t* ctx,
  oapi_ipc_message_t* message,
  oapi_json_any_t* json
) {
  oro::runtime::JSON::Any value = nullptr;

  if (!ctx || !ctx->router || !json) {
    return false;
  }

  if (json->type > oro::runtime::JSON::Type::Any) {
    if (json->isObject()) {
      auto object = reinterpret_cast<const oro::runtime::JSON::Object*>(json);
      value = oro::runtime::JSON::Object(object->data);
    } else if (json->isArray()) {
      auto array = reinterpret_cast<const oro::runtime::JSON::Array*>(json);
      value = oro::runtime::JSON::Array(array->data);
    } else if (json->isString()) {
      auto string = reinterpret_cast<const oro::runtime::JSON::String*>(json);
      value = oro::runtime::JSON::String(string->data);
    } else if (json->isBoolean()) {
      auto boolean = reinterpret_cast<const oro::runtime::JSON::Boolean*>(json);
      value = oro::runtime::JSON::Boolean(boolean->data);
    } else if (json->isNumber()) {
      auto number = reinterpret_cast<const oro::runtime::JSON::Number*>(json);
      value = oro::runtime::JSON::Number(number->data);
    } else if (json->isRaw()) {
      auto raw = reinterpret_cast<const oro::runtime::JSON::Raw*>(json);
      value = oro::runtime::JSON::Raw(raw->data);
    }
  } else {
    value = nullptr;
  }

  if (message) {
    auto result = oro::runtime::ipc::Result(
      message->seq,
      *message,
      value
    );

    return ctx->router->bridge.send(result.seq, result.str(), result.queuedResponse);
  }

  auto result = oro::runtime::ipc::Result(value);
  return ctx->router->bridge.send(result.seq, result.str(), result.queuedResponse);
}

bool oapi_ipc_send_json_with_result (
  oapi_context_t* ctx,
  oapi_ipc_result_t* result,
  oapi_json_any_t* json
) {
  oro::runtime::JSON::Any value = nullptr;

  if (!ctx || !ctx->router || !result || !json) {
    return false;
  }

  if (json->type > oro::runtime::JSON::Type::Any) {
    if (json->isObject()) {
      auto object = reinterpret_cast<const oro::runtime::JSON::Object*>(json);
      value = oro::runtime::JSON::Object(object->data);
    } else if (json->isArray()) {
      auto array = reinterpret_cast<const oro::runtime::JSON::Array*>(json);
      value = oro::runtime::JSON::Array(array->data);
    } else if (json->isString()) {
      auto string = reinterpret_cast<const oro::runtime::JSON::String*>(json);
      value = oro::runtime::JSON::String(string->data);
    } else if (json->isBoolean()) {
      auto boolean = reinterpret_cast<const oro::runtime::JSON::Boolean*>(json);
      value = oro::runtime::JSON::Boolean(boolean->data);
    } else if (json->isNumber()) {
      auto number = reinterpret_cast<const oro::runtime::JSON::Number*>(json);
      value = oro::runtime::JSON::Number(number->data);
    } else if (json->isRaw()) {
      auto raw = reinterpret_cast<const oro::runtime::JSON::Raw*>(json);
      value = oro::runtime::JSON::Raw(raw->data);
    }
  } else {
    value = nullptr;
  }

  auto res = oro::runtime::ipc::Result(result->seq, result->message, value);
  return ctx->router->bridge.send(res.seq, res.str(), res.queuedResponse);
}

bool oapi_ipc_emit (
  oapi_context_t* ctx,
  const char* name,
  const char* data
) {
  return ctx && ctx->router ? ctx->router->bridge.emit(name, oro::runtime::String(data)) : false;
}

bool oapi_ipc_invoke (
  oapi_context_t* ctx,
  const char* url,
  unsigned int size,
  const unsigned char* bytes,
  oapi_ipc_router_result_callback_t callback
) {
  if (ctx == nullptr || ctx->router == nullptr) return false;
  auto uri = oro::runtime::String(url);

  if (!uri.starts_with("ipc://")) {
    uri = "ipc://" + uri;
  }

  oro::runtime::SharedPointer<unsigned char[]> data = nullptr;

  if (bytes != nullptr && size > 0) {
    data.reset(new unsigned char[size]{0});
    memcpy(data.get(), bytes, size);
  }

  return ctx->router->invoke(uri, data, size, [ctx, callback](auto result) {
    callback(
      reinterpret_cast<const oapi_ipc_result_t*>(&result),
      reinterpret_cast<const oapi_ipc_router_t*>(&ctx->router)
    );
  });
}

oapi_ipc_result_t* oapi_ipc_result_from_json (
  oapi_context_t* ctx,
  oapi_ipc_message_t* message,
  const oapi_json_any_t* json
) {
  if (ctx == nullptr) return nullptr;
  auto result = ctx->memory.alloc<oapi_ipc_result_t>(ctx);
  result->context = ctx;

  if (json) oapi_ipc_result_set_json(result, json);
  if (message) {
    oapi_ipc_result_set_seq(result, message->seq.c_str());
    oapi_ipc_result_set_message(result, message);
  }

  return result;
}

oapi_ipc_result_t* oapi_ipc_result_create (
  oapi_context_t* ctx,
  oapi_ipc_message_t* message
) {
  if (ctx == nullptr) return nullptr;
  auto result = ctx->memory.alloc<oapi_ipc_result_t>(ctx);
  if (message) {
    oapi_ipc_result_set_seq(result, message->seq.c_str());
    oapi_ipc_result_set_message(result, message);
  }
  return result;
}

int oapi_ipc_message_get_index (const oapi_ipc_message_t* message) {
  return message ? message->index : -1;
}

const char* oapi_ipc_message_get_value (const oapi_ipc_message_t* message) {
  if (message == nullptr || message->value.size() == 0) return nullptr;
  return message->value.c_str();
}

const char* oapi_ipc_message_get_name (const oapi_ipc_message_t* message) {
  if (message == nullptr || message->name.size() == 0) return nullptr;
  return message->name.c_str();
}

const char* oapi_ipc_message_get_seq (const oapi_ipc_message_t* message) {
  if (message == nullptr || message->seq.size() == 0) return nullptr;
  return message->seq.c_str();
}

const char* oapi_ipc_message_get_uri (const oapi_ipc_message_t* message) {
  if (message == nullptr) return nullptr;
  if (message->href.size() == 0) return nullptr;
  return message->href.c_str();
}

const char* oapi_ipc_message_get (
  const oapi_ipc_message_t* message,
  const char* key
) {
  if (!message || !key || !message->contains(key)) return nullptr;
  const auto& value = message->at(key);
  if (value.size() == 0) return nullptr;
  return value.c_str();
}

oapi_ipc_message_t* oapi_ipc_message_clone (
  oapi_context_t* context,
  const oapi_ipc_message_t* message
) {
  if (message == nullptr) return nullptr;
  if (context == nullptr) return nullptr;

  return context->memory.alloc<oapi_ipc_message_t>(*message);
}

oapi_ipc_result_t* oapi_ipc_result_clone (
  oapi_context_t* context,
  const oapi_ipc_result_t* result
) {
  if (result == nullptr) return nullptr;
  if (context == nullptr) return nullptr;

  return context->memory.alloc<oapi_ipc_result_t>(context, *result);
}

const unsigned char* oapi_ipc_message_get_bytes (
  const oapi_ipc_message_t* message
) {
  if (!message) return nullptr;
  return message->buffer.data();
}

unsigned int oapi_ipc_message_get_bytes_size (
  const oapi_ipc_message_t* message
) {
  if (!message || !message->buffer.data()) return 0;
  return static_cast<unsigned int>(message->buffer.size());
}

void oapi_ipc_result_set_seq (oapi_ipc_result_t* result, const char* seq) {
  if (result && seq) {
    result->seq = seq;
  }
}

const char* oapi_ipc_result_get_seq (const oapi_ipc_result_t* result) {
  return result ? result->seq.c_str() : nullptr;
}

void oapi_ipc_result_set_message (
  oapi_ipc_result_t* result,
  oapi_ipc_message_t* message
) {
  if (result && message) {
    result->message = *message;
    result->source = message->name;
    result->seq = message->seq;
  }
}

const oapi_ipc_message_t* oapi_ipc_result_get_message (
  const oapi_ipc_result_t* result
) {
  return result
    ? reinterpret_cast<const oapi_ipc_message_t*>(&result->message)
    : nullptr;
}

oapi_context_t* oapi_ipc_result_get_context (
  const oapi_ipc_result_t* result
) {
  return result ? result->context : nullptr;
}

void oapi_ipc_result_set_json (
  oapi_ipc_result_t* result,
  const oapi_json_any_t* json
) {
  if (result == nullptr) return;
  if (json == nullptr) {
    result->value = nullptr;
  } else if (json->type > oro::runtime::JSON::Type::Any) {
    if (json->isObject()) {
      auto object = reinterpret_cast<const oro::runtime::JSON::Object*>(json);
      result->value = oro::runtime::JSON::Object(object->data);
    } else if (json->isArray()) {
      auto array = reinterpret_cast<const oro::runtime::JSON::Array*>(json);
      result->value = oro::runtime::JSON::Array(array->data);
    } else if (json->isString()) {
      auto string = reinterpret_cast<const oro::runtime::JSON::String*>(json);
      result->value = oro::runtime::JSON::String(string->data);
    } else if (json->isBoolean()) {
      auto boolean = reinterpret_cast<const oro::runtime::JSON::Boolean*>(json);
      result->value = oro::runtime::JSON::Boolean(boolean->data);
    } else if (json->isNumber()) {
      auto number = reinterpret_cast<const oro::runtime::JSON::Number*>(json);
      result->value = oro::runtime::JSON::Number(number->data);
    } else if (json->isRaw()) {
      auto raw = reinterpret_cast<const oro::runtime::JSON::Raw*>(json);
      result->value = oro::runtime::JSON::Raw(raw->data);
    }
  }
}

const oapi_json_any_t* oapi_ipc_result_get_json (
  const oapi_ipc_result_t* result
) {
  return result
    ? reinterpret_cast<const oapi_json_any_t*>(&result->value)
    : nullptr;
}

void oapi_ipc_result_set_json_data (
  oapi_ipc_result_t* result,
  const oapi_json_any_t* json
) {
  if (result == nullptr) return;
  if (json == nullptr) {
    result->data = nullptr;
  } else if (json->type > oro::runtime::JSON::Type::Any) {
    if (json->isObject()) {
      auto object = reinterpret_cast<const oro::runtime::JSON::Object*>(json);
      result->data = oro::runtime::JSON::Object(object->data);
    } else if (json->isArray()) {
      auto array = reinterpret_cast<const oro::runtime::JSON::Array*>(json);
      result->data = oro::runtime::JSON::Array(array->data);
    } else if (json->isString()) {
      auto string = reinterpret_cast<const oro::runtime::JSON::String*>(json);
      result->data = oro::runtime::JSON::String(string->data);
    } else if (json->isBoolean()) {
      auto boolean = reinterpret_cast<const oro::runtime::JSON::Boolean*>(json);
      result->data = oro::runtime::JSON::Boolean(boolean->data);
    } else if (json->isNumber()) {
      auto number = reinterpret_cast<const oro::runtime::JSON::Number*>(json);
      result->data = oro::runtime::JSON::Number(number->data);
    } else if (json->isRaw()) {
      auto raw = reinterpret_cast<const oro::runtime::JSON::Raw*>(json);
      result->data = oro::runtime::JSON::Raw(raw->data);
    }
  }
}

const oapi_json_any_t* oapi_ipc_result_get_json_data (
  const oapi_ipc_result_t* result
) {
  return result
    ? reinterpret_cast<const oapi_json_any_t*>(&result->data)
    : nullptr;
}

void oapi_ipc_result_set_json_error (
  oapi_ipc_result_t* result,
  const oapi_json_any_t* json
) {
  if (result == nullptr) return;
  if (json == nullptr) {
    result->err = nullptr;
  } else if (json->type > oro::runtime::JSON::Type::Any) {
    if (json->isObject()) {
      auto object = reinterpret_cast<const oro::runtime::JSON::Object*>(json);
      result->err = oro::runtime::JSON::Object(object->data);
    } else if (json->isArray()) {
      auto array = reinterpret_cast<const oro::runtime::JSON::Array*>(json);
      result->err = oro::runtime::JSON::Array(array->data);
    } else if (json->isString()) {
      auto string = reinterpret_cast<const oro::runtime::JSON::String*>(json);
      result->err = oro::runtime::JSON::String(string->data);
    } else if (json->isBoolean()) {
      auto boolean = reinterpret_cast<const oro::runtime::JSON::Boolean*>(json);
      result->err = oro::runtime::JSON::Boolean(boolean->data);
    } else if (json->isNumber()) {
      auto number = reinterpret_cast<const oro::runtime::JSON::Number*>(json);
      result->err = oro::runtime::JSON::Number(number->data);
    } else if (json->isRaw()) {
      auto raw = reinterpret_cast<const oro::runtime::JSON::Raw*>(json);
      result->err  = oro::runtime::JSON::Raw(raw->data);
    }
  }
}

const oapi_json_any_t* oapi_ipc_result_get_json_error (
  const oapi_ipc_result_t* result
) {
  return result
    ? reinterpret_cast<const oapi_json_any_t*>(&result->err)
    : nullptr;
}

void oapi_ipc_result_set_bytes (
  oapi_ipc_result_t* result,
  unsigned int size,
  unsigned char* bytes
) {
  if (result && size && bytes) {
    result->queuedResponse.length = size;
    result->queuedResponse.body = std::make_shared<unsigned char[]>(size);
    memcpy(result->queuedResponse.body.get(), bytes, size);
  }
}

unsigned char* oapi_ipc_result_get_bytes (
  const oapi_ipc_result_t* result
) {
  return result
    ? reinterpret_cast<unsigned char*>(result->queuedResponse.body.get())
    : nullptr;
}

size_t oapi_ipc_result_get_bytes_size (
  const oapi_ipc_result_t* result
) {
  return result ? result->queuedResponse.length : 0;
}

void oapi_ipc_result_set_header (
  oapi_ipc_result_t* result,
  const char* name,
  const char* value
) {
  if (result && name && value) {
    result->headers.set(name, value);

    if (result->headers.get("content-type") == "text/event-stream") {
      result->context->retain();
      result->queuedResponse = oro::runtime::QueuedResponse();
      result->queuedResponse.eventStreamCallback = std::make_shared<oro::runtime::QueuedResponse::EventStreamCallback>(
        [result](const char* name, const unsigned char* data, bool finished) {
          return false;
        }
      );
    } else if (result->headers.get("transfer-encoding") == "chunked") {
      result->context->retain();
      result->queuedResponse = oro::runtime::QueuedResponse();
      result->queuedResponse.chunkStreamCallback = std::make_shared<oro::runtime::QueuedResponse::ChunkStreamCallback>(
        [result](const unsigned char* chunk, size_t chunk_size, bool finished) {
          return false;
        }
      );
    }
  }
}

const char* oapi_ipc_result_get_header (
  const oapi_ipc_result_t* result,
  const char* name
) {
  if (result && result->headers.has(name)) {
    return result->headers.get(name).value.string.c_str();
  }

  return nullptr;
}
