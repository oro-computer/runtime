#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

declare IPHONEOS_VERSION_MIN="${IPHONEOS_VERSION_MIN:-15.0}"
declare IOS_SIMULATOR_VERSION_MIN="${IOS_SIMULATOR_VERSION_MIN:-$IPHONEOS_VERSION_MIN}"

declare cflags=()
declare arch="$(uname -m | sed 's/aarch64/arm64/g')"
arch=${ARCH:-$arch}
declare host="${TARGET_HOST:-"$(uname -s)"}"
declare platform="desktop"

declare ios_sdk_path=""
declare build_platform_dir="desktop"

cflags+=(
  $CFLAGS
  $CXXFLAGS
)

if [[ "$host" = "Linux" ]]; then
  if [ -n "$WSL_DISTRO_NAME" ] || uname -r | grep 'Microsoft'; then
    host="Win32"
  fi
fi

if [[ "$host" == *"MINGW64_NT"* ]]; then
  host="Win32"
fi

if (( TARGET_OS_IPHONE )); then
  platform="iPhoneOS"
  build_platform_dir="iPhoneOS"
elif (( TARGET_IPHONE_SIMULATOR )); then
  platform="iPhoneSimulator"
  build_platform_dir="iPhoneSimulator"
elif (( TARGET_ANDROID_EMULATOR )); then
  arch="${ARCH:-x86_64}"
  platform="AndroidEmulator"
  build_platform_dir="android"
elif (( TARGET_OS_ANDROID )); then
  arch="${ARCH:-aarch64}"
  platform="Android"
  build_platform_dir="android"
fi

if [ -z "$ANDROID_HOME" ]; then
  if [[ "$host" = "Darwin" ]]; then
    ANDROID_HOME="$HOME/Library/Android/sdk"
  elif [[ "$host" = "Linux" ]]; then
    # Default to common SDK locations (prefer the canonical Android Studio path).
    ANDROID_HOME="$HOME/Android/Sdk"
    if [[ ! -d "$ANDROID_HOME" ]] && [[ -d "$HOME/Android/sdk" ]]; then
      ANDROID_HOME="$HOME/Android/sdk"
    elif [[ ! -d "$ANDROID_HOME" ]] && [[ -d "$HOME/android" ]]; then
      ANDROID_HOME="$HOME/android"
    fi
  fi
fi

if (( !TARGET_OS_ANDROID && !TARGET_ANDROID_EMULATOR )); then
  if [[ "$(basename "$CXX")" =~ clang ]]; then
    if [[ "$host" = "Linux" ]]; then
      cflags+=("-Wno-unused-command-line-argument")
      cflags+=("-stdlib=libstdc++")
    fi
    if [[ "$host" = "Win32" ]]; then
      cflags+=("-Wno-unused-command-line-argument")
    fi
  fi
else
  source "$root/bin/android-functions.sh"
  android_fte > /dev/null
  cflags+=("-DANDROID -pthread -fexceptions -fPIC -frtti -fsigned-char -D_FILE_OFFSET_BITS=64 -Wno-invalid-command-line-argument -Wno-unused-command-line-argument")
  cflags+=("$(android_clang_target "$arch")")
  cflags+=($(android_arch_includes "$arch"))
fi

declare build_arch_dir="$arch"
if [[ "$build_platform_dir" == "android" ]]; then
  case "$build_arch_dir" in
    aarch64|arm64)
      build_arch_dir="arm64-v8a"
      ;;
    x86-64|x86_64|amd64)
      build_arch_dir="x86_64"
      ;;
  esac
fi

declare libsodium_include_dir=""
for candidate in \
  "$root/build/$build_arch_dir-$build_platform_dir/include" \
  "$root/build/libsodium/src/libsodium/include"
do
  if [[ -f "$candidate/sodium.h" && -f "$candidate/sodium/version.h" ]]; then
    libsodium_include_dir="$candidate"
    break
  fi
done

