#ifndef RUNTIME_CORE_TESTS_OK_H
#define RUNTIME_CORE_TESTS_OK_H

#include <oro/extension.h>

#define LIBOK_PRINTF_NEEDS_NEWLINE 0
#define LIBOK_PRINTF(...) oapi_printf(0, __VA_ARGS__)
#define LIBOK_FPRINTF(unused, ...) oapi_printf(0, __VA_ARGS__)

#include "test/deps/ok/ok.h"
#endif
