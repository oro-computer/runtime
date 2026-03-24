#include <oro/_user-config-bytes.hh>
#include <oro/runtime_init.h>

#if defined(__cplusplus)
extern "C" {
#endif
  const unsigned char* oro_runtime_init_get_user_config_bytes () {
    if (sizeof(__oro_runtime_user_config_bytes) == 0) {
      return nullptr;
    }

    return __oro_runtime_user_config_bytes;
  }

  unsigned int oro_runtime_init_get_user_config_bytes_size () {
    return sizeof(__oro_runtime_user_config_bytes);
  }

  bool oro_runtime_init_is_debug_enabled () {
  #if DEBUG
    return true;
  #endif
    return false;
  }

  const char* oro_runtime_init_get_dev_host () {
  #if defined(HOST)
    return HOST;
  #endif
    return "";
  }

  int oro_runtime_init_get_dev_port () {
  #if defined(PORT)
    return PORT;
  #endif
    return 0;
  }

  int oro_runtime_init_get_user_config_format () {
    return __oro_runtime_user_config_format;
  }
#if defined(__cplusplus)
}
#endif