cflags+=(
  -std=c++2a
  -ferror-limit=6
  -I"$root/include"
  -I"$root/build/uv/include"
  -I"$root/build"
  -I"$root/build/jsoncons/include"
  -I"$root/build/llama"
  -I"$root/build/llama/common"
  -I"$root/build/llama/include"
  -I"$root/build/llama/ggml/include"
  -I"$root/build/whisper.cpp/include"
  -I"$root/build/libusb/libusb"
)

if [[ -n "$libsodium_include_dir" ]]; then
  cflags+=(-I"$libsodium_include_dir")
fi

cflags+=(
  -I"$root/build/include"
  -DSODIUM_STATIC
)

if [[ -z "${ORO_EXCLUDE_BUILD_METADATA:-}" ]]; then
  cflags+=(
    -DORO_RUNTIME_BUILD_TIME="$(date '+%s')"
    -DORO_RUNTIME_VERSION_HASH=$(git rev-parse --short=8 HEAD)
    -DORO_RUNTIME_VERSION=$(cat "$root/VERSION.txt")
  )
fi

iroh_manifest="$root/build/iroh/iroh/Cargo.toml"
if [[ ! -f "$iroh_manifest" ]]; then
  iroh_manifest="$root/rust/oro-iroh/Cargo.toml"
fi

