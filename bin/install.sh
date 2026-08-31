#!/usr/bin/env bash

declare root=""

root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

function usage() {
  cat <<'EOF'
Usage: ./bin/install.sh [options]

Build and stage the Oro Runtime source tree.

Options:
  -h, --help                 Show this help and exit
  -f, --force                Force dependency and runtime rebuild work
  -y, --yes-deps             Accept supported dependency setup prompts
      --arch <architecture>  Override the detected host architecture
      --link                 Link the staged runtime into the install prefix
      --no-android-fte       Skip interactive Android first-time setup; this is
                             not a reliable Android artifact exclusion when an
                             Android toolchain is already configured
      --ignore-header-mtimes Do not rebuild solely for newer header mtimes

Independent mobile target exclusions:
  NO_ANDROID=<non-empty>     Disable only Android setup, ABI libraries, and
                             staged Android artifacts. This does not disable
                             iOS or any desktop target.
  NO_IOS=<non-empty>         Disable only iOS and iOS Simulator dependency,
                             library, prebuild, and staging work on macOS. This
                             does not disable Android or macOS desktop.

These are presence flags: 0 and false are non-empty and still disable the
named target. Leave a variable unset or empty to enable that target. Set both
for an explicitly desktop-only source build:

  NO_ANDROID=1 NO_IOS=1 ./bin/install.sh

The variables control runtime source bootstrap; they do not select an
application target for oroc build --platform. See docs/BUILD_ENVIRONMENT.md.
EOF
}

for arg in "$@"; do
  if [[ "$arg" == "--help" ]] || [[ "$arg" == "-h" ]]; then
    usage
    exit 0
  fi
done

source "$root/bin/functions.sh"
source "$root/bin/android-functions.sh"
source "$root/bin/runtime-artifacts.sh"

declare runtime_artifact_name="$ORO_RUNTIME_ARTIFACT_NAME"
declare -a runtime_artifact_aliases=()
if [[ -n "${ORO_RUNTIME_ARTIFACT_ALIASES+x}" ]]; then
  runtime_artifact_aliases=("${ORO_RUNTIME_ARTIFACT_ALIASES[@]}")
fi
declare canonical_runtime_lib="lib${runtime_artifact_name}"
declare runtime_link_flag="-l${runtime_artifact_name}"

if [[ -z "$CPU_CORES" ]]; then
  export CPU_CORES=$(set_cpu_cores)
fi

if [[ -n $VERBOSE ]]; then
  echo "# using cores: $CPU_CORES"
fi

if [[ -n "$NO_ANDROID" ]]; then
  unset BUILD_ANDROID
fi

declare arch="$(host_arch)"
declare args=()
declare pids=()
declare pid_labels=()
declare force=0
declare pass_force=""
# pass_ignore_header_mtimes is set during arg parsing above; avoid re-declaring
declare host="$(host_os)"
declare do_link=0

LIPO=""
declare CWD=$(pwd)
declare PREFIX="${PREFIX:-"/usr/local"}"
declare BUILD_DIR="$CWD/build"
declare -a apple_mobile_targets=()

