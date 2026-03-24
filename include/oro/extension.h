// Oro Runtime: public C99 extension header (Oro-first, oapi_* API).
#ifndef ORO_RUNTIME_EXTENSION_H
#define ORO_RUNTIME_EXTENSION_H

#if defined(ORO_RUNTIME_EXTENSION_WASM)
#include "webassembly.h"
#else
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <uv.h>
#include "platform.h"
#endif

#if defined(__cplusplus)
#define ORO_RUNTIME_EXTENSION_EXTERN_BEGIN extern "C" {
#define ORO_RUNTIME_EXTENSION_EXTERN_END }
#else
#define ORO_RUNTIME_EXTENSION_EXTERN_BEGIN
#define ORO_RUNTIME_EXTENSION_EXTERN_END
#endif

#if defined(ORO_RUNTIME_EXTENSION_WASM)
# define ORO_RUNTIME_EXTENSION_EXPORT extern
#elif ORO_RUNTIME_PLATFORM_WINDOWS
# define ORO_RUNTIME_EXTENSION_EXPORT __declspec(dllexport)
#elif ORO_RUNTIME_PLATFORM_LINUX
# define ORO_RUNTIME_EXTENSION_EXPORT __attribute__((visibility("default")))
#elif ORO_RUNTIME_PLATFORM_MACOS || ORO_RUNTIME_PLATFORM_IOS
# define ORO_RUNTIME_EXTENSION_EXPORT __attribute__((visibility("default")))
#else
# define ORO_RUNTIME_EXTENSION_EXPORT
#endif

/**
 * Major version of the extension ABI.
 */
#define ORO_RUNTIME_EXTENSION_ABI_VERSION_MAJOR (unsigned int) 0

/**
 * Minor version of the extension ABI.
 */
#define ORO_RUNTIME_EXTENSION_ABI_VERSION_MINOR (unsigned int) 0

/**
 * Patch version of the extension ABI.
 */
#define ORO_RUNTIME_EXTENSION_ABI_VERSION_PATCH (unsigned int) 3

/**
 * The packed version of the extension ABI useful for semantic
 * comparison purposes.
 */
#define ORO_RUNTIME_EXTENSION_ABI_VERSION ((int) (                          \
  ORO_RUNTIME_EXTENSION_ABI_VERSION_MAJOR << 16 |                           \
  ORO_RUNTIME_EXTENSION_ABI_VERSION_MINOR << 8  |                           \
  ORO_RUNTIME_EXTENSION_ABI_VERSION_PATCH << 0                              \
))

ORO_RUNTIME_EXTENSION_EXTERN_BEGIN

/**
 * An opaque pointer for an extension context. A context is provided to
 * initializers, deinitializers, and IPC route requests.
 */
typedef struct oapi_context oapi_context_t;

/**
 * A callback to be called at a later time given to the
 * `oapi_context_dispatch()` function.
 * @param context
 * @param data
 */
typedef void (*oapi_context_dispatch_callback)(
  oapi_context_t* context,
  const void* data
);

/**
 * Extension registration initializer callback.
 * @param context - Extension context
 * @param data    - Optional user data
 * @return `true` if initialization was successful, otherwise `false`
 */
typedef bool (*oapi_extension_registration_initializer_t)(
  oapi_context_t* context,
  const void* data
);

/**
 * Extension registration deinitializer callback.
 * @param context - Extension context
 * @param data    - Optional user data
 * @return `true` if deinitialization was successful, otherwise `false`
 */
typedef bool (*oapi_extension_registration_deinitializer_t)(
  oapi_context_t* context,
  const void* data
);

/**
 * Container for an extension registration.
 */
typedef struct oapi_extension_registration {
  unsigned long abi:32;
  // required
  const char* name;
  const oapi_extension_registration_initializer_t initializer;

  // optional
  const oapi_extension_registration_deinitializer_t deinitializer;
  const char* description;
  const char* version;

  // reserved for future ABI changes
  char __reserved__[1024];
} oapi_extension_registration_t;

typedef struct oapi_extension_registration oapi_extension_registration;

/**
 * JSON API
 * The _JSON API_ is a general purpose JSON builder.
 */

/**
 * JSON type enumeration.
 */
