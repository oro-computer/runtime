#include "extension.hh"

static bool initialize (
  oapi_context_t* context,
  const void* data
) {
  atexit([]() {
    ok("atexit callback called");
  });
  initialize_libc_tests();
  initialize_extension_api_tests(context);
  return true;
}

ORO_RUNTIME_REGISTER_EXTENSION(
  "wasm",
  initialize,
  NULL,
  "A native extension compiled to WASM",
  "0.0.1"
);
