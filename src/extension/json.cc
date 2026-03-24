#include <exception>
#include <string.h>
#include "extension.hh"

const oapi_json_type_t oapi_json_typeof (const oapi_json_any_t* json) {
  if (json->isNull()) return OAPI_JSON_TYPE_NULL;
  if (json->isObject()) return OAPI_JSON_TYPE_OBJECT;
  if (json->isArray()) return OAPI_JSON_TYPE_ARRAY;
  if (json->isBoolean()) return OAPI_JSON_TYPE_BOOLEAN;
  if (json->isNumber()) return OAPI_JSON_TYPE_NUMBER;
  if (json->isString()) return OAPI_JSON_TYPE_STRING;
  if (json->isEmpty()) return OAPI_JSON_TYPE_EMPTY;
  if (json->isRaw()) return OAPI_JSON_TYPE_RAW;
  return OAPI_JSON_TYPE_ANY;
}

oapi_json_object_t* oapi_json_object_create (oapi_context_t* ctx) {
  return ctx->memory.alloc<oapi_json_object_t>(ctx);
}

oapi_json_array_t* oapi_json_array_create (oapi_context_t* ctx) {
  return ctx->memory.alloc<oapi_json_array_t>(ctx);
}

oapi_json_string_t* oapi_json_string_create (
  oapi_context_t* ctx,
  const char* string
) {
  return ctx->memory.alloc<oapi_json_string_t>(ctx, string);
}

oapi_json_boolean_t* oapi_json_boolean_create (
  oapi_context_t* ctx,
  bool boolean
) {
  return ctx->memory.alloc<oapi_json_boolean_t>(ctx, boolean);
}

oapi_json_number_t* oapi_json_number_create (
  oapi_context_t* ctx,
  const double number
 ) {
  return ctx->memory.alloc<oapi_json_number_t>(ctx, number);
}

oapi_json_any_t* oapi_json_raw_from (
  oapi_context_t* ctx,
  const char* source
) {
  return reinterpret_cast<oapi_json_any_t*>(
    ctx->memory.alloc<oapi_json_raw_t>(ctx, source)
  );
}

oapi_json_any_t* oapi_json_parse (
  oapi_context_t* ctx,
  const char* source
) {
  if (ctx != nullptr) {
    oapi_context_error_reset(ctx);
  }

  if (ctx == nullptr || source == nullptr) {
    if (ctx != nullptr) {
      oapi_context_error_set_name(ctx, "JSONParseError");
      oapi_context_error_set_message(ctx, "Invalid JSON source");
    }
    return nullptr;
  }

  try {
    auto parsed = oro::runtime::JSON::parse(source);

    if (parsed.isNull()) {
      return reinterpret_cast<oapi_json_any_t*>(
        ctx->memory.alloc<oapi_json_null_t>(ctx)
      );
    }

    if (parsed.isObject()) {
      auto object = ctx->memory.alloc<oapi_json_object_t>(ctx);
      object->data = parsed.as<oro::runtime::JSON::Object>().value();
      return reinterpret_cast<oapi_json_any_t*>(object);
    }

    if (parsed.isArray()) {
      auto array = ctx->memory.alloc<oapi_json_array_t>(ctx);
      array->data = parsed.as<oro::runtime::JSON::Array>().value();
      return reinterpret_cast<oapi_json_any_t*>(array);
    }

    if (parsed.isString()) {
      const auto value = parsed.as<oro::runtime::JSON::String>().value();
      return reinterpret_cast<oapi_json_any_t*>(
        ctx->memory.alloc<oapi_json_string_t>(ctx, value.c_str())
      );
    }

    if (parsed.isBoolean()) {
      return reinterpret_cast<oapi_json_any_t*>(
        ctx->memory.alloc<oapi_json_boolean_t>(
          ctx,
          parsed.as<oro::runtime::JSON::Boolean>().value()
        )
      );
    }

    if (parsed.isNumber()) {
      return reinterpret_cast<oapi_json_any_t*>(
        ctx->memory.alloc<oapi_json_number_t>(
          ctx,
          parsed.as<oro::runtime::JSON::Number>().value()
        )
      );
    }

    if (parsed.isRaw()) {
      const auto value = parsed.as<oro::runtime::JSON::Raw>().value();
      return reinterpret_cast<oapi_json_any_t*>(
        ctx->memory.alloc<oapi_json_raw_t>(ctx, value.c_str())
      );
    }

    auto any = ctx->memory.alloc<oapi_json_any_t>(ctx);
    *static_cast<oro::runtime::JSON::Any*>(any) = parsed;
    return any;
  } catch (const oro::runtime::JSON::Error& error) {
    oapi_context_error_set_code(ctx, error.code);
    oapi_context_error_set_name(
      ctx,
      error.name.size() > 0 ? error.name.c_str() : "JSONParseError"
    );
    oapi_context_error_set_message(
      ctx,
      error.message.size() > 0 ? error.message.c_str() : error.what()
    );
    if (error.location.size() > 0) {
      oapi_context_error_set_location(ctx, error.location.c_str());
    }
  } catch (const std::exception& error) {
    oapi_context_error_set_name(ctx, "JSONParseError");
    oapi_context_error_set_message(ctx, error.what());
  }

  return nullptr;
}