function _configure_apple_mobile_targets() {
  apple_mobile_targets=()

  if [[ "$host" != "Darwin" ]] || [[ -n "$NO_IOS" ]]; then
    return
  fi

  local requested_targets="${ORO_CI_APPLE_MOBILE_TARGETS:-}"
  if [[ -z "$requested_targets" ]]; then
    requested_targets="arm64-iPhoneOS x86_64-iPhoneSimulator"
    if [[ "$arch" = "arm64" ]]; then
      requested_targets+=" arm64-iPhoneSimulator"
    fi
  fi

  local target=""
  local enabled_target=""
  local duplicate=0
  for target in $requested_targets; do
    case "$target" in
      arm64-iPhoneOS|x86_64-iPhoneSimulator|arm64-iPhoneSimulator) ;;
      *) die 1 "not ok - unsupported Apple-mobile target: $target" ;;
    esac

    duplicate=0
    for enabled_target in "${apple_mobile_targets[@]}"; do
      if [[ "$enabled_target" == "$target" ]]; then
        duplicate=1
        break
      fi
    done

    if (( !duplicate )); then
      apple_mobile_targets+=("$target")
    fi
  done

  if (( ${#apple_mobile_targets[@]} == 0 )); then
    die 1 "not ok - no Apple-mobile targets were selected"
  fi

  echo "# Apple-mobile targets: ${apple_mobile_targets[*]}"
}

while (( $# > 0 )); do
  declare arg="$1"; shift
  if [[ "$arg" = "--arch" ]]; then
    arch="$1"; shift; continue
  fi

  if [[ "$arg" = "--force" ]] || [[ "$arg" = "-f" ]]; then
    pass_force="$arg"
    force=1; continue
  fi

  if [[ "$arg" = "--yes-deps" ]] || [[ "$arg" = "-y" ]]; then
    pass_yes_deps="$arg"; continue
  fi

  if [[ "$arg" == "--no-android-fte" ]]; then
    no_android_fte=1; continue
  fi

  if [[ "$arg" == "--link" ]]; then
    do_link=1; continue
  fi

  # Don't rebuild if header mtimes are newer than .o files - Be sure to manually delete affected assets as required
  if [[ "$arg" == "--ignore-header-mtimes" ]]; then
    pass_ignore_header_mtimes="$arg"
    ignore_header_mtimes=1; continue
  fi

  args+=("$arg")
done

_configure_apple_mobile_targets

if [[ "$host" = "Linux" ]]; then
  if [ -n "$WSL_DISTRO_NAME" ] || uname -r | grep 'Microsoft'; then
    echo "not ok - WSL is not supported."
    exit 1
  fi
fi

declare default_runtime_home=""

if [[ "$host" == "Win32" ]]; then
  declare appdata_base="${LOCALAPPDATA:-"$HOME/AppData/Local"}"
  default_runtime_home="$appdata_base/Programs/oro"
else
  declare data_home="${XDG_DATA_HOME:-"$HOME/.local/share"}"
  default_runtime_home="$data_home/oro"
fi

declare runtime_home_source="${ORO_HOME:-$default_runtime_home}"

if [[ -z "$runtime_home_source" ]]; then
  runtime_home_source="$default_runtime_home"
fi

mkdir -p "$runtime_home_source"

ORO_HOME="$runtime_home_source"
export ORO_HOME

if [[ "$host" == "Win32" ]] && [[ "$PREFIX" == "/usr/local" ]] && [[ ! -d "$PREFIX/bin" ]]; then
  # User probably doesn't want to install to /usr/local on windows. Reset PREFIX so script doesn't terminate later.
  PREFIX="$ORO_HOME"
fi

write_log "h" "Installing to '$PREFIX'"

if [[ "$ORO_INSTALL_MODE" == "probe-env" ]]; then
  echo "ORO_HOME=$ORO_HOME"
  exit 0
fi

declare pass_ignore_header_mtimes=""

declare d=""
if [[ "$host" == "Win32" ]]; then
  # We have to differentiate release and debug for Win32
  # Problem:
  # When building libuv and the runtime static library with debug enabled, our apps crash when
  # using ifstream:
  # `Debug Assertion Failed. Expression: (_osfile(fh) & fopen)`
  # This build issue also prevents debugging in Visual Studio.

  # This occurs because by default clang incorrectly links to the non
  # threaded, production version of C runtime .lib (libcrt)
  # After taking the nessary steps to manually link to the correct lib
  # (Including adding preprocessor definitions for /MT[d]), see ldflags.sh under Win32
  # the CLI and apps won't link, therefore:

  # Solution:
  # Splits debug and release build artifacts:
  # d is set if $DEBUG and $host == Win32
  # The file[d].lib suffix is commonly used within the Windows SDK to differentiate debug and non debug files
  # In Visual Studio, Debug profiles usually have to be manually modified to include eg ole32d.lib instead ole32.lib.
  # I have used this convention to separate debug objects and libs where possible
  # *.o files are now named *$d.o
  # Libs are copied to build/platform/lib$d (libuv.lib didn't support being renamed, this would require modification of the build chain)
  # The runtime archive (liboro-runtime.a) now follows the same debug suffix convention.
  # oroc build --prod defines whether or not the app is being built for debug
  # and therefore links to the app being built to the correct liboro-runtime$d.a
  if [[ -n "$DEBUG" ]]; then
    d="d"
  fi
fi

determine_cxx || exit $?
read_env_data

declare package_manager="$(determine_package_manager)"

function advice {
  local sudo="sudo ";
  [[ "$package_manager" = "brew install" ]] && sudo=""
  echo "$sudo""$package_manager $1"
}

declare _cmake_supports_fresh_cache=""
declare _toolchain_supports_openmp_cache=""

# Refresh staged CMake build trees when cached absolute paths no longer match.
function _cmake_supports_fresh () {
  if [[ -z "$_cmake_supports_fresh_cache" ]]; then
    if cmake --help 2>/dev/null | grep -q -- '--fresh'; then
      _cmake_supports_fresh_cache=1
    else
      _cmake_supports_fresh_cache=0
    fi
  fi

  [[ "$_cmake_supports_fresh_cache" == "1" ]]
}

function _cmake_build_dir_needs_refresh () {
  local source_dir="$1"
  local build_dir="$2"
  local cache_file="$build_dir/CMakeCache.txt"

  if [[ ! -f "$cache_file" ]]; then
    return 1
  fi

  local expected_source="$(cd "$source_dir" 2>/dev/null && pwd -P)"
  local expected_build="$(mkdir -p "$build_dir" && cd "$build_dir" 2>/dev/null && pwd -P)"
  if [[ -z "$expected_source" ]] || [[ -z "$expected_build" ]]; then
    return 1
  fi

  local cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache_file" | tail -n 1)"
  local cached_build="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$cache_file" | tail -n 1)"

  if [[ -n "$cached_source" ]] && [[ "$cached_source" != "$expected_source" ]]; then
    return 0
  fi

  if [[ -n "$cached_build" ]] && [[ "$cached_build" != "$expected_build" ]]; then
    return 0
  fi

  return 1
}

function _cmake_configuration_signature () {
  local source_dir="$1"
  local build_dir="$2"
  shift 2

  local source_path=""
  local build_path=""
  source_path="$(cd "$source_dir" 2>/dev/null && pwd -P)"
  build_path="$(mkdir -p "$build_dir" && cd "$build_dir" 2>/dev/null && pwd -P)"

  printf 'schema=oro-cmake-v1\n'
  printf 'source=%q\n' "$source_path"
  printf 'build=%q\n' "$build_path"
  printf 'CC=%q\n' "${CC:-}"
  printf 'CXX=%q\n' "${CXX:-}"
  printf 'CFLAGS=%q\n' "${CFLAGS:-}"
  printf 'CXXFLAGS=%q\n' "${CXXFLAGS:-}"
  printf 'OBJCFLAGS=%q\n' "${OBJCFLAGS:-}"
  printf 'OBJCXXFLAGS=%q\n' "${OBJCXXFLAGS:-}"
  printf 'LDFLAGS=%q\n' "${LDFLAGS:-}"

  local argument=""
  for argument in "$@"; do
    printf 'arg=%q\n' "$argument"
  done
}

function _cmake_configure () {
  local source_dir="$1"
  local build_dir="$2"
  shift 2

  local refresh=0
  local cache_file="$build_dir/CMakeCache.txt"
  local configuration_file="$build_dir/.oro-cmake-configuration"
  local configuration=""
  configuration="$(_cmake_configuration_signature "$source_dir" "$build_dir" "$@")"

  if (( force )); then
    refresh=1
  elif _cmake_build_dir_needs_refresh "$source_dir" "$build_dir"; then
    refresh=1
  elif [[ -f "$cache_file" ]] && [[ -f "$configuration_file" ]] && \
       [[ "$(cat "$configuration_file")" != "$configuration" ]]; then
    refresh=1
  fi

  if (( refresh )); then
    if [[ -n "$VERBOSE" ]]; then
      echo "# refreshing CMake build tree at $build_dir"
    fi

    if _cmake_supports_fresh; then
      quiet cmake --fresh -S "$source_dir" -B "$build_dir" "$@" || return $?
    else
      quiet cmake -E rm -f "$build_dir/CMakeCache.txt" || return $?
      quiet cmake -E remove_directory "$build_dir/CMakeFiles" || return $?
      quiet cmake -S "$source_dir" -B "$build_dir" "$@" || return $?
    fi
  else
    quiet cmake -S "$source_dir" -B "$build_dir" "$@" || return $?
  fi

  if [[ ! -f "$configuration_file" ]] || [[ "$(cat "$configuration_file")" != "$configuration" ]]; then
    printf '%s\n' "$configuration" > "$configuration_file"
  fi

  return 0
}

function _darwin_third_party_warning_flags () {
  if [[ "$host" != "Darwin" ]]; then
    return 0
  fi

  # Third-party native deps pull in Apple SDK headers that emit tens of thousands
  # of warnings under current Xcode/CLT releases. Use a single compiler-wide
  # suppression flag here so CMake propagates it reliably across C, C++, ObjC,
  # and ObjC++ compilation units.
  printf '%s' "-w"
}

function _autotools_configure_needed () {
  local stage_dir="$1"
  local expected_prefix="$2"
  local makefile="$stage_dir/Makefile"
  local config_status="$stage_dir/config.status"

  if (( force )); then
    return 0
  fi

  if [[ ! -f "$makefile" ]]; then
    return 0
  fi

  if [[ -f "$config_status" ]] && ! grep -Fq "$expected_prefix" "$config_status"; then
    return 0
  fi

  if ! grep -Fq "$expected_prefix" "$makefile"; then
    return 0
  fi

  return 1
}

function _toolchain_supports_openmp () {
  if [[ -z "$_toolchain_supports_openmp_cache" ]]; then
    local source_file="$(mktemp "${TMPDIR:-/tmp}/oro-openmp-check.XXXXXX.c")"
    local output_file="${source_file%.c}"

    printf 'int main(void) { return 0; }\n' > "$source_file"
    if "$CC" -fopenmp "$source_file" -o "$output_file" >/dev/null 2>&1; then
      _toolchain_supports_openmp_cache=1
    else
      _toolchain_supports_openmp_cache=0
    fi

    quiet cmake -E rm -f "$source_file" "$output_file"
  fi

  [[ "$_toolchain_supports_openmp_cache" == "1" ]]
}

function _resolve_darwin_libomp () {
  local -a llvms=()
  local libomp_path=""

  if compgen -G "/opt/homebrew/opt/llvm*" >/dev/null; then
    llvms+=(/opt/homebrew/opt/llvm*)
  fi

  if compgen -G "/usr/local/opt/llvm*" >/dev/null; then
    llvms+=(/usr/local/opt/llvm*)
  fi

  if [[ -n "${LLVM_PATHS:-}" ]]; then
    while IFS= read -r path; do
      if [[ -n "$path" ]]; then
        llvms+=("$path")
      fi
    done < <(printf '%s' "$LLVM_PATHS" | tr ':' '\n')
  fi

  if command -v brew >/dev/null 2>&1; then
    local brew_llvm_prefix="$(brew --prefix llvm 2>/dev/null || true)"
    if [[ -n "$brew_llvm_prefix" ]]; then
      llvms+=("$brew_llvm_prefix")
    fi
  fi

  for path in "${llvms[@]}"; do
    local libomp="$(find -L "$path" -path '*/lib/libomp.dylib' 2>/dev/null | head -n1)"
    if [[ -n "$libomp" ]] && [[ -f "$libomp" ]]; then
      libomp_path="$libomp"
      break
    fi
  done

  if [[ -z "$libomp_path" ]] || [[ ! -f "$libomp_path" ]]; then
    local fallback_prefix=""
    if command -v brew >/dev/null 2>&1; then
      fallback_prefix="$(brew --prefix libomp 2>/dev/null || true)"
    fi
    if [[ -n "$fallback_prefix" ]] && [[ -f "$fallback_prefix/lib/libomp.dylib" ]]; then
      libomp_path="$fallback_prefix/lib/libomp.dylib"
    fi
  fi

  printf '%s' "$libomp_path"
}

function _wait_for_queued_dependency () {
  local pid="${pids[0]:-}"
  local label="${pid_labels[0]:-target dependency}"
  local rc=0

  if [[ -n "$pid" ]]; then
    wait "$pid" 2>/dev/null
    rc=$?
  fi

  pids=("${pids[@]:1}")
  pid_labels=("${pid_labels[@]:1}")

  if (( rc != 0 )); then
    echo >&2 "not ok - $label build failed"
  fi
  return "$rc"
}

function _queue_target_dependency () {
  local label="$1"
  shift

  local builder_limit="$CPU_CORES"
  local builder_jobs=1
  if (( CPU_CORES >= 4 )); then
    builder_limit=2
    builder_jobs=$(( CPU_CORES / builder_limit ))
  fi

  while (( ${#pids[@]} >= builder_limit )); do
    _wait_for_queued_dependency
    die $? "not ok - target dependency build failed"
  done

  (
    export CPU_CORES="$builder_jobs"
    export CARGO_BUILD_JOBS="$builder_jobs"
    "$@"
  ) &
  pids+=("$!")
  pid_labels+=("$label")
}

function _wait_for_target_dependencies () {
  local message="$1"
  local status=0

  while (( ${#pids[@]} > 0 )); do
    if ! _wait_for_queued_dependency; then
      status=1
    fi
  done

  die "$status" "$message"
}

if [[ "$host" != "Win32" ]]; then
  if ! quiet command -v sudo; then
    sudo () {
      "$@"
      return $?
    }
  fi
fi

if [[ "$(uname -s)" != *"_NT"* ]]; then
  quiet command -v make
  die $? "not ok - missing build tools, try \"$(advice "make")\""
fi

if [ "$host" == "Darwin" ]; then
  quiet command -v automake
  die $? "not ok - missing build tools, try \"$(advice "automake")\""
  quiet command -v glibtoolize
  die $? "not ok - missing build tools, try \"$(advice "libtool")\""
  quiet command -v libtool
  die $? "not ok - missing build tools, try \"$(advice "libtool")\""
  quiet command -v curl
  die $? "not ok - missing curl, try \"$(advice "curl")\""
fi

if [ "$host" == "Linux" ]; then
  quiet command -v autoconf
  die $? "not ok - missing build tools, try \"$(advice "autoconf")\""
  quiet command -v pkg-config
  die $? "not ok - missing pkg-config tool, \"$(advice 'pkg-config')\""
  quiet command -v libtoolize
  die $? "not ok - missing build tools, try \"$(advice "libtool")\""
  quiet command -v curl
  die $? "not ok - missing curl, \"$(advice 'curl')\""
fi

 

  if [[ -n "$BUILD_ANDROID" ]] && [[ "arm64" == "$(host_arch)" ]] && [[ "Linux" == "$host" ]]; then
    echo "warn - Android not supported on "$host"-"$(uname -m)", will unset BUILD_ANDROID"
    unset BUILD_ANDROID
  fi

  if [[ -n "$no_android_fte" ]] && [[ -z "$ANDROID_HOME" ]]; then
    unset BUILD_ANDROID
  fi

if [[ -n "$BUILD_ANDROID" ]]; then
  android_fte "$pass_yes_deps" && rc=$?
  # android_fte will unset BUILD_ANDROID if user elects not to install
fi

if [[ -n "$BUILD_ANDROID" ]]; then
  abis=($(android_supported_abis))
  platform="android"
  clang="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$(host_arch)")"

  if ! quiet "$clang" -v; then
    echo "not ok - Android clang call failed. This could indicate an issue with ANDROID_HOME, missing ndk tools, or incorrectly determined host or target architectures."
    exit 1
  fi
fi

function _build_cli {
  local arch="$(host_arch)"
  local platform="desktop"
  local src="$root/src"
  local output_directory="$BUILD_DIR/$arch-$platform"

  echo "# building cli for desktop ($arch)..."

  # Expansion won't work under _NT
  # uv found by -L
  # referenced directly below
  # local libs=(-luv -lllama "$runtime_link_flag")
  local -a libs=()

  if [[ "$(uname -s)" != *"_NT"* ]]; then
    #
    # Add libuv and the runtime archive; on macOS we also link
    # against llama via -lllama. On Linux, the CLI already links
    # libllama via the explicit static archive group below, so we
    # avoid an extra -lllama here to keep the linker happy in dev
    # environments where pkg-config/lib paths may not be fully set.
    #
    if [[ "$(uname -s)" == "Darwin" ]]; then
      libs=(-luv -lllama "$runtime_link_flag")
      if [[ -f "$BUILD_DIR/$arch-$platform/lib/libusb-1.0.a" ]]; then
        libs+=("-lusb-1.0")
      fi
    else
      libs=(-luv "$runtime_link_flag")
    fi

    # Add whisper only if it was successfully built
    if [[ -f "$BUILD_DIR/$arch-$platform/lib/libwhisper.a" ]] || [[ -f "$BUILD_DIR/$arch-$platform/lib64/libwhisper.a" ]]; then
      libs+=("-lwhisper")
    fi
  fi

  if [[ -n "$VERBOSE" ]]; then
    echo "# cli libs: ${libs[*]}, $(uname -s)"
  fi

  local -a ldflags=()
  read -r -a ldflags <<< "$("$root/bin/ldflags.sh" --arch "$arch" --platform "$platform" "${libs[@]}")"
  local cflags=($("$root/bin/cflags.sh"))

  local test_headers=()
  if [[ -z "$ignore_header_mtimes" ]]; then
    while IFS= read -r header; do
      test_headers+=("$header")
    done < <(find "$src"/cli -name '*.hh' 2>/dev/null)
  fi
  test_headers+=("$src"/../VERSION.txt)
  local newest_mtime=0
  newest_mtime="$(latest_mtime "${test_headers[@]}")"

  local win_static_libs=()
  local static_libs=()
  local test_sources=($(find "$src"/cli/*.cc 2>/dev/null))
  local sources=()
  local outputs=()

  mkdir -p "$BUILD_DIR/$arch-$platform/bin"
  local build_cli=0

  for source in "${test_sources[@]}"; do
    local output="${source/$src/$output_directory}"
    # For some reason cli causes issues when debug and release are in the same folder
    output="${output/.cc/$d.o}"
    output="${output/cli/cli$d}"
    if (( force )) || ! test -f "$output" || (( newest_mtime > $(stat_mtime "$output") )) || (( newest_mtime > $(stat_mtime "$output") )) || (( $(stat_mtime "$source") > $(stat_mtime "$output") )); then
      sources+=("$source")
      outputs+=("$output")
      build_cli=1
    fi
  done

  for (( i = 0; i < ${#sources[@]}; i++ )); do
    mkdir -p "$(dirname "${outputs[$i]}")"
    quiet "$CXX" "${cflags[@]}"  \
      -c "${sources[$i]}"      \
      -o "${outputs[$i]}"
    die $? "$CXX ${cflags[*]} -c \"${sources[$i]}\" -o \"${outputs[$i]}\""
  done

  local exe=""
  local obj_files=($(find "$BUILD_DIR/$arch-$platform"/cli$d/*$d.o 2>/dev/null))
  local libipfs_archive="$BUILD_DIR/$arch-$platform/lib/libipfs.a"

  if [[ "$(uname -s)" == *"_NT"* ]]; then
    declare d=""
    if [[ -n "$DEBUG" ]]; then
      d="d"
    fi
    exe=".exe"
    win_static_libs+=("$BUILD_DIR/$arch-$platform/lib$d/${canonical_runtime_lib}${d}.a")
    win_static_libs+=("$BUILD_DIR/$arch-$platform/lib$d/llama.lib")
    win_static_libs+=("$BUILD_DIR/$arch-$platform/lib$d/whisper.lib")
    if [[ "${ORO_SKIP_LIBIPFS:-0}" != "1" ]]; then
      local libipfs_win_archive="$BUILD_DIR/$arch-$platform/lib$d/libipfs${d}.lib"
      local libipfs_win_archive_a="$BUILD_DIR/$arch-$platform/lib$d/libipfs${d}.a"
      if [[ -f "$libipfs_win_archive" ]]; then
        win_static_libs+=("$libipfs_win_archive")
      elif [[ -f "$libipfs_win_archive_a" ]]; then
        win_static_libs+=("$libipfs_win_archive_a")
      elif [[ -f "$libipfs_archive" ]]; then
        win_static_libs+=("$libipfs_archive")
      fi
    fi
  elif [[ "$(uname -s)" == "Linux" ]]; then
    # On Linux, ensure static archives participating in mutual references are
    # resolved by the linker by grouping them. This avoids undefined references
    # when symbols are spread across these archives.
    static_libs+=("-Wl,--start-group")
    static_libs+=("$BUILD_DIR/$arch-$platform/lib/libuv.a")
    static_libs+=("$BUILD_DIR/$arch-$platform/lib/libusb-1.0.a")
    if [[ "${ORO_SKIP_LIBIPFS:-0}" != "1" ]] && [[ -f "$libipfs_archive" ]]; then
      static_libs+=("$libipfs_archive")
    fi
    if [[ -f "$BUILD_DIR/$arch-$platform/lib64/libllama.a" ]]; then
      static_libs+=("$BUILD_DIR/$arch-$platform/lib64/libllama.a")
    elif [[ -f "$BUILD_DIR/$arch-$platform/lib/libllama.a" ]]; then
      static_libs+=("$BUILD_DIR/$arch-$platform/lib/libllama.a")
    fi
    if [[ -f "$BUILD_DIR/$arch-$platform/lib/libwhisper.a" ]]; then
      static_libs+=("$BUILD_DIR/$arch-$platform/lib/libwhisper.a")
    fi
    static_libs+=("$BUILD_DIR/$arch-$platform/lib/libmbedtls.a")
    static_libs+=("$BUILD_DIR/$arch-$platform/lib/libmbedx509.a")
    static_libs+=("$BUILD_DIR/$arch-$platform/lib/libmbedcrypto.a")
    static_libs+=("$BUILD_DIR/$arch-$platform/lib/${canonical_runtime_lib}.a")
    static_libs+=("-Wl,--end-group")
  elif [[ "$(uname -s)" == "Darwin" ]]; then
    if _toolchain_supports_openmp; then
      cflags+=("-fopenmp")
    else
      echo "warn - skipping OpenMP for the macOS CLI link step because the current toolchain does not support -fopenmp"
    fi
    if [[ "${ORO_SKIP_LIBIPFS:-0}" != "1" ]] && [[ -f "$libipfs_archive" ]]; then
      static_libs+=("$libipfs_archive")
    fi
  fi

  # Include built static libs in the mtime check so we relink when they change
  libs=($(find "$root/build/$arch-$platform/lib$d" -maxdepth 1 -type f 2>/dev/null))
  obj_files+=(${libs[@]})
  local oroc_output="$BUILD_DIR/$arch-$platform/bin/oroc$exe"

  for source in "${obj_files[@]}"; do
    if (( force )) || (( build_cli )) || ! test -f "$oroc_output" || (( $(stat_mtime "$source") > $(stat_mtime "$oroc_output") )); then
      build_cli=1
      # break
    fi
  done

  if (( build_cli )); then
    #
    # TODO "$static_libs" where it was doesn't work, if windows requires it to
    # be where it was, there should be a separate branch for windows.
    #
    quiet "$CXX"                                 \
      "${win_static_libs[@]}"                    \
      "$BUILD_DIR/$arch-$platform"/cli$d/*$d.o   \
      "${static_libs[@]}"                        \
      "${cflags[@]}"                             \
      "${ldflags[@]}"                            \
      -o "$oroc_output"

    die $? "not ok - unable to build. See trouble shooting guide in the README.md file:\n$CXX ${cflags[*]} ${ldflags[*]} -o \"$BUILD_DIR/$arch-$platform/bin/oroc\""
    echo "ok - built the cli for desktop"
  fi
}

function _build_runtime_library() {
  local arch="$(host_arch)"
  echo "# building runtime library"
  local -a runtime_arches=("$arch")
  local -a runtime_platforms=("desktop")
  local runtime_pids=()

  if [[ "$host" = "Darwin" ]] && [[ -z "$NO_IOS" ]]; then
    local apple_target=""
    for apple_target in "${apple_mobile_targets[@]}"; do
      runtime_arches+=("${apple_target%%-*}")
      runtime_platforms+=("${apple_target#*-}")
    done
  fi

  if [[ -n "$BUILD_ANDROID" ]]; then
    for abi in $(android_supported_abis); do
      runtime_arches+=("$abi")
      runtime_platforms+=("android")
    done
  fi

  local runtime_target_count=${#runtime_arches[@]}
  local runtime_jobs=$(( CPU_CORES / runtime_target_count ))
  local runtime_job_remainder=$(( CPU_CORES % runtime_target_count ))
  if (( runtime_jobs < 1 )); then
    runtime_jobs=1
    runtime_job_remainder=0
  fi

  local runtime_status=0
  local runtime_rc=0
  local target_jobs=0
  local index=0
  echo "# distributing $CPU_CORES compile jobs across $runtime_target_count runtime targets"

  for index in "${!runtime_arches[@]}"; do
    target_jobs=$runtime_jobs
    if (( index < runtime_job_remainder )); then
      ((target_jobs += 1))
    fi

    ORO_RUNTIME_BUILD_JOBS="$target_jobs" \
      "$root/bin/build-runtime-library.sh" \
        --arch "${runtime_arches[$index]}" \
        --platform "${runtime_platforms[$index]}" \
        $pass_force $pass_ignore_header_mtimes & runtime_pids+=("$!")
  done

  for pid in "${runtime_pids[@]}"; do
    wait "$pid" 2>/dev/null
    runtime_rc=$?
    if (( runtime_rc != 0 )); then
      runtime_status=1
    fi
  done

  die "$runtime_status" "not ok - unable to build runtime library"
}

function _get_web_view2() {
  if [[ "$(uname -s)" != *"_NT"* ]] && [ -z "$FORCE_WEBVIEW2_DOWNLOAD" ]; then
    return
  fi

  local arch="$(host_arch)"
  local platform="desktop"
  local webview2_license_dir="$BUILD_DIR/webview2"

  if [ -z "$FORCE_WEBVIEW2_DOWNLOAD" ] && \
    test -f "$BUILD_DIR/$arch-$platform/lib$d/WebView2LoaderStatic.lib" && \
    test -f "$webview2_license_dir/LICENSE.txt" && \
    test -f "$webview2_license_dir/NOTICE.txt"; then
    echo "$BUILD_DIR/$arch-$platform/lib$d/WebView2LoaderStatic.lib exists."
    return
  fi

  local tmp=$(mktemp -d)
  local pwd=$(pwd)
  local webview2_sha256="805c79e05184fab18c9fe7b8ba820c598399b97adc1fbf5b0ea490efad91d5b8"

  echo "# Downloading Webview2"

  curl --fail --location --silent --show-error https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2592.51 --output "$tmp/webview2.zip"
  local observed_webview2_sha256
  if command -v sha256sum >/dev/null 2>&1; then
    observed_webview2_sha256="$(sha256sum "$tmp/webview2.zip" | awk '{print $1}')"
  else
    observed_webview2_sha256="$(shasum -a 256 "$tmp/webview2.zip" | awk '{print $1}')"
  fi
  if [[ "$observed_webview2_sha256" != "$webview2_sha256" ]]; then
    echo >&2 "not ok - WebView2 archive checksum mismatch"
    rm -rf "$tmp"
    return 1
  fi
  cd "$tmp" || exit 1
  unzip -q "$tmp/webview2.zip"
  mkdir -p "$BUILD_DIR/include"
  mkdir -p "$BUILD_DIR/$arch-$platform/lib$d"/
  mkdir -p "$webview2_license_dir"

  cp -pf build/native/include/WebView2.h "$BUILD_DIR/include/WebView2.h"
  cp -pf build/native/include/WebView2EnvironmentOptions.h "$BUILD_DIR/include/WebView2EnvironmentOptions.h"
  cp -pf build/native/x64/WebView2LoaderStatic.lib "$BUILD_DIR/$arch-$platform/lib$d/WebView2LoaderStatic.lib"
  cp -pf LICENSE.txt "$webview2_license_dir/LICENSE.txt"
  cp -pf NOTICE.txt "$webview2_license_dir/NOTICE.txt"

  cd "$pwd"

  rm -rf "$tmp"
}

function _prebuild_desktop_main () {
  echo "# precompiling main program for desktop"
  local arch="$(host_arch)"
  local platform="desktop"

  local src="$root/src"
  local objects="$BUILD_DIR/$arch-$platform/objects"

  local test_headers=()
  if [[ -z "$ignore_header_mtimes" ]]; then
    while IFS= read -r header; do
      test_headers+=("$header")
    done < <(find "$src" -name '*.hh' 2>/dev/null)
  fi
  local newest_mtime=0
  newest_mtime="$(latest_mtime "${test_headers[@]}")"

  local cflags=($("$root/bin/cflags.sh"))
  local test_sources=($(find "$src"/desktop/*.{cc,mm} 2>/dev/null))
  local sources=()
  local outputs=()

  mkdir -p "$objects"

  for source in "${test_sources[@]}"; do
    local output="${source/$src/$objects}"
    output="${output/.cc/$d.o}"
    output="${output/.mm/$d.o}"
    if (( force )) || ! test -f "$output" || (( newest_mtime > $(stat_mtime "$output") )) || (( $(stat_mtime "$source") > $(stat_mtime "$output") )); then
      sources+=("$source")
      outputs+=("$output")
    fi
  done

  for (( i = 0; i < ${#sources[@]}; i++ )); do
    mkdir -p "$(dirname "${outputs[$i]}")"
    quiet "$CXX" "${cflags[@]}" \
      -c "${sources[$i]}"       \
      -o "${outputs[$i]}"
    die $? "not ok - unable to build. See trouble shooting guide in the README.md file:\n$CXX ${cflags[*]} -c ${sources[$i]} -o ${outputs[$i]}"
  done

  echo "ok - precompiled main program for desktop"
}

function _prebuild_ios_main () {
  echo "# precompiling main program for iOS"
  local arch="arm64"
  local platform="iPhoneOS"

  local src="$root/src"
  local objects="$BUILD_DIR/$arch-$platform/objects"

  local clang="$(xcrun -sdk iphoneos -find clang++)"
  local cflags=($(TARGET_OS_IPHONE=1 ARCH="$arch" "$root/bin/cflags.sh"))
  local test_sources=($(find "$src"/ios/*.mm 2>/dev/null))
  local sources=()
  local outputs=()

  mkdir -p "$objects"

  for source in "${test_sources[@]}"; do
    local output="${source/$src/$objects}"
    output="${output/.cc/$d.o}"
    output="${output/.mm/$d.o}"
    if (( force )) || ! test -f "$output" || (( $(stat_mtime "$source") > $(stat_mtime "$output") )); then
      sources+=("$source")
      outputs+=("$output")
    fi
  done

  for (( i = 0; i < ${#sources[@]}; i++ )); do
    mkdir -p "$(dirname "${outputs[$i]}")"
    "$clang" "${cflags[@]}" \
      -c "${sources[$i]}"   \
      -o "${outputs[$i]}"
    die $? "not ok - unable to build. See trouble shooting guide in the README.md file:\n$clang ${cflags[*]} -c ${sources[$i]} -o ${outputs[$i]}"
  done
  echo "ok - precompiled main program for iOS"
}

function _prebuild_ios_simulator_main () {
  echo "# precompiling main program for iOS Simulator"
  local arch="$1"
  local platform="iPhoneSimulator"

  local src="$root/src"
  local objects="$BUILD_DIR/$arch-$platform/objects"

  local clang="$(xcrun -sdk iphonesimulator -find clang++)"
  local cflags=($(TARGET_IPHONE_SIMULATOR=1 ARCH="$arch" $root/bin/cflags.sh))
  local test_sources=($(find "$src"/ios/*.mm 2>/dev/null))
  local sources=()
  local outputs=()

  mkdir -p "$objects"

  for source in "${test_sources[@]}"; do
    local output="${source/$src/$objects}"
    output="${output/.cc/$d.o}"
    output="${output/.mm/$d.o}"
    if (( force )) || ! test -f "$output" || (( $(stat_mtime "$source") > $(stat_mtime "$output") )); then
      sources+=("$source")
      outputs+=("$output")
    fi
  done

  for (( i = 0; i < ${#sources[@]}; i++ )); do
    mkdir -p "$(dirname "${outputs[$i]}")"
    quiet "$clang" "${cflags[@]}" \
      -c "${sources[$i]}"         \
      -o "${outputs[$i]}"
    die $? "not ok - unable to build. See trouble shooting guide in the README.md file:\n$clang ${cflags[*]} -c \"${sources[$i]}\" -o \"${outputs[$i]}\""
  done
  echo "ok - precompiled main program for iOS Simulator ($arch)"
}

function _prepare {
  echo "# preparing directories..."
  local arch="$(host_arch)"
  rm -rf "$ORO_HOME"/{lib$d,src,bin,include,objects,api,pkgconfig}
  rm -rf "$ORO_HOME"/share/man/{man1,man3,man7}
  rm -rf "$ORO_HOME"/share/doc/oroc
  rm -rf "$ORO_HOME"/{lib$d,objects}/"$arch-desktop"

  mkdir -p "$ORO_HOME"/{lib$d,src,bin,include,objects,api,pkgconfig}
  mkdir -p "$ORO_HOME/share/man/man1"
  mkdir -p "$ORO_HOME/share/man/man3"
  mkdir -p "$ORO_HOME/share/man/man7"
  mkdir -p "$ORO_HOME/share/doc/oroc"
  mkdir -p "$ORO_HOME"/{lib$d,objects}/"$arch-desktop"

  if [[ "$host" = "Darwin" ]] && [[ -z "$NO_IOS" ]]; then
    local apple_target=""
    for apple_target in "${apple_mobile_targets[@]}"; do
      mkdir -p "$ORO_HOME"/{lib$d,objects}/"$apple_target"
    done
  fi

  if [[ -n $BUILD_ANDROID ]]; then
    for abi in $(android_supported_abis); do
      mkdir -p "$ORO_HOME"/{lib$d,objects}/"$abi-android"
    done
  fi

  # Ensure build directory exists before populating third‑party sources
  mkdir -p "$BUILD_DIR"

  function _rewrite_github_submodule_urls {
    local repo="$1"
    local gitmodules="$repo/.gitmodules"

    if [ ! -f "$gitmodules" ]; then
      return 0
    fi

    local rc=0
    local changed=0

    while IFS= read -r line; do
      local key="${line%% *}"
      local url="${line#* }"
      local normalized="$url"

      if [[ "$url" =~ ^git@github\.com:(.+)$ ]]; then
        normalized="https://github.com/${BASH_REMATCH[1]}"
      elif [[ "$url" =~ ^ssh://git@github\.com/(.+)$ ]]; then
        normalized="https://github.com/${BASH_REMATCH[1]}"
      fi

      if [[ "$normalized" != "$url" ]]; then
        git -C "$repo" config -f .gitmodules "$key" "$normalized" > /dev/null 2>&1
        rc=$?
        if (( rc != 0 )); then
          return $rc
        fi
        changed=1
      fi
    done < <(git -C "$repo" config -f .gitmodules --get-regexp '^submodule\..*\.url$')

    if (( changed != 0 )); then
      git -C "$repo" submodule sync --recursive > /dev/null 2>&1
      rc=$?
      if (( rc != 0 )); then
        return $rc
      fi
    fi

    return 0
  }

  function _clone_pinned_dependency {
    local name="$1"
    local url="$2"
    local ref="$3"
    local expected_revision="$4"
    local destination="$5"

    if [[ ! "$expected_revision" =~ ^[0-9a-f]{40}$ ]]; then
      echo >&2 "not ok - $name expected revision must be a full 40-character Git commit"
      return 1
    fi

    if ! git clone --depth=1 --branch "$ref" "$url" "$destination" > /dev/null 2>&1; then
      return 1
    fi

    local observed_revision
    observed_revision="$(git -C "$destination" rev-parse HEAD 2>/dev/null)"
    if [[ "$observed_revision" != "$expected_revision" ]]; then
      echo >&2 "not ok - $name revision mismatch: expected $expected_revision, found ${observed_revision:-unknown}"
      rm -rf "$destination"
      return 1
    fi

    return 0
  }

  if [ ! -f "$BUILD_DIR/sqlite/sqlite3.c" ]; then
    if [[ -n "${SQLITE_SOURCE_DIR:-}" ]]; then
      if [ -d "$SQLITE_SOURCE_DIR" ]; then
        rm -rf "$BUILD_DIR/sqlite"
        mkdir -p "$BUILD_DIR/sqlite"
        cp -a "$SQLITE_SOURCE_DIR"/. "$BUILD_DIR/sqlite/"
      else
        die 1 "not ok - SQLITE_SOURCE_DIR '$SQLITE_SOURCE_DIR' not found"
      fi
    else
      if ! "$root/bin/fetch-sqlite.sh"; then
        local rc=$?
        die ${rc:-1} "not ok - unable to obtain sqlite amalgamation (set SQLITE_SOURCE_DIR to a local directory or enable network)"
      fi
    fi

    if [[ ! -f "$BUILD_DIR/sqlite/sqlite3.c" ]]; then
      die 1 "not ok - sqlite amalgamation missing after preparation"
    fi
  fi

  if [[ -f "$BUILD_DIR/sqlite/sqlite3.c" ]]; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
      sed -i '' 's/^# *define SQLITE_OMIT_LOAD_EXTENSION.*$/\/\* SQLITE_OMIT_LOAD_EXTENSION disabled for Oro loadable extensions \*\//' "$BUILD_DIR/sqlite/sqlite3.c" 2>/dev/null || true
      sed -i '' 's/^# *define SQLITE_OMIT_LOAD_EXTENSION.*$/\/\* SQLITE_OMIT_LOAD_EXTENSION disabled for Oro loadable extensions \*\//' "$BUILD_DIR/sqlite/sqlite3.h" 2>/dev/null || true
    else
      sed -i 's/^# *define SQLITE_OMIT_LOAD_EXTENSION.*$/\/\* SQLITE_OMIT_LOAD_EXTENSION disabled for Oro loadable extensions \*\//' "$BUILD_DIR/sqlite/sqlite3.c" 2>/dev/null || true
      sed -i 's/^# *define SQLITE_OMIT_LOAD_EXTENSION.*$/\/\* SQLITE_OMIT_LOAD_EXTENSION disabled for Oro loadable extensions \*\//' "$BUILD_DIR/sqlite/sqlite3.h" 2>/dev/null || true
    fi
  fi

  if [ ! -f "$BUILD_DIR/jsoncons/include/jsoncons/json.hpp" ]; then
    rm -rf "$BUILD_DIR/jsoncons"
    local rc=1
    if [[ -n "${JSONCONS_SOURCE_DIR:-}" ]] &&
       [ -f "$JSONCONS_SOURCE_DIR/include/jsoncons/json.hpp" ]; then
      cp -r "$JSONCONS_SOURCE_DIR" "$BUILD_DIR/jsoncons" > /dev/null 2>&1
      rc=$?
    else
      local JSONCONS_GIT_URL="${JSONCONS_GIT:-https://github.com/danielaparker/jsoncons.git}"
      local JSONCONS_GIT_TAG="${JSONCONS_GIT_TAG:-v1.7.0}"
      local JSONCONS_GIT_REVISION="${JSONCONS_GIT_REVISION:-cb54cdc3134a62634466bf7bcd24f1a906f4ef25}"
      _clone_pinned_dependency "jsoncons" "$JSONCONS_GIT_URL" "$JSONCONS_GIT_TAG" "$JSONCONS_GIT_REVISION" "$BUILD_DIR/jsoncons"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain jsoncons sources (set JSONCONS_SOURCE_DIR to a local checkout or enable network)"

    rm -rf "$BUILD_DIR/jsoncons/.git" 2>/dev/null
  fi

  if [ ! -d "$BUILD_DIR/libsodium" ]; then
    local rc=1
    if [[ -n "$LIBSODIUM_SOURCE_DIR" ]] && [ -d "$LIBSODIUM_SOURCE_DIR" ]; then
      cp -r "$LIBSODIUM_SOURCE_DIR" "$BUILD_DIR/libsodium" > /dev/null 2>&1
      rc=$?
    else
      local LIBSODIUM_GIT_URL="${LIBSODIUM_GIT:-https://github.com/jedisct1/libsodium.git}"
      local LIBSODIUM_GIT_TAG="${LIBSODIUM_GIT_TAG:-1.0.20-RELEASE}"
      local LIBSODIUM_GIT_REVISION="${LIBSODIUM_GIT_REVISION:-9511c982fb1d046470a8b42aa36556cdb7da15de}"
      _clone_pinned_dependency "libsodium" "$LIBSODIUM_GIT_URL" "$LIBSODIUM_GIT_TAG" "$LIBSODIUM_GIT_REVISION" "$BUILD_DIR/libsodium"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain libsodium sources (set LIBSODIUM_SOURCE_DIR to a local checkout or enable network)"
  fi

  if [ ! -d "$BUILD_DIR/zlib" ]; then
    local rc=1
    if [[ -n "$ZLIB_SOURCE_DIR" ]] && [ -d "$ZLIB_SOURCE_DIR" ]; then
      cp -r "$ZLIB_SOURCE_DIR" "$BUILD_DIR/zlib" > /dev/null 2>&1
      rc=$?
    elif [ -d "$root/zlib" ]; then
      cp -r "$root/zlib" "$BUILD_DIR/zlib" > /dev/null 2>&1
      rc=$?
    else
      local ZLIB_GIT_URL="${ZLIB_GIT:-https://github.com/madler/zlib.git}"
      local ZLIB_GIT_TAG="${ZLIB_GIT_TAG:-v1.3.1}"
      local ZLIB_GIT_REVISION="${ZLIB_GIT_REVISION:-51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf}"
      _clone_pinned_dependency "zlib" "$ZLIB_GIT_URL" "$ZLIB_GIT_TAG" "$ZLIB_GIT_REVISION" "$BUILD_DIR/zlib"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain zlib sources (set ZLIB_SOURCE_DIR to a local checkout or enable network)"

    if [ -d "$BUILD_DIR/zlib/.git" ]; then
      rm -rf "$BUILD_DIR/zlib/.git" 2>/dev/null
    fi
  fi

  if [ ! -d "$BUILD_DIR/uv" ]; then
    local rc=1
    local libuv_source_candidates=()
    local libuv_source=""

    if [[ -n "$LIBUV_SOURCE_DIR" ]]; then
      libuv_source_candidates+=("$LIBUV_SOURCE_DIR")
    fi

    if [[ -n "$ORO_HOME" ]]; then
      libuv_source_candidates+=("$ORO_HOME/uv")
    fi

    for candidate in "${libuv_source_candidates[@]}"; do
      if [[ -z "$candidate" ]]; then
        continue
      fi

      # Skip if the candidate isn't a usable libuv checkout
      if [ ! -d "$candidate" ] || [ ! -f "$candidate/include/uv.h" ]; then
        continue
      fi

      # Avoid copying from the destination we are about to create
      if [[ "$candidate" == "$BUILD_DIR/uv" ]]; then
        continue
      fi

      if cp -r "$candidate" "$BUILD_DIR/uv" > /dev/null 2>&1; then
        libuv_source="$candidate"
        rc=0
        break
      fi
    done

    if (( rc != 0 )); then
      local LIBUV_GIT_URL="${LIBUV_GIT:-https://github.com/libuv/libuv.git}"
      local LIBUV_GIT_TAG="${LIBUV_GIT_TAG:-v1.52.1}"
      local LIBUV_GIT_REVISION="${LIBUV_GIT_REVISION:-1cfa32ff59c076ffb6ed735bbc8c18361558661f}"
      _clone_pinned_dependency "libuv" "$LIBUV_GIT_URL" "$LIBUV_GIT_TAG" "$LIBUV_GIT_REVISION" "$BUILD_DIR/uv"
      rc=$?
    else
      if [[ -n "$VERBOSE" ]]; then
        echo "# using cached libuv sources from $libuv_source"
      fi
    fi

    die ${rc:-1} "not ok - unable to obtain libuv sources (set LIBUV_SOURCE_DIR to a local checkout or enable network)"

    rm -rf "$BUILD_DIR/uv/.git" 2>/dev/null
    # Comment out compiler tests when supported by the source tree
    if [[ -z "$ENABLE_LIBUV_C_COMPILER_CHECKS" ]] && [ -f "$BUILD_DIR/uv/CMakeLists.txt" ]; then
      tempmkl=$(mktemp)
      sed 's/check_c_compiler_flag/# check_c_compiler_flag/' "$BUILD_DIR/uv/CMakeLists.txt" > "$tempmkl" 2>/dev/null
      mv "$tempmkl" "$BUILD_DIR/uv/CMakeLists.txt" 2>/dev/null
    fi
  fi

  if [ ! -d "$BUILD_DIR/libusb" ]; then
    local rc=1
    if [[ -n "$LIBUSB_SOURCE_DIR" ]] && [ -d "$LIBUSB_SOURCE_DIR" ]; then
      cp -r "$LIBUSB_SOURCE_DIR" "$BUILD_DIR/libusb" > /dev/null 2>&1
      rc=$?
    else
      local LIBUSB_GIT_URL="${LIBUSB_GIT:-https://github.com/libusb/libusb.git}"
      local LIBUSB_GIT_TAG="${LIBUSB_GIT_TAG:-v1.0.29}"
      local LIBUSB_GIT_REVISION="${LIBUSB_GIT_REVISION:-15a7ebb4d426c5ce196684347d2b7cafad862626}"
      _clone_pinned_dependency "libusb" "$LIBUSB_GIT_URL" "$LIBUSB_GIT_TAG" "$LIBUSB_GIT_REVISION" "$BUILD_DIR/libusb"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain libusb sources (set LIBUSB_SOURCE_DIR to a local checkout or enable network)"

    rm -rf "$BUILD_DIR/libusb/.git" 2>/dev/null
  fi

  if [ ! -d "$BUILD_DIR/asn1c" ]; then
    local rc=1
    if [[ -n "$ASN1C_SOURCE_DIR" ]] && [ -d "$ASN1C_SOURCE_DIR" ]; then
      cp -r "$ASN1C_SOURCE_DIR" "$BUILD_DIR/asn1c" > /dev/null 2>&1
      rc=$?
    else
      local ASN1C_GIT_URL="${ASN1C_GIT:-https://github.com/vlm/asn1c.git}"
      local ASN1C_GIT_TAG="${ASN1C_GIT_TAG:-v0.9.28}"
      local ASN1C_GIT_REVISION="${ASN1C_GIT_REVISION:-792b22b91282c28f5fd3f0574542fcb6827b72d3}"
      _clone_pinned_dependency "asn1c" "$ASN1C_GIT_URL" "$ASN1C_GIT_TAG" "$ASN1C_GIT_REVISION" "$BUILD_DIR/asn1c"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain asn1c sources (set ASN1C_SOURCE_DIR to a local checkout or enable network)"

    rm -rf "$BUILD_DIR/asn1c/.git" "$BUILD_DIR/asn1c/.github" 2>/dev/null

    local required_asn1_dirs=(
      "libasn1parser"
      "libasn1fix"
      "libasn1compiler"
      "libasn1print"
    )

    for dir in "${required_asn1_dirs[@]}"; do
      if [ ! -d "$BUILD_DIR/asn1c/$dir" ]; then
        die 1 "not ok - asn1c checkout missing required directory '$dir' (check ASN1C_SOURCE_DIR or ASN1C_GIT_TAG)"
      fi
    done
  fi

  if [[ "${ORO_SKIP_LIBIPFS:-0}" != "1" ]]; then
    if [ ! -d "$BUILD_DIR/libipfs" ]; then
      local rc=1
      if [[ -n "$LIBIPFS_SOURCE_DIR" ]] && [ -d "$LIBIPFS_SOURCE_DIR" ]; then
        cp -r "$LIBIPFS_SOURCE_DIR" "$BUILD_DIR/libipfs" > /dev/null 2>&1
        rc=$?
      else
        local LIBIPFS_GIT_URL="${LIBIPFS_GIT:-https://github.com/scala-network/libipfs.git}"
        local LIBIPFS_GIT_BRANCH="${LIBIPFS_GIT_BRANCH:-v3.0.1}"
        local LIBIPFS_GIT_REVISION="${LIBIPFS_GIT_REVISION:-4169320a81aaca8ea29052a79a0c0d6c1af9f1d5}"
        if [[ -n "$LIBIPFS_GIT_BRANCH" ]]; then
          _clone_pinned_dependency "libipfs" "$LIBIPFS_GIT_URL" "$LIBIPFS_GIT_BRANCH" "$LIBIPFS_GIT_REVISION" "$BUILD_DIR/libipfs"
        else
          echo >&2 "not ok - LIBIPFS_GIT_BRANCH must identify the revision being verified"
          false
        fi
        rc=$?
      fi

      die ${rc:-1} "not ok - unable to obtain libipfs sources (set LIBIPFS_SOURCE_DIR to a local checkout or enable network)"
    fi

    if [ -d "$BUILD_DIR/libipfs/.git" ]; then
      rm -rf "$BUILD_DIR/libipfs/.git" 2>/dev/null
    fi
  fi

  if [ ! -d "$BUILD_DIR/cr-sqlite" ]; then
    local rc=1
    if [[ -n "$CRSQLITE_SOURCE_DIR" ]] && [ -d "$CRSQLITE_SOURCE_DIR" ]; then
      cp -r "$CRSQLITE_SOURCE_DIR" "$BUILD_DIR/cr-sqlite" > /dev/null 2>&1
      rc=$?
    else
      local CRSQLITE_GIT_URL="${CRSQLITE_GIT:-https://github.com/superfly/cr-sqlite.git}"
      local CRSQLITE_GIT_TAG="${CRSQLITE_GIT_TAG:-prebuild-test.main-8b0b67d6}"
      local CRSQLITE_GIT_REVISION="${CRSQLITE_GIT_REVISION:-8b0b67d6553d425ab5b6e30afd18b4a8eff61cf3}"
      _clone_pinned_dependency "cr-sqlite" "$CRSQLITE_GIT_URL" "$CRSQLITE_GIT_TAG" "$CRSQLITE_GIT_REVISION" "$BUILD_DIR/cr-sqlite"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain cr-sqlite sources (set CRSQLITE_SOURCE_DIR to a local checkout or enable network)"

    # Ensure cr-sqlite Rust submodules (including sqlite-rs-embedded) are
    # available so that cargo can build the bundle crates.
    if [ -d "$BUILD_DIR/cr-sqlite/.git" ] && [ -f "$BUILD_DIR/cr-sqlite/.gitmodules" ]; then
      _rewrite_github_submodule_urls "$BUILD_DIR/cr-sqlite"
      rc=$?
      die ${rc:-1} "not ok - unable to normalize cr-sqlite submodule URLs (expected public GitHub HTTPS access)"

      quiet git -C "$BUILD_DIR/cr-sqlite" submodule update --init --recursive
      rc=$?
      die ${rc:-1} "not ok - unable to obtain cr-sqlite submodules (sqlite-rs-embedded); set CRSQLITE_SOURCE_DIR to a local checkout with submodules or enable network"
    fi
  fi

  if [[ "$host" == "Linux" ]]; then
    if [ ! -d "$BUILD_DIR/mbedtls" ]; then
      local rc=1
      if [[ -n "$MBEDTLS_SOURCE_DIR" ]] && [ -d "$MBEDTLS_SOURCE_DIR" ]; then
        cp -r "$MBEDTLS_SOURCE_DIR" "$BUILD_DIR/mbedtls" > /dev/null 2>&1
        rc=$?
      else
        local MBEDTLS_GIT_URL="${MBEDTLS_GIT:-https://github.com/Mbed-TLS/mbedtls.git}"
        local MBEDTLS_GIT_BRANCH="${MBEDTLS_GIT_BRANCH:-mbedtls-3.6.4}"
        local MBEDTLS_GIT_REVISION="${MBEDTLS_GIT_REVISION:-c765c831e5c2a0971410692f92f7a81d6ec65ec2}"
        _clone_pinned_dependency "mbedtls" "$MBEDTLS_GIT_URL" "$MBEDTLS_GIT_BRANCH" "$MBEDTLS_GIT_REVISION" "$BUILD_DIR/mbedtls"
        rc=$?
      fi

      if (( rc == 0 )) && [ -d "$BUILD_DIR/mbedtls/.git" ]; then
        if [ -f "$BUILD_DIR/mbedtls/.gitmodules" ]; then
          _rewrite_github_submodule_urls "$BUILD_DIR/mbedtls"
          rc=$?
        fi
      fi

      if (( rc == 0 )) && [ -d "$BUILD_DIR/mbedtls/.git" ]; then
        quiet git -C "$BUILD_DIR/mbedtls" submodule update --init --recursive
        (( rc = $? ))
      fi

      if (( rc == 0 )) && [ ! -f "$BUILD_DIR/mbedtls/framework/CMakeLists.txt" ]; then
        die 1 "not ok - missing mbedtls framework sources (expected framework/CMakeLists.txt); ensure submodules are available"
      fi

      die ${rc:-1} "not ok - unable to obtain mbedtls sources (set MBEDTLS_SOURCE_DIR to a local checkout or enable network)"

      rm -rf "$BUILD_DIR/mbedtls/.git" 2>/dev/null
    fi
  fi

  if [ ! -d "$BUILD_DIR/llama" ]; then
    local rc=1
    if [[ -n "$LLAMA_SOURCE_DIR" ]] && [ -d "$LLAMA_SOURCE_DIR" ]; then
      cp -r "$LLAMA_SOURCE_DIR" "$BUILD_DIR/llama" > /dev/null 2>&1
      rc=$?
    else
      local LLAMA_GIT_URL="${LLAMA_GIT:-https://github.com/ggml-org/llama.cpp.git}"
      local LLAMA_GIT_BRANCH="${LLAMA_GIT_BRANCH:-b7117}"
      local LLAMA_GIT_REVISION="${LLAMA_GIT_REVISION:-2286a360ff5c6b5edd33e53b5773bdf67bc25d23}"
      _clone_pinned_dependency "llama.cpp" "$LLAMA_GIT_URL" "$LLAMA_GIT_BRANCH" "$LLAMA_GIT_REVISION" "$BUILD_DIR/llama"
      rc=$?
    fi
    # rm -rf $BUILD_DIR/llama/.git

    die ${rc:-1} "not ok - unable to obtain llama.cpp sources (set LLAMA_SOURCE_DIR to a local checkout or enable network)"
  fi

  if [ ! -d "$BUILD_DIR/whisper.cpp" ]; then
    local rc=1
    if [[ -n "$WHISPER_SOURCE_DIR" ]] && [ -d "$WHISPER_SOURCE_DIR" ]; then
      cp -r "$WHISPER_SOURCE_DIR" "$BUILD_DIR/whisper.cpp" > /dev/null 2>&1
      rc=$?
    else
      local WHISPER_GIT_URL="${WHISPER_GIT:-https://github.com/ggml-org/whisper.cpp.git}"
      local WHISPER_GIT_TAG="${WHISPER_GIT_TAG:-v1.8.2}"
      local WHISPER_GIT_REVISION="${WHISPER_GIT_REVISION:-4979e04f5dcaccb36057e059bbaed8a2f5288315}"
      _clone_pinned_dependency "whisper.cpp" "$WHISPER_GIT_URL" "$WHISPER_GIT_TAG" "$WHISPER_GIT_REVISION" "$BUILD_DIR/whisper.cpp"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain whisper.cpp sources (set WHISPER_SOURCE_DIR to a local checkout or enable network)"
  fi

  if [ ! -d "$BUILD_DIR/iroh" ]; then
    local rc=1
    if [[ -n "$IROH_SOURCE_DIR" ]] && [ -d "$IROH_SOURCE_DIR" ]; then
      cp -r "$IROH_SOURCE_DIR" "$BUILD_DIR/iroh" > /dev/null 2>&1
      rc=$?
    else
      local IROH_GIT_URL="${IROH_GIT_URL:-https://github.com/n0-computer/iroh.git}"
      local IROH_GIT_REF="${IROH_GIT_REF:-v0.93.2}"
      local IROH_GIT_REVISION="${IROH_GIT_REVISION:-b39b325f25779a29b60b77c896d81f38f06ed764}"
      _clone_pinned_dependency "iroh" "$IROH_GIT_URL" "$IROH_GIT_REF" "$IROH_GIT_REVISION" "$BUILD_DIR/iroh"
      rc=$?
    fi

    die ${rc:-1} "not ok - unable to obtain iroh sources (set IROH_SOURCE_DIR to a local checkout or enable network)"

    rm -rf "$BUILD_DIR/iroh/.git" 2>/dev/null
  fi

  echo "ok - directories prepared"
}

function _install {
  local arch="$1"
  local platform="$2"

  if [ "$platform" == "desktop" ]; then
    echo "# copying sources to $ORO_HOME/src"
    if (( do_link == 1 )); then
      mkdir -p "$ORO_HOME/src"
      ln -sf "$CWD"/src/* "$ORO_HOME/src"
    else
      cp -r "$CWD"/src/* "$ORO_HOME/src"
    fi
    if [[ "$arch" = "aarch64" ]]; then
      arch="arm64"
    fi
  fi

  # TODO: set lib types per platform once mobile CI coverage exists

  if test -d "$BUILD_DIR/$arch-$platform/objects"; then
    echo "# copying objects to $ORO_HOME/objects/$arch-$platform"
    rm -rf "$ORO_HOME/objects/$arch-$platform"
    mkdir -p "$ORO_HOME/objects/$arch-$platform"
    if (( do_link == 1 )); then
      ln -sf "$BUILD_DIR/$arch-$platform/objects"/* "$ORO_HOME/objects/$arch-$platform"
    else
      cp -rfp "$BUILD_DIR/$arch-$platform/objects"/* "$ORO_HOME/objects/$arch-$platform"
    fi
  fi

  if test -d "$BUILD_DIR/lib$d"; then
    echo "# copying libraries to $ORO_HOME/lib$d"
    mkdir -p "$ORO_HOME/lib$d"
    shopt -s nullglob
    local lib_files=( "$BUILD_DIR/lib$d"/*.a )
    if (( ${#lib_files[@]} > 0 )); then
      if (( do_link == 1 )); then
        for f in "${lib_files[@]}"; do ln -sf "$f" "$ORO_HOME/lib$d/"; done
      else
        cp -rfp "${lib_files[@]}" "$ORO_HOME/lib$d/"
      fi
    else
      echo "warn - no static libs in $BUILD_DIR/lib$d"
    fi
    shopt -u nullglob
  fi

  _d=$d

  if [[ "$platform" == "android" ]]; then
    # Debug builds not currently supported for android
    _d=""
  fi

  if test -d "$BUILD_DIR/$arch-$platform"/lib$_d; then
    echo "# copying libraries to $ORO_HOME/lib$_d/$arch-$platform"
    rm -rf "$ORO_HOME/lib$_d/$arch-$platform"
    mkdir -p "$ORO_HOME/lib$_d/$arch-$platform"

    if [[ "$platform" != "android" ]]; then
      shopt -s nullglob
      local arch_libs=( "$BUILD_DIR/$arch-$platform"/lib$_d/*.a )
      if (( ${#arch_libs[@]} > 0 )); then
        if (( do_link == 1 )); then
          for f in "${arch_libs[@]}"; do ln -sf "$f" "$ORO_HOME/lib$_d/$arch-$platform"; done
        else
          cp -rfp "${arch_libs[@]}" "$ORO_HOME/lib$_d/$arch-$platform"
        fi
      else
        echo "warn - no static libs in $BUILD_DIR/$arch-$platform/lib$_d"
      fi
      shopt -u nullglob

      if [[ "$host" == "Darwin" ]]; then
        if [[ "$platform" == "desktop" ]]; then
          echo "# locating 'libomp.dylib...'"
          local libomp_path="$(_resolve_darwin_libomp)"

          if [[ -z "$libomp_path" ]] || [[ ! -f "$libomp_path" ]]; then
            die 1 "not ok - could not locate 'libomp.dylib'. Install an LLVM package (preferred) or the libomp package, then rerun relink. Examples: \"$(advice "llvm")\" or \"$(advice "libomp")\""
          fi

          echo "# found libomp at: '$libomp_path'"
          if ! _toolchain_supports_openmp; then
            echo "warn - staging libomp.dylib for macOS app packaging even though the current toolchain does not support -fopenmp"
          fi

          mkdir -p "$ORO_HOME/lib/$arch-desktop/codesign"
          cp -f "$libomp_path" "$ORO_HOME/lib/$arch-desktop/codesign/$(basename "$libomp_path")"
          echo "# copied '$libomp_path'"

          echo "# modifying the install name of the copied 'libomp.dylib'"
          quiet install_name_tool -id "@rpath/$(basename "$libomp_path")" "$ORO_HOME/lib/$arch-desktop/codesign/$(basename "$libomp_path")"
          if (( $? != 0 )); then
            sudo install_name_tool -id "@rpath/$(basename "$libomp_path")" "$ORO_HOME/lib/$arch-desktop/codesign/$(basename "$libomp_path")"
            die $? "not ok - failed to modify the install name of copied 'libomp.dylib'"
          fi
        else
          if (( do_link == 1 )); then
            ln -sf "$BUILD_DIR/$arch-$platform"/lib/*.metallib "$ORO_HOME/lib/$arch-$platform"
          else
            cp -rfp "$BUILD_DIR/$arch-$platform"/lib/*.metallib "$ORO_HOME/lib/$arch-$platform"
          fi
        fi
      fi
    fi

    if [[ "$host" == "Win32" ]] && [[ "$platform" == "desktop" ]]; then
      shopt -s nullglob
      local arch_libs_ms=( "$BUILD_DIR/$arch-$platform"/lib$_d/*.lib )
      if (( ${#arch_libs_ms[@]} > 0 )); then
        if (( do_link == 1 )); then
          for f in "${arch_libs_ms[@]}"; do ln -sf "$f" "$ORO_HOME/lib$_d/$arch-$platform"; done
        else
          cp -rfp "${arch_libs_ms[@]}" "$ORO_HOME/lib$_d/$arch-$platform"
        fi
      else
        echo "warn - no MSVC libs in $BUILD_DIR/$arch-$platform/lib$_d"
      fi
      shopt -u nullglob
    fi

    if [[ "$platform" == "android" ]] && [[ -d "$BUILD_DIR/$arch-$platform"/lib ]]; then
      shopt -s nullglob
      local and_libs=( "$BUILD_DIR/$arch-$platform"/lib/*.a )
      if (( ${#and_libs[@]} > 0 )); then
        if (( do_link == 1 )); then
          for f in "${and_libs[@]}"; do ln -sf "$f" "$ORO_HOME/lib/$arch-$platform"; done
        else
          cp -fr "${and_libs[@]}" "$ORO_HOME/lib/$arch-$platform"
        fi
      else
        echo "warn - no android libs in $BUILD_DIR/$arch-$platform/lib"
      fi
      shopt -u nullglob
    fi

    if [[ -d "$BUILD_DIR/$arch-$platform"/extensions ]]; then
      local dest_ext_dir="$ORO_HOME/lib/$arch-$platform/extensions"
      echo "# copying sqlite extensions to $dest_ext_dir"
      rm -rf "$dest_ext_dir"
      mkdir -p "$dest_ext_dir"

      shopt -s nullglob
      local ext_files=( "$BUILD_DIR/$arch-$platform"/extensions/* )
      if (( ${#ext_files[@]} > 0 )); then
        if (( do_link == 1 )); then
          for f in "${ext_files[@]}"; do ln -sf "$f" "$dest_ext_dir/"; done
        else
          cp -rfp "${ext_files[@]}" "$dest_ext_dir/"
        fi
      else
        echo "warn - no sqlite extensions in $BUILD_DIR/$arch-$platform/extensions"
      fi
      shopt -u nullglob
    fi
  else
    echo >&2 "not ok - Missing $BUILD_DIR/$arch-$platform/lib"
    exit 1
  fi

  if [ "$platform" == "desktop" ]; then
    if [ "$host" == "Linux" ] || [ "$host" == "Darwin" ]; then
      echo "# copying pkgconfig to $ORO_HOME/pkgconfig"
      rm -rf "$ORO_HOME/pkgconfig"
      mkdir -p "$ORO_HOME/pkgconfig"
      if (( do_link == 1 )); then
        ln -sf "$BUILD_DIR/$arch-desktop/pkgconfig"/* "$ORO_HOME/pkgconfig"
      else
        cp -rfp "$BUILD_DIR/$arch-desktop/pkgconfig"/* "$ORO_HOME/pkgconfig"
      fi
    fi

    echo "# copying js api to $ORO_HOME/api"
    mkdir -p "$ORO_HOME/api"

    if (( do_link == 1 )); then
      ln -sf "$root"/api/* "$ORO_HOME/api"
    else
      cp -frp "$root"/api/* "$ORO_HOME/api"
    fi

    mkdir -p "$ORO_HOME/assets"
    if (( do_link == 1 )); then
      ln -sf "$root"/assets/* "$ORO_HOME/assets"
    else
      cp -rf "$root"/assets/* "$ORO_HOME/assets"
    fi

    # only do this for desktop, no need to copy again for other platforms
    mkdir -p "$ORO_HOME/include"
    if (( do_link != 1 )); then
      rm -rf "$ORO_HOME/include"
    fi

    if (( do_link == 1 )); then
      ln -sf "$BUILD_DIR"/uv/include/* "$ORO_HOME/include"
      ln -sf "$root"/include/* "$ORO_HOME/include"
      ln -sf "$BUILD_DIR/$arch-desktop/include/sodium.h" "$ORO_HOME/include"
      ln -sf "$BUILD_DIR/$arch-desktop/include/sodium" "$ORO_HOME/include"
      ln -sf "$root"/build/whisper.cpp/include/* "$ORO_HOME/include"
      if [[ "$host" = "Linux" && -d "$BUILD_DIR/include/mbedtls" ]]; then
        rm -rf "$ORO_HOME/include/mbedtls"
        ln -sf "$BUILD_DIR/include/mbedtls" "$ORO_HOME/include/mbedtls"
      fi
      if [[ "$host" = "Linux" && -d "$BUILD_DIR/include/psa" ]]; then
        rm -rf "$ORO_HOME/include/psa"
        ln -sf "$BUILD_DIR/include/psa" "$ORO_HOME/include/psa"
      fi
    else
      mkdir -p $ORO_HOME/include
      cp -rfp "$BUILD_DIR"/uv/include/* "$ORO_HOME/include"
      cp -rfp "$root"/include/* "$ORO_HOME/include"
      cp -fp "$BUILD_DIR/$arch-desktop/include/sodium.h" "$ORO_HOME/include"
      cp -rfp "$BUILD_DIR/$arch-desktop/include/sodium" "$ORO_HOME/include"
      cp -rfp "$root"/build/whisper.cpp/include/* "$ORO_HOME/include"
      if [[ "$host" = "Linux" && -d "$BUILD_DIR/include/mbedtls" ]]; then
        rm -rf "$ORO_HOME/include/mbedtls"
        cp -rfp "$BUILD_DIR/include/mbedtls" "$ORO_HOME/include"
      fi
      if [[ "$host" = "Linux" && -d "$BUILD_DIR/include/psa" ]]; then
        rm -rf "$ORO_HOME/include/psa"
        cp -rfp "$BUILD_DIR/include/psa" "$ORO_HOME/include"
      fi
    fi

    if (( do_link != 1 )); then
      rm -f "$ORO_HOME/include/oro/_user-config-bytes.hh"
    fi

    mkdir -p "$ORO_HOME/include/llama"
    for header in $(find "$root/build/llama" -name *.h); do
      if [[ "$header" =~  examples/ ]]; then continue; fi
      if [[ "$header" =~  tests/ ]]; then continue; fi

      local llama_build_dir="$root/build/llama/"
      local destination="$ORO_HOME/include/llama/${header/$llama_build_dir/}"

      mkdir -p "$(dirname "$destination")"
      if (( do_link == 1 )); then
        ln -sf "$header" "$destination"
      else
        cp -f "$header" "$destination"
      fi
    done

    if [[ -f "$root/$ORO_ENV_FILENAME" ]]; then
      echo "# copying $ORO_ENV_FILENAME to $ORO_HOME"
      cp -fp "$root/$ORO_ENV_FILENAME" "$ORO_HOME/$ORO_ENV_FILENAME"
    fi
  fi

  if [ "$platform" == "desktop" ]; then
    mkdir -p "$ORO_HOME/bin"
    if (( do_link == 1 )); then
      ln -sf "$root/bin/functions.sh" "$ORO_HOME/bin"
      ln -sf "$root/bin/android-functions.sh" "$ORO_HOME/bin"
    else
      # Required for FTE setup
      cp -ap "$root/bin/functions.sh" "$ORO_HOME/bin"
      cp -ap "$root/bin/android-functions.sh" "$ORO_HOME/bin"
    fi

    if [[ "$(uname -s)" == *"_NT"* ]]; then
      if (( do_link == 1 )); then
        ln -sf "$root/bin/"*.ps1 "$ORO_HOME/bin"
        ln -sf "$root/bin/".vs* "$ORO_HOME/bin"
      else
        cp -ap "$root/bin/"*.ps1 "$ORO_HOME/bin"
        cp -ap "$root/bin/".vs* "$ORO_HOME/bin"
      fi
    fi

    for man_section in man1 man3 man7; do
      local section_ext="${man_section#man}"
      local root_man_dir="$root/share/man/$man_section"
      local home_man_dir="$ORO_HOME/share/man/$man_section"
      if compgen -G "$root_man_dir/*.${section_ext}" > /dev/null; then
        mkdir -p "$home_man_dir"
        rm -f "$home_man_dir"/*.${section_ext}
        if (( do_link == 1 )); then
          ln -sf "$root_man_dir/"*.${section_ext} "$home_man_dir"
        else
          cp -fp "$root_man_dir/"*.${section_ext} "$home_man_dir"
        fi
      fi
    done

    local home_doc_dir="$ORO_HOME/share/doc/oroc"
    mkdir -p "$home_doc_dir"
    for existing_doc in "$home_doc_dir"/*; do
      if [[ -f "$existing_doc" ]] || [[ -L "$existing_doc" ]]; then
        rm -f "$existing_doc"
      fi
    done
    mkdir -p "$home_doc_dir/docs"
    rm -f "$home_doc_dir/docs"/*
    local runtime_docs=(
      "$root/README.md:README.md"
      "$root/LICENSE.txt:LICENSE.txt"
      "$root/NOTICE:NOTICE"
      "$root/THIRD_PARTY_NOTICES.md:THIRD_PARTY_NOTICES.md"
      "$root/docs/BUILD_ENVIRONMENT.md:docs/BUILD_ENVIRONMENT.md"
      "$root/docs/LIMITATIONS.md:LIMITATIONS.md"
      "$root/docs/MCP.md:MCP.md"
      "$root/docs/llms.txt:llms.txt"
    )
    for doc_entry in "${runtime_docs[@]}"; do
      local source_doc="${doc_entry%%:*}"
      local dest_name="${doc_entry##*:}"
      if [[ ! -f "$source_doc" ]]; then
        continue
      fi
      mkdir -p "$(dirname "$home_doc_dir/$dest_name")"
      if (( do_link == 1 )); then
        ln -sf "$source_doc" "$home_doc_dir/$dest_name"
      else
        cp -fp "$source_doc" "$home_doc_dir/$dest_name"
      fi
    done

    if (( do_link == 1 )); then
      ln -sf "$root/LICENSE.txt" "$ORO_HOME/LICENSE.txt"
      ln -sf "$root/NOTICE" "$ORO_HOME/NOTICE"
      ln -sf "$root/THIRD_PARTY_NOTICES.md" "$ORO_HOME/THIRD_PARTY_NOTICES.md"
    else
      cp -fp "$root/LICENSE.txt" "$ORO_HOME/LICENSE.txt"
      cp -fp "$root/NOTICE" "$ORO_HOME/NOTICE"
      cp -fp "$root/THIRD_PARTY_NOTICES.md" "$ORO_HOME/THIRD_PARTY_NOTICES.md"
    fi

    local third_party_license_dir="$ORO_HOME/share/licenses/oro-runtime"
    mkdir -p "$third_party_license_dir"
    rm -f "$third_party_license_dir"/*
    local third_party_licenses=(
      "asn1c-LICENSE:$BUILD_DIR/asn1c/LICENSE"
      "cr-sqlite-LICENSE:$BUILD_DIR/cr-sqlite/LICENSE"
      "iroh-LICENSE-APACHE:$BUILD_DIR/iroh/LICENSE-APACHE"
      "iroh-LICENSE-MIT:$BUILD_DIR/iroh/LICENSE-MIT"
      "jsoncons-LICENSE:$BUILD_DIR/jsoncons/LICENSE"
      "libipfs-LICENSE:$BUILD_DIR/libipfs/LICENSE"
      "libsodium-LICENSE:$BUILD_DIR/libsodium/LICENSE"
      "libusb-COPYING:$BUILD_DIR/libusb/COPYING"
      "libuv-LICENSE:$BUILD_DIR/uv/LICENSE"
      "libuv-LICENSE-docs:$BUILD_DIR/uv/LICENSE-docs"
      "libuv-LICENSE-extra:$BUILD_DIR/uv/LICENSE-extra"
      "llama.cpp-LICENSE:$BUILD_DIR/llama/LICENSE"
      "llama.cpp-LICENSE-curl:$BUILD_DIR/llama/licenses/LICENSE-curl"
      "llama.cpp-LICENSE-httplib:$BUILD_DIR/llama/licenses/LICENSE-httplib"
      "llama.cpp-LICENSE-jsonhpp:$BUILD_DIR/llama/licenses/LICENSE-jsonhpp"
      "llama.cpp-LICENSE-linenoise:$BUILD_DIR/llama/licenses/LICENSE-linenoise"
      "mbedtls-LICENSE:$BUILD_DIR/mbedtls/LICENSE"
      "mbedtls-framework-LICENSE:$BUILD_DIR/mbedtls/framework/LICENSE"
      "Microsoft-WebView2-LICENSE:$BUILD_DIR/webview2/LICENSE.txt"
      "Microsoft-WebView2-NOTICE:$BUILD_DIR/webview2/NOTICE.txt"
      "whisper.cpp-LICENSE:$BUILD_DIR/whisper.cpp/LICENSE"
      "zlib-LICENSE:$BUILD_DIR/zlib/LICENSE"
    )
    for license_entry in "${third_party_licenses[@]}"; do
      local license_name="${license_entry%%:*}"
      local license_source="${license_entry#*:}"
      if [[ ! -f "$license_source" ]]; then
        continue
      fi
      if (( do_link == 1 )); then
        ln -sf "$license_source" "$third_party_license_dir/$license_name"
      else
        cp -fp "$license_source" "$third_party_license_dir/$license_name"
      fi
    done

    if [[ "${ORO_SKIP_IROH:-0}" != "1" ]] && command -v cargo >/dev/null 2>&1; then
      node "$root/bin/collect-cargo-licenses.js" \
        "$root/rust/oro-iroh" \
        "$third_party_license_dir"
      die $? "not ok - unable to collect Cargo dependency licenses"
    fi
  fi
}

function _install_cli {
  local arch="$(host_arch)"

  if [ -z "$TEST" ] && [ -z "$NO_INSTALL" ]; then
    echo "# moving binary to '$ORO_HOME/bin' (prompting to copy file into directory)"

    cp -f "$BUILD_DIR/$arch-desktop"/bin/* "$ORO_HOME/bin"
    die $? "not ok - unable to move binary into '$ORO_HOME'"

    if [[ "$ORO_HOME" != "$PREFIX" ]]; then
      if [[ ! -d $PREFIX/bin ]]; then
        echo "not ok - $PREFIX/bin is not a directory, unable to install."
        exit 1
      fi

      for cli in oroc; do
        local target="$ORO_HOME/bin/$cli"
        local link="$PREFIX/bin/$cli"
        if [[ ! -e "$target" && -e "${target}.exe" ]]; then
          target="${target}.exe"
          link="${link}.exe"
        fi
        if [[ ! -e "$target" ]]; then
          continue
        fi

        echo "# linking binary to $link"
        local status="$(ln -sf "$target" "$link" 2>&1)"
        local rc=$?

        if [[ " $status " =~ " Permission denied " ]]; then
          echo "warn - Failed to link binary to '$link': Trying 'sudo'"
          sudo rm -f "$link"
          sudo ln -sf "$target" "$link"
          die $? "not ok - unable to link binary into '$link'"
        fi

        die $rc "not ok - unable to link binary into '$link'"
      done

      if [[ "$host" != "Win32" ]]; then
        for man_section in man1 man3 man7; do
          local section_ext="${man_section#man}"
          local source_pattern="$ORO_HOME/share/man/$man_section/*.${section_ext}"
          if ! compgen -G "$source_pattern" > /dev/null; then
            continue
          fi

          local man_dir="$PREFIX/share/man/$man_section"
          if [[ ! -d "$man_dir" ]]; then
            local status="$(mkdir -p "$man_dir" 2>&1)"
            local rc=$?
            if [[ " $status " =~ " Permission denied " ]]; then
              echo "warn - Failed to create man directory '$man_dir': Trying 'sudo'"
              sudo mkdir -p "$man_dir"
              die $? "not ok - unable to create man directory '$man_dir'"
            fi
            die $rc "not ok - unable to create man directory '$man_dir'"
          fi

          for page in "$ORO_HOME"/share/man/"$man_section"/*.${section_ext}; do
            local page_name="$(basename "$page")"
            local link="$man_dir/$page_name"
            echo "# linking man page to $link"
            local status="$(ln -sf "$page" "$link" 2>&1)"
            local rc=$?

            if [[ " $status " =~ " Permission denied " ]]; then
              echo "warn - Failed to link man page to '$link': Trying 'sudo'"
              sudo rm -f "$link"
              sudo ln -sf "$page" "$link"
              die $? "not ok - unable to link man page into '$link'"
            fi

            die $rc "not ok - unable to link man page into '$link'"
          done
        done

        local source_doc_dir="$ORO_HOME/share/doc/oroc"
        if compgen -G "$source_doc_dir/*" > /dev/null; then
          local doc_dir="$PREFIX/share/doc/oroc"
          if [[ ! -d "$doc_dir" ]]; then
            local status="$(mkdir -p "$doc_dir" 2>&1)"
            local rc=$?
            if [[ " $status " =~ " Permission denied " ]]; then
              echo "warn - Failed to create documentation directory '$doc_dir': Trying 'sudo'"
              sudo mkdir -p "$doc_dir"
              die $? "not ok - unable to create documentation directory '$doc_dir'"
            fi
            die $rc "not ok - unable to create documentation directory '$doc_dir'"
          fi

          for doc in "$source_doc_dir"/*; do
            if [[ -d "$doc" ]] && [[ ! -L "$doc" ]]; then
              continue
            fi
            local doc_name="$(basename "$doc")"
            local link="$doc_dir/$doc_name"
            echo "# linking documentation to $link"
            local status="$(ln -sf "$doc" "$link" 2>&1)"
            local rc=$?

            if [[ " $status " =~ " Permission denied " ]]; then
              echo "warn - Failed to link documentation to '$link': Trying 'sudo'"
              sudo rm -f "$link"
              sudo ln -sf "$doc" "$link"
              die $? "not ok - unable to link documentation into '$link'"
            fi

            die $rc "not ok - unable to link documentation into '$link'"
          done

          local source_build_environment="$source_doc_dir/docs/BUILD_ENVIRONMENT.md"
          if [[ -f "$source_build_environment" ]]; then
            local build_doc_dir="$doc_dir/docs"
            if [[ ! -d "$build_doc_dir" ]]; then
              local status="$(mkdir -p "$build_doc_dir" 2>&1)"
              local rc=$?
              if [[ " $status " =~ " Permission denied " ]]; then
                echo "warn - Failed to create documentation directory '$build_doc_dir': Trying 'sudo'"
                sudo mkdir -p "$build_doc_dir"
                die $? "not ok - unable to create documentation directory '$build_doc_dir'"
              fi
              die $rc "not ok - unable to create documentation directory '$build_doc_dir'"
            fi

            local build_doc_link="$build_doc_dir/BUILD_ENVIRONMENT.md"
            echo "# linking documentation to $build_doc_link"
            local status="$(ln -sf "$source_build_environment" "$build_doc_link" 2>&1)"
            local rc=$?
            if [[ " $status " =~ " Permission denied " ]]; then
              echo "warn - Failed to link documentation to '$build_doc_link': Trying 'sudo'"
              sudo rm -f "$build_doc_link"
              sudo ln -sf "$source_build_environment" "$build_doc_link"
              die $? "not ok - unable to link documentation into '$build_doc_link'"
            fi
            die $rc "not ok - unable to link documentation into '$build_doc_link'"
          fi
        fi
      fi
    fi

    echo "ok - done. type 'oroc -h' for help"
    if [[ "$host" != "Win32" ]] && \
      { compgen -G "$ORO_HOME/share/man/man1/*.1" > /dev/null || compgen -G "$ORO_HOME/share/man/man3/*.3" > /dev/null || compgen -G "$ORO_HOME/share/man/man7/*.7" > /dev/null; }; then
      echo "ok - man pages installed. try 'man oroc', 'man 3 oro-fs', or 'man 7 oro-ipc'"
    fi
  else
    echo "ok - done."
  fi
}

function _setSDKVersion {
  sdks=$(ls "$PLATFORMPATH"/"$1".platform/Developer/SDKs)
  arr=()
  for sdk in $sdks
  do
    echo "ok - found SDK $sdk"
    arr[${#arr[@]}]=$sdk
  done

  # Last item will be the current SDK, since it is alpha ordered
  count=${#arr[@]}

  if [ $count -gt 0 ]; then
    sdk=${arr[$count-1]:${#1}}
    num=$(expr ${#sdk}-4)
    SDKVERSION=${sdk:0:$num}
  else
    SDKVERSION="8.0"
  fi
}

function _compile_libuv_android {
  local platform="android"
  local arch=$1
  local host_arch="$(host_arch)"
  local clang="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"
  local clang_target="$(android_clang_target "$arch")"
  local ar="$(android_ar "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"
  local -a android_includes=()
  android_includes=($(android_arch_includes "$arch"))

  # Match libuv's required C11 mode with compiler extensions enabled.
  local cflags=("$clang_target" -std=gnu11 -g -pedantic -I"$root"/build/uv/include -I"$root"/build/uv/src -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D_LARGEFILE_SOURCE -fPIC -Wall -Wextra -Wno-pedantic -Wno-sign-compare -Wno-unused-parameter -Wno-implicit-function-declaration)
  cflags+=("${android_includes[@]}")
  local objects=()
  local sources=("unix/async.c" "unix/core.c" "unix/dl.c" "unix/fs.c" "unix/getaddrinfo.c" "unix/getnameinfo.c" "unix/linux.c" "unix/loop.c" "unix/loop-watcher.c" "unix/pipe.c" "unix/poll.c" "unix/process.c" "unix/proctitle.c" "unix/random-devurandom.c" "unix/random-getentropy.c" "unix/random-getrandom.c" "unix/random-sysctl-linux.c" "unix/signal.c" "unix/stream.c" "unix/tcp.c" "unix/thread.c" "unix/tty.c" "unix/udp.c" fs-poll.c idna.c inet.c random.c strscpy.c strtok.c threadpool.c timer.c uv-common.c uv-data-getter-setters.c version.c)

  local output_directory="$root/build/$arch-$platform/uv$d"
  mkdir -p "$output_directory"

  local src_directory="$root/build/uv/src"

  trap onsignal INT TERM
  local max_concurrency=$CPU_CORES
  if (( max_concurrency < 1 )); then
    max_concurrency=1
  fi
  local -a compile_pids=()
  local compile_pid=""
  local compile_status=0
  local compile_rc=0
  local archive_rc=0
  local build_static=0
  local base_lib="libuv"
  local static_library="$root/build/$arch-$platform/lib/$base_lib.a"

  for source in "${sources[@]}"; do
    if (( ${#compile_pids[@]} >= max_concurrency )); then
      compile_pid="${compile_pids[0]}"
      wait "$compile_pid" 2>/dev/null
      compile_rc=$?
      if (( compile_rc != 0 )); then
        compile_status=1
      fi
      compile_pids=("${compile_pids[@]:1}")
    fi

    declare object="${source/.c/.o}"
    object="$(basename "$object")"
    objects+=("$output_directory/$object")

    {
      if (( force )) || ! test -f "$output_directory/$object" || (( $(stat_mtime "$src_directory/$source") > $(stat_mtime "$output_directory/$object") )); then
        mkdir -p "$(dirname "$object")"
        echo "# compiling object ($arch-$platform) $(basename "$source")"
        quiet "$clang" "${cflags[@]}" -c "$src_directory/$source" -o "$output_directory/$object" || exit 1
        echo "ok - built $source -> $object ($arch-$platform)"
        # Can't write back to variable in block, remove final library to force rebuild
        rm -f -- "$static_library"
      fi
    } & compile_pids+=("$!")
  done

  for compile_pid in "${compile_pids[@]}"; do
    wait "$compile_pid" 2>/dev/null
    compile_rc=$?
    if (( compile_rc != 0 )); then
      compile_status=1
    fi
  done

  if (( compile_status != 0 )); then
    echo >&2 "not ok - failed to compile libuv objects ($arch-$platform)"
    return 1
  fi

  if [ ! -f "$static_library" ]; then
    build_static=1
  fi
  mkdir -p "$(dirname "$static_library")"

  if (( build_static )); then
    quiet "$ar" crs "$static_library" "${objects[@]}"
    archive_rc=$?
    if (( archive_rc != 0 )); then
      echo >&2 "not ok - failed to archive $static_library"
      return 1
    fi

    if [ -f "$static_library" ]; then
      echo "ok - built $base_lib ($arch-$platform): $(basename "$static_library")"
    else
       echo >&2 "not ok - failed to build $static_library"
      exit 1
    fi

  else
    if [ -f "$static_library" ]; then
      echo "ok - using cached static library ($arch-$platform): $(basename "$static_library")"
    else
      echo >&2 "not ok - static library doesn't exist after cache check passed: ($arch-$platform): $(basename "$static_library")"
      exit 1
    fi
  fi

  # This is a sanity check to confirm that the static_library is > 8 bytes
  # If an empty ${objects[@]} is provided to ar, it will still spit out a header without an error code.
  # therefore check the output size
  # This error condition should only occur after a code change
  local lib_size="$(stat_size "$static_library")"
  if (( lib_size < $(android_min_expected_static_lib_size "$base_lib") )); then
    echo >&2 "not ok - $static_library size looks wrong: $lib_size, renaming as .bad"
    mv "$static_library" "$static_library.bad"
    exit 1
  fi
}

function _compile_llama_metal {
  local target=$1
  local hosttarget=$1
  local platform=$2

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  echo "# building METAL for $platform ($target) on $host..."
  local STAGING_DIR="$BUILD_DIR/$target-$platform/llama"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/llama/* "$STAGING_DIR"
    cd "$STAGING_DIR" || exit 1
  else
    cd "$STAGING_DIR" || exit 1
  fi

  local sdk="iphoneos"
  [[ "$platform" == "iPhoneSimulator" ]] && sdk="iphonesimulator"

  mkdir -p "$STAGING_DIR/build/"
  mkdir -p ../lib

  xcrun -sdk "$sdk" metal \
    -O3 \
    -I ggml/src \
    -c ggml/src/ggml-metal/ggml-metal.metal \
    -o ggml-metal.air
  die $? "not ok - unable to compile Metal source for $platform"

  xcrun -sdk "$sdk" metallib ggml-metal.air -o ../lib/default.metallib
  die $? "not ok - unable to create Metal library for $platform"

  rm -f ggml-metal.air

  echo "ok - metal built for $platform"
}

function _prune_disabled_llama_outputs {
  local staging_dir="$1"
  local install_dir="$2"

  # These CMake trees and executables belong only to targets disabled by
  # _compile_llama. Preserve src/ and ggml/ so library-only incremental builds
  # continue to reuse their compiled objects.
  rm -rf \
    "$staging_dir/build/bin" \
    "$staging_dir/build/common" \
    "$staging_dir/build/tools" \
    "$staging_dir/build/vendor"

  local -a stale_installed_tools=()
  shopt -s nullglob
  stale_installed_tools=("$install_dir/bin"/llama-*)
  shopt -u nullglob

  if (( ${#stale_installed_tools[@]} > 0 )); then
    rm -f -- "${stale_installed_tools[@]}"
  fi
}

function _compile_llama {
  local target=$1
  local hosttarget=$1
  local platform=$2
  local rc=0

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  echo "# building llama.cpp for $platform ($target) on $host..."
  local STAGING_DIR="$BUILD_DIR/$target-$platform/llama"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/llama/* "$STAGING_DIR"
    cd "$STAGING_DIR" || exit 1
  else
    cd "$STAGING_DIR" || exit 1
  fi

  local sdk="iphoneos"
  [[ "$platform" == "iPhoneSimulator" ]] && sdk="iphonesimulator"

  mkdir -p "$STAGING_DIR/build/"
  mkdir -p ../bin

  local cmake_args=(
    -DLLAMA_BUILD_COMMON=OFF
    -DLLAMA_BUILD_TESTS=OFF
    -DLLAMA_BUILD_TOOLS=OFF
    -DLLAMA_BUILD_SERVER=OFF
    -DLLAMA_BUILD_EXAMPLES=OFF
    -DLLAMA_CURL=OFF
    -DBUILD_SHARED_LIBS=OFF
  )

  _prune_disabled_llama_outputs "$STAGING_DIR" "$BUILD_DIR/$target-$platform"

  if [[ "$platform" == "desktop" ]] && ! _toolchain_supports_openmp; then
    cmake_args+=(-DGGML_OPENMP=OFF -DGGML_OPENMP_ENABLED=OFF)
  fi

  if [[ "$platform" == "desktop" && "$host" == "Darwin" ]]; then
    local darwin_warning_flags="$(_darwin_third_party_warning_flags)"
    cmake_args+=(
      -DCMAKE_C_FLAGS="$darwin_warning_flags"
      -DCMAKE_CXX_FLAGS="$darwin_warning_flags"
      -DCMAKE_OBJC_FLAGS="$darwin_warning_flags"
      -DCMAKE_OBJCXX_FLAGS="$darwin_warning_flags"
    )
  fi

  if [ "$platform" == "desktop" ]; then
    if [[ "$host" != "Win32" ]]; then
      cmake_args+=(-DCMAKE_POSITION_INDEPENDENT_CODE=ON)
      quiet command -v cmake
      die $? "not ok - missing cmake, \"$(advice 'cmake')\""
      local cflags="-fPIC"
      if [[ "$host" == "Darwin" ]]; then
        cflags+=" $(_darwin_third_party_warning_flags)"
      fi
      (
        export CFLAGS="$cflags"
        export CXXFLAGS="$cflags"
        export OBJCFLAGS="$cflags"
        export OBJCXXFLAGS="$cflags"

        _cmake_configure . build -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform" "${cmake_args[@]}" &&
        quiet cmake --build build -- -j"$CPU_CORES"
      )
      rc=$?
      die $rc "not ok - libllama.a (desktop)"

      _stage_llama_desktop_outputs "$STAGING_DIR" "$BUILD_DIR/$target-$platform"
      die $? "not ok - libllama.a (desktop)"
    else
      if ! test -f "$BUILD_DIR/$target-$platform/lib$d/llama.lib"; then
        local config="Release"
        if [[ -n "$DEBUG" ]]; then
          config="Debug"
        fi
        cd "$STAGING_DIR/build/" || exit 1
        quiet command -v cmake
        die $? "not ok - missing cmake, \"$(advice 'cmake')\""
        _cmake_configure .. . "${cmake_args[@]}"
        quiet cmake --build . --config $config --parallel "$CPU_CORES"
        mkdir -p "$BUILD_DIR/$target-$platform/lib$d"
        quiet echo "copy_if_newer $STAGING_DIR/build/$config/llama.lib "$BUILD_DIR/$target-$platform/lib$d/llama.lib""
        copy_if_newer "$STAGING_DIR/build/$config/llama.lib" "$BUILD_DIR/$target-$platform/lib$d/llama.lib"
        if [[ -n "$DEBUG" ]]; then
          copy_if_newer "$STAGING_DIR"/build/$config/llama_a.pdb "$BUILD_DIR/$target-$platform/lib$d/llama_a.pdb"
        fi;
      fi
    fi

    rm -f "$root/build/$(host_arch)-desktop/lib$d"/*.{so,la,dylib}*
    return
  elif [ "$platform" == "iPhoneOS" ] || [ "$platform" == "iPhoneSimulator" ]; then
    # https://github.com/ggerganov/llama.cpp/discussions/4508
    local ar="$(xcrun -sdk $sdk -find ar)"

    local cc="$(xcrun -sdk $sdk -find clang)"
    local cxx="$(xcrun -sdk $sdk -find clang++)"
    local cflags="--target=$target-apple-ios -isysroot $PLATFORMPATH/$platform.platform/Developer/SDKs/$platform$SDKVERSION.sdk -m$sdk-version-min=$SDKMINVERSION -DLLAMA_METAL_EMBED_LIBRARY=ON -DUSE_NEON_DOTPROD "
    if [ "$platform" == "iPhoneOS" ]; then
      cflags+="-march=armv8.2-a+dotprod"
    elif [ "$platform" == "iPhoneSimulator" ] && [ "$target" == "arm64" ]; then
      cflags+="-march=armv8.2-a+dotprod"
    elif [ "$platform" == "iPhoneSimulator" ] && [ "$target" == "x86_64" ]; then
      cflags+="-march=x86-64 --target=x86-apple-ios-simulator"
    fi

    local sdkroot="$PLATFORMPATH/$platform.platform/Developer/SDKs/$platform$SDKVERSION.sdk"
    (
      export AR="$ar"
      export CFLAGS="$cflags"
      export CXXFLAGS="$cflags"
      export CXX="$cxx"
      export CC="$cc"
      export SDKROOT="$sdkroot"

      # FindBLAS performs host-style link probes that cannot validate a simulator
      # architecture different from the macOS runner. iOS uses Accelerate/Metal.
      _cmake_configure . build -DCMAKE_SYSTEM_NAME="iOS" -DCMAKE_OSX_ARCHITECTURES="$target" -DCMAKE_OSX_SYSROOT="$SDKROOT" -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform" -DLLAMA_NATIVE=OFF -DGGML_ARM_DOTPROD=ON -DGGML_BLAS=OFF "${cmake_args[@]}" &&
      cmake --build build -- -j"$CPU_CORES" &&
      cmake --install build
    )
    rc=$?
    die $rc "not ok - Unable to compile libllama for '$platform'"

    return
  elif [ "$platform" == "android" ]; then
    local host_arch="$(host_arch)"
    local cc="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"
    local cxx="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch" "++")"
    local ar="$(android_ar "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"
    local cflags=""

    local android_ndk="$ANDROID_HOME/ndk/$NDK_VERSION"

    if [ "$target" == "arm64-v8a" ]; then
      cflags+="-march=armv8.7a+dotprod"
    elif [ "$target" == "x86_64" ]; then
      cflags+="-march=x86-64"
    fi

    _cmake_configure . build \
      -DCMAKE_TOOLCHAIN_FILE="$android_ndk/build/cmake/android.toolchain.cmake" \
      -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform" \
      -DCMAKE_SYSTEM_NAME=Android \
      -DCMAKE_CXX_COMPILER="$cxx" \
      -DCMAKE_C_COMPILER="$cc" \
      -DCMAKE_C_FLAGS="$cflags" \
      -DCMAKE_CXX_FLAGS="$cflags" \
      -DCMAKE_ANDROID_NDK="$android_ndk" \
      -DCMAKE_ANDROID_ARCH_ABI="$target" \
      -DANDROID_PLATFORM="android-$ANDROID_PLATFORM" \
      -DANDROID_ABI="$target" \
      -DGGML_ARM_DOTPROD=ON \
      -DGGML_LLAMAFILE=OFF \
      -DGGML_OPENMP=OFF \
      "${cmake_args[@]}" &&
    cmake --build build --config Release -j"$CPU_CORES" &&
    cmake --install build --config Release

    rc=$?
    die $rc "not ok - Unable to compile libllama for '$platform'"

    return
  fi

  if [[ "$host" != "Win32" ]]; then
    cp libllama.a ../lib
    die $? "not ok - Unable to compile libllama for '$platform'"
  fi

  cd "$BUILD_DIR" || exit 1
  rm -f "$root/build/$target-$platform/lib$d"/*.{so,la,dylib}*
  echo "ok - built libllama for $target-$platform"
  return 0
}

function _stage_llama_desktop_outputs {
  local staging_dir=$1
  local install_prefix=$2
  local libdir="$install_prefix/lib"
  local includedir="$install_prefix/include"

  mkdir -p "$libdir" "$includedir"

  local archives=(
    "$staging_dir/build/src/libllama.a"
    "$staging_dir/build/ggml/src/libggml.a"
    "$staging_dir/build/ggml/src/libggml-base.a"
    "$staging_dir/build/ggml/src/libggml-cpu.a"
    "$staging_dir/build/ggml/src/ggml-blas/libggml-blas.a"
    "$staging_dir/build/ggml/src/ggml-metal/libggml-metal.a"
  )

  local archive=""
  for archive in "${archives[@]}"; do
    if [[ -f "$archive" ]]; then
      copy_if_newer "$archive" "$libdir/$(basename "$archive")"
    fi
  done

  local header=""
  for header in "$staging_dir/include/"*.h "$staging_dir/ggml/include/"*.h; do
    if [[ -f "$header" ]]; then
      copy_if_newer "$header" "$includedir/$(basename "$header")"
    fi
  done
}

function _compile_libuv {
  local target=$1
  local hosttarget=$1
  local platform=$2

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  echo "# building libuv for $platform ($target) on $host..."
  local STAGING_DIR="$BUILD_DIR/$target-$platform/uv"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/uv/* "$STAGING_DIR"
    cd "$STAGING_DIR" || exit 1
    # Doesn't work in mingw
    if [[ "$host" != "Win32" ]]; then
      quiet sh autogen.sh
    fi;
  else
    cd "$STAGING_DIR" || exit 1
  fi

  mkdir -p "$STAGING_DIR/build/"

  if [ "$platform" == "desktop" ]; then
    if [[ "$host" != "Win32" ]]; then
      local libuv_archive="$BUILD_DIR/$target-$platform/lib/libuv.a"
      if _autotools_configure_needed "$STAGING_DIR" "$BUILD_DIR/$target-$platform" || ! test -f "$libuv_archive"; then
        if [[ "$host" == "Linux" ]]; then
          CFLAGS="-fPIC" quiet ./configure --prefix="$BUILD_DIR/$target-$platform"
          die $? "not ok - desktop configure"
        else
          quiet ./configure --prefix="$BUILD_DIR/$target-$platform"
          die $? "not ok - desktop configure"
        fi

        quiet make "-j$CPU_CORES"
        die $? "not ok - libuv desktop make -j$CPU_CORES"
        quiet make install
        die $? "not ok - libuv desktop make install"
      fi
    else
      if ! test -f "$BUILD_DIR/$target-$platform/lib$d/libuv.lib"; then
        local config="Release"
        if [[ -n "$DEBUG" ]]; then
          config="Debug"
        fi
        cd "$STAGING_DIR/build/" || exit 1
        quiet command -v cmake
        die $? "not ok - missing cmake, \"$(advice 'cmake')\""
        quiet cmake .. -DBUILD_TESTING=OFF -DLIBUV_BUILD_SHARED=OFF
        die $? "not ok - libuv cmake configure (Win32)"
        cd "$STAGING_DIR" || exit 1
        quiet cmake --build "$STAGING_DIR/build/" --config $config --parallel "$CPU_CORES"
        die $? "not ok - libuv cmake build (Win32)"
        mkdir -p "$BUILD_DIR/$target-$platform/lib$d"
        quiet echo "copy_if_newer $STAGING_DIR/build/$config/libuv.lib "$BUILD_DIR/$target-$platform/lib$d/libuv.lib""
        copy_if_newer "$STAGING_DIR/build/$config/libuv.lib" "$BUILD_DIR/$target-$platform/lib$d/libuv.lib"
        if [[ -n "$DEBUG" ]]; then
          copy_if_newer "$STAGING_DIR"/build/$config/uv_a.pdb "$BUILD_DIR/$target-$platform/lib$d/uv_a.pdb"
        fi;
      fi
    fi

    rm -f "$root/build/$(host_arch)-desktop/lib$d"/*.{so,la,dylib}*
    return
  fi

  if [ "$hosttarget" == "arm64" ]; then
    hosttarget="arm"
  fi

  # Use correct sdk, fixes:
  # ld: in /Users/ec2-user/app2/build/ios-simulator/lib/libuv.a(libuv_la-fs-poll.o), building for iOS Simulator, but linking in object file built for iOS, file '/Users/ec2-user/app2/build/ios-simulator/lib/libuv.a' for architecture arm64
  local sdk="iphoneos"
  [[ "$platform" == "iPhoneSimulator" ]] && sdk="iphonesimulator"

  export PLATFORM=$platform
  export CC="$(xcrun -sdk $sdk -find clang)"
  export CXX="$(xcrun -sdk $sdk -find clang++)"
  export STRIP="$(xcrun -sdk $sdk -find strip)"
  export LD="$(xcrun -sdk $sdk -find ld)"
  export CPP="$CC -E"
  export CFLAGS="-fembed-bitcode -arch ${target} -isysroot $PLATFORMPATH/$platform.platform/Developer/SDKs/$platform$SDKVERSION.sdk -m$sdk-version-min=$SDKMINVERSION"
  export AR=$(xcrun -sdk $sdk -find ar)
  export RANLIB=$(xcrun -sdk $sdk -find ranlib)
  export CPPFLAGS="-fembed-bitcode -arch ${target} -isysroot $PLATFORMPATH/$platform.platform/Developer/SDKs/$platform$SDKVERSION.sdk -m$sdk-version-min=$SDKMINVERSION"
  export LDFLAGS="-Wc,-fembed-bitcode -arch ${target} -isysroot $PLATFORMPATH/$platform.platform/Developer/SDKs/$platform$SDKVERSION.sdk"

  if ! test -f Makefile; then
    quiet ./configure --prefix="$BUILD_DIR/$target-$platform" --host="$hosttarget-apple-darwin"
  fi

  if [ ! $? = 0 ]; then
    echo "WARNING! - iOS will not be enabled. iPhone simulator not found, try \"sudo xcode-select --switch /Applications/Xcode.app\"."
    return
  fi

  quiet make "-j$CPU_CORES"
  quiet make install

  cd "$BUILD_DIR" || exit 1
  rm -f "$root/build/$target-$platform/lib$d"/*.{so,la,dylib}*
  echo "ok - built libuv for $target"
}

function _compile_whisper {
  local target=$1
  local platform=$2

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  echo "# building whisper.cpp for $platform ($target) on $host..."
  local STAGING_DIR="$BUILD_DIR/$target-$platform/whisper"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/whisper.cpp/* "$STAGING_DIR"
  fi

  cd "$STAGING_DIR" || exit 1

  local cmake_args=(
    -DWHISPER_BUILD_TESTS=OFF
    -DWHISPER_BUILD_EXAMPLES=OFF
    -DWHISPER_BUILD_SERVER=OFF
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_PREFIX_PATH="$BUILD_DIR/$target-$platform"
    -DCMAKE_INCLUDE_PATH="$BUILD_DIR/$target-$platform/include"
    -DCMAKE_LIBRARY_PATH="$BUILD_DIR/$target-$platform/lib:$BUILD_DIR/$target-$platform/lib64"
  )

  local ggml_dir="$BUILD_DIR/$target-$platform/lib/cmake/ggml"
  if _toolchain_supports_openmp && ([[ -f "$ggml_dir/ggml-config.cmake" ]] || [[ -f "$ggml_dir/ggmlConfig.cmake" ]]); then
    cmake_args+=(-DWHISPER_USE_SYSTEM_GGML=ON -Dggml_DIR="$ggml_dir")
  else
    cmake_args+=(-DWHISPER_USE_SYSTEM_GGML=OFF)
    if ! _toolchain_supports_openmp; then
      cmake_args+=(-DGGML_OPENMP=OFF)
      echo "warn - disabling system ggml for whisper because the current toolchain cannot link OpenMP"
    else
      echo "warn - ggml cmake package not found for whisper ($ggml_dir); building with bundled ggml"
    fi
  fi

  if [[ "$platform" == "desktop" && "$host" == "Darwin" ]]; then
    local darwin_warning_flags="$(_darwin_third_party_warning_flags)"
    cmake_args+=(
      -DCMAKE_C_FLAGS="$darwin_warning_flags"
      -DCMAKE_CXX_FLAGS="$darwin_warning_flags"
      -DCMAKE_OBJC_FLAGS="$darwin_warning_flags"
      -DCMAKE_OBJCXX_FLAGS="$darwin_warning_flags"
    )
  fi

  quiet command -v cmake
  die $? "not ok - missing cmake, \"$(advice 'cmake')\""

  if [ "$platform" == "desktop" ]; then
    if [[ "$host" != "Win32" ]]; then
      cmake_args+=(-DCMAKE_POSITION_INDEPENDENT_CODE=ON)
      local cflags="-fPIC"
      if [[ "$host" == "Darwin" ]]; then
        cflags+=" $(_darwin_third_party_warning_flags)"
      fi
      export CFLAGS="$cflags"
      export CXXFLAGS="$cflags"
      export OBJCFLAGS="$cflags"
      export OBJCXXFLAGS="$cflags"

      _cmake_configure . build -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform" "${cmake_args[@]}"
      die $? "not ok - libwhisper.a (desktop) configure"

      quiet cmake --build build --config Release -- -j"$CPU_CORES"
      die $? "not ok - libwhisper.a (desktop) build"

      quiet cmake --install build --config Release
      die $? "not ok - libwhisper.a (desktop) install"
    else
      if ! test -f "$BUILD_DIR/$target-$platform/lib$d/whisper.lib"; then
        local config="Release"
        if [[ -n "$DEBUG" ]]; then
          config="Debug"
        fi

        mkdir -p "$STAGING_DIR/build"
        cd "$STAGING_DIR/build" || exit 1
        _cmake_configure .. . "${cmake_args[@]}"
        die $? "not ok - libwhisper.lib (desktop) configure"

        quiet cmake --build . --config $config --parallel "$CPU_CORES"
        die $? "not ok - libwhisper.lib (desktop) build"

        mkdir -p "$BUILD_DIR/$target-$platform/lib$d"
        copy_if_newer "$STAGING_DIR/build/$config/whisper.lib" "$BUILD_DIR/$target-$platform/lib$d/whisper.lib"
        if [[ -n "$DEBUG" ]]; then
          if [ -f "$STAGING_DIR/build/$config/whisper.pdb" ]; then
            copy_if_newer "$STAGING_DIR/build/$config/whisper.pdb" "$BUILD_DIR/$target-$platform/lib$d/whisper.pdb"
          fi
        fi
      fi
      cd "$STAGING_DIR" || exit 1
    fi

    rm -f "$root/build/$target-$platform/lib$d"/*whisper*.{so,la,dylib}* 2>/dev/null || true
    echo "ok - built libwhisper for $target-$platform"
    return
  fi

  if [ "$platform" == "iPhoneOS" ] || [ "$platform" == "iPhoneSimulator" ]; then
    local sdk="iphoneos"
    [[ "$platform" == "iPhoneSimulator" ]] && sdk="iphonesimulator"

    local cc="$(xcrun -sdk $sdk -find clang)"
    local cxx="$(xcrun -sdk $sdk -find clang++)"
    local ar="$(xcrun -sdk $sdk -find ar)"
    local ranlib="$(xcrun -sdk $sdk -find ranlib)"
    local sdkroot="$PLATFORMPATH/$platform.platform/Developer/SDKs/$platform$SDKVERSION.sdk"

    export CC="$cc"
    export CXX="$cxx"
    export AR="$ar"
    export RANLIB="$ranlib"

    local cflags="--target=$target-apple-ios -isysroot $sdkroot -m$sdk-version-min=$SDKMINVERSION -fembed-bitcode"
    if [ "$platform" == "iPhoneOS" ]; then
      cflags+=" -march=armv8.2-a+dotprod"
    elif [ "$target" == "arm64" ]; then
      cflags+=" -march=armv8.2-a+dotprod"
    elif [ "$target" == "x86_64" ]; then
      cflags+=" -march=x86-64 --target=x86_64-apple-ios-simulator"
    fi

    export CFLAGS="$cflags"
    export CXXFLAGS="$cflags"

    _cmake_configure . build \
      -DCMAKE_SYSTEM_NAME="iOS" \
      -DCMAKE_OSX_ARCHITECTURES="$target" \
      -DCMAKE_OSX_SYSROOT="$sdkroot" \
      -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform" \
      -DCMAKE_C_COMPILER="$cc" \
      -DCMAKE_CXX_COMPILER="$cxx" \
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
      "${cmake_args[@]}"
    die $? "not ok - libwhisper.a ($platform) configure"

    quiet cmake --build build --config Release -- -j"$CPU_CORES"
    die $? "not ok - libwhisper.a ($platform) build"

    quiet cmake --install build --config Release
    die $? "not ok - libwhisper.a ($platform) install"

    rm -f "$root/build/$target-$platform/lib"/*whisper*.{so,la,dylib}* 2>/dev/null || true
    echo "ok - built libwhisper for $target-$platform"
    return
  fi

  if [ "$platform" == "android" ]; then
    local host_arch="$(host_arch)"
    local cc="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"
    local cxx="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch" "++")"
    local ar="$(android_ar "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"

    export AR="$ar"
    export CC="$cc"
    export CXX="$cxx"

    local cflags=""
    if [ "$target" == "arm64-v8a" ]; then
      cflags+="-march=armv8.7a+dotprod"
    elif [ "$target" == "x86_64" ]; then
      cflags+="-march=x86-64"
    fi

    export CFLAGS="$cflags"
    export CXXFLAGS="$cflags"

    _cmake_configure . build \
      -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/$NDK_VERSION/build/cmake/android.toolchain.cmake" \
      -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform" \
      -DCMAKE_SYSTEM_NAME=Android \
      -DCMAKE_ANDROID_ARCH_ABI="$target" \
      -DCMAKE_ANDROID_NDK="$ANDROID_HOME/ndk/$NDK_VERSION" \
      -DANDROID_PLATFORM="android-$ANDROID_PLATFORM" \
      -DANDROID_ABI="$target" \
      "${cmake_args[@]}"
    die $? "not ok - libwhisper.a (android) configure"

    quiet cmake --build build --config Release -- -j"$CPU_CORES"
    die $? "not ok - libwhisper.a (android) build"

    quiet cmake --install build --config Release
    die $? "not ok - libwhisper.a (android) install"

    rm -f "$root/build/$target-$platform/lib"/*whisper*.{so,la,dylib}* 2>/dev/null || true
    echo "ok - built libwhisper for $target-$platform"
    return
  fi

  echo "warn - whisper build for $platform not implemented; skipping"
}

function _compile_iroh_ffi {
  local platform="${1:-desktop}"
  local arch="$(host_arch)"

  if [[ "${ORO_SKIP_IROH:-}" = "1" ]]; then
    echo "warn - skipping oro-iroh build (ORO_SKIP_IROH=1)"
    return 1
  fi

  if [[ "$platform" != "desktop" ]]; then
    echo "warn - oro-iroh build for $platform not implemented; skipping"
    return 1
  fi

  if ! quiet command -v cargo; then
    echo "warn - cargo not found; skipping oro-iroh build"
    return 1
  fi

  local crate_dir="$root/rust/oro-iroh"
  if [[ ! -d "$crate_dir" ]]; then
    echo "warn - oro-iroh crate not found; skipping build"
    return 1
  fi

  if [[ ! -d "$BUILD_DIR/iroh" ]]; then
    echo "warn - iroh sources not staged; skipping oro-iroh build"
    return 1
  fi

  echo "# building oro-iroh bindings for $platform ($arch)..."
  (cd "$crate_dir" && cargo build --release --locked)
  local rc=$?
  if (( rc != 0 )); then
    die $rc "not ok - oro-iroh cargo build ($platform)"
  fi

  local cargo_target_dir="${CARGO_TARGET_DIR:-$crate_dir/target}"
  if [[ "$cargo_target_dir" != /* ]] && [[ ! "$cargo_target_dir" =~ ^[A-Za-z]:[\\/] ]]; then
    cargo_target_dir="$crate_dir/$cargo_target_dir"
  fi
  local release_dir="$cargo_target_dir/release"
  mkdir -p "$BUILD_DIR/$arch-$platform/lib"
  mkdir -p "$BUILD_DIR/include/iroh"
  mkdir -p "$BUILD_DIR/pkgconfig"

  local copied=0
  local -a oro_libs=(liboro_iroh.a liboro_iroh.so liboro_iroh.dylib oro_iroh.dll oro_iroh.lib)
  for oro_lib in "${oro_libs[@]}"; do
    local source="$release_dir/$oro_lib"
    if [[ -f "$source" ]]; then
      copy_if_newer "$source" "$BUILD_DIR/$arch-$platform/lib/$oro_lib"
      copied=1
    fi
  done

  if (( ! copied )); then
    echo "warn - oro-iroh artifacts not found in $release_dir"
    return 1
  fi

  for header in oro_iroh socket_iroh; do
    if [[ -f "$root/include/iroh/$header.h" ]]; then
      copy_if_newer "$root/include/iroh/$header.h" "$BUILD_DIR/include/iroh/$header.h"
    fi
  done

  # Android CI builds two ABI trees alongside the desktop CLI. Once the FFI
  # library is staged, its multi-gigabyte Cargo target is no longer needed.
  # Keep incremental outputs for normal developer builds.
  if [[ "${ORO_PRUNE_IROH_BUILD_OUTPUTS:-false}" == "true" ]] || \
     [[ "${ORO_PRUNE_TRANSIENT_BUILD_OUTPUTS:-false}" == "true" ]]; then
    echo "# pruning staged oro-iroh Cargo build outputs..."
    (cd "$crate_dir" && cargo clean --target-dir "$cargo_target_dir")
    rc=$?
    if (( rc != 0 )); then
      die $rc "not ok - unable to prune staged oro-iroh Cargo build outputs"
    fi
  fi

  return 0
}

function _crsqlite_rust_toolchain {
  local rust_toolchain="nightly-2023-10-05"
  local rust_toolchain_file="$BUILD_DIR/cr-sqlite/core/rs/bundle_static/rust-toolchain.toml"

  if [[ -f "$rust_toolchain_file" ]]; then
    local parsed_toolchain=""
    parsed_toolchain="$(grep -E '^[[:space:]]*channel[[:space:]]*=' "$rust_toolchain_file" | head -n 1 | sed -E 's/.*"([^"]+)".*/\1/')"
    if [[ -n "$parsed_toolchain" ]]; then
      rust_toolchain="$parsed_toolchain"
    fi
  fi

  echo "$rust_toolchain"
}

function _compile_crsqlite_loadable {
  local platform="${1:-desktop}"
  local target="${2:-}"
  local arch="$(host_arch)"

  if [[ ! -d "$BUILD_DIR/cr-sqlite/core" ]]; then
    die 1 "not ok - cr-sqlite sources not staged (expected at '$BUILD_DIR/cr-sqlite/core')"
  fi

  if ! quiet command -v cargo; then
    die 1 "not ok - cargo not found; cr-sqlite build requires a Rust toolchain"
  fi

  local crsqlite_cargo_target_dir="$BUILD_DIR/cr-sqlite/core/rs/bundle_static/target"

  case "$platform" in
    desktop)
      echo "# building cr-sqlite loadable extension for $platform ($arch)..."
      local -a crsqlite_make_args=(-j"$CPU_CORES" loadable)
      if [[ "$host" == "Win32" ]]; then
        # Rust's MSVC target emits a .lib archive, while the upstream Makefile
        # assumes Unix's lib*.a name, defaults to an unprovisioned gcc, and
        # unconditionally adds the unsupported -fPIC flag.
        crsqlite_make_args=(
          "CI_GCC=clang"
          "LOADABLE_CFLAGS=-std=c99 -shared -Wall"
          "rs_lib_loadable=./rs/bundle_static/target/release/crsql_bundle_static.lib"
          -j"$CPU_CORES"
          loadable
        )
      fi
      (
        cd "$BUILD_DIR/cr-sqlite/core" &&
        CARGO_TARGET_DIR="$crsqlite_cargo_target_dir" quiet make "${crsqlite_make_args[@]}"
      )
      local rc=$?
      if (( rc != 0 )); then
        die $rc "not ok - cr-sqlite loadable extension build ($platform)"
      fi

      local ext="so"
      if [[ "$host" == "Darwin" ]]; then
        ext="dylib"
      elif [[ "$host" == "Win32" ]]; then
        ext="dll"
      fi

      local built="$BUILD_DIR/cr-sqlite/core/dist/crsqlite.$ext"
      if [[ ! -f "$built" ]]; then
        die 1 "not ok - cr-sqlite loadable extension not found at '$built'"
      fi

      mkdir -p "$BUILD_DIR/$arch-$platform/extensions"
      copy_if_newer "$built" "$BUILD_DIR/$arch-$platform/extensions/$(basename "$built")"
      echo "ok - built cr-sqlite loadable extension for $platform ($arch)"
      ;;

    ios)
      if [[ "$host" != "Darwin" ]]; then
        die 1 "not ok - cr-sqlite iOS build requested on non-Darwin host"
      fi

      # bindgen 0.68.1 maps Rust's aarch64 iOS Simulator target to
      # arm64-apple-ios-sim, which Apple Clang rejects. A target-specific
      # argument prevents bindgen from adding that invalid implicit target.
      local simulator_bindgen_args="--target=arm64-apple-ios-simulator ${BINDGEN_EXTRA_CLANG_ARGS_aarch64_apple_ios_sim:-}"

      echo "# building cr-sqlite iOS loadable variants..."
      (
        cd "$BUILD_DIR/cr-sqlite/core" &&
        BINDGEN_EXTRA_CLANG_ARGS_aarch64_apple_ios_sim="$simulator_bindgen_args" \
        CARGO_TARGET_DIR="$crsqlite_cargo_target_dir" \
        bash -e ./all-ios-loadable.sh
      )
      local rc=$?
      die $rc "not ok - cr-sqlite iOS loadable build"

      local core_dir="$BUILD_DIR/cr-sqlite/core"
      local device_dylib="$core_dir/dist-ios/crsqlite-aarch64-apple-ios.dylib"
      local sim_universal="$core_dir/dist-ios-sim/crsqlite-universal-ios-sim.dylib"

      if [[ ! -f "$device_dylib" ]]; then
        die 1 "not ok - missing cr-sqlite iOS device dylib at '$device_dylib'"
      fi

      if [[ ! -f "$sim_universal" ]]; then
        die 1 "not ok - missing cr-sqlite iOS simulator dylib at '$sim_universal'"
      fi

      # Stage iOS device extension for arm64 iPhoneOS
      mkdir -p "$BUILD_DIR/arm64-iPhoneOS/extensions"
      copy_if_newer "$device_dylib" "$BUILD_DIR/arm64-iPhoneOS/extensions/crsqlite.dylib"

      # Stage iOS simulator extension for x86_64 and arm64 simulators
      mkdir -p "$BUILD_DIR/x86_64-iPhoneSimulator/extensions"
      mkdir -p "$BUILD_DIR/arm64-iPhoneSimulator/extensions"
      copy_if_newer "$sim_universal" "$BUILD_DIR/x86_64-iPhoneSimulator/extensions/crsqlite.dylib"
      copy_if_newer "$sim_universal" "$BUILD_DIR/arm64-iPhoneSimulator/extensions/crsqlite.dylib"

      echo "ok - staged cr-sqlite iOS extensions for arm64 iPhoneOS and simulators"
      ;;

    android)
      if [[ -z "$target" ]]; then
        die 1 "not ok - cr-sqlite android build requested without ABI target"
      fi

      if [[ -z "$ANDROID_HOME" ]] || [[ -z "$NDK_VERSION" ]]; then
        die 1 "not ok - Android toolchain not configured for cr-sqlite build"
      fi

      local android_triple=""
      case "$target" in
        arm64-v8a) android_triple="aarch64-linux-android" ;;
        x86_64) android_triple="x86_64-linux-android" ;;
        *)
          die 1 "not ok - unsupported Android ABI for cr-sqlite: $target"
          ;;
      esac

      local cargo_home="${CARGO_HOME:-$HOME/.cargo}"
      local rustup_home="${RUSTUP_HOME:-$HOME/.rustup}"
      local rustup_write_test="$rustup_home/downloads/.oro-write-test"
      local cargo_write_test="$cargo_home/bin/.oro-write-test"

      # Some build environments restrict writes outside the workspace (e.g. sandboxed CI),
      # so fall back to per-build caches under build/ when needed.
      if ! mkdir -p "$(dirname "$rustup_write_test")" "$cargo_home/bin" 2>/dev/null || ! : > "$rustup_write_test" 2>/dev/null || ! : > "$cargo_write_test" 2>/dev/null; then
        cargo_home="$BUILD_DIR/.cargo"
        rustup_home="$BUILD_DIR/.rustup"
        export CARGO_HOME="$cargo_home"
        export RUSTUP_HOME="$rustup_home"

        rustup_write_test="$RUSTUP_HOME/downloads/.oro-write-test"
        cargo_write_test="$CARGO_HOME/bin/.oro-write-test"
        mkdir -p "$(dirname "$rustup_write_test")" "$CARGO_HOME/bin"
        die $? "not ok - unable to initialize Rust toolchain caches at $RUSTUP_HOME"

        : > "$rustup_write_test" 2>/dev/null
        die $? "not ok - unable to write to Rust toolchain cache at $RUSTUP_HOME"

        : > "$cargo_write_test" 2>/dev/null
        die $? "not ok - unable to write to cargo bin cache at $CARGO_HOME/bin"
      fi

      quiet cmake -E rm -f "$rustup_write_test" "$cargo_write_test"
      export PATH="$cargo_home/bin:$PATH"

      if ! command -v rustup >/dev/null 2>&1; then
        die 1 "not ok - rustup not found; required to install the pinned Rust toolchain for cr-sqlite"
      fi

      local rust_toolchain=""
      rust_toolchain="$(_crsqlite_rust_toolchain)"

      echo "# ensuring Rust toolchain for cr-sqlite android builds ($rust_toolchain / $android_triple)..."
      if ! rustup toolchain list | awk '{ print $1 }' | grep -q "^${rust_toolchain}"; then
        rustup toolchain install "$rust_toolchain"
        die $? "not ok - rustup toolchain install $rust_toolchain failed"
      fi

      if ! rustup component list --toolchain "$rust_toolchain" --installed | grep -q '^rust-src'; then
        rustup component add rust-src --toolchain "$rust_toolchain"
        die $? "not ok - rustup component add rust-src ($rust_toolchain) failed"
      fi

      if ! rustup target list --toolchain "$rust_toolchain" --installed | grep -qx "$android_triple"; then
        rustup target add "$android_triple" --toolchain "$rust_toolchain"
        die $? "not ok - rustup target add $android_triple ($rust_toolchain) failed"
      fi

      local cargo_ndk_version="${CARGO_NDK_VERSION:-4.1.2}"
      local installed_cargo_ndk_version=""
      if command -v cargo-ndk >/dev/null 2>&1; then
        installed_cargo_ndk_version="$(cargo ndk --version 2>/dev/null | awk '{ print $2 }')"
      fi

      if [[ "$installed_cargo_ndk_version" != "$cargo_ndk_version" ]]; then
        echo "# installing cargo-ndk $cargo_ndk_version (required for cr-sqlite android build)..."
        cargo install cargo-ndk --version "$cargo_ndk_version" --locked
        die $? "not ok - cargo install cargo-ndk $cargo_ndk_version failed"

        if ! command -v cargo-ndk >/dev/null 2>&1; then
          die 1 "not ok - cargo-ndk installed but not found on PATH (expected in $cargo_home/bin)"
        fi
      fi

      export ANDROID_NDK_HOME="${ANDROID_HOME}/ndk/${NDK_VERSION}"

      echo "# building cr-sqlite loadable extension for android ($target / $android_triple)..."
      (
        cd "$BUILD_DIR/cr-sqlite/core" &&
        CARGO_TARGET_DIR="$crsqlite_cargo_target_dir" make clean &&
        CARGO_TARGET_DIR="$crsqlite_cargo_target_dir" ANDROID_TARGET="$android_triple" make -j"$CPU_CORES" loadable
      )
      local rc=$?
      if (( rc != 0 )); then
        die $rc "not ok - cr-sqlite android loadable extension build ($target)"
      fi

      local built_android="$BUILD_DIR/cr-sqlite/core/dist/crsqlite.so"
      if [[ ! -f "$built_android" ]]; then
        die 1 "not ok - cr-sqlite android loadable extension not found at '$built_android'"
      fi

      local dest_arch="$target"
      local dest_platform="android"
      mkdir -p "$BUILD_DIR/$dest_arch-$dest_platform/extensions"
      copy_if_newer "$built_android" "$BUILD_DIR/$dest_arch-$dest_platform/extensions/crsqlite.so"
      echo "ok - built cr-sqlite loadable extension for android ($target)"
      ;;

    *)
      die 1 "not ok - cr-sqlite build for platform '$platform' not implemented"
      ;;
  esac

  return 0
}

function _compile_libusb {
  local target=$1
  local platform=$2

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  echo "# building libusb for $platform ($target) on $host..."
  local STAGING_DIR="$BUILD_DIR/$target-$platform/libusb"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/libusb/* "$STAGING_DIR"
  fi

  cd "$STAGING_DIR" || exit 1

  if [ "$platform" == "desktop" ]; then
    if [[ "$host" != "Win32" ]]; then
      local use_autotools=1

      if [ ! -f "$STAGING_DIR/configure" ]; then
        local bootstrap_script=""
        if [ -x "$STAGING_DIR/bootstrap.sh" ]; then
          bootstrap_script="$STAGING_DIR/bootstrap.sh"
        elif [ -x "$STAGING_DIR/autogen.sh" ]; then
          bootstrap_script="$STAGING_DIR/autogen.sh"
        fi

        if [[ -n "$bootstrap_script" ]]; then
          if ! quiet sh "$bootstrap_script"; then
            echo "warn - libusb bootstrap failed, falling back to cmake"
            use_autotools=0
          fi
        else
          use_autotools=0
        fi
      fi

      if (( use_autotools )) && [ ! -f "$STAGING_DIR/configure" ]; then
        use_autotools=0
        echo "warn - libusb configure script missing, falling back to cmake"
      fi

      if (( use_autotools )); then
        local libusb_archive="$BUILD_DIR/$target-$platform/lib/libusb-1.0.a"
        if _autotools_configure_needed "$STAGING_DIR" "$BUILD_DIR/$target-$platform" || ! test -f "$libusb_archive"; then
          quiet ./configure --disable-shared --enable-shared=no --disable-udev --prefix="$BUILD_DIR/$target-$platform"
          die $? "not ok - libusb desktop configure"

          quiet make "-j$CPU_CORES"
          die $? "not ok - libusb desktop make"
          quiet make install
          die $? "not ok - libusb desktop install"
        fi

        local libdir="$BUILD_DIR/$target-$platform/lib"
        mkdir -p "$libdir"
        if [ ! -f "$libdir/libusb-1.0.a" ] && [ -f "$STAGING_DIR/libusb/.libs/libusb-1.0.a" ]; then
          copy_if_newer "$STAGING_DIR/libusb/.libs/libusb-1.0.a" "$libdir/libusb-1.0.a"
        fi
      else
        quiet command -v cmake
        die $? "not ok - missing cmake, \"$(advice 'cmake')\""

        local cmake_build_dir="$STAGING_DIR/build-cmake"
        quiet cmake -S . -B "$cmake_build_dir" \
          -DLIBUSB_BUILD_SHARED_LIBS=OFF \
          -DBUILD_SHARED_LIBS=OFF \
          -DLIBUSB_BUILD_TESTING=OFF \
          -DLIBUSB_BUILD_EXAMPLES=OFF \
          -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform"
        die $? "not ok - libusb cmake configure ($platform)"

        quiet cmake --build "$cmake_build_dir" --config Release -- -j"$CPU_CORES"
        die $? "not ok - libusb cmake build ($platform)"

        quiet cmake --install "$cmake_build_dir" --config Release
        die $? "not ok - libusb cmake install ($platform)"

        local libdir="$BUILD_DIR/$target-$platform/lib"
        if [ ! -d "$libdir" ] && [ -d "$BUILD_DIR/$target-$platform/lib64" ]; then
          libdir="$BUILD_DIR/$target-$platform/lib64"
        fi

        if [ -f "$libdir/libusb-1.0.a" ]; then
          mkdir -p "$BUILD_DIR/$target-$platform/lib"
          copy_if_newer "$libdir/libusb-1.0.a" "$BUILD_DIR/$target-$platform/lib/libusb-1.0.a"
        else
          die 1 "not ok - libusb static archive not found after cmake install ($platform)"
        fi
      fi
    else
      local config="Release"
      local suffix=""
      if [[ -n "$DEBUG" ]]; then
        config="Debug"
        suffix="d"
      fi

      local output_lib="$BUILD_DIR/$target-$platform/lib$suffix/libusb-1.0.lib"
      if ! test -f "$output_lib"; then
        local msbuild_platform=""
        case "$target" in
          x86_64|amd64) msbuild_platform="x64" ;;
          arm64|aarch64) msbuild_platform="ARM64" ;;
          x86|i686) msbuild_platform="Win32" ;;
          *) die 1 "not ok - unsupported Windows libusb architecture: $target" ;;
        esac

        local libusb_project="$STAGING_DIR/msvc/libusb_static.vcxproj"
        if [[ ! -f "$libusb_project" ]]; then
          die 1 "not ok - libusb MSBuild project not found at $libusb_project"
        fi

        quiet command -v MSBuild.exe
        die $? "not ok - missing MSBuild.exe; install the Visual C++ build tools"

        quiet env "_CL_=${_CL_:+$_CL_ }-wd5287" MSBuild.exe "$libusb_project" \
          "-m:$CPU_CORES" \
          "-p:Configuration=$config" \
          "-p:Platform=$msbuild_platform"
        die $? "not ok - libusb MSBuild build (Win32)"

        mkdir -p "$BUILD_DIR/$target-$platform/lib$suffix"

        local staged_lib=""
        local staged_lib_candidate=""
        for staged_lib_candidate in "$STAGING_DIR"/build/*/"$msbuild_platform"/"$config"/lib/libusb-1.0.lib; do
          if [[ -f "$staged_lib_candidate" ]] &&
             { [[ -z "$staged_lib" ]] ||
               (( $(stat_mtime "$staged_lib_candidate") > $(stat_mtime "$staged_lib") )); }; then
            staged_lib="$staged_lib_candidate"
          fi
        done

        if [[ -f "$staged_lib" ]]; then
          copy_if_newer "$staged_lib" "$output_lib"
        else
          die 1 "not ok - libusb lib not found after build (Win32)"
        fi

        if [[ -n "$DEBUG" ]]; then
          local staged_pdb="${staged_lib%.lib}.pdb"
          if [[ -f "$staged_pdb" ]]; then
            copy_if_newer "$staged_pdb" "$BUILD_DIR/$target-$platform/lib$suffix/libusb-1.0.pdb"
          fi
        fi
      fi

      cd "$STAGING_DIR" || exit 1
    fi

    rm -f "$root/build/$target-$platform/lib$d"/*.{so,la,dylib}* 2>/dev/null || true
    return
  elif [ "$platform" == "iPhoneOS" ] || [ "$platform" == "iPhoneSimulator" ]; then
    echo "warn - skipping libusb for $platform because upstream libusb does not support iOS"
    return 0
  fi

  echo "warn - libusb build for $platform not implemented; skipping"
}

function _compile_libipfs {
  local target="${1:-$(host_arch)}"
  local platform="${2:-desktop}"

  if [[ "${ORO_SKIP_LIBIPFS:-0}" = "1" ]]; then
    echo "warn - skipping libipfs build (ORO_SKIP_LIBIPFS=1)"
    return 0
  fi

  if [[ "$platform" != "desktop" ]]; then
    echo "warn - libipfs build for $platform not implemented; skipping"
    return 0
  fi

  if [[ "$host" == "Win32" ]]; then
    echo "warn - libipfs uses a MinGW C archive that is incompatible with the MSVC runtime build; skipping"
    return 0
  fi

  local goos=""
  case "$host" in
    Linux) goos="linux" ;;
    Darwin) goos="darwin" ;;
    Win32) goos="windows" ;;
    *) echo "warn - unsupported host $host for libipfs"; return 0 ;;
  esac

  local goarch=""
  case "$target" in
    x86_64|amd64) goarch="amd64" ;;
    arm64) goarch="arm64" ;;
    riscv64) goarch="riscv64" ;;
    *) echo "warn - unsupported libipfs target arch $target"; return 0 ;;
  esac

  local source="$BUILD_DIR/libipfs"
  if [ ! -d "$source" ]; then
    echo "warn - libipfs sources not found at $source; skipping"
    return 0
  fi

  if [ ! -f "$source/libipfs.go" ]; then
    echo "warn - libipfs entrypoint libipfs.go missing; skipping"
    return 0
  fi

  if ! command -v go >/dev/null 2>&1; then
    die 1 "not ok - Go 1.23.2 or newer is required to build libipfs. Install Go or set ORO_SKIP_LIBIPFS=1 to disable libipfs."
  fi

  mkdir -p "$source/bin"
  mkdir -p "$BUILD_DIR/libipfs/include"
  mkdir -p "$BUILD_DIR/$target-$platform/lib"

  local gocache="${GOCACHE:-$root/.cache/go-build/$goos-$goarch}"
  local gomodcache="${GOMODCACHE:-$root/.cache/go-mod}"
  local output_base="libipfs-${goos}-${goarch}"
  local archive_path="$source/bin/${output_base}.a"
  local header_path="$source/bin/${output_base}.h"

  local goflags=()
  if [ -d "$source/vendor" ]; then
    goflags+=("-mod=vendor")
  fi

  local cgo_env=()
  if [[ "$host" == "Win32" ]]; then
    if ! command -v clang >/dev/null 2>&1; then
      die 1 "not ok - clang is required to build libipfs on Windows. Install clang or set ORO_SKIP_LIBIPFS=1 to disable libipfs."
    fi

    # Go parses CC as a command line. Using the absolute LLVM path from
    # install.ps1 makes cgo split C:\Program Files at the first space.
    cgo_env+=("CC=clang")
  fi

  mkdir -p "$gocache" "$gomodcache"

  echo "# building libipfs for $goos/$goarch..."
  local attempt=1
  local max_attempts="${ORO_LIBIPFS_BUILD_ATTEMPTS:-3}"
  local rc=1
  if [[ ! "$max_attempts" =~ ^[1-9][0-9]*$ ]]; then
    die 1 "not ok - ORO_LIBIPFS_BUILD_ATTEMPTS must be a positive integer"
  fi

  while (( attempt <= max_attempts )); do
    (
      cd "$source" || exit 1
      env \
        "${cgo_env[@]}" \
        CGO_ENABLED=1 \
        GOOS="$goos" \
        GOARCH="$goarch" \
        GOCACHE="$gocache" \
        GOMODCACHE="$gomodcache" \
        go build \
          -buildmode=c-archive \
          -ldflags="-s -w" \
          -trimpath \
          -modcacherw \
          "${goflags[@]}" \
          -o "$archive_path" \
          ./libipfs.go
    )
    rc=$?

    if (( rc == 0 || attempt == max_attempts )); then
      break
    fi

    echo "warn - libipfs go build failed (attempt $attempt/$max_attempts); retrying with the populated Go cache"
    sleep $(( attempt * 2 ))
    (( attempt += 1 ))
  done

  if (( rc != 0 )); then
    die $rc "not ok - libipfs go build ($goos/$goarch). Ensure dependencies are vendored or set ORO_SKIP_LIBIPFS=1 to skip."
  fi

  if [ ! -f "$archive_path" ] || [ ! -f "$header_path" ]; then
    die 1 "not ok - libipfs artifacts missing after build ($archive_path)"
  fi

  local libdir="$BUILD_DIR/$target-$platform/lib"
  mkdir -p "$libdir"
  cp -pf "$archive_path" "$libdir/libipfs.a"

  if [[ "$host" == "Win32" ]]; then
    local win_suffix=""
    [[ -n "$DEBUG" ]] && win_suffix="d"
    mkdir -p "$BUILD_DIR/$target-$platform/lib$win_suffix"
    cp -pf "$archive_path" "$BUILD_DIR/$target-$platform/lib$win_suffix/libipfs${win_suffix}.a"
    cp -pf "$archive_path" "$BUILD_DIR/$target-$platform/lib$win_suffix/libipfs${win_suffix}.lib"
  fi

  cp -pf "$header_path" "$BUILD_DIR/libipfs/include/$output_base.h"
  cp -pf "$header_path" "$BUILD_DIR/libipfs/include/libipfs.h"
  cp -pf "$header_path" "$BUILD_DIR/include/libipfs.h"

  return 0
}

function _compile_zlib {
  local target=$1
  local platform=$2

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  local source_root="$BUILD_DIR/zlib"
  if [[ ! -d "$source_root" ]]; then
    echo "warn - zlib sources not staged (expected in $source_root); skipping"
    return
  fi

  echo "# building zlib for $platform ($target) on $host..."

  local STAGING_DIR="$BUILD_DIR/$target-$platform/zlib"

  if [[ ! -d "$STAGING_DIR" ]] || (( force )); then
    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR"
    cp -r "$source_root"/* "$STAGING_DIR"
  fi

  cd "$STAGING_DIR" || exit 1

  quiet command -v cmake
  die $? "not ok - missing cmake, \"$(advice 'cmake')\""
  local cmake_args=(
    -DBUILD_SHARED_LIBS=OFF
    -DZLIB_BUILD_EXAMPLES=OFF
    -DSKIP_INSTALL_LIBRARIES=OFF
    -DSKIP_INSTALL_HEADERS=OFF
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform"
    -DINSTALL_LIB_DIR="lib"
    -DINSTALL_INC_DIR="include"
  )

  if [[ "$platform" == "desktop" ]]; then
    if [[ "$host" != "Win32" ]]; then
      _cmake_configure . build -DCMAKE_BUILD_TYPE=Release "${cmake_args[@]}"
      die $? "not ok - zlib configure ($platform)"

      quiet cmake --build build --config Release -- -j"$CPU_CORES"
      die $? "not ok - zlib build ($platform)"

      quiet cmake --install build --config Release
      die $? "not ok - zlib install ($platform)"
    else
      local config="Release"
      local suffix=""
      if [[ -n "$DEBUG" ]]; then
        config="Debug"
        suffix="d"
      fi

      mkdir -p "$STAGING_DIR/build"
      cd "$STAGING_DIR/build" || exit 1

      _cmake_configure .. . "${cmake_args[@]}"
      die $? "not ok - zlib cmake configure (Win32)"

      quiet cmake --build . --config "$config" --parallel "$CPU_CORES"
      die $? "not ok - zlib cmake build (Win32)"

      quiet cmake --install . --config "$config"
      die $? "not ok - zlib cmake install (Win32)"

      local output_libdir="$BUILD_DIR/$target-$platform/lib$suffix"
      mkdir -p "$output_libdir"

      local staged_lib=""
      local staged_base="$STAGING_DIR/build/$config"
      if [[ -d "$staged_base" ]]; then
        for candidate in \
          "$staged_base/zlibstatic.lib" \
          "$staged_base/zlib.lib"       \
          "$staged_base/z.lib"
        do
          if [[ -f "$candidate" ]]; then
            staged_lib="$candidate"
            break
          fi
        done
      fi

      if [[ -z "$staged_lib" ]]; then
        for candidate in \
          "$BUILD_DIR/$target-$platform/lib/zlibstatic.lib" \
          "$BUILD_DIR/$target-$platform/lib/zlib.lib"       \
          "$BUILD_DIR/$target-$platform/lib/z.lib"
        do
          if [[ -f "$candidate" ]]; then
            staged_lib="$candidate"
            break
          fi
        done
      fi

      if [[ -z "$staged_lib" ]]; then
        die 1 "not ok - zlib lib not found after build (Win32)"
      fi

      copy_if_newer "$staged_lib" "$output_libdir/z.lib"
      copy_if_newer "$staged_lib" "$output_libdir/zlib.lib"

      # Also ensure a non-suffixed lib directory has a copy so feature
      # detection in bin/cflags.sh can find the archive.
      if [[ "$suffix" != "" ]]; then
        mkdir -p "$BUILD_DIR/$target-$platform/lib"
        copy_if_newer "$staged_lib" "$BUILD_DIR/$target-$platform/lib/z.lib"
        copy_if_newer "$staged_lib" "$BUILD_DIR/$target-$platform/lib/zlib.lib"
      fi

      cd "$STAGING_DIR" || exit 1
    fi
  elif [[ "$platform" == "iPhoneOS" ]] || [[ "$platform" == "iPhoneSimulator" ]]; then
    local sdk="iphoneos"
    [[ "$platform" == "iPhoneSimulator" ]] && sdk="iphonesimulator"

    local cc="$(xcrun -sdk "$sdk" -find clang)"
    local cxx="$(xcrun -sdk "$sdk" -find clang++)"
    local ar="$(xcrun -sdk "$sdk" -find ar)"
    local ranlib="$(xcrun -sdk "$sdk" -find ranlib)"
    local strip="$(xcrun -sdk "$sdk" -find strip)"
    local sdk_path
    sdk_path="$(xcrun --sdk "$sdk" --show-sdk-path)"

    export CC="$cc"
    export CXX="$cxx"
    export AR="$ar"
    export RANLIB="$ranlib"
    export STRIP="$strip"
    export SDKROOT="$sdk_path"

    local ios_cmake_args=("${cmake_args[@]}")
    ios_cmake_args+=(
      -DCMAKE_SYSTEM_NAME=iOS
      -DCMAKE_OSX_ARCHITECTURES="$target"
      -DCMAKE_OSX_SYSROOT="$sdk_path"
    )

    _cmake_configure . build "${ios_cmake_args[@]}"
    die $? "not ok - zlib configure ($platform)"

    quiet cmake --build build --config Release -j"$CPU_CORES"
    die $? "not ok - zlib build ($platform)"

    quiet cmake --install build --config Release
    die $? "not ok - zlib install ($platform)"
  elif [[ "$platform" == "android" ]]; then
    if [ -z "$ANDROID_HOME" ]; then
      echo "warn - ANDROID_HOME not set, skipping zlib for $target"
      cd "$BUILD_DIR" || exit 1
      return
    fi

    export ANDROID_NDK="$ANDROID_HOME/ndk/$NDK_VERSION"

    local android_cmake_args=(
      -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake"
      -DCMAKE_SYSTEM_NAME=Android
      -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform"
      -DANDROID_ABI="$target"
      -DANDROID_PLATFORM="android-$ANDROID_PLATFORM"
      -DBUILD_SHARED_LIBS=OFF
      -DZLIB_BUILD_EXAMPLES=OFF
      -DSKIP_INSTALL_LIBRARIES=OFF
      -DSKIP_INSTALL_HEADERS=OFF
    )

    _cmake_configure . build "${android_cmake_args[@]}"
    die $? "not ok - zlib cmake configure (android $target)"

    quiet cmake --build build --config Release -j"$CPU_CORES"
    die $? "not ok - zlib cmake build (android $target)"

    quiet cmake --install build --config Release
    die $? "not ok - zlib cmake install (android $target)"
  else
    echo "warn - zlib build for $platform not implemented; skipping"
    cd "$BUILD_DIR" || exit 1
    return
  fi

  # Strip any shared libraries if they were installed despite BUILD_SHARED_LIBS=OFF
  rm -f "$BUILD_DIR/$target-$platform/lib"/libz.so* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/libz.dylib* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/z.dll* 2>/dev/null || true

  # Promote headers into the shared build/include prefix for convenience
  if [[ -d "$BUILD_DIR/$target-$platform/include" ]]; then
    mkdir -p "$BUILD_DIR/include"
    cp -pf "$BUILD_DIR/$target-$platform/include/zlib.h" "$BUILD_DIR/include/zlib.h" 2>/dev/null || true
    cp -pf "$BUILD_DIR/$target-$platform/include/zconf.h" "$BUILD_DIR/include/zconf.h" 2>/dev/null || true
  fi

  cd "$BUILD_DIR" || exit 1
}

function _compile_mbedtls {
  local target=$1
  local platform=$2

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  if [[ "$host" != "Linux" ]]; then
    echo "warn - skipping mbedtls build for $platform on $host"
    return
  fi

  if [[ "$platform" != "desktop" ]]; then
    echo "warn - skipping mbedtls build for platform $platform"
    return
  fi

  local source_root="$BUILD_DIR/mbedtls"
  if [[ ! -d "$source_root" ]]; then
    die 1 "not ok - mbedtls sources not staged (expected in $source_root)"
  fi

  echo "# building mbedtls for $platform ($target) on $host..."

  local STAGING_DIR="$BUILD_DIR/$target-$platform/mbedtls"

  if [[ ! -d "$STAGING_DIR" ]] || (( force )); then
    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR"
    cp -r "$source_root"/* "$STAGING_DIR"
  fi

  cd "$STAGING_DIR" || exit 1

  local cmake_args=(
    -DENABLE_PROGRAMS=OFF
    -DENABLE_TESTING=OFF
    -DMBEDTLS_FATAL_WARNINGS=OFF
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
    -DUSE_STATIC_MBEDTLS_LIBRARY=ON
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/$target-$platform"
    -DCMAKE_INSTALL_LIBDIR="lib"
    -DCMAKE_INSTALL_INCLUDEDIR="include"
  )

  quiet command -v cmake
  die $? "not ok - missing cmake, \"$(advice 'cmake')\""

  quiet cmake -E remove_directory "$STAGING_DIR/build"

  local pic_flags="-fPIC"
  CFLAGS="$pic_flags" CXXFLAGS="$pic_flags" \
    _cmake_configure . build -DCMAKE_BUILD_TYPE=Release "${cmake_args[@]}"
  die $? "not ok - mbedtls configure (desktop)"

  quiet cmake --build build --config Release -- -j"$CPU_CORES"
  die $? "not ok - mbedtls build (desktop)"

  quiet cmake --install build --config Release
  die $? "not ok - mbedtls install (desktop)"

  rm -f "$BUILD_DIR/$target-$platform/lib"/libmbedtls*.so* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/libmbedx509*.so* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/libmbedcrypto*.so* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/libmbedtls*.dylib* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/libmbedx509*.dylib* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/libmbedcrypto*.dylib* 2>/dev/null || true
  rm -f "$BUILD_DIR/$target-$platform/lib"/mbedtls*.dll* 2>/dev/null || true

  if [[ -d "$BUILD_DIR/$target-$platform/include/mbedtls" ]]; then
    mkdir -p "$BUILD_DIR/include"
    cp -rf "$BUILD_DIR/$target-$platform/include/mbedtls" "$BUILD_DIR/include/" 2>/dev/null || true
  fi
  if [[ -d "$BUILD_DIR/$target-$platform/include/psa" ]]; then
    mkdir -p "$BUILD_DIR/include"
    cp -rf "$BUILD_DIR/$target-$platform/include/psa" "$BUILD_DIR/include/" 2>/dev/null || true
  fi

  unset CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS

  local libdir="$BUILD_DIR/$target-$platform/lib"
  for lib in libmbedtls.a libmbedx509.a libmbedcrypto.a; do
    if [[ ! -f "$libdir/$lib" ]]; then
      die 1 "not ok - missing $lib in $libdir (mbedtls build failed)"
    fi
  done

  cd "$BUILD_DIR" || exit 1
}

function _compile_libusb_android {
  local target=$1

  if [ -z "$target" ]; then
    echo "warn - missing android target for libusb"
    return
  fi

  local platform="android"
  local STAGING_DIR="$BUILD_DIR/$target-$platform/libusb"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/libusb/* "$STAGING_DIR"
  fi

  cd "$STAGING_DIR" || exit 1

  if [ -z "$ANDROID_HOME" ]; then
    echo "warn - ANDROID_HOME not set, skipping libusb for $target"
    return
  fi

  # libusb doesn't ship a top-level CMakeLists.txt, so we rely on autotools.
  local bootstrap_script=""
  if [ -x "$STAGING_DIR/bootstrap.sh" ]; then
    bootstrap_script="$STAGING_DIR/bootstrap.sh"
  elif [ -x "$STAGING_DIR/autogen.sh" ]; then
    bootstrap_script="$STAGING_DIR/autogen.sh"
  fi

  if [[ ! -d "$STAGING_DIR/tests" ]]; then
    mkdir -p "$STAGING_DIR/tests"
    printf 'EXTRA_DIST =\n' > "$STAGING_DIR/tests/Makefile.am"
  elif [[ ! -f "$STAGING_DIR/tests/Makefile.am" ]]; then
    printf 'EXTRA_DIST =\n' > "$STAGING_DIR/tests/Makefile.am"
  fi

  if [[ -n "$bootstrap_script" ]] && ([[ ! -f "$STAGING_DIR/configure" ]] || [[ ! -f "$STAGING_DIR/Makefile.in" ]]); then
    quiet sh "$bootstrap_script"
    die $? "not ok - libusb bootstrap (android $target)"
  fi

  if [[ ! -f "$STAGING_DIR/configure" ]]; then
    die 1 "not ok - libusb configure script missing (android $target)"
  fi

  if [[ ! -f "$STAGING_DIR/Makefile.in" ]]; then
    die 1 "not ok - libusb Makefile.in missing after bootstrap (android $target)"
  fi

  local host_arch="$(host_arch)"
  local prebuilt="$(android_prebuilt_toolchain_dir "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"
  local toolchain_bin="$prebuilt/bin"
  local host_compiler="$(android_arch "$target")-linux-android$(android_eabi "$target")"
  local api="$ANDROID_PLATFORM"
  local cc="$toolchain_bin/${host_compiler}${api}-clang"
  local ar="$toolchain_bin/llvm-ar"
  local ranlib="$toolchain_bin/llvm-ranlib"

  if [[ ! -x "$cc" ]]; then
    die 1 "not ok - Android clang not found at $cc (check ANDROID_HOME/NDK_VERSION)"
  fi

  local cflags="-fPIC"

  local libusb_archive="$BUILD_DIR/$target-$platform/lib/libusb-1.0.a"
  if _autotools_configure_needed "$STAGING_DIR" "$BUILD_DIR/$target-$platform" || ! test -f Makefile || ! test -f "$libusb_archive"; then
    env \
      CC="$cc" \
      AR="$ar" \
      RANLIB="$ranlib" \
      CFLAGS="$cflags" \
      ./configure \
        --host="$host_compiler" \
        --disable-shared \
        --enable-static \
        --disable-udev \
        --prefix="$BUILD_DIR/$target-$platform"
    die $? "not ok - libusb configure (android $target)"
  fi

  quiet make "-j$CPU_CORES"
  die $? "not ok - libusb make (android $target)"

  quiet make install
  die $? "not ok - libusb install (android $target)"

  local libdir="$BUILD_DIR/$target-$platform/lib"
  mkdir -p "$libdir"
  if [ ! -f "$libdir/libusb-1.0.a" ] && [ -f "$STAGING_DIR/libusb/.libs/libusb-1.0.a" ]; then
    copy_if_newer "$STAGING_DIR/libusb/.libs/libusb-1.0.a" "$libdir/libusb-1.0.a"
  fi

  rm -f "$root/build/$target-$platform/lib"/*.{so,la,dylib}* 2>/dev/null || true
  return
}

function _compile_libsodium {
  local target=$1
  local platform=$2

  if [ -z "$target" ]; then
    target="$(host_arch)"
    platform="desktop"
  fi

  if [[ "$platform" == "desktop" && "$host" == "Win32" ]]; then
    echo "warn - libsodium build for Win32 not implemented; skipping"
    return
  fi

  echo "# building libsodium for $platform ($target) on $host..."

  local STAGING_DIR="$BUILD_DIR/$target-$platform/libsodium"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/libsodium/* "$STAGING_DIR"
  fi

  cd "$STAGING_DIR" || exit 1

  if [ ! -f "$STAGING_DIR/configure" ]; then
    quiet sh ./autogen.sh -s
    die $? "not ok - libsodium autogen ($platform)"
  fi

  if [ "$platform" == "desktop" ]; then
    export CFLAGS="-O2 -fPIC"
    export CXXFLAGS="$CFLAGS"

    local libsodium_archive="$BUILD_DIR/$target-$platform/lib/libsodium.a"
    if _autotools_configure_needed "$STAGING_DIR" "$BUILD_DIR/$target-$platform" || ! [ -f "$libsodium_archive" ]; then
      quiet ./configure --enable-static --disable-shared --prefix="$BUILD_DIR/$target-$platform"
      die $? "not ok - libsodium configure ($platform)"

      quiet make "-j$CPU_CORES"
      die $? "not ok - libsodium make ($platform)"

      quiet make install
      die $? "not ok - libsodium install ($platform)"
    fi

    rm -f "$BUILD_DIR/$target-$platform/lib"/*.{so,dylib,la}* 2>/dev/null || true

    return
  fi

  if [ "$platform" == "iPhoneOS" ] || [ "$platform" == "iPhoneSimulator" ]; then
    if [[ "$host" != "Darwin" ]]; then
      echo "warn - libsodium $platform build requires macOS; skipping"
      return
    fi

    quiet command -v xcrun
    die $? "not ok - missing xcrun for libsodium ($platform)"

    local sdk="iphoneos"
    if [ "$platform" == "iPhoneSimulator" ]; then
      sdk="iphonesimulator"
    fi

    local sdk_path="$(xcrun --sdk "$sdk" --show-sdk-path)"
    local cc="$(xcrun -sdk "$sdk" -find clang)"
    local cxx="$(xcrun -sdk "$sdk" -find clang++)"
    local ar="$(xcrun -sdk "$sdk" -find ar)"
    local ranlib="$(xcrun -sdk "$sdk" -find ranlib)"

    export CC="$cc"
    export CXX="$cxx"
    export AR="$ar"
    export RANLIB="$ranlib"

    local min_flag="-miphoneos-version-min=$SDKMINVERSION"
    if [ "$platform" == "iPhoneSimulator" ]; then
      min_flag="-mios-simulator-version-min=$SDKMINVERSION"
    fi

    export CFLAGS="-O2 -fembed-bitcode -arch $target -isysroot $sdk_path $min_flag"
    export CXXFLAGS="$CFLAGS"
    export LDFLAGS="-arch $target -isysroot $sdk_path $min_flag"
    export CPPFLAGS="$CFLAGS"

    if ! [ -f Makefile ]; then
      local host_triple="aarch64-apple-darwin"
      if [ "$target" == "x86_64" ]; then
        host_triple="x86_64-apple-darwin"
      fi

      quiet ./configure --enable-static --disable-shared --host="$host_triple" --prefix="$BUILD_DIR/$target-$platform"
      die $? "not ok - libsodium configure ($platform $target)"
    fi

    quiet make "-j$CPU_CORES"
    die $? "not ok - libsodium make ($platform $target)"

    quiet make install
    die $? "not ok - libsodium install ($platform $target)"

    rm -f "$BUILD_DIR/$target-$platform/lib"/*.{so,dylib,la}* 2>/dev/null || true
    return
  fi

  echo "warn - libsodium build for $platform not implemented; skipping"
}

function _compile_libsodium_android {
  local target=$1

  if [ -z "$target" ]; then
    echo "warn - missing android target for libsodium"
    return
  fi

  if [ -z "$ANDROID_HOME" ]; then
    echo "warn - ANDROID_HOME not set, skipping libsodium for $target"
    return
  fi

  local platform="android"
  local STAGING_DIR="$BUILD_DIR/$target-$platform/libsodium"

  if [ ! -d "$STAGING_DIR" ]; then
    mkdir -p "$STAGING_DIR"
    cp -r "$BUILD_DIR"/libsodium/* "$STAGING_DIR"
  fi

  cd "$STAGING_DIR" || exit 1

  if [ ! -f "$STAGING_DIR/configure" ]; then
    quiet sh ./autogen.sh -s
    die $? "not ok - libsodium autogen (android $target)"
  fi

  export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/$NDK_VERSION"
  if [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "warn - ANDROID_NDK_HOME not found at $ANDROID_NDK_HOME, skipping libsodium for $target"
    return
  fi

  export NDK_PLATFORM="android-$ANDROID_PLATFORM"
  export NDK_PLATFORM_COMPAT="$NDK_PLATFORM"

  local script=""
  local suffix=""
  case "$target" in
    arm64-v8a)
      script="android-armv8-a.sh"
      suffix="armv8-a+crypto"
      ;;
    x86_64)
      script="android-x86_64.sh"
      suffix="x86_64"
      ;;
    *)
      echo "warn - unsupported android target for libsodium: $target"
      return
      ;;
  esac

  rm -rf "$STAGING_DIR"/libsodium-android-*

  local rc=0
  (
    # libsodium dist-build scripts only set CC if it is unset; avoid
    # leaking the host/toolchain compiler into Android builds.
    unset CC CXX AR RANLIB
    quiet sh "dist-build/$script"
  ) || rc=$?
  die $rc "not ok - libsodium android build ($target)"

  local prefix_dir=""
  for candidate in "$STAGING_DIR"/libsodium-android-*; do
    if [[ -f "$candidate/lib/libsodium.a" ]]; then
      prefix_dir="$candidate"
      break
    fi
  done

  if [[ -z "$prefix_dir" ]]; then
    echo "warn - libsodium output not found after build in: $STAGING_DIR"
    return
  fi

  local output_lib="$prefix_dir/lib/libsodium.a"
  if [ ! -f "$output_lib" ]; then
    echo "warn - libsodium static archive missing for $target"
    return
  fi

  local dest_lib_dir="$BUILD_DIR/$target-$platform/lib"
  local dest_include_dir="$BUILD_DIR/$target-$platform/include"

  mkdir -p "$dest_lib_dir"
  rm -f "$dest_lib_dir/libsodium.a"
  rm -rf "$dest_include_dir"
  mkdir -p "$dest_include_dir"

  copy_if_newer "$output_lib" "$dest_lib_dir/$(basename "$output_lib")"
  die $? "not ok - libsodium android archive copy ($target)"

  cp -rfp "$prefix_dir/include/"* "$dest_include_dir/"
  die $? "not ok - libsodium android headers copy ($target)"
}

function _check_compiler_features {
  if [[ -n "$DEBUG" ]]; then
    return
  fi

  if [[ "$host" == "Win32" ]]; then
    # Compiler test not working on windows, 9 unresolved externals
    return;
  fi

  echo "# checking compiler features"
  local cflags=($("$root/bin/cflags.sh"))
  local ldflags=($("$root/bin/ldflags.sh"))

  if [[ "$host" == "Darwin" ]]; then
    cflags+=(-x objective-c++)
  else
    cflags+=(-x c++)
  fi

  cflags+=("-I$root")

  $CXX "${cflags[@]}" "${ldflags[@]}" - -o /dev/null >/dev/null << EOF_CC
    #include "src/runtime.hh"
    int main () { return 0; }
EOF_CC

  die $? "not ok - $CXX ($("$CXX" -dumpversion)) failed in feature check required for building Oro Runtime"
}

function onsignal () {
  local status=${1:-$?}
  for pid in "${pids[@]}"; do
    kill TERM $pid >/dev/null 2>&1
    kill -9 "$pid" >/dev/null 2>&1
    wait "$pid" 2>/dev/null
  done
  exit "$status"
}

_prepare
cd "$BUILD_DIR" || exit 1

trap onsignal INT TERM

if [[ "$(uname -s)" == "Darwin" ]] && [[ -z "$NO_IOS" ]]; then
  quiet xcode-select -p
  die $? "not ok - xcode needs to be installed from the mac app store: https://apps.apple.com/us/app/xcode/id497799835"

  for apple_target in "${apple_mobile_targets[@]}"; do
    _compile_llama_metal "${apple_target%%-*}" "${apple_target#*-}"
  done
fi

_compile_llama
echo "ok - built libllama for desktop ($(host_arch))"

if [[ "${ORO_SKIP_IROH:-0}" != "1" ]]; then
  if _compile_iroh_ffi; then
    echo "ok - built oro-iroh for desktop ($(host_arch))"
  else
    echo "warn - oro-iroh build skipped or incomplete for desktop ($(host_arch))"
  fi
else
  echo "warn - skipping oro-iroh build (ORO_SKIP_IROH=1)"
fi

_compile_crsqlite_loadable

_queue_target_dependency "libwhisper desktop ($(host_arch))" _compile_whisper
_queue_target_dependency "libuv desktop ($(host_arch))" _compile_libuv
_queue_target_dependency "libusb desktop ($(host_arch))" _compile_libusb

if [[ "${ORO_SKIP_LIBIPFS:-0}" != "1" ]]; then
  _queue_target_dependency "libipfs desktop ($(host_arch))" _compile_libipfs
else
  echo "warn - skipping libipfs build (ORO_SKIP_LIBIPFS=1)"
fi

_queue_target_dependency "libsodium desktop ($(host_arch))" _compile_libsodium

if [[ "$host" = "Linux" ]]; then
  _queue_target_dependency "mbedtls desktop ($(host_arch))" _compile_mbedtls
fi

# Build vendored zlib for desktop; mobile platforms are wired up below.
_queue_target_dependency "zlib desktop ($(host_arch))" _compile_zlib

if [[ "$(uname -s)" == "Darwin" ]] && [[ -z "$NO_IOS" ]]; then
  quiet xcode-select -p
  die $? "not ok - xcode needs to be installed from the mac app store: https://apps.apple.com/us/app/xcode/id497799835"

  SDKMINVERSION="13.0"
  export IPHONEOS_DEPLOYMENT_TARGET="13.0"

  LIPO=$(xcrun -sdk iphoneos -find lipo)
  PLATFORMPATH="/Applications/Xcode.app/Contents/Developer/Platforms"

  _setSDKVersion iPhoneOS

  _queue_target_dependency "cr-sqlite (iOS)" _compile_crsqlite_loadable ios

  for apple_target in "${apple_mobile_targets[@]}"; do
    target_arch="${apple_target%%-*}"
    target_platform="${apple_target#*-}"
    _queue_target_dependency "libuv ($apple_target)" _compile_libuv "$target_arch" "$target_platform"
    _queue_target_dependency "libusb ($apple_target)" _compile_libusb "$target_arch" "$target_platform"
    _queue_target_dependency "libsodium ($apple_target)" _compile_libsodium "$target_arch" "$target_platform"
    _queue_target_dependency "zlib ($apple_target)" _compile_zlib "$target_arch" "$target_platform"
    _queue_target_dependency "llama ($apple_target)" _compile_llama "$target_arch" "$target_platform"
    _queue_target_dependency "whisper ($apple_target)" _compile_whisper "$target_arch" "$target_platform"
  done

  _wait_for_target_dependencies "not ok - desktop or iOS dependency build failed"
  echo "ok - built iOS dependency libraries"

  unset PLATFORM CC CXX STRIP LD CPP CFLAGS CXXFLAGS AR RANLIB \
    CPPFLAGS LDFLAGS SDKROOT IPHONEOS_DEPLOYMENT_TARGET

  # iOS dependency helpers select SDK-specific compilers. Restore the host
  # compiler before the desktop CLI and main program are built.
  determine_cxx || exit $?
fi

# Apple builds drain desktop and mobile dependencies together above. Other
# hosts finish the bounded desktop dependency pool before mobile targets.
if (( ${#pids[@]} > 0 )); then
  _wait_for_target_dependencies "not ok - desktop dependency build failed"
fi

if [[ -n "$BUILD_ANDROID" ]]; then
  for abi in $(android_supported_abis); do
    _queue_target_dependency "libuv android ($abi)" _compile_libuv_android "$abi"
    _queue_target_dependency "libusb android ($abi)" _compile_libusb_android "$abi"
    _queue_target_dependency "libsodium android ($abi)" _compile_libsodium_android "$abi"
    _queue_target_dependency "zlib android ($abi)" _compile_zlib "$abi" android
    _queue_target_dependency "llama android ($abi)" _compile_llama "$abi" android
    _queue_target_dependency "whisper android ($abi)" _compile_whisper "$abi" android
    _queue_target_dependency "cr-sqlite android ($abi)" _compile_crsqlite_loadable android "$abi"
    _wait_for_target_dependencies "not ok - Android dependency build failed ($abi)"
  done

  if [[ "${ORO_PRUNE_TRANSIENT_BUILD_OUTPUTS:-false}" == "true" ]]; then
    echo "# pruning transient Android Rust build outputs..."
    rm -rf "$BUILD_DIR/cr-sqlite/core/rs/bundle_static/target"

    declare crsqlite_rust_toolchain=""
    crsqlite_rust_toolchain="$(_crsqlite_rust_toolchain)"
    if command -v rustup >/dev/null 2>&1 && \
       rustup toolchain list | awk '{ print $1 }' | grep -q "^${crsqlite_rust_toolchain}"; then
      rustup toolchain uninstall "$crsqlite_rust_toolchain"
      die $? "not ok - unable to prune cr-sqlite Rust toolchain $crsqlite_rust_toolchain"
    fi

    if [[ "${RUSTUP_HOME:-}" == "$BUILD_DIR/.rustup" ]]; then
      unset RUSTUP_HOME
    fi
  fi
fi

if [[ "${ORO_PRUNE_TRANSIENT_BUILD_OUTPUTS:-false}" == "true" ]]; then
  echo "# available disk after Android dependency cleanup"
  df -h "$BUILD_DIR" || true
fi

mkdir -p  "$ORO_HOME"/uv/{src/unix,include}
cp -fr "$BUILD_DIR"/uv/LICENSE "$ORO_HOME"/uv/LICENSE
cp -fr "$BUILD_DIR"/uv/src/*.{c,h} "$ORO_HOME"/uv/src
cp -fr "$BUILD_DIR"/uv/src/unix/*.{c,h} "$ORO_HOME"/uv/src/unix
die $? "not ok - could not copy headers"
echo "ok - copied headers"
cd "$CWD" || exit 1

cd "$BUILD_DIR" || exit 1

_get_web_view2

_check_compiler_features
_build_runtime_library
_build_cli & pids+=($!)

_prebuild_desktop_main & pids+=($!)

echo "arch: $arch"

if [[ "$host" = "Darwin" ]] && [[ -z "$NO_IOS" ]]; then
  if test -d "$(xcrun -sdk iphoneos -show-sdk-path 2>/dev/null)"; then
    for apple_target in "${apple_mobile_targets[@]}"; do
      if [[ "${apple_target#*-}" == "iPhoneOS" ]]; then
        _prebuild_ios_main & pids+=($!)
      else
        _prebuild_ios_simulator_main "${apple_target%%-*}" & pids+=($!)
      fi
    done
  fi
fi

for pid in "${pids[@]}"; do
  wait "$pid" 2>/dev/null
  die $? "not ok - unable to build. See trouble shooting guide in the README.md file"
done

_install "$(host_arch)" desktop

if [[ "$host" = "Darwin" ]] && [[ -z "$NO_IOS" ]]; then
  for apple_target in "${apple_mobile_targets[@]}"; do
    _install "${apple_target%%-*}" "${apple_target#*-}"
  done
fi

if [[ -n "$BUILD_ANDROID" ]]; then
  pid_labels=()
  for abi in $(android_supported_abis); do
    _install "$abi" android & pids+=($!) && pid_labels+=("runtime install android ($abi)")
  done
  _wait_for_target_dependencies "not ok - Android runtime install failed"
fi

_install_cli

if [[ "${ORO_PRUNE_BUILD_OUTPUTS_AFTER_INSTALL:-false}" == "true" ]]; then
  cd "$CWD" || exit 1
  if [[ "$BUILD_DIR" != "$CWD/build" ]] || \
     [[ "$ORO_HOME" == "$BUILD_DIR" ]] || \
     [[ "$ORO_HOME" == "$BUILD_DIR/"* ]]; then
    die 1 "not ok - refusing to prune a build directory that contains staged runtime artifacts"
  fi

  echo "# pruning transient build outputs after staged artifacts were installed..."
  rm -rf -- "$BUILD_DIR"
  die $? "not ok - unable to prune transient build outputs"
  echo "# available disk after staged build cleanup"
  df -h "$CWD" || true
fi

exit $?
