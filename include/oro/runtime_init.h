// Oro Runtime: public C init shim header (Oro-first, oro_runtime_init_* API).
#ifndef ORO_RUNTIME_INIT_H
#define ORO_RUNTIME_INIT_H

#include <stdbool.h>

#if defined(__cplusplus)
#define ORO_RUNTIME_INIT_EXTERN_BEGIN extern "C" {
#define ORO_RUNTIME_INIT_EXTERN_END }
#else
#define ORO_RUNTIME_INIT_EXTERN_BEGIN
#define ORO_RUNTIME_INIT_EXTERN_END
#endif

/**
 * Oro-branded initialization entry points for embedders.
 *
 * These functions expose runtime configuration and embedded user config bytes
 * for host applications that embed Oro Runtime.
 */
ORO_RUNTIME_INIT_EXTERN_BEGIN

/**
 * Get a pointer to the embedded user configuration bytes, if any.
 *
 * The buffer and its lifetime are owned by the runtime; callers must not
 * free it. When no configuration is embedded this returns `NULL` and
 * `oro_runtime_init_get_user_config_bytes_size()` returns `0`.
 */
const unsigned char* oro_runtime_init_get_user_config_bytes (void);

/**
 * Get the size in bytes of the buffer returned by
 * `oro_runtime_init_get_user_config_bytes()`.
 */
unsigned int oro_runtime_init_get_user_config_bytes_size (void);

/**
 * Return `true` when the runtime was built in debug mode.
 */
bool oro_runtime_init_is_debug_enabled (void);

/**
 * Get the development host configured for the embedded runtime, or an
 * empty string when unset.
 *
 * The returned pointer is owned by the runtime and remains valid for the
 * lifetime of the process.
 */
const char* oro_runtime_init_get_dev_host (void);

/**
 * Get the development port configured for the embedded runtime, or `0`
 * when unset.
 */
int oro_runtime_init_get_dev_port (void);

/**
 * Known user configuration formats returned by
 * `oro_runtime_init_get_user_config_format()`.
 *
 * Additional formats may be introduced over time; callers should handle
 * unrecognized values by treating them as an opaque enum.
 */
enum oro_runtime_user_config_format {
  ORO_RUNTIME_USER_CONFIG_FORMAT_UNKNOWN = 0,
  ORO_RUNTIME_USER_CONFIG_FORMAT_INI = 1,
  ORO_RUNTIME_USER_CONFIG_FORMAT_TOML = 2
};

/**
 * Get the user configuration format as an integer value.
 *
 * This corresponds to the `oro_runtime_user_config_format` enum above
 * (for example, INI vs TOML). Callers should treat the value as an
 * opaque enum and compare it against the symbolic constants rather than
 * hard-coding numeric literals.
 */
int oro_runtime_init_get_user_config_format (void);

ORO_RUNTIME_INIT_EXTERN_END

#endif
