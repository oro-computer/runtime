#include <oro/extension.h>

void onping (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  const oapi_ipc_router_t* router
) {
  const char* pong = oapi_ipc_message_get_value(message);
  oapi_ipc_result_t* result = oapi_ipc_result_create(context, message);
  oapi_ipc_result_set_json_data(
    result,
    oapi_json_any(oapi_json_string_create(context, pong))
  );
  oapi_ipc_reply(result);
}

bool initialize (oapi_context_t* context, const void *data) {
  if (oapi_extension_is_allowed(context, "ipc_router_map")) {
    oapi_ipc_router_map(context, "simple.ping", onping, data);
  }
  return true;
}

bool deinitialize (oapi_context_t* context, const void *data) {
  if (oapi_extension_is_allowed(context, "ipc_router_unmap")) {
    oapi_ipc_router_unmap(context, "simple.ping");
  }
  return true;
}

ORO_RUNTIME_REGISTER_EXTENSION(
  "simple-ipc-ping", // name
  initialize, // initializer
  deinitialize, // deinitializer
  "a simple IPC ping extension", // description
  "0.1.2" // version
);
