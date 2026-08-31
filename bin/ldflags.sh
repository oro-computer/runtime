#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

declare ldflags=()

declare args=()
declare arch="$(uname -m | sed 's/aarch64/arm64/g')"
declare host="$(uname -s)"
declare platform="desktop"

declare ios_sdk_path=""

if [[ "$host" = "Linux" ]]; then
  if [ -n "$WSL_DISTRO_NAME" ] || uname -r | grep 'Microsoft'; then
    host="Win32"
  fi
fi

if [[ "$host" == *"MINGW64_NT"* ]]; then
  host="Win32"
fi

declare d=""
if [[ "$host" == "Win32" ]]; then
  # We have to differentiate release and debug for Win32
  if [[ -n "$DEBUG" ]]; then
    d="d"
  fi
fi

if (( TARGET_OS_IPHONE )); then
  arch="arm64"
  platform="iPhoneOS"
elif (( TARGET_IPHONE_SIMULATOR )); then
  arch="x86_64"
  platform="iPhoneSimulator"
elif (( TARGET_OS_ANDROID )); then
  arch="aarch64"
  platform="Android"
elif (( TARGET_ANDROID_EMULATOR )); then
  arch="x86_64"
  platform="AndroidEmulator"
fi

while (( $# > 0 )); do
  declare arg="$1"; shift
  if [[ "$arg" = "--arch" ]]; then
    arch="$1"; shift; continue
  fi

  if [[ "$arg" = "--force" ]] || [[ "$arg" = "-f" ]]; then
   force=1; continue
  fi

  if [[ "$arg" = "--platform" ]]; then
    if [[ "$1" = "ios" ]] || [[ "$1" = "iPhoneOS" ]] || [[ "$1" = "iphoneos" ]]; then
      arch="arm64"
      platform="iPhoneOS";
      export TARGET_OS_IPHONE=1
    elif [[ "$1" = "ios-simulator" ]] || [[ "$1" = "iPhoneSimulator" ]] || [[ "$1" = "iphonesimulator" ]]; then
      arch="x86_64"
      platform="iPhoneSimulator";
      export TARGET_IPHONE_SIMULATOR=1
    elif [[ "$1" = "android" ]] || [[ "$1" = "Android" ]]; then
      arch="aarch64"
      platform="Android";
      export TARGET_OS_ANDROID=1
    elif [[ "$1" = "android-emulator" ]] || [[ "$1" = "AndroidEmulator" ]]; then
      arch="x86_64"
      platform="AndroidEmulator";
      export TARGET_ANDROID_EMULATOR=1
    else
      platform="$1";
    fi
    shift
    continue
  fi

  args+=("$arg")
done

append_pkgconfig_libs () {
  declare libs_string="$1"
  if [[ -z "$libs_string" ]]; then
    return
  fi
  read -r -a __tokens <<< "$libs_string"
  for __token in "${__tokens[@]}"; do
    if [[ "${__token}" = "-lsystemd" ]]; then
      continue
    fi
    ldflags+=("${__token}")
  done
}

if [[ "$host" = "Darwin" ]]; then
  if (( !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR )); then
    ldflags+=("-framework" "Cocoa")
    ldflags+=("-framework" "Carbon")
    ldflags+=("-framework" "IOKit")
    ldflags+=("-framework" "Security")
    ldflags+=("-framework" "SystemConfiguration")
  fi

  if (( TARGET_OS_IPHONE )) || (( TARGET_IPHONE_SIMULATOR )); then
    ldflags+=("-framework" "UIKit")

    if (( TARGET_OS_IPHONE )); then
      ios_sdk_path="$(xcrun -sdk iphoneos -show-sdk-path)"
      ldflags+=("-arch arm64")
    elif (( TARGET_IPHONE_SIMULATOR )); then
      ios_sdk_path="$(xcrun -sdk iphonesimulator -show-sdk-path)"
      ldflags+=("-arch x86_64")
    fi

    ldflags+=("-isysroot $ios_sdk_path/")
    ldflags+=("-iframeworkwithsysroot /System/Library/Frameworks/")
    ldflags+=("-F $ios_sdk_path/System/Library/Frameworks/")
  fi

  ldflags+=("-framework" "CoreFoundation")
  ldflags+=("-framework" "CoreBluetooth")
  ldflags+=("-framework" "CoreLocation")
  ldflags+=("-framework" "Foundation")
  ldflags+=("-framework" "Network")
  ldflags+=("-framework" "UniformTypeIdentifiers")
  ldflags+=("-framework" "WebKit")
  ldflags+=("-framework" "Metal")
  ldflags+=("-framework" "Accelerate")
  ldflags+=("-framework" "UserNotifications")
  ldflags+=("-framework" "OSLog")
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists dbus-1 2>/dev/null; then
    if dbus_static=$(pkg-config --libs --static dbus-1 2>/dev/null); then
      append_pkgconfig_libs "$dbus_static"
    else
      append_pkgconfig_libs "$(pkg-config --libs dbus-1)"
    fi
  fi
  if !(( TARGET_OS_IPHONE )) && !(( TARGET_IPHONE_SIMULATOR )); then
    llvm_prefix=""
    if command -v brew >/dev/null 2>&1; then
      llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
    fi
    if [[ -n "$llvm_prefix" ]]; then
      ldflags+=("-L$llvm_prefix/lib/c++")
      ldflags+=("-L$llvm_prefix/lib")
    fi
  fi
  # Optional developer sanitizers (desktop only)
  if [[ -n "$ORO_ENABLE_SANITIZERS" ]] && (( !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR )); then
    ldflags+=("-fsanitize=address,undefined")
  fi
  ldflags+=("-ldl")
  ldflags+=("-lggml")
  ldflags+=("-lggml-cpu")
  ldflags+=("-lggml-base")
  ldflags+=("-lggml-blas")
  ldflags+=("-lggml-metal")
elif [[ "$host" = "Linux" ]]; then
  ldflags+=("-ldl")
  # Link ggml static libraries on Linux desktop builds so that
  # whisper/llama integrations that depend on ggml (e.g., CLI
  # version reporting) resolve their symbols.
  if [[ "$platform" = "desktop" ]]; then
    ldflags+=("-lggml")
    ldflags+=("-lggml-cpu")
    ldflags+=("-lggml-base")
  fi
  if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists dbus-1 2>/dev/null; then
      if dbus_static=$(pkg-config --libs --static dbus-1 2>/dev/null); then
        append_pkgconfig_libs "$dbus_static"
      else
        append_pkgconfig_libs "$(pkg-config --libs dbus-1)"
      fi
    fi
    append_pkgconfig_libs "$(pkg-config --libs gtk+-3.0 webkit2gtk-4.1 gio-unix-2.0)"
  fi
  # Hardened linking on Linux: RELRO + NOW
  ldflags+=("-Wl,-z,relro")
  ldflags+=("-Wl,-z,now")
  # Optional developer sanitizers (desktop only)
  if [[ -n "$ORO_ENABLE_SANITIZERS" ]] && (( !TARGET_OS_ANDROID && !TARGET_ANDROID_EMULATOR )); then
    ldflags+=("-fsanitize=address,undefined")
  fi
elif [[ "$host" = "Win32" ]]; then
  if [[ -n "$DEBUG" ]]; then
    # https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features?view=msvc-170
    # TODO: populate debug library list from vcvars64.bat
    IFS=',' read -r -a libs <<< "$WIN_DEBUG_LIBS"
    for (( i = 0; i < ${#libs[@]}; ++i )); do
      ldflags+=("${libs[$i]}")
    done
  fi
fi

declare -a ld_platform_variants=("$platform")
case "$platform" in
  Android)
    ld_platform_variants+=("android")
    ;;
  AndroidEmulator)
    ld_platform_variants+=("android-emulator" "android")
    ;;
  android)
    ld_platform_variants+=("Android")
    ;;
esac

for variant in "${ld_platform_variants[@]}"; do
  ldflags+=("-L$root/build/$arch-$variant/lib$d")
  ldflags+=("-L$root/build/$arch-$variant/lib64$d")
done

have_sodium=0
for variant in "${ld_platform_variants[@]}"; do
  for libdir in "$root/build/$arch-$variant/lib$d" "$root/build/$arch-$variant/lib" "$root/build/$arch-$variant/lib64$d" "$root/build/$arch-$variant/lib64"; do
    if [[ -f "$libdir/libsodium.a" ]] || [[ -f "$libdir/libsodium.lib" ]]; then
      have_sodium=1
      break 2
    fi
  done
done

if (( have_sodium )); then
  ldflags+=("-lsodium")
fi

# Prefer a vendored zlib build when available. Only add -lz when a candidate
# archive is present in standard build output directories so we do not assume
# that a system zlib is installed.
have_zlib=0
for variant in "${ld_platform_variants[@]}"; do
  for libdir in "$root/build/$arch-$variant/lib$d" "$root/build/$arch-$variant/lib" "$root/build/$arch-$variant/lib64$d" "$root/build/$arch-$variant/lib64"; do
    if [[ -f "$libdir/libz.a" ]] || [[ -f "$libdir/z.lib" ]] || [[ -f "$libdir/zlib.lib" ]]; then
      have_zlib=1
      break 2
    fi
  done
done

if (( have_zlib )); then
  ldflags+=("-lz")
fi

libipfs_header="$root/build/include/libipfs.h"
have_libipfs=0
if [[ "$host" != "Win32" ]] && [[ "${ORO_SKIP_LIBIPFS:-0}" != "1" ]] && [[ -f "$libipfs_header" ]]; then
  for variant in "${ld_platform_variants[@]}"; do
    if [[ "$host" = "Win32" ]]; then
      if [[ -f "$root/build/$arch-$variant/lib$d/libipfs${d}.a" ]]; then
        have_libipfs=1
        break
      fi
      if [[ -f "$root/build/$arch-$variant/lib$d/libipfs${d}.lib" ]]; then
        have_libipfs=1
        break
      fi
      if [[ -f "$root/build/$arch-$variant/lib/libipfs.a" ]]; then
        have_libipfs=1
        break
      fi
      if [[ -f "$root/build/$arch-$variant/lib/libipfs.lib" ]]; then
        have_libipfs=1
        break
      fi
    else
      if [[ -f "$root/build/$arch-$variant/lib/libipfs.a" ]]; then
        have_libipfs=1
        break
      fi
    fi
  done
fi

if (( have_libipfs )); then
  ldflags+=("-lipfs")
  if [[ "$host" = "Darwin" ]] && (( !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR )); then
    ldflags+=("-lresolv")
  fi
fi

# TLS provider detection and linkage
declare -a tls_extra_ldflags=()

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

detect_openssl_ldflags () {
  tls_manual_ldflags="${ORO_TLS_LDFLAGS:-${ORO_TLS_LDFLAGS:-}}"
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

  if [[ -z "$openssl_pkg" && -z "$tls_manual_ldflags" ]]; then
    echo "not ok - OpenSSL not found via pkg-config (tried: openssl, openssl@3, libssl) and ORO_TLS_LDFLAGS is not set" >&2
    exit 1
  fi

  if [[ -n "$openssl_pkg" ]]; then
    tls_extra_ldflags+=($(pkg-config --libs "$openssl_pkg"))
  fi
  if [[ -n "$tls_manual_ldflags" ]]; then
    tls_extra_ldflags+=($tls_manual_ldflags)
  fi
}

link_mbedtls () {
  mbedtls_libdir="$root/build/$arch-$platform/lib"
  for lib in libmbedtls.a libmbedx509.a libmbedcrypto.a; do
    if [[ ! -f "$mbedtls_libdir/$lib" ]]; then
      echo "not ok - missing $lib in $mbedtls_libdir; run bin/install.sh on Linux desktop (or vendor mbedTLS into build/)" >&2
      exit 1
    fi
  done
  ldflags+=(-lmbedtls -lmbedx509 -lmbedcrypto)
}

link_schannel () {
  if [[ "$host" != "Win32" ]]; then
    echo "not ok - Schannel TLS provider is only supported when building on Windows" >&2
    exit 1
  fi
}

if [[ "$host" = "Linux" && "$platform" = "desktop" ]]; then
  if [[ -z "$tls_provider" || "$tls_provider" == "mbedtls" ]]; then
    link_mbedtls
  elif [[ "$tls_provider" == "openssl" ]]; then
    detect_openssl_ldflags
  else
    echo "not ok - unknown TLS provider '$tls_provider' (expected mbedtls|openssl)" >&2
    exit 1
  fi
elif [[ "$tls_provider" == "mbedtls" ]]; then
  link_mbedtls
elif [[ "$tls_provider" == "openssl" ]]; then
  detect_openssl_ldflags
elif [[ "$tls_provider" == "schannel" ]]; then
  link_schannel
elif [[ -n "$tls_provider" ]]; then
  echo "not ok - unknown TLS provider '$tls_provider' (expected mbedtls|openssl|schannel)" >&2
  exit 1
fi

if (( ${#tls_extra_ldflags[@]} )); then
  ldflags+=("${tls_extra_ldflags[@]}")
fi

declare -a iroh_dirs=()
for variant in "${ld_platform_variants[@]}"; do
  iroh_dirs+=("$root/build/$arch-$variant/lib")
  if [[ "$host" = "Win32" ]]; then
    iroh_dirs+=("$root/build/$arch-$variant/libd")
  fi
done

declare -a iroh_candidates=(
  "liboro_iroh.a"
  "liboro_iroh.so"
  "liboro_iroh.dylib"
  "oro_iroh.dll"
  "oro_iroh.lib"
)

iroh_candidate_path=""
iroh_candidate_flag=""
for dir in "${iroh_dirs[@]}"; do
  for lib in "${iroh_candidates[@]}"; do
    candidate="$dir/$lib"
    if [[ -f "$candidate" ]]; then
      iroh_candidate_path="$candidate"
      iroh_candidate_flag="-loro_iroh"
      break 2
    fi
  done
done

if [[ -n "$iroh_candidate_path" ]]; then
  iroh_has_symbol=0
  if command -v nm >/dev/null 2>&1; then
    if nm --defined-only "$iroh_candidate_path" 2>/dev/null | grep -Eq 'oro_iroh_node_memory|socket_iroh_node_memory'; then
      iroh_has_symbol=1
    fi
  fi

  if (( !iroh_has_symbol )) && command -v strings >/dev/null 2>&1; then
    if strings "$iroh_candidate_path" 2>/dev/null | grep -Eq 'oro_iroh_node_memory|socket_iroh_node_memory'; then
      iroh_has_symbol=1
    fi
  fi

  if (( iroh_has_symbol )); then
    ldflags+=("$iroh_candidate_flag")
  fi
fi

for (( i = 0; i < ${#args[@]}; ++i )); do
  ldflags+=("${args[$i]}")
done

echo "${ldflags[@]}"
