#ifndef ORO_RUNTIME_TEST_EXTENSIONS_WASM_H
#define ORO_RUNTIME_TEST_EXTENSIONS_WASM_H

#include <oro/extension.h>
#include "ok.hh"

#define _CONVERT_TO_STRING(value) #value
#define CONVERT_TO_STRING(value) _CONVERT_TO_STRING(value)

#define test(condition) ({               \
  if ((condition)) {                     \
    ok(CONVERT_TO_STRING(condition));    \
  } else {                               \
    notok(CONVERT_TO_STRING(condition)); \
  }                                      \
})


extern "C" {
  void initialize_libc_tests ();
  void initialize_extension_api_tests (oapi_context_t*);
}

#endif