if [[ -f "$iroh_manifest" ]]; then
  iroh_package_name="$(awk '
    BEGIN { in_pkg = 0 }
    /^\[package\]/ { in_pkg = 1; next }
    /^\[/ && in_pkg { exit }
    in_pkg && $1 == "name" && $2 == "=" {
      gsub(/"/, "", $3)
      print $3
      exit
    }
  ' "$iroh_manifest")"

  if [[ -z "$iroh_package_name" ]]; then
    iroh_package_name="iroh"
  fi

  iroh_version="$(awk '
    BEGIN { in_pkg = 0 }
    /^\[package\]/ { in_pkg = 1; next }
    /^\[/ && in_pkg { exit }
    in_pkg && $1 == "version" && $2 == "=" {
      gsub(/"/, "", $3)
      print $3
      exit
    }
  ' "$iroh_manifest")"

  if [[ -z "$iroh_version" ]]; then
    iroh_dir="$(dirname "$iroh_manifest")"
    iroh_lock="$iroh_dir/Cargo.lock"
    if [[ -f "$iroh_lock" ]]; then
      iroh_version="$(awk -v target="$iroh_package_name" '
        /^\[\[package\]\]/ { current=""; next }
        /^name = "/ {
          current=$0
          sub(/^name = "/, "", current)
          sub(/"$/, "", current)
          next
        }
        /^version = "/ && current == target {
          version=$0
          sub(/^version = "/, "", version)
          sub(/"$/, "", version)
          print version
          exit
        }
      ' "$iroh_lock")"
    fi
  fi

  if [[ -n "$iroh_version" ]]; then
    cflags+=("-DORO_RUNTIME_IROH_VERSION=\"$iroh_version\"")
  fi
fi

libipfs_header="$root/build/include/libipfs.h"
declare -a libipfs_candidates=()
libipfs_candidates+=("$root/build/$arch-$platform/lib/libipfs.a")
libipfs_candidates+=("$root/build/$arch-$platform/lib/libipfs.lib")
if [[ "$host" == "Win32" ]]; then
  if [[ -n "$DEBUG" ]]; then
    libipfs_candidates+=("$root/build/$arch-$platform/libd/libipfsd.a")
    libipfs_candidates+=("$root/build/$arch-$platform/libd/libipfsd.lib")
  else
    libipfs_candidates+=("$root/build/$arch-$platform/lib/libipfs.a")
    libipfs_candidates+=("$root/build/$arch-$platform/lib/libipfs.lib")
  fi
fi

have_libipfs=0
if [[ -f "$libipfs_header" ]]; then
  for candidate in "${libipfs_candidates[@]}"; do
    if [[ -f "$candidate" ]]; then
      have_libipfs=1
      break
    fi
  done
fi

if [[ "${ORO_SKIP_LIBIPFS:-0}" == "1" ]]; then
  have_libipfs=0
fi

if [[ "$host" == "Win32" ]]; then
  have_libipfs=0
fi

if (( have_libipfs )); then
  cflags+=("-DORO_RUNTIME_HAVE_LIBIPFS=1")
  if [[ -d "$root/build/libipfs/include" ]]; then
    cflags+=("-I$root/build/libipfs/include")
  fi
else
  cflags+=("-DORO_RUNTIME_HAVE_LIBIPFS=0")
fi

# Detect vendored zlib and advertise availability via ORO_RUNTIME_HAS_ZLIB so
# runtime code can include <zlib.h> only when a corresponding library and its
# generated headers are present. We intentionally look only at our build tree
# to avoid mismatches between system headers and missing libraries.
declare -a zlib_variants=("$platform")
case "$platform" in
  Android)
    zlib_variants+=("android")
    ;;
  AndroidEmulator)
    zlib_variants+=("android-emulator" "android")
    ;;
  android)
    zlib_variants+=("Android")
    ;;
esac

have_zlib=0
zlib_include_dir=""
for variant in "${zlib_variants[@]}"; do
  for libdir in "$root/build/$arch-$variant/lib" "$root/build/$arch-$variant/lib64"; do
    if [[ -f "$libdir/libz.a" ]] || [[ -f "$libdir/z.lib" ]] || [[ -f "$libdir/zlib.lib" ]]; then
      for include_dir in \
        "$root/build/$arch-$variant/include" \
        "$root/build/$arch-$variant/zlib/build/include" \
        "$root/build/$arch-$variant/zlib/include" \
        "$root/build/include"
      do
        if [[ -f "$include_dir/zlib.h" ]] && [[ -f "$include_dir/zconf.h" ]]; then
          have_zlib=1
          zlib_include_dir="$include_dir"
          break 3
        fi
      done
    fi
  done
done

if (( have_zlib )); then
  cflags+=("-DORO_RUNTIME_HAS_ZLIB=1")
  cflags+=("-I$zlib_include_dir")
else
  cflags+=("-DORO_RUNTIME_HAS_ZLIB=0")
fi

if (( !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR && !TARGET_OS_ANDROID && !TARGET_ANDROID_EMULATOR )); then
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists dbus-1 2>/dev/null; then
    cflags+=($(pkg-config --cflags dbus-1))
    cflags+=(-DORO_RUNTIME_HAVE_DBUS=1)
  else
    cflags+=(-DORO_RUNTIME_HAVE_DBUS=0)
  fi
else
  cflags+=(-DORO_RUNTIME_HAVE_DBUS=0)
fi

if (( TARGET_OS_IPHONE )) || (( TARGET_IPHONE_SIMULATOR )); then
  if (( TARGET_OS_IPHONE )); then
    ios_sdk_path="$(xcrun -sdk iphoneos -show-sdk-path)"
    cflags+=("-arch arm64")
    cflags+=("-target arm64-apple-ios")
    cflags+=("-Wno-unguarded-availability-new")
    if [ -n "$IPHONEOS_VERSION_MIN" ]; then
      cflags+=("-miphoneos-version-min=$IPHONEOS_VERSION_MIN")
    fi
  elif (( TARGET_IPHONE_SIMULATOR )); then
    ios_sdk_path="$(xcrun -sdk iphonesimulator -show-sdk-path)"
    cflags+=("-arch $arch")
    if [ -n "$IOS_SIMULATOR_VERSION_MIN" ]; then
      cflags+=("-mios-simulator-version-min=$IOS_SIMULATOR_VERSION_MIN")
    fi
  fi

  cflags+=("-iframeworkwithsysroot /System/Library/Frameworks")
  cflags+=("-isysroot $ios_sdk_path/")
  cflags+=("-F $ios_sdk_path/System/Library/Frameworks/")
  cflags+=("-fembed-bitcode")
fi

if (( !TARGET_OS_ANDROID && !TARGET_ANDROID_EMULATOR )); then
  if [[ "$host" = "Darwin" ]]; then
    cflags+=("-ObjC++")
    cflags+=("-fPIC")
  elif [[ "$host" = "Linux" ]]; then
    cflags+=($(pkg-config --cflags --static gtk+-3.0 webkit2gtk-4.1 gio-unix-2.0) -fPIC)
  elif [[ "$host" = "Win32" ]]; then
    # https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features?view=msvc-170
    # Because we can't pass /MT[d] directly, we have to manually set the flags
    cflags+=(
      -D_MT
      -D_DLL
      -DWIN32
      -DWIN32_LEAN_AND_MEAN
      -DNOMINMAX
      -DWINVER=0x0A00
      -D_WIN32_WINNT=0x0A00
      -DNTDDI_VERSION=0x0A000000
      "-Xlinker /NODEFAULTLIB:libcmt"
      "-Xlinker /NXCOMPAT"
      "-Xlinker /DYNAMICBASE"
      "-Xlinker /HIGHENTROPYVA"
      "-Xlinker /guard:cf"
      -Wno-nonportable-include-path
    )
    if [[ -n "$DEBUG" ]]; then
      cflags+=("-D_DEBUG")
    fi

    ## TODO(@jwerle): figure this out for macOS
    if [[ "$(uname -s)" == "Linux" ]]; then
      cflags+=(
        "-fdeclspec"
        "-I/usr/share/mingw-w64/include"
        "-I/usr/include/x86_64-linux-gnu/c++/12/"
        "-I/usr/lib/gcc/x86_64-w64-mingw32/10-win32/include/"
        "-I/usr/lib/gcc/x86_64-w64-mingw32/10-posix/include/c++"
        "-I/usr/lib/gcc/x86_64-w64-mingw32/10-win32/include/c++"
        "-I/usr/lib/gcc/x86_64-w64-mingw32/10-win32/include/c++/x86_64-w64-mingw32"
        "-I/usr/lib/gcc/x86_64-w64-mingw32/10-win32/include/c++/backward"
        "-DWIN32"
        "-D_WIN32"
        "-D_WIN64"
        "-D_MSC_VER=1940"
        "-D_MSC_FULL_VER=193933519"
        "-D_MSC_BUILD=0"
        "-D_GLIBCXX_HAS_GTHREADS=1"
        "-DORO_RUNTIME_CROSS_COMPILED_HOST=1"
        "-DORO_RUNTIME_PLATFORM_WANTS_MINGW=1"
        $(pkg-config gthread-2.0 --cflags)
      )
    fi
  fi
fi

# Optional developer sanitizers (desktop only)
if (( !TARGET_OS_ANDROID && !TARGET_ANDROID_EMULATOR && !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR )); then
  if [[ -n "$ORO_ENABLE_SANITIZERS" ]]; then
    if [[ "$host" = "Linux" || "$host" = "Darwin" ]]; then
      cflags+=("-fsanitize=address,undefined")
      cflags+=("-fno-omit-frame-pointer")
      cflags+=("-fsanitize-recover=undefined")
    fi
  fi
fi

# TLS provider selection
declare -a tls_defines=()
declare -a tls_cflags=()

tls_provider="${ORO_TLS_BUILD_PROVIDER:-${ORO_TLS_PROVIDER_BUILD:-}}"
if [[ -z "$tls_provider" ]]; then
  if [[ "${ORO_TLS_ENABLE_OPENSSL:-0}" == "1" ]]; then
    tls_provider="openssl"
  elif [[ "${ORO_TLS_ENABLE_GNUTLS:-0}" == "1" ]]; then
    tls_provider="gnutls"
  fi
fi
if [[ -n "$tls_provider" ]]; then
  tls_provider="$(printf '%s' "$tls_provider" | tr '[:upper:]' '[:lower:]')"
fi

if [[ "$host" == "Darwin" && "$platform" == "desktop" ]] || (( TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR || TARGET_OS_ANDROID || TARGET_ANDROID_EMULATOR )); then
  if [[ -n "$tls_provider" ]]; then
    echo "warn - ignoring TLS provider '$tls_provider' for $platform builds; continuing without a built-in TLS provider and oro:tls will return NOT_IMPLEMENTED" >&2
    tls_provider=""
  fi
fi

if [[ "$tls_provider" == "gnutls" ]]; then
  echo "not ok - GnuTLS TLS provider is not implemented in src/runtime/tls yet" >&2
  exit 1
fi

if [[ "$tls_provider" == "securetransport" ]]; then
  echo "not ok - SecureTransport TLS provider is not implemented in src/runtime/tls yet" >&2
  exit 1
fi

if [[ "$tls_provider" == "android" ]]; then
  echo "not ok - Android TLS provider is not implemented in src/runtime/tls yet" >&2
  exit 1
fi

enable_mbedtls () {
  mbedtls_header="$root/build/include/mbedtls/ssl.h"
  if [[ ! -f "$mbedtls_header" ]]; then
    echo "not ok - mbedtls headers not found at $mbedtls_header; run bin/install.sh on Linux desktop (or vendor mbedTLS into build/)" >&2
    exit 1
  fi
  mbedtls_libdir="$root/build/$arch-$platform/lib"
  for lib in libmbedtls.a libmbedx509.a libmbedcrypto.a; do
    if [[ ! -f "$mbedtls_libdir/$lib" ]]; then
      echo "not ok - missing $lib in $mbedtls_libdir; run bin/install.sh on Linux desktop (or vendor mbedTLS into build/)" >&2
      exit 1
    fi
  done
  tls_defines+=(-DORO_RUNTIME_ENABLE_MBEDTLS=1)
  tls_cflags+=(-I"$root/build/include")
  tls_cflags+=(-I"$root/build/$arch-$platform/include")
}

enable_openssl () {
  tls_manual_cflags="${ORO_TLS_CFLAGS:-${ORO_TLS_CFLAGS:-}}"
  if (( TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR || TARGET_OS_ANDROID || TARGET_ANDROID_EMULATOR )); then
    echo "not ok - OpenSSL TLS provider is not supported for mobile targets via this build script" >&2
    exit 1
  fi

  openssl_pkg=""
  if command -v pkg-config >/dev/null 2>&1; then
    for candidate in openssl openssl@3 libssl; do
      if pkg-config --exists "$candidate" 2>/dev/null; then
        openssl_pkg="$candidate"
        break
      fi
    done
  fi

  if [[ -z "$openssl_pkg" && -z "$tls_manual_cflags" ]]; then
    echo "not ok - OpenSSL not found via pkg-config (tried: openssl, openssl@3, libssl) and ORO_TLS_CFLAGS is not set" >&2
    exit 1
  fi

  tls_defines+=(-DORO_RUNTIME_TLS_OPENSSL=1)
  if [[ -n "$openssl_pkg" ]]; then
    tls_cflags+=($(pkg-config --cflags "$openssl_pkg"))
  fi
  if [[ -n "$tls_manual_cflags" ]]; then
    tls_cflags+=($tls_manual_cflags)
  fi
}

enable_schannel () {
  if [[ "$host" != "Win32" ]]; then
    echo "not ok - Schannel TLS provider is only supported when building on Windows" >&2
    exit 1
  fi

  tls_defines+=(-DORO_RUNTIME_TLS_SCHANNEL=1)
}

if [[ "$host" = "Linux" && "$platform" = "desktop" ]]; then
  if [[ -z "$tls_provider" || "$tls_provider" == "mbedtls" ]]; then
    enable_mbedtls
  elif [[ "$tls_provider" == "openssl" ]]; then
    enable_openssl
  else
    echo "not ok - unknown TLS provider '$tls_provider' (expected mbedtls|openssl)" >&2
    exit 1
  fi
else
  if [[ "$tls_provider" == "mbedtls" ]]; then
    enable_mbedtls
  fi

  if [[ "$tls_provider" == "openssl" ]]; then
    enable_openssl
  fi

  if [[ "$tls_provider" == "schannel" ]]; then
    enable_schannel
  fi

  if [[ -n "$tls_provider" ]] && [[ "$tls_provider" != "mbedtls" ]] && [[ "$tls_provider" != "openssl" ]] && [[ "$tls_provider" != "schannel" ]]; then
    echo "not ok - unknown TLS provider '$tls_provider' (expected mbedtls|openssl|schannel)" >&2
    exit 1
  fi
fi

cflags+=("${tls_defines[@]}")
cflags+=("${tls_cflags[@]}")

# Iroh FFI availability
declare -a iroh_libdirs=()
declare -a iroh_platform_variants=("$platform")
case "$platform" in
  Android)
    iroh_platform_variants+=("android")
    ;;
  AndroidEmulator)
    iroh_platform_variants+=("android-emulator" "android")
    ;;
  android)
    iroh_platform_variants+=("Android")
    ;;
