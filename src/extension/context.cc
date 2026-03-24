#include "extension.hh"

oapi_context_t* oapi_context_create (
  oapi_context_t* parent,
  bool retained
) {
  if (parent && !parent->isAllowed("context_create")) {
    oapi_debug(parent, "'context_create' is not allowed.");
    return nullptr;
  }

  retained = retained || parent == nullptr;
  auto context = retained
    ? new oapi_context_t(parent)
    : parent->memory.alloc<oapi_context_t>(parent, parent);

  if (retained || parent == nullptr) {
    context->retain();
  }

  return context;
}

bool oapi_context_set_data (
  oapi_context_t* ctx,
  const void* data
) {
  if (ctx == nullptr) return false;
  ctx->data = data;
  return true;
}

bool oapi_context_dispatch (
  oapi_context_t* ctx,
  const void* data,
  oapi_context_dispatch_callback callback
) {
  if (ctx == nullptr) return false;
  if (ctx->router == nullptr) return false;
  if (callback == nullptr) return false;

  if (!ctx->isAllowed("context_dispatch")) {
    oapi_debug(ctx, "'context_dispatch' is not allowed.");
    return false;
  }

  return ctx->router->bridge.dispatch([=]() {
    callback(ctx, data);
  });
}

void oapi_context_retain (oapi_context_t* ctx) {
  if (ctx == nullptr) return;
  if (!ctx->isAllowed("context_retain")) {
    oapi_debug(ctx, "'context_retain' is not allowed.");
    return;
  }

  ctx->retain();
}

bool oapi_context_retained (const oapi_context_t* ctx) {
  if (ctx == nullptr) return false;
  return ctx->retain_count > 0;
}

void oapi_context_release (oapi_context_t* ctx) {
  if (ctx == nullptr) return;
  if (!ctx->isAllowed("context_release")) {
    oapi_debug(ctx, "'context_release' is not allowed.");
    return;
  }
  if (ctx->release()) {
    delete ctx;
  }
}

uv_loop_t* oapi_context_get_loop (const oapi_context_t* ctx) {
  if (ctx == nullptr) return nullptr;
  if (ctx->router == nullptr) return nullptr;
  if (!ctx->isAllowed("context_get_loop")) {
    oapi_debug(ctx, "'context_get_loop' is not allowed.");
    return nullptr;
  }

  return ctx->router->bridge.context.loop.get();
}

const oapi_ipc_router_t* oapi_context_get_router (const oapi_context_t* ctx) {
  if (ctx == nullptr) return nullptr;
  if (ctx->router == nullptr) return nullptr;
  if (!ctx->isAllowed("context_get_router")) {
    oapi_debug(ctx, "'context_get_router' is not allowed.");
    return nullptr;
  }
  return reinterpret_cast<const oapi_ipc_router_t*>(ctx->router);
}

const void * oapi_context_get_data (const oapi_context_t* context) {
  return context != nullptr ? context->data : nullptr;
}

const oapi_context_t* oapi_context_get_parent (const oapi_context_t* context) {
  return context != nullptr ? reinterpret_cast<oapi_context_t*>(context->context) : nullptr;
}

void oapi_context_error_reset (oapi_context_t* context) {
  if (context == nullptr) return;
  context->error.code = 0;
  context->error.name = "";
  context->error.message = "";
  context->error.location = "";
  context->state = oro::extension::Extension::Context::State::None;
}

void oapi_context_error_set_code (
  oapi_context_t* context,
  int code
) {
  if (context == nullptr) return;
  context->error.code = code;
  context->state = oro::extension::Extension::Context::State::Error;
}

int oapi_context_error_get_code (const oapi_context_t* context) {
  if (context == nullptr) return -1;
  return context->error.code;
}

void oapi_context_error_set_name (
  oapi_context_t* context,
  const char* name
) {
  if (context == nullptr) return;
  context->error.name = name;
  context->state = oro::extension::Extension::Context::State::Error;
}

const char* oapi_context_error_get_name (const oapi_context_t* context) {
  if (context == nullptr) return nullptr;
  return context->error.name.c_str();
}

void oapi_context_error_set_message (
  oapi_context_t* context,
  const char* message
) {
  if (context == nullptr) return;
  context->error.message = message;
  context->state = oro::extension::Extension::Context::State::Error;
}

const char* oapi_context_error_get_message (const oapi_context_t* context) {
  if (context == nullptr) return nullptr;
  return context->error.message.c_str();
}

void oapi_context_error_set_location (
  oapi_context_t* context,
  const char* location
) {
  if (context == nullptr) return;
  context->error.location = location;
  context->state = oro::extension::Extension::Context::State::Error;
}

const char* oapi_context_error_get_location (const oapi_context_t* context) {
  if (context == nullptr) return nullptr;
  return context->error.location.c_str();
}

const char * oapi_context_config_get (
  const oapi_context_t* context,
  const char* key
) {
  if (context == nullptr || key == nullptr) return nullptr;
  if (!context->config.contains(key)) return nullptr;
  return context->config.at(key).c_str();
}

void oapi_context_config_set (
  oapi_context_t* context,
  const char* key,
  const char* value
) {
  if (context == nullptr || key == nullptr) return;
  context->config[key] = value;
}

void* oapi_context_alloc (oapi_context_t* context, unsigned int size) {
  if (context == nullptr || size == 0) {
    return nullptr;
  }

  return reinterpret_cast<void*>(context->memory.alloc<unsigned char>(size));
}
