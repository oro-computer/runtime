#!/usr/bin/env bash
# vim: set syntax=bash:

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

source "$root/bin/mush.sh"
source "$root/bin/functions.sh"
source "$root/bin/android-functions.sh"
source "$root/bin/runtime-artifacts.sh"

declare runtime_artifact_name="$ORO_RUNTIME_ARTIFACT_NAME"
declare -a runtime_artifact_aliases=()
if declare -p ORO_RUNTIME_ARTIFACT_ALIASES >/dev/null 2>&1; then
  runtime_artifact_aliases=("${ORO_RUNTIME_ARTIFACT_ALIASES[@]}")
fi

declare platform="desktop"
declare host="$(host_os)"
declare arch="$(host_arch)"

if (( TARGET_OS_IPHONE )); then
  arch="arm64"
  platform="iPhoneOS"
elif (( TARGET_IPHONE_SIMULATOR )); then
  arch="x86_64"
  platform="iPhoneSimulator"
elif (( TARGET_OS_ANDROID )); then
  arch="aarch64"
  platform="android"
elif (( TARGET_ANDROID_EMULATOR )); then
  arch="x86_64"
  platform="android"
fi

while (( $# > 0 )); do
  declare arg="$1"; shift
  if [[ "$arg" = "--arch" ]]; then
    arch="$1"; shift; continue
  fi

  if [[ "$arg" = "--force" ]] || [[ "$arg" = "-f" ]]; then
    pass_force="$arg"
    force=1; continue
  fi

  if [[ "$arg" = "--platform" ]]; then
    if [[ "$1" = "ios" ]] || [[ "$1" = "iPhoneOS" ]] || [[ "$1" = "iphoneos" ]]; then
      arch="arm64"
      platform="iPhoneOS";
      export TARGET_OS_IPHONE=1
    elif [[ "$1" = "ios-simulator" ]] || [[ "$1" = "iPhoneSimulator" ]] || [[ "$1" = "iphonesimulator" ]]; then
      [[ -z "$arch" ]] && arch="x86_64"
      platform="iPhoneSimulator";
      export TARGET_IPHONE_SIMULATOR=1
    elif [[ "$1" = "android" ]] || [[ "$1" = "android-emulator" ]]; then
      platform="android";
      export TARGET_OS_ANDROID=1
    else
      platform="$1";
    fi
    shift
    continue
  fi

  # Don't rebuild if header mtimes are newer than .o files - Be sure to manually delete affected assets as required
  if [[ "$arg" == "--ignore-header-mtimes" ]]; then
    ignore_header_mtimes=1; continue
  fi

  args+=("$arg")
done

declare input="$root/oro-runtime.pc.in"
declare pkgconfig_dir="$root/build/$arch-$platform/pkgconfig"
declare output="$pkgconfig_dir/$runtime_artifact_name.pc"
declare canonical_pkg="$(basename "$output")"

mkdir -p "$pkgconfig_dir"

declare version="$(cat "$root/VERSION.txt")"
declare lib_directory="$root/build/$arch-$platform/lib"
declare include_directory="$root/build/$arch-$platform/include"
declare d=""
if [[ "$host" == "Win32" ]] && [[ -n "$DEBUG" ]]; then
  d="d"
fi

declare ldflags=()
declare dependencies=()
declare cflags=(
  "-Os"
  "-std=c++2a"
  "-fvisibility=hidden"
)

declare -a iroh_variants=("$platform")
case "$platform" in
  Android)
    iroh_variants+=("android")
    ;;
  AndroidEmulator)
    iroh_variants+=("android-emulator" "android")
    ;;
  android)
    iroh_variants+=("Android")
    ;;
esac

declare -a iroh_dirs=()
for variant in "${iroh_variants[@]}"; do
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
  "liboro_iroh.a"
  "liboro_iroh.so"
  "liboro_iroh.dylib"
  "oro_iroh.dll"
  "oro_iroh.lib"
)

for dir in "${iroh_dirs[@]}"; do
  for lib in "${iroh_candidates[@]}"; do
    if [[ -f "$dir/$lib" ]]; then
      if [[ "$lib" == *oro_iroh* ]]; then
        ldflags+=("-loro_iroh")
      else
        ldflags+=("-loro_iroh")
      fi
      break 2
    fi
  done
done

libipfs_header="$root/build/include/libipfs.h"
have_libipfs=0
if [[ "$host" != "Win32" ]] && [[ "${ORO_SKIP_LIBIPFS:-0}" != "1" ]] && [[ -f "$libipfs_header" ]]; then
  declare -a libipfs_candidates=()
  libipfs_candidates+=("$root/build/$arch-$platform/lib/libipfs.a")
  if [[ "$host" == "Win32" ]]; then
    libipfs_candidates+=("$root/build/$arch-$platform/lib$d/libipfs${d}.a")
    libipfs_candidates+=("$root/build/$arch-$platform/lib$d/libipfs${d}.lib")
    libipfs_candidates+=("$root/build/$arch-$platform/lib/libipfs.lib")
  else
    libipfs_candidates+=("$root/build/$arch-$platform/lib/libipfs.lib")
  fi

  for candidate in "${libipfs_candidates[@]}"; do
    if [[ -f "$candidate" ]]; then
      have_libipfs=1
      break
    fi
  done
fi