#define OAPI_JSON_TYPE_EMPTY   -1
#define OAPI_JSON_TYPE_ANY      0
#define OAPI_JSON_TYPE_NULL     1
#define OAPI_JSON_TYPE_OBJECT   2
#define OAPI_JSON_TYPE_ARRAY    3
#define OAPI_JSON_TYPE_BOOLEAN  4
#define OAPI_JSON_TYPE_NUMBER   5
#define OAPI_JSON_TYPE_STRING   6
#define OAPI_JSON_TYPE_RAW      7

/**
 * An opaque JSON type that represents "any" JSON value
 */
typedef struct oapi_json_any oapi_json_any_t;

/**
 * An opaque JSON type that represents a `null` JSON value.
 */
typedef struct oapi_json_null oapi_json_null_t;

/**
 * An opaque JSON type that represents an "object" JSON value.
 */
typedef struct oapi_json_object oapi_json_object_t;

/**
 * An opaque JSON type that represents an "array" JSON value.
 */
typedef struct oapi_json_array oapi_json_array_t;

/**
 * An opaque JSON type that represents a `true` or `false` JSON value.
 */
typedef struct oapi_json_boolean oapi_json_boolean_t;

/**
 * An opaque JSON type that represents a "number" JSON value.
 */
typedef struct oapi_json_number oapi_json_number_t;

/**
 * An opaque JSON type that represents a "string" JSON value.
 */
typedef struct oapi_json_string oapi_json_string_t;

/**
 * An opaque JSON type that represents a "raw" JSON value.
 */
typedef struct oapi_json_raw oapi_json_raw_t;

/**
 * A scalar type that represents the JSON type enumeration.
 */
typedef int oapi_json_type_t;

/**
 * Casts `value` to a `oapi_json_any_t*`.
 * @param value - The value to cast
 */
#define oapi_json_any(value) ((oapi_json_any_t*)(value))

/**
 * Casts `value` to a `oapi_json_any_t*`.
 * @param value - The value to cast
 */
#define oapi_json_bool(value) ((oapi_json_any_t*)(value))

/**
 * `true` if `value` is an empty JSON type.
 * @param value - The value to test
 */
#define oapi_json_value_is_empty(value)                                        \
  (OAPI_JSON_TYPE_EMPTY == oapi_json_typeof((oapi_json_any_t*) (value)))

/**
 * `true` if `value` is a null JSON type.
 * @param value - The value to test
 */
#define oapi_json_value_is_null(value)                                         \
  (OAPI_JSON_TYPE_NULL == oapi_json_typeof((oapi_json_any_t*) (value)))

/**
 * `true` if `value` is an object JSON type.
 * @param value - The value to test
 */
#define oapi_json_value_is_object(value)                                       \
  (OAPI_JSON_TYPE_OBJECT == oapi_json_typeof((oapi_json_any_t*) (value)))

/**
 * `true` if `value` is an array JSON type.
 * @param value - The value to test
 */
#define oapi_json_value_is_array(value)                                        \
  (OAPI_JSON_TYPE_ARRAY == oapi_json_typeof((oapi_json_any_t*) (value)))

/**
 * `true` if `value` is a boolean JSON type.
 * @param value - The value to test
 */
#define oapi_json_value_is_boolean(value)                                      \
  (OAPI_JSON_TYPE_BOOLEAN == oapi_json_typeof((oapi_json_any_t*) (value)))

/**
 * `true` if `value` is a number JSON type.
 * @param value - The value to test
 */
#define oapi_json_value_is_number(value)                                       \
  (OAPI_JSON_TYPE_NUMBER == oapi_json_typeof((oapi_json_any_t*) (value)))

/**
 * `true` if `value` is a string JSON type.
 * @param value - The value to test
 */
#define oapi_json_value_is_string(value)                                       \
  (OAPI_JSON_TYPE_STRING == oapi_json_typeof((oapi_json_any_t*) (value)))

/**
 * Set JSON `value` for JSON `object` at `key`. Generally, an alias to the
 * `oapi_json_object_set_value` function.
 * @param object - The object to set a value on
 * @param key    - The key of the value to set
 * @param value  - The JSON value to set
 */
#define oapi_json_object_set(object, key, value)                               \
  oapi_json_object_set_value(                                                  \
    (oapi_json_object_t*) (object),                                            \
    (const char*)(key),                                                        \
    (oapi_json_any((value)))                                                   \
  )