const char * oapi_json_stringify_value (const oapi_json_any_t* json) {
  oro::runtime::String string;
  switch (oapi_json_typeof(json)) {
    case OAPI_JSON_TYPE_NULL:
      string = "null";
      break;

    case OAPI_JSON_TYPE_OBJECT:
      string = reinterpret_cast<const oro::runtime::JSON::Object*>(json)->str();
      break;

    case OAPI_JSON_TYPE_ARRAY:
      string = reinterpret_cast<const oro::runtime::JSON::Array*>(json)->str();
      break;
    case OAPI_JSON_TYPE_BOOLEAN:
      string = reinterpret_cast<const oro::runtime::JSON::Boolean*>(json)->str();
      break;
    case OAPI_JSON_TYPE_NUMBER:
      string = reinterpret_cast<const oro::runtime::JSON::Number*>(json)->str();
      break;
    case OAPI_JSON_TYPE_STRING:
      string = reinterpret_cast<const oro::runtime::JSON::String*>(json)->str();
      break;
    case OAPI_JSON_TYPE_RAW:
      string = reinterpret_cast<const oro::runtime::JSON::Raw*>(json)->str();
      break;

    case OAPI_JSON_TYPE_EMPTY:
    case OAPI_JSON_TYPE_ANY:
      break;
  }

  size_t length = string.size();

  if (length > 0) {
    auto bytes = json->context->memory.alloc<char>(length + 1);
    if (bytes != nullptr) {
    #if defined(_WIN32)
      strncat_s(bytes, length + 1, string.c_str(), length);
    #else
      strncat(bytes, string.c_str(), length);
    #endif
    }

    return bytes;
  }

  return nullptr;
}

void oapi_json_object_set_value (
  oapi_json_object_t* json,
  const char* key,
  oapi_json_any_t* any
) {
  if (json == nullptr || key == nullptr) return;

  if (any == nullptr) {
    json->set(key, oro::runtime::JSON::Null());
  } else if (any->type > oro::runtime::JSON::Type::Any) {
    if (any->isObject()) {
      auto object = reinterpret_cast<oro::runtime::JSON::Object*>(any);
      json->set(key, oro::runtime::JSON::Object(object->data));
    } else if (any->isArray()) {
      auto array = reinterpret_cast<oro::runtime::JSON::Array*>(any);
      json->set(key, oro::runtime::JSON::Array(array->data));
    } else if (any->isString()) {
      auto string = reinterpret_cast<oro::runtime::JSON::String*>(any);
      json->set(key, oro::runtime::JSON::String(string->data));
    } else if (any->isBoolean()) {
      auto boolean = reinterpret_cast<oro::runtime::JSON::Boolean*>(any);
      json->set(key, oro::runtime::JSON::Boolean(boolean->data));
    } else if (any->isNumber()) {
      auto number = reinterpret_cast<oro::runtime::JSON::Number*>(any);
      json->set(key, oro::runtime::JSON::Number(number->data));
    } else if (any->isRaw()) {
      auto raw = reinterpret_cast<oro::runtime::JSON::Raw*>(any);
      json->set(key, oro::runtime::JSON::Raw(raw->data));
    }
  }
}