if (( have_libipfs )); then
  ldflags+=("-lipfs")
  if [[ -d "$root/build/libipfs/include" ]]; then
    cflags+=("-I$root/build/libipfs/include")
  fi
  cflags+=("-DORO_RUNTIME_HAVE_LIBIPFS=1")
else
  cflags+=("-DORO_RUNTIME_HAVE_LIBIPFS=0")
fi

# Prefer a vendored zlib build when available. Only add -lz when a candidate
# archive is present in our build tree so consumers don't inadvertently rely
# on a system zlib.
have_zlib=0
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

for variant in "${zlib_variants[@]}"; do
  for libdir in "$root/build/$arch-$variant/lib" "$root/build/$arch-$variant/lib64"; do
    if [[ -f "$libdir/libz.a" ]] || [[ -f "$libdir/z.lib" ]] || [[ -f "$libdir/zlib.lib" ]]; then
      have_zlib=1
      break 2
    fi
  done
done

if (( have_zlib )); then
  ldflags+=("-lz")
fi

if [ "$platform" == "iPhoneOS" ]; then
  platform="ios"
elif [ "$platform" == "iPhoneSimulator" ]; then
  platform="ios-simulator"
fi

if [ "$host" == "Linux" ]; then
  if [ "$platform" == "desktop" ]; then
    if [[ "$(basename "$CXX")" =~ clang ]]; then
      cflags+=("-stdlib=libstdc++")
      cflags+=("-Wno-unused-command-line-argument")
    fi
    cflags+=("-fPIC")
    ldflags+=("-ldl")
    dependencies+=("gtk+-3.0" "webkit2gtk-4.1")
  fi
elif [ "$host" == "Win32" ]; then
  if [ "$platform" == "desktop" ]; then
    if [[ "$(basename "$CXX")" =~ clang ]]; then
      cflags+=("-Wno-unused-command-line-argument")
    fi

    # Keep downstream extensions on the same DLL CRT as the runtime.
    crt_flag="-fms-runtime-lib=dll"
    [[ -n "$DEBUG" ]] && crt_flag="-fms-runtime-lib=dll_dbg"
    cflags+=(
      "$crt_flag"
      "-DWIN32"
      "-DWIN32_LEAN_AND_MEAN"
      "-Wno-nonportable-include-path"
    )
    ldflags+=("$crt_flag" "-Wl,-NODEFAULTLIB:libcmt")
  fi
elif [ "$host" == "Darwin" ]; then
  if [ "$platform" == "desktop" ]; then
    cflags+=("-ObjC++")
    cflags+=("-fPIC")
  fi
fi

if [ "$platform" == "android" ]; then
  android_fte > /dev/null 2>&1
  cflags+=("-DANDROID -pthread -fexceptions -fPIC -frtti -fsigned-char -D_FILE_OFFSET_BITS=64 -Wno-invalid-command-line-argument -Wno-unused-command-line-argument")
  cflags+=("$(android_clang_target "$arch")")
  cflags+=($(android_arch_includes "$arch"))
fi

if [ "$platform" == "ios" ] || [ "$platform" == "ios-simulator" ]; then
  if [ "$host" != "Darwin" ]; then
    echo "error: Cannot generate pkgconfig file for iPhoneOS or iPhoneSimulator on '$host'" >&2
    exit 1
  fi

  if [ "$platform" == "ios" ]; then
    ios_sdk_path="$(xcrun -sdk iphoneos -show-sdk-path)"
    cflags+=("-arch arm64")
    cflags+=("-target arm64-apple-ios")
    cflags+=("-Wno-unguarded-availability-new")
    cflags+=("-miphoneos-version-min=$IPHONEOS_VERSION_MIN")
  elif [ "$platform" == "ios-simulator" ]; then
    ios_sdk_path="$(xcrun -sdk iphonesimulator -show-sdk-path)"
    cflags+=("-arch $arch")
    cflags+=("-mios-simulator-version-min=$IPHONEOS_VERSION_MIN")
  fi
  cflags+=("-iframeworkwithsysroot /System/Library/Frameworks")
  cflags+=("-isysroot $ios_sdk_path/")
  cflags+=("-F $ios_sdk_path/System/Library/Frameworks/")
  cflags+=("-fembed-bitcode")
fi

export CFLAGS="${cflags[@]}"
export LDFLAGS="${ldflags[@]}"
export DEPENDENCIES="${dependencies[@]}"
export VERSION="$version"
export LIB_DIRECTORY="$lib_directory"
export INCLUDE_DIRECTORY="$include_directory"

rm -f "$output"

if ! cat "$input" | mush > "$output"; then
  exit $?
fi

python - "$output" "$runtime_artifact_name" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
name = sys.argv[2]
lines = path.read_text().splitlines()
for idx, line in enumerate(lines):
  if line.startswith("Name: "):
    lines[idx] = f"Name: {name}"
    break
path.write_text("\n".join(lines) + "\n")
PY

echo "Wrote pkgconfig file to '$output'"

for alias in "${runtime_artifact_aliases[@]}"; do
  if [[ "$alias" == "$runtime_artifact_name" ]]; then
    continue
  fi
  declare alias_path="$pkgconfig_dir/$alias.pc"
  declare alias_pkg="$(basename "$alias_path")"
  (
    cd "$pkgconfig_dir" >/dev/null 2>&1 || exit 1
    ln -sf "$canonical_pkg" "$alias_pkg"
  ) || exit $?
  echo "Linked pkgconfig alias '$alias_path' -> $canonical_pkg"
done
