#ifndef ORO_RUNTIME_WEBASSEMBLY_H
#define ORO_RUNTIME_WEBASSEMBLY_H

#include "webassembly/stdbool.h"
#include "webassembly/stddef.h"
#include "webassembly/stdint.h"
#include "webassembly/stdio.h"
#include "webassembly/stdlib.h"
#include "webassembly/string.h"
#include "webassembly/errno.h"
#include "webassembly/uv.h"

// platform
#define ORO_RUNTIME_PLATFORM_NAME "wasm32"
#define ORO_RUNTIME_PLATFORM_OS "web"
#define ORO_RUNTIME_PLATFORM_ANDROID 0
#define ORO_RUNTIME_PLATFORM_IOS 0
#define ORO_RUNTIME_PLATFORM_IOS_SIMULATOR 0
#define ORO_RUNTIME_PLATFORM_LINUX 0
#define ORO_RUNTIME_PLATFORM_MACOS 0
#define ORO_RUNTIME_PLATFORM_WINDOWS 0
#define ORO_RUNTIME_PLATFORM_UNIX 0
// when this header is included, this is always `1`
#define ORO_RUNTIME_PLATFORM_WASM 1

// arch
#define ORO_RUNTIME_PLATFORM_ARCH_x64 0
#define ORO_RUNTIME_PLATFORM_ARCH_ARM64 0
#define ORO_RUNTIME_PLATFORM_ARCH_UNKNOWN 1

#endif