esac
for variant in "${iroh_platform_variants[@]}"; do
  iroh_libdirs+=("$root/build/$arch-$variant/lib")
  if [[ "$host" = "Win32" ]]; then
    iroh_libdirs+=("$root/build/$arch-$variant/libd")
  fi
done
iroh_header=""
for candidate in \
  "$root/include/iroh/oro_iroh.h" \
  "$root/build/include/iroh/oro_iroh.h" \
  "$root/include/iroh/oro_iroh.h" \
  "$root/build/include/iroh/oro_iroh.h"
do
  if [[ -f "$candidate" ]]; then
    iroh_header="$candidate"
    break
  fi
done

iroh_has_lib=0
iroh_lib_path=""
declare -a iroh_lib_candidates=(
  "liboro_iroh.a"
  "liboro_iroh.so"
  "liboro_iroh.dylib"
  "oro_iroh.dll"
  "oro_iroh.lib"
)
for libdir in "${iroh_libdirs[@]}"; do
  for lib in "${iroh_lib_candidates[@]}"; do
    if [[ -f "$libdir/$lib" ]]; then
      iroh_has_lib=1
      iroh_lib_path="$libdir/$lib"
      break 2
    fi
  done
done

iroh_has_expected_symbols=0
if [[ -n "$iroh_lib_path" ]]; then
  if command -v nm >/dev/null 2>&1; then
    if nm --defined-only "$iroh_lib_path" 2>/dev/null | grep -Eq 'oro_iroh_node_memory|oro_iroh_node_memory'; then
      iroh_has_expected_symbols=1
    fi
  fi

  if (( !iroh_has_expected_symbols )) && command -v strings >/dev/null 2>&1; then
    if strings "$iroh_lib_path" 2>/dev/null | grep -Eq 'oro_iroh_node_memory|oro_iroh_node_memory'; then
      iroh_has_expected_symbols=1
    fi
  fi
fi

if [[ -n "$iroh_header" && $iroh_has_lib -eq 1 && $iroh_has_expected_symbols -eq 1 ]]; then
  cflags+=(-DORO_RUNTIME_HAS_IROH_FFI=1)
else
  cflags+=(-DORO_RUNTIME_HAS_IROH_FFI=0)
fi

while (( $# > 0 )); do
  cflags+=("$1")
  shift
done

if [[ -n "$DEBUG" ]]; then
  cflags+=("-g")
  cflags+=("-O0")
  cflags+=("-DORO_RUNTIME_BUILD_DEBUG=1")
else
  cflags+=("-Os")
fi

echo "${cflags[@]}"
