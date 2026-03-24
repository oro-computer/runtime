#include "extension.hh"
#include <string.h>

static void onhello (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  const oapi_ipc_router_t* router
) {
  auto result = oapi_ipc_result_create(context, message);
  auto object = oapi_json_object_create(context);
  auto string = oapi_json_string_create(context, "world");
  oapi_json_object_set(object, "hello", string);
  oapi_ipc_result_set_json_data(result, oapi_json_any(object));
  oapi_ipc_result_set_header(result, "header-name", "header value");
  oapi_ipc_reply(result);
}

void initialize_extension_api_tests (oapi_context_t* context) {
  auto object = oapi_json_object_create(context);
  auto string = oapi_json_string_create(context, "world");

  test(context != NULL);
  test(object != NULL);
  test(string != NULL);

  oapi_json_object_set(object, "hello", string);

  test(strcmp("{\"hello\":\"world\"}", oapi_json_stringify(object)) == 0);
  test(oapi_json_typeof(oapi_json_any(object)) == OAPI_JSON_TYPE_OBJECT);
  test(oapi_json_typeof(oapi_json_any(string)) == OAPI_JSON_TYPE_STRING);

  auto parsed = oapi_json_parse(context, "{\"hello\":\"world\",\"count\":1.5}");
  test(parsed != NULL);
  test(oapi_json_typeof(parsed) == OAPI_JSON_TYPE_OBJECT);

  auto parsedObject = reinterpret_cast<oapi_json_object_t*>(parsed);
  auto parsedHello = oapi_json_object_get(parsedObject, "hello");
  test(parsedHello != NULL);
  test(oapi_json_typeof(parsedHello) == OAPI_JSON_TYPE_STRING);
  test(strcmp(
    "world",
    reinterpret_cast<oro::runtime::JSON::String*>(parsedHello)->value().c_str()
  ) == 0);

  auto parsedCount = oapi_json_object_get(parsedObject, "count");
  test(parsedCount != NULL);
  test(oapi_json_typeof(parsedCount) == OAPI_JSON_TYPE_NUMBER);
  test(reinterpret_cast<oro::runtime::JSON::Number*>(parsedCount)->value() == 1.5);

  oapi_context_error_reset(context);
  auto invalid = oapi_json_parse(context, "{invalid");
  test(invalid == NULL);
  test(strcmp("JSONParseError", oapi_context_error_get_name(context)) == 0);
  test(oapi_context_error_get_message(context) != NULL);

  oapi_context_config_set(context, "foo", "bar");
  test(strcmp("bar", oapi_context_config_get(context, "foo")) == 0);

  oapi_ipc_router_map(context, "wasm.hello", onhello, NULL);
}
