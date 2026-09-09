#ifndef ORO_RUNTIME_PLATFORM_SYSTEM_H
#define ORO_RUNTIME_PLATFORM_SYSTEM_H

#if ORO_RUNTIME_PLATFORM_WANTS_MINGW
#include <_mingw.h>
#endif

// All Platforms
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

// Apple (macOS/iOS)
#if defined(__APPLE__)
#include <CoreLocation/CoreLocation.h>
#include <CoreBluetooth/CoreBluetooth.h>
#include <Foundation/Foundation.h>
#include <Network/Network.h>
#include <OSLog/OSLog.h>
#include <TargetConditionals.h>
#include <UserNotifications/UserNotifications.h>
#include <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <Webkit/Webkit.h>

#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
#include <_types/_uint64_t.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <UIKit/UIKit.h>
#include <objc/runtime.h>
#else
#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <objc/objc-runtime.h>
#endif // `TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR`
#endif // `__APPLE__`

// Linux
#if defined(__linux__) && !defined(__ANDROID__)
#include <cstring>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
  #if ORO_RUNTIME_DESKTOP_EXTENSION
  #include <webkit2/webkit-web-extension.h>
  #else
  #include <webkit2/webkit2.h>
  #endif
#endif // `__linux__`

// Android (Linux)
#if defined(__ANDROID__)
#include "android/native.hh"
#endif // `__ANDROID__`

// Windows
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#undef _WINSOCKAPI_
#define _WINSOCKAPI_

#include <winsock2.h>
#include <windows.h>

#include <dwmapi.h>
#include <fileapi.h>
#include <io.h>
#include <objidl.h>
#include <roapi.h>
#include <signal.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <tchar.h>
#include <urlmon.h>
#include <uxtheme.h>
#include <wingdi.h>
#include <wrl.h>

#if !defined(ORO_RUNTIME_EXTENSION)
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#endif

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "libuv.lib")
#pragma comment(lib, "llama.lib")
#pragma comment(lib, "whisper.lib")
#pragma comment(lib, "ggml.lib")
#pragma comment(lib, "ggml-cpu.lib")
#pragma comment(lib, "ggml-base.lib")
#pragma comment(lib, "z.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "WebView2LoaderStatic.lib")
#pragma comment(lib, "Ws2_32.lib")

#define isatty _isatty
#define fileno _fileno
#endif // `_WIN32`


// POSIX[like] Platforms
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#else
#endif

#if ORO_RUNTIME_CROSS_COMPILED_HOST
#include <windows.foundation.h>
#endif

#undef ORO_RUNTIME_PLATFORM_H
#include <oro/platform.h>
#include "types.hh"

#if ORO_RUNTIME_PLATFORM_ANDROID
#include "android.hh"
#endif

#ifdef _WIN32
typedef __int64 ssize_t;
#endif

#endif