/**
 * Set JSON `value` for JSON `array` at `index`. Generally, an alias to the
 * `oapi_json_array_set_value` function.
 * @param array - The array to set a value on
 * @param index - The index of the value to set
 * @param value - The JSON value to set
 */
#define oapi_json_array_set(array, index, value)                               \
  oapi_json_array_set_value(                                                   \
    (oapi_json_array_t*) (array),                                              \
    (unsigned int) (index),                                                    \
    (oapi_json_any((value)))                                                   \
  )

/**
 * Push a JSON `value` to the end of a JSON `array`. Generally, an alias to
 * the `oapi_json_array_push_value` function.
 * @param array - The array to set a value on
 * @param value - The JSON value to set
 */
#define oapi_json_array_push(array, value)                                     \
  oapi_json_array_push_value (                                                 \
    (oapi_json_array_t*) (array),                                              \
    oapi_json_any((value))                                                     \
  )

/**
 * Convert JSON `value` to a string. Generally, an alias to the
 * `oapi_json_stringify_value` function.
 * @param value - The JSON value to convert to a string
 * @return The JSON value as a string
 */
#define oapi_json_stringify(value)                                             \
  oapi_json_stringify_value(oapi_json_any(value))

/**
 * Platform metadata describing the current runtime environment.
 */
typedef struct oapi_runtime_platform_info {
  const char* arch;
  const char* os;
  bool mac;
  bool ios;
  bool win;
  bool android;
  bool linux;
  bool unix;
} oapi_runtime_platform_info_t;

/**
 * IPC API opaque types.
 */
typedef struct oapi_ipc_message oapi_ipc_message_t;
typedef struct oapi_ipc_bridge oapi_ipc_bridge_t;
typedef struct oapi_ipc_result oapi_ipc_result_t;
typedef struct oapi_ipc_router oapi_ipc_router_t;

/**
 * IPC router callbacks.
 */
typedef void (*oapi_ipc_router_message_callback_t)(
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  const oapi_ipc_router_t* router
);

typedef void (*oapi_ipc_router_result_callback_t)(
  const oapi_ipc_result_t* result,
  const oapi_ipc_router_t* router
);

/**
 * Process API opaque types and callbacks.
 */
typedef struct oapi_process_exec oapi_process_exec_t;
typedef struct oapi_process_spawn oapi_process_spawn_t;

typedef void (*oapi_process_spawn_stdout_callback_t)(
  const oapi_process_spawn_t* process,
  const char* output,
  unsigned int size
);

typedef void (*oapi_process_spawn_stderr_callback_t)(
  const oapi_process_spawn_t* process,
  const char* output,
  unsigned int size
);

typedef void (*oapi_process_spawn_exit_callback_t)(
  const oapi_process_spawn_t* process,
  int exit_code
);

/**
 * Additional runtime metadata about a registered extension.
 */
typedef struct oapi_extension_info {
  unsigned long abi;
  const char* name;
  const char* description;
  const char* version;
  const char* path;
  const char* type;
  bool loaded;
  bool initialized;
} oapi_extension_info_t;

typedef void (*oapi_extension_enumerate_callback)(
  const oapi_extension_info_t* info,
  void* data
);

/**
 * Service metadata and callbacks.
 */
typedef struct oapi_service_info {
  const char* name;
  bool enabled;
} oapi_service_info_t;

typedef void (*oapi_service_enumerate_callback)(
  const oapi_service_info_t* info,
  void* data
);

/**
 * Notifications callbacks.
 */
typedef void (*oapi_notifications_permission_callback)(
  oapi_context_t* context,
  const char* json,
  void* data
);

typedef void (*oapi_notifications_response_callback)(
  oapi_context_t* context,
  const char* json,
  void* data
);

typedef void (*oapi_notifications_presented_callback)(
  oapi_context_t* context,
  const char* json,
  void* data
);

/**
 * Secure storage callback.
 */
typedef void (*oapi_secure_storage_callback)(
  oapi_context_t* context,
  const char* json,
  void* data
);

/**
 * Canonical oapi_* entry points.
 */

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_extension_registration_t* __oapi_extension_init ();

