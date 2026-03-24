#ifndef ORO_RUNTIME_DEPS_H
#define ORO_RUNTIME_DEPS_H

// Core libuv version helpers
#include <uv.h>

// Optional crypto/runtime deps mirrored from `src/runtime/ipc/routes.cc`
// so both IPC routes and CLI surfaces can report consistent versions.

#if __has_include(<sodium.h>)
#include <sodium.h>
#define ORO_RUNTIME_HAS_SODIUM 1
#else
#define ORO_RUNTIME_HAS_SODIUM 0
#endif

#if __has_include(<mbedtls/version.h>)
#include <mbedtls/version.h>
#define ORO_RUNTIME_HAS_MBEDTLS 1
#else
#define ORO_RUNTIME_HAS_MBEDTLS 0
#endif

#if __has_include(<libusb-1.0/libusb.h>)
#include <libusb-1.0/libusb.h>
#define ORO_RUNTIME_HAS_LIBUSB 1
#elif __has_include(<libusb.h>)
#include <libusb.h>
#define ORO_RUNTIME_HAS_LIBUSB 1
#else
#define ORO_RUNTIME_HAS_LIBUSB 0
#endif

#if __has_include(<cpp-httplib/httplib.h>)
#include <cpp-httplib/httplib.h>
#define ORO_RUNTIME_HAS_CPPHTTPLIB 1
#else
#define ORO_RUNTIME_HAS_CPPHTTPLIB 0
#endif

#if __has_include(<nlohmann/json_fwd.hpp>)
#include <nlohmann/json_fwd.hpp>
#define ORO_RUNTIME_HAS_NLOHMANN_JSON 1
#else
#define ORO_RUNTIME_HAS_NLOHMANN_JSON 0
#endif

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#endif

#if __has_include(<whisper.h>)
#include <whisper.h>
#define ORO_RUNTIME_HAS_WHISPER 1
#else
#define ORO_RUNTIME_HAS_WHISPER 0
#endif

#ifndef ORO_RUNTIME_HAS_ZLIB
#define ORO_RUNTIME_HAS_ZLIB 0
#endif

#if ORO_RUNTIME_HAS_ZLIB
#include <zlib.h>
#endif

#endif