oapi_json_any_t* oapi_json_object_get (
  const oapi_json_object_t* json,
  const char* key
) {
  if (json->has(key)) {
    auto pointer = json->data.at(key).data.get();
    return reinterpret_cast<oapi_json_any_t*>(pointer);
  }

  return nullptr;
}

void oapi_json_array_set_value (
  oapi_json_array_t* json,
  unsigned int index,
  oapi_json_any_t* any
) {
  if (json == nullptr || any == nullptr) return;

  if (any == nullptr) {
    json->set(index, oro::runtime::JSON::Null());
  } else if (any->type > oro::runtime::JSON::Type::Any) {
    if (any->isObject()) {
      auto object = reinterpret_cast<oro::runtime::JSON::Object*>(any);
      json->set(index, oro::runtime::JSON::Object(object->data));
    } else if (any->isArray()) {
      auto array = reinterpret_cast<oro::runtime::JSON::Array*>(any);
      json->set(index, oro::runtime::JSON::Array(array->data));
    } else if (any->isString()) {
      auto string = reinterpret_cast<oro::runtime::JSON::String*>(any);
      json->set(index, oro::runtime::JSON::String(string->data));
    } else if (any->isBoolean()) {
      auto boolean = reinterpret_cast<oro::runtime::JSON::Boolean*>(any);
      json->set(index, oro::runtime::JSON::Boolean(boolean->data));
    } else if (any->isNumber()) {
      auto number = reinterpret_cast<oro::runtime::JSON::Number*>(any);
      json->set(index, oro::runtime::JSON::Number(number->data));
    } else if (any->isRaw()) {
      auto raw = reinterpret_cast<oro::runtime::JSON::Raw*>(any);
      json->set(index, oro::runtime::JSON::Raw(raw->data));
    }
  }
}

void oapi_json_array_push_value (
  oapi_json_array_t* json,
  oapi_json_any_t* any
) {
  if (json == nullptr || any == nullptr) return;

  if (any == nullptr) {
    json->push(oro::runtime::JSON::Null());
  } else if (any->type > oro::runtime::JSON::Type::Any) {
    if (any->isObject()) {
      auto object = reinterpret_cast<oro::runtime::JSON::Object*>(any);
      json->push(oro::runtime::JSON::Object(object->data));
    } else if (any->isArray()) {
      auto array = reinterpret_cast<oro::runtime::JSON::Array*>(any);
      json->push(oro::runtime::JSON::Array(array->data));
    } else if (any->isString()) {
      auto string = reinterpret_cast<oro::runtime::JSON::String*>(any);
      json->push(oro::runtime::JSON::String(string->data));
    } else if (any->isBoolean()) {
      auto boolean = reinterpret_cast<oro::runtime::JSON::Boolean*>(any);
      json->push(oro::runtime::JSON::Boolean(boolean->data));
    } else if (any->isNumber()) {
      auto number = reinterpret_cast<oro::runtime::JSON::Number*>(any);
      json->push(oro::runtime::JSON::Number(number->data));
    } else if (any->isRaw()) {
      auto raw = reinterpret_cast<oro::runtime::JSON::Raw*>(any);
      json->push(oro::runtime::JSON::Raw(raw->data));
    }
  }
}

oapi_json_any_t* oapi_json_array_get (
  const oapi_json_array_t* json,
  unsigned int index
) {
  if (json->has(index)) {
    auto pointer = json->data.at(index).data.get();
    return reinterpret_cast<oapi_json_any_t*>(pointer);
  }

  return nullptr;
}

oapi_json_any_t* oapi_json_array_pop (
  oapi_json_array_t* json
) {
  auto pointer = json->pop().data.get();
  return reinterpret_cast<oapi_json_any_t*>(pointer);
}