ORO_RUNTIME_EXTENSION_EXPORT
oapi_context_t* oapi_context_create (
  oapi_context_t* parent,
  bool retained
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_context_set_data (
  oapi_context_t* context,
  const void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_context_dispatch (
  oapi_context_t* context,
  const void* data,
  oapi_context_dispatch_callback callback
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_retain (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_release (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_context_retained (const oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
uv_loop_t* oapi_context_get_loop (const oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_ipc_router_t* oapi_context_get_router (
  const oapi_context_t* context
);

ORO_RUNTIME_EXTENSION_EXPORT
const void * oapi_context_get_data (const oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_context_t* oapi_context_get_parent (
  const oapi_context_t* context
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_error_reset (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_error_set_code (oapi_context_t* context, int code);

ORO_RUNTIME_EXTENSION_EXPORT
int oapi_context_error_get_code (const oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_error_set_name (
  oapi_context_t* context,
  const char* name
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_context_error_get_name (const oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_error_set_message (
  oapi_context_t* context,
  const char* message
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_context_error_get_message (const oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_error_set_location (
  oapi_context_t* context,
  const char* location
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_context_error_get_location (
  const oapi_context_t* context
);

ORO_RUNTIME_EXTENSION_EXPORT
void* oapi_context_alloc (oapi_context_t* context, unsigned int size);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_javascript_evaluate (
  oapi_context_t* context,
  const char* name,
  const char* source
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_env_get (oapi_context_t* context, const char* name);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_env_has (oapi_context_t* context, const char* name);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_json_type_t oapi_json_typeof (const oapi_json_any_t*);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_object_t* oapi_json_object_create (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_array_t* oapi_json_array_create (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_string_t* oapi_json_string_create (
  oapi_context_t* context,
  const char* string
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_boolean_t* oapi_json_boolean_create (
  oapi_context_t* context,
  bool boolean
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_number_t* oapi_json_number_create (
  oapi_context_t* context,
  const double number
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_any_t* oapi_json_raw_from (
  oapi_context_t* context,
  const char* source
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_any_t* oapi_json_parse (
  oapi_context_t* context,
  const char* source
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_json_object_set_value (
  oapi_json_object_t* object,
  const char* key,
  oapi_json_any_t* value
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_any_t* oapi_json_object_get (
  const oapi_json_object_t* object,
  const char* key
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_json_array_set_value (
  oapi_json_array_t* array,
  unsigned int index,
  oapi_json_any_t* value
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_any_t* oapi_json_array_get (
  const oapi_json_array_t* array,
  unsigned int index
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_json_array_push_value (
  oapi_json_array_t* array,
  oapi_json_any_t* value
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_json_any_t* oapi_json_array_pop (oapi_json_array_t* json);

ORO_RUNTIME_EXTENSION_EXPORT
const char * oapi_json_stringify_value (const oapi_json_any_t*);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_extension_register (
  const oapi_extension_registration_t* registration
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_extension_is_allowed (
  oapi_context_t* context,
  const char *allowed
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_log (const oapi_context_t* ctx, const char *message);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_debug (const oapi_context_t* ctx, const char *message);

ORO_RUNTIME_EXTENSION_EXPORT
uint64_t oapi_rand64 ();

ORO_RUNTIME_EXTENSION_EXPORT
int oapi_rand32 ();

ORO_RUNTIME_EXTENSION_EXPORT
int oapi_rand_range (int min, int max);

ORO_RUNTIME_EXTENSION_EXPORT
const unsigned char* oapi_rand_bytes (
  oapi_context_t* context,
  size_t size
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_crypto_sha1 (
  oapi_context_t* context,
  const unsigned char* bytes,
  size_t size
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_uuid_v7 (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_runtime_version (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_runtime_version_hash (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_runtime_version_full (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_base64_encode (
  oapi_context_t* context,
  const unsigned char* bytes,
  size_t size
);

ORO_RUNTIME_EXTENSION_EXPORT
const unsigned char* oapi_base64_decode (
  oapi_context_t* context,
  const char* string,
  size_t* size
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_hex_encode (
  oapi_context_t* context,
  const unsigned char* bytes,
  size_t size
);

ORO_RUNTIME_EXTENSION_EXPORT
const unsigned char* oapi_hex_decode (
  oapi_context_t* context,
  const char* string,
  size_t* size
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_runtime_getcwd (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_runtime_sleep (uint64_t milliseconds);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_runtime_platform_info_t* oapi_runtime_platform ();

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_os_constants_json (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
int oapi_ipc_message_get_index (const oapi_ipc_message_t* message);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_ipc_message_get_value (
  const oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
const unsigned char* oapi_ipc_message_get_bytes (
  const oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
unsigned int oapi_ipc_message_get_bytes_size (
  const oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_ipc_message_get_name (
  const oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_ipc_message_get_seq (
  const oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_ipc_message_get_uri (
  const oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_ipc_message_get (
  const oapi_ipc_message_t* message,
  const char* key
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_ipc_result_t* oapi_ipc_result_create (
  oapi_context_t* context,
  oapi_ipc_message_t *message
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_ipc_result_set_seq (
  oapi_ipc_result_t* result,
  const char* seq
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_ipc_result_get_seq (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_context_t* oapi_ipc_result_get_context (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_ipc_result_set_message (
  oapi_ipc_result_t* result,
  oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_ipc_message_t* oapi_ipc_result_get_message (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_ipc_result_set_json (
  oapi_ipc_result_t* result,
  const oapi_json_any_t* json
);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_json_any_t* oapi_ipc_result_get_json (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_ipc_result_set_json_data (
  oapi_ipc_result_t* result,
  const oapi_json_any_t* json
);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_json_any_t* oapi_ipc_result_get_json_data (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_ipc_result_set_json_error (
  oapi_ipc_result_t* result,
  const oapi_json_any_t* json
);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_json_any_t* oapi_ipc_result_get_json_error (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_ipc_result_set_bytes (
  oapi_ipc_result_t* result,
  unsigned int size,
  unsigned char* bytes
);

ORO_RUNTIME_EXTENSION_EXPORT
unsigned char* oapi_ipc_result_get_bytes (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
size_t oapi_ipc_result_get_bytes_size (
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_ipc_result_set_header (
  oapi_ipc_result_t* result,
  const char* name,
  const char* value
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_ipc_result_get_header (
  const oapi_ipc_result_t* result,
  const char* name
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_ipc_result_t* oapi_ipc_result_from_json (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  const oapi_json_any_t* json
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_send_chunk (
  oapi_ipc_result_t* result,
  const unsigned char* chunk,
  size_t chunk_size,
  bool finished
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_send_event (
  oapi_ipc_result_t* result,
  const char* name,
  const unsigned char* data,
  bool finished
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_set_cancellation_handler (
  oapi_ipc_result_t* result,
  void (*handler)(void*),
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_reply (const oapi_ipc_result_t* result);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_reply_with_error (
  oapi_ipc_result_t* result,
  const char* error
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_send_json (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  oapi_json_any_t* json
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_send_json_with_result (
  oapi_context_t* context,
  oapi_ipc_result_t* result,
  oapi_json_any_t* json
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_send_bytes (
  oapi_context_t* context,
  oapi_ipc_message_t* message,
  unsigned int size,
  unsigned char* bytes,
  const char* headers
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_send_bytes_with_result (
  oapi_context_t* context,
  oapi_ipc_result_t* result,
  unsigned int size,
  unsigned char* bytes,
  const char* headers
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_emit (
  oapi_context_t* context,
  const char* name,
  const char* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_invoke (
  oapi_context_t* context,
  const char* url,
  unsigned int size,
  const char* bytes,
  oapi_ipc_router_result_callback_t callback
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_router_map (
  oapi_context_t* context,
  const char* route,
  oapi_ipc_router_message_callback_t callback,
  const void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_router_unmap (
  oapi_context_t* context,
  const char* route
);

ORO_RUNTIME_EXTENSION_EXPORT
uint64_t oapi_ipc_router_listen (
  oapi_context_t* context,
  const char* route,
  oapi_ipc_router_message_callback_t callback,
  const void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_ipc_router_unlisten (
  oapi_context_t* context,
  const char* route,
  uint64_t token
);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_process_exec_t* oapi_process_exec (
  oapi_context_t* context,
  const char* command
);

ORO_RUNTIME_EXTENSION_EXPORT
int oapi_process_exec_get_exit_code (
  const oapi_process_exec_t* process
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_process_exec_get_output (
  const oapi_process_exec_t* process
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_process_spawn_t* oapi_process_spawn (
  oapi_context_t* context,
  const char* command,
  const char* argv,
  const char* path,
  oapi_process_spawn_stdout_callback_t onstdout,
  oapi_process_spawn_stderr_callback_t onstderr,
  oapi_process_spawn_exit_callback_t onexit
);

ORO_RUNTIME_EXTENSION_EXPORT
int oapi_process_spawn_get_exit_code (
  const oapi_process_spawn_t* process
);

ORO_RUNTIME_EXTENSION_EXPORT
unsigned long oapi_process_spawn_get_pid (
  const oapi_process_spawn_t* process
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_context_t* oapi_process_spawn_get_context (
  const oapi_process_spawn_t* process
);

ORO_RUNTIME_EXTENSION_EXPORT
int oapi_process_spawn_wait (oapi_process_spawn_t* process);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_process_spawn_write (
  oapi_process_spawn_t* process,
  const char* bytes,
  size_t size
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_process_spawn_close_stdin (oapi_process_spawn_t* process);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_process_spawn_kill (
  oapi_process_spawn_t* process,
  int code
);

ORO_RUNTIME_EXTENSION_EXPORT
const char * oapi_context_config_get (
  const oapi_context_t* context,
  const char* key
);

ORO_RUNTIME_EXTENSION_EXPORT
void oapi_context_config_set (
  oapi_context_t* context,
  const char* key,
  const char* value
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_extension_load (
  oapi_context_t* context,
  const char *name,
  const void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_extension_unload (
  oapi_context_t* context,
  const char *name
);

ORO_RUNTIME_EXTENSION_EXPORT
const oapi_extension_registration_t* oapi_extension_get (
  const oapi_context_t* context,
  const char *name
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_extension_is_loaded (const char *name);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_extension_get_path (
  oapi_context_t* context,
  const char* name
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_extension_get_type (
  oapi_context_t* context,
  const char* name
);

ORO_RUNTIME_EXTENSION_EXPORT
size_t oapi_extension_enumerate (
  oapi_context_t* context,
  oapi_extension_enumerate_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_config_has (
  oapi_context_t* context,
  const char* key
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_config_get (
  oapi_context_t* context,
  const char* key
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_config_get_json (
  oapi_context_t* context,
  const char* prefix
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_config_keys_json (
  oapi_context_t* context,
  const char* prefix
);

ORO_RUNTIME_EXTENSION_EXPORT
size_t oapi_services_enumerate (
  oapi_context_t* context,
  oapi_service_enumerate_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_service_is_enabled (
  oapi_context_t* context,
  const char* name
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_services_json (oapi_context_t* context);

ORO_RUNTIME_EXTENSION_EXPORT
uint64_t oapi_notifications_on_permission_change (
  oapi_context_t* context,
  oapi_notifications_permission_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_notifications_off_permission_change (
  oapi_context_t* context,
  uint64_t token
);

ORO_RUNTIME_EXTENSION_EXPORT
uint64_t oapi_notifications_on_response (
  oapi_context_t* context,
  oapi_notifications_response_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_notifications_off_response (
  oapi_context_t* context,
  uint64_t token
);

ORO_RUNTIME_EXTENSION_EXPORT
uint64_t oapi_notifications_on_presented (
  oapi_context_t* context,
  oapi_notifications_presented_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_notifications_off_presented (
  oapi_context_t* context,
  uint64_t token
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_secure_storage_set (
  oapi_context_t* context,
  const char* scope,
  const char* key,
  const unsigned char* value,
  size_t value_size,
  oapi_secure_storage_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_secure_storage_get (
  oapi_context_t* context,
  const char* scope,
  const char* key,
  const char* encoding,
  oapi_secure_storage_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_secure_storage_remove (
  oapi_context_t* context,
  const char* scope,
  const char* key,
  oapi_secure_storage_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_secure_storage_clear (
  oapi_context_t* context,
  const char* scope,
  oapi_secure_storage_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_secure_storage_keys (
  oapi_context_t* context,
  const char* scope,
  oapi_secure_storage_callback callback,
  void* data
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_resource_exists (
  oapi_context_t* context,
  const char* path
);

ORO_RUNTIME_EXTENSION_EXPORT
size_t oapi_resource_size (
  oapi_context_t* context,
  const char* path
);

ORO_RUNTIME_EXTENSION_EXPORT
const unsigned char* oapi_resource_read (
  oapi_context_t* context,
  const char* path,
  bool cached,
  size_t* size
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_resource_read_string (
  oapi_context_t* context,
  const char* path,
  bool cached
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_resource_resolve (
  oapi_context_t* context,
  const char* path
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_resource_well_known_paths (
  oapi_context_t* context
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_resource_is_file (
  oapi_context_t* context,
  const char* path
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_resource_is_directory (
  oapi_context_t* context,
  const char* path
);

ORO_RUNTIME_EXTENSION_EXPORT
bool oapi_resource_is_mounted_path (
  oapi_context_t* context,
  const char* path
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_resource_get_resources_path (
  oapi_context_t* context
);

ORO_RUNTIME_EXTENSION_EXPORT
const char* oapi_resource_mounted_paths_json (
  oapi_context_t* context
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_ipc_message_t* oapi_ipc_message_clone (
  oapi_context_t* context,
  const oapi_ipc_message_t* message
);

ORO_RUNTIME_EXTENSION_EXPORT
oapi_ipc_result_t* oapi_ipc_result_clone (
  oapi_context_t* context,
  const oapi_ipc_result_t* result
);

ORO_RUNTIME_EXTENSION_EXTERN_END

/**
 * Helper for exporting the registration symbol set for a given prefix.
 *
 * This is used by ORO_RUNTIME_REGISTER_EXTENSION to export the canonical
 * `__oapi_extension_*` entry points that the runtime loads.
 */
#define __ORO_RUNTIME_EXTENSION_REGISTER_SYMBOL_SET(                          \
    _prefix, _registration_type, _initializer_type, _deinitializer_type       \
  )                                                                           \
  ORO_RUNTIME_EXTENSION_EXPORT                                                \
  unsigned long __##_prefix##_extension_abi () {                              \
    return __oro_extension__.abi;                                             \
  }                                                                           \
                                                                              \
  ORO_RUNTIME_EXTENSION_EXPORT                                                \
  const char* __##_prefix##_extension_name () {                               \
    return __oro_extension__.name;                                            \
  }                                                                           \
                                                                              \
  ORO_RUNTIME_EXTENSION_EXPORT                                                \
  const char* __##_prefix##_extension_description () {                        \
    return __oro_extension__.description;                                     \
  }                                                                           \
                                                                              \
  ORO_RUNTIME_EXTENSION_EXPORT                                                \
  const char* __##_prefix##_extension_version () {                            \
    return __oro_extension__.version;                                         \
  }                                                                           \
                                                                              \
  ORO_RUNTIME_EXTENSION_EXPORT                                                \
  const _initializer_type __##_prefix##_extension_initializer () {            \
    return __oro_extension__.initializer;                                     \
  }                                                                           \
                                                                              \
  ORO_RUNTIME_EXTENSION_EXPORT                                                \
  const _deinitializer_type __##_prefix##_extension_deinitializer () {        \
    return __oro_extension__.deinitializer;                                   \
  }                                                                           \
                                                                              \
  ORO_RUNTIME_EXTENSION_EXPORT                                                \
  const _registration_type* __##_prefix##_extension_init () {                 \
    return &__oro_extension__;                                                \
  }

/**
 * Register an extension using the canonical oapi_* registration struct.
 *
 * This macro emits the `__oapi_extension_*` symbol set used by the runtime.
 */
#define ORO_RUNTIME_REGISTER_EXTENSION(_name, _initializer, ...)               \
  ORO_RUNTIME_EXTENSION_EXTERN_BEGIN                                           \
    static oapi_extension_registration_t __oro_extension__ = {                 \
      ORO_RUNTIME_EXTENSION_ABI_VERSION,                                       \
      _name, _initializer, ##__VA_ARGS__                                       \
    };                                                                         \
    __ORO_RUNTIME_EXTENSION_REGISTER_SYMBOL_SET(                               \
      oapi,                                                                    \
      oapi_extension_registration_t,                                           \
      oapi_extension_registration_initializer_t,                               \
      oapi_extension_registration_deinitializer_t                              \
    )                                                                          \
  ORO_RUNTIME_EXTENSION_EXTERN_END

/**
 * A simple printf macro for extension implementers with a total max output
 * buffer size of `BUFSIZ` (@see `stdio.h`, `string.h`)
 * @param format - Format string for formatted output
 * @param ...    - `format` argument values
 */
#define oapi_printf(ctx, format, ...) ({                                       \
  char _buffer[BUFSIZ] = {0};                                                  \
  snprintf(_buffer, BUFSIZ, format, ##__VA_ARGS__);                            \
  oapi_log(ctx, _buffer);                                                      \
})

#endif
