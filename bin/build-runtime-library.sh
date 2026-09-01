#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
declare clang="${CXX:-"${CLANG:-"$(which clang++)"}"}"
declare clang_c=""
declare cache_path="$root/build/cache"

source "$root/bin/functions.sh"
source "$root/bin/runtime-artifacts.sh"
export CPU_CORES=$(set_cpu_cores)

declare runtime_build_jobs="${ORO_RUNTIME_BUILD_JOBS:-$CPU_CORES}"
if ! [[ "$runtime_build_jobs" =~ ^[0-9]+$ ]] || (( runtime_build_jobs < 1 )); then
  runtime_build_jobs=1
fi

declare runtime_artifact_name="$ORO_RUNTIME_ARTIFACT_NAME"
declare -a runtime_artifact_aliases=()
if [[ -n "${ORO_RUNTIME_ARTIFACT_ALIASES+x}" ]]; then
  runtime_artifact_aliases=("${ORO_RUNTIME_ARTIFACT_ALIASES[@]}")
fi
declare canonical_runtime_lib_prefix
canonical_runtime_lib_prefix="$(oro_runtime_canonical_lib_prefix)"

function stage_runtime_compat_archives() {
  local canonical="$1"
  if [[ ! -f "$canonical" ]] || (( ${#runtime_artifact_aliases[@]} == 0 )); then
    return
  fi

  for alias in "${runtime_artifact_aliases[@]}"; do
    if [[ "$alias" == "$runtime_artifact_name" ]]; then
      continue
    fi

    local alias_prefix="lib$alias"
    local alias_path="${canonical/$canonical_runtime_lib_prefix/$alias_prefix}"
    if [[ "$alias_path" == "$canonical" ]]; then
      continue
    fi

    mkdir -p "$(dirname "$alias_path")"
    cp -f "$canonical" "$alias_path"
  done
}

declare args=()
declare pids=()
declare force=0
declare ignore_header_mtimes=0
declare syntax_only=0
declare d=""

declare arch="$(host_arch)"
declare host_arch=$arch
declare host=$(host_os)
declare platform="desktop"

if [[ "$host" == "Win32" ]]; then
  # We have to differentiate release and debug for Win32
  if [[ -n "$DEBUG" ]]; then
    d="d"
  fi

  if command -v "$clang" >/dev/null 2>&1; then
    write_log "d" "Found clang++ on path"
  elif [ -f "$clang" ]; then
    write_log "d" "Full path clang++ exists: $clang"
  elif [ -n "$CXX" ]; then
    write_log "v" "no clang on path, making tmp link."
    # POSIX doesn't handle quoted commands
    # Quotes inside variables don't escape spaces, quotes only help on the line we are executing
    # Make a temp link
    clang_tmp=$(mktemp)
    rm $clang_tmp
    ln -s "$clang" $clang_tmp
    clang=$clang_tmp
    # Make tmp.etc look like clang++.etc, makes clang output look correct
    clang=$(echo $CXX|sed 's/tmp\./clang++\./')
    mv $clang_tmp $clang
  fi

  declare find_test="$(sh -c 'find --version')"
  if [[ $find_test != *"GNU findutils"* ]]; then
    echo "GNU find not detected. Consider adding %ProgramFiles%\Git\bin\ to PATH."
    echo "NOTE: %ProgramFiles%\Git\usr\bin\ WILL NOT work."
    echo "uname -m: $(uname -s)"
    exit 1
  fi
fi

if (( TARGET_OS_IPHONE )); then
  arch="arm64"
  platform="iPhoneOS"
elif (( TARGET_IPHONE_SIMULATOR )); then
  arch="x86_64"
  platform="iPhoneSimulator"
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

  if [[ "$arg" = "--ignore-header-mtimes" ]]; then
    ignore_header_mtimes=1; continue
  fi

  if [[ "$arg" = "--syntax-only" ]]; then
    syntax_only=1; continue
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
    elif [[ "$1" = "android" ]]; then
      platform="$1";
      d=""
      export TARGET_OS_ANDROID=1
    else
      platform="$1";
    fi
    shift
    continue
  fi

  args+=("$arg")
done

declare objects=()

declare sqlite_amalgamation="$root/build/sqlite/sqlite3.c"
if (( ! syntax_only )) && [[ ! -f "$sqlite_amalgamation" ]]; then
  echo >&2 "not ok - missing sqlite amalgamation. run bin/fetch-sqlite.sh first."
  exit 1
fi

declare sources=(
  $(find "$root"/src/app/*.cc)
  ## extension API
  $(find "$root"/src/extension/*.cc)
  ## runtime
  $(find "$root"/src/runtime/*.cc)
  $(find "$root/src/runtime/ai" -name '*.cc')
  $(find "$root"/src/runtime/app/app.cc)
  $(find "$root"/src/runtime/bridge/*.cc)
  $(find "$root"/src/runtime/bytes/*.cc)
  $(find "$root"/src/runtime/background -name '*.cc' 2>/dev/null)
  $(find "$root"/src/runtime/color/*.cc)
  $(find "$root"/src/runtime/config/*.cc)
  $(find "$root"/src/runtime/context/*.cc)
  $(find "$root"/src/runtime/core/*.cc)
  $(find "$root"/src/runtime/core/*/*.cc)
  $(find "$root"/src/runtime/core/*/*/*.cc 2>/dev/null)
  $(find "$root"/src/runtime/core/*/*.mm 2>/dev/null)
  $(find "$root"/src/runtime/core/*/*/*.mm 2>/dev/null)
  $(find "$root"/src/runtime/otp/*.mm 2>/dev/null)
  $(find "$root"/src/runtime/concurrent/*.cc)
  $(find "$root"/src/runtime/crypto/*.cc)
  $(find "$root"/src/runtime/cwd/*.cc)
  $(find "$root"/src/runtime/debug/*.cc)
  $(find "$root"/src/runtime/env/*.cc)
  $(find "$root"/src/runtime/filesystem/*.cc)
  $(find "$root"/src/runtime/http/*.cc)
  $(find "$root"/src/runtime/io/*.cc)
  $(find "$root"/src/runtime/iroh/*.cc)
  $(find "$root"/src/runtime/ipc/*.cc)
  $(find "$root"/src/runtime/javascript/*.cc)
  $(find "$root"/src/runtime/json/*.cc)
  $(find "$root/src/runtime/mcp" -name '*.cc')
  $(find "$root"/src/runtime/loop/*.cc)
  $(find "$root"/src/runtime/tcp/*.cc)
  $(find "$root"/src/runtime/tls/*.cc)
  $(find "$root"/src/runtime/os/*.cc)
  $(find "$root"/src/runtime/platform/*.cc)
  $(find "$root"/src/runtime/serviceworker/*.cc)
  $(find "$root"/src/runtime/string/*.cc)
  $(find "$root"/src/runtime/sqlite/*.cc)
  $(find "$root"/src/runtime/udp/*.cc)
  $(find "$root"/src/runtime/uuid/*.cc)
  $(find "$root"/src/runtime/url/*.cc)
  $(find "$root"/src/runtime/webview/*.cc)
  $(find "$root"/src/runtime/window/dialog.cc)
  $(find "$root"/src/runtime/window/hotkey.cc)
  $(find "$root"/src/runtime/window/manager.cc)
)

if (( ! syntax_only )); then
  sources+=(
    ## deps
    "$root/build/sqlite/sqlite3.c"
    "$root/build/llama/common/log.cpp"
    "$root/build/llama/common/common.cpp"
    "$root/build/llama/common/sampling.cpp"
    "$root/build/llama/common/json-schema-to-grammar.cpp"
    "$root/build/llama/src/llama.cpp"
  )
fi

declare has_asn1c_sources=0
declare asn1_source_dirs=(
  "$root/build/asn1c/libasn1parser"
)

if [[ -d "$root/build/asn1c" ]]; then
  has_asn1c_sources=1
  if (( ! syntax_only )); then
    for dir in "${asn1_source_dirs[@]}"; do
      if [[ -d "$dir" ]]; then
        while IFS= read -r file; do
          sources+=("$file")
        done < <(find "$dir" -maxdepth 1 -type f -name '*.c' 2>/dev/null)
      fi
    done
  fi
else
  echo "# warn - build/asn1c not found; skipping ASN.1 integration" >&2
fi

declare cflags

if [[ "$platform" = "android" ]]; then
  source "$root/bin/android-functions.sh"
  android_fte

  if [[ -n $ANDROID_DEPS_ERROR ]]; then
    echo >&2 "not ok - Android dependencies not satisfied."
    exit 1
  fi

  clang="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch" "++")"
  clang_target="$(android_clang_target "$arch")"
  android_includes=($(android_arch_includes "$arch"))
  sources+=("$root/src/runtime/app/android.cc")
  sources+=("$root/src/runtime/process/unix.cc")
  sources+=("$root/src/runtime/window/android.cc")
  sources+=($(find "$root/src/runtime/platform/android"/*.cc))
elif [[ "$host" = "Darwin" ]]; then
  sources+=("$root/src/runtime/app/apple.mm")
  sources+=("$root/src/runtime/window/apple.mm")

  if (( TARGET_OS_IPHONE)); then
    clang="xcrun -sdk iphoneos "$clang""
  elif (( TARGET_IPHONE_SIMULATOR )); then
    clang="xcrun -sdk iphonesimulator "$clang""
  else
    sources+=("$root/src/runtime/process/unix.cc")
  fi
elif [[ "$host" = "Linux" ]]; then
  sources+=("$root/src/runtime/window/linux.cc")
  sources+=("$root/src/runtime/process/unix.cc")
elif [[ "$host" = "Win32" ]]; then
  sources+=("$root/src/runtime/app/win.cc")
  sources+=("$root/src/runtime/window/win.cc")
  sources+=("$root/src/runtime/process/win.cc")
fi

if [[ -n "${CLANG_C:-}" ]]; then
  clang_c="$CLANG_C"
elif [[ "$platform" = "android" ]]; then
  clang_c="$(android_clang "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch" "")"
elif [[ "$clang" == *"clang++"* ]]; then
  clang_c="${clang/clang++/clang}"
else
  clang_c="$(which clang 2>/dev/null)"
fi

if [[ -z "$clang_c" ]]; then
  echo >&2 "not ok - C compiler 'clang' not found (set CLANG_C)."
  exit 1
fi

cflags+=($(ORO_EXCLUDE_BUILD_METADATA=1 ARCH="$arch" "$root/bin/cflags.sh"))
declare -a runtime_metadata_cflags=(
  -DORO_RUNTIME_VERSION_HASH=$(git rev-parse --short=8 HEAD)
  -DORO_RUNTIME_VERSION=$(cat "$root/VERSION.txt")
)

if [[ "$platform" = "android" ]]; then
  cflags+=("$clang_target")
  cflags+=("${android_includes[@]}")
fi

if (( has_asn1c_sources )); then
  cflags+=("-I$root/src/runtime/asn1")
  cflags+=("-I$root/src/runtime/asn1c/include")
  cflags+=("-I$root/build/asn1c")
  cflags+=("-I$root/build/asn1c/libasn1parser")
  cflags+=("-DHAVE_CONFIG_H")
  cflags+=("-DORO_RUNTIME_HAVE_ASN1C=1")
else
  cflags+=("-DORO_RUNTIME_HAVE_ASN1C=0")
fi

declare output_directory="$root/build/$arch-$platform"
if (( ! syntax_only )); then
  mkdir -p "$output_directory"
fi

declare runtime_compiler_launcher=""
if (( ! syntax_only )) && [[ "$host" = "Win32" ]] && command -v sccache >/dev/null 2>&1; then
  # The Windows installer selects clang++ by absolute path, bypassing CMake's
  # compiler launcher. Invoke sccache directly for runtime object compilation.
  runtime_compiler_launcher="sccache"
elif [[ "$platform" = "android" ]] && command -v ccache >/dev/null 2>&1; then
  # Android selects the NDK compiler by absolute path, bypassing the compiler
  # wrapper directories used by hosted CI. Invoke ccache explicitly so the
  # restored native cache also covers the runtime objects for each Android ABI.
  runtime_compiler_launcher="ccache"
fi

function run_runtime_compiler () {
  local compiler="$1"
  local compiler_output=""
  local compiler_rc=0
  shift

  if [[ -n "$runtime_compiler_launcher" ]]; then
    if [[ -n "$VERBOSE" ]]; then
      echo "$runtime_compiler_launcher" "$compiler" "$@"
      "$runtime_compiler_launcher" "$compiler" "$@"
    else
      compiler_output="$(
        "$runtime_compiler_launcher" "$compiler" "$@" 2>&1
      )" || compiler_rc=$?

      if (( compiler_rc != 0 )) && [[ -n "$compiler_output" ]]; then
        printf '%s\n' "$compiler_output" >&2
      fi

      return "$compiler_rc"
    fi
  else
    quiet "$compiler" "$@"
  fi
}

declare newest_header_mtime=0
if (( ! ignore_header_mtimes )); then
  while IFS= read -r header; do
    header_mtime="$(stat_mtime "$header")"
    if (( header_mtime > newest_header_mtime )); then
      newest_header_mtime=$header_mtime
    fi
  done < <(
    find "$root/src" "$root/include" -type f \
      \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.inc' \) \
      2>/dev/null
  )
fi

cd "$(dirname "$output_directory")"

if (( syntax_only )); then
  echo "# checking runtime source syntax ($arch-$platform)"
else
  echo "# building runtime static library ($arch-$platform)"
  for source in "${sources[@]}"; do
    declare src_directory="$root/src"

    declare object="${source/.cc/$d.o}"
    object="${object/.cpp/$d.o}"
    object="${object/.c/$d.o}"

    declare build_dir="$root/build"

    if [[ "$object" =~ ^"$src_directory" ]]; then
      object="${object/$src_directory/$output_directory}"
    else
      object="${object/$build_dir/$output_directory}"
    fi

    objects+=("$object")
  done

  objects+=("$output_directory/llama/build-info.o")
fi

function generate_llama_build_info () {
  build_number="0"
  build_commit="unknown"
  build_compiler="unknown"
  build_target="unknown"

  if out=$(git rev-list --count HEAD); then
    # git is broken on WSL so we need to strip extra newlines
    build_number=$(printf '%s' "$out" | tr -d '\n')
  fi

  if out=$(git rev-parse --short HEAD); then
    build_commit=$(printf '%s' "$out" | tr -d '\n')
  fi

  if [[ "$(host_os)" == "Win32" ]]; then
    if out=$("$clang" --version | head -1); then
      build_compiler="$out"
    fi

    if out=$("$clang" -dumpmachine); then
      build_target="$out"
    fi
  else
    if out=$(eval "$clang" --version | head -1); then
      build_compiler="$out"
    fi

    if out=$(eval "$clang" -dumpmachine); then
      build_target="$out"
    fi
  fi

  echo "# generating llama build info"
  declare source="$output_directory/llama/build-info.cpp"

  cat > $source << LLAMA_BUILD_INFO
    int LLAMA_BUILD_NUMBER = $build_number;
    char const *LLAMA_COMMIT = "$build_commit";
    char const *LLAMA_COMPILER = "$build_compiler";
    char const *LLAMA_BUILD_TARGET = "$build_target";
LLAMA_BUILD_INFO

  run_runtime_compiler "$clang" "${cflags[@]}" -c $source -o ${source/cpp/o} || onsignal
}

function build_linux_desktop_extension_object () {
  declare source="$root/src/desktop/extension/linux.cc"
  declare destination="$root/build/$arch-$platform/objects/extensions/linux.o"

  mkdir -p "$(dirname "$destination")"

  if
    (( force )) ||
    ! test -f "$destination" ||
    (( newest_header_mtime > $(stat_mtime "$destination") )) ||
    (( $(stat_mtime "$source") > $(stat_mtime "$destination") ));
  then
    run_runtime_compiler "$clang" "${cflags[@]}" -DORO_RUNTIME_DESKTOP_EXTENSION=1 -c "$source" -o "$destination" || onsignal
    return $?
  fi

  return 0
}

function main () {
  trap onsignal INT TERM
  local i=0
  local max_concurrency=$runtime_build_jobs
  local compile_status=0
  local compile_rc=0

  if (( ! syntax_only )); then
    mkdir -p "$output_directory/include"
    cp -rf "$root/include"/* "$output_directory/include"
    rm -f "$output_directory/include/oro/_user-config-bytes.hh"

    generate_llama_build_info || return $?

    if [[ "$host" = "Linux" ]] && [[ "$platform" = "desktop" ]]; then
      build_linux_desktop_extension_object || return $?
    fi
  fi

  for source in "${sources[@]}"; do
    if (( ${#pids[@]} >= max_concurrency )); then
      wait "${pids[0]}" 2>/dev/null
      compile_rc=$?
      if (( compile_rc != 0 )); then
        compile_status=1
      fi
      pids=("${pids[@]:1}")
    fi

    {
      declare src_directory="$root/src"
      declare object="${source/.cc/$d.o}"
      declare build_dir="$root/build"

      object="${object/.cpp/$d.o}"
      object="${object/.c/$d.o}"

      declare source_ext="${source##*.}"
      declare compiler="$clang"
      declare -a compile_flags=("${cflags[@]}")

      # Commit metadata is defined in one translation unit so a new revision
      # does not invalidate every otherwise unchanged runtime object in ccache.
      if [[ "$source" == "$root/src/runtime/version.cc" ]]; then
        compile_flags+=("${runtime_metadata_cflags[@]}")
      fi

      if [[ "$source_ext" = "c" ]]; then
        compiler="$clang_c"
        compile_flags=()
        for flag in "${cflags[@]}"; do
          case "$flag" in
            -std=c++*|-stdlib=libstdc++|-ObjC++)
              continue
              ;;
          esac
          compile_flags+=("$flag")
        done
        compile_flags+=("-std=c17")
        if [[ "$source" == "$root/build/asn1c/"* ]]; then
          compile_flags+=("-include" "$root/src/runtime/asn1c/include/compat.h")
          compile_flags+=("-D_GNU_SOURCE")
          if [[ "$host" == "Win32" ]]; then
            compile_flags+=("-DYY_NO_UNISTD_H")
          elif [[ "$host" == "Darwin" ]]; then
            compile_flags+=("-D_DARWIN_C_SOURCE")
          else
            compile_flags+=("-D_POSIX_C_SOURCE=200809L")
          fi
          compile_flags+=("-Dtypeof=__typeof__")
        fi
      fi

      if [[ "$object" =~ ^"$src_directory" ]]; then
        object="${object/$src_directory/$output_directory}"
      else
        object="${object/$build_dir/$output_directory}"
      fi

      if (( syntax_only )); then
        echo "# checking syntax ($arch-$platform) $(basename "$source")"
        run_runtime_compiler "$compiler" "${compile_flags[@]}" -fsyntax-only "$source" || onsignal
        echo "ok - checked ${source/$src_directory\//} ($arch-$platform)"
      elif
        (( force )) ||
        ! test -f "$object" ||
        (( $(stat_mtime "$source") > $(stat_mtime "$object") )) ||
        (( newest_header_mtime > $(stat_mtime "$object") ));
      then
        mkdir -p "$(dirname "$object")"

        echo "# compiling object ($arch-$platform) $(basename "$source")"
        run_runtime_compiler "$compiler" "${compile_flags[@]}" -c "$source" -o "$object" || onsignal
        echo "ok - built ${source/$src_directory\//} -> ${object/$output_directory\//} ($arch-$platform)"
      fi
    } & pids+=($!)
  done

  for pid in "${pids[@]}"; do
    wait "$pid" 2>/dev/null
    compile_rc=$?
    if (( compile_rc != 0 )); then
      compile_status=1
    fi
  done
  pids=()

  if (( compile_status != 0 )); then
    if (( syntax_only )); then
      echo >&2 "not ok - runtime source syntax check failed ($arch-$platform)"
    else
      echo >&2 "not ok - failed to compile runtime objects ($arch-$platform)"
    fi
    return 1
  fi

  if (( syntax_only )); then
    echo "ok - runtime source syntax check passed ($arch-$platform)"
    return 0
  fi

  declare base_lib="$canonical_runtime_lib_prefix"
  declare static_library="$root/build/$arch-$platform/lib$d/$base_lib$d.a"
  mkdir -p "$(dirname "$static_library")"
  declare ar="ar"

  if [[ "$platform" = "android" ]]; then
    ar="$(android_ar "$ANDROID_HOME" "$NDK_VERSION" "$host" "$host_arch")"
  elif [[ "$host" = "Win32" ]]; then
    ar="llvm-ar"
  fi

  #
  # Build the static library. To avoid duplicate member names (e.g. many
  # files named manager.o, server.o, socket.o across subfolders) replacing
  # each other within the archive ("ar r" replaces by basename), we stage
  # uniquely named filesystem entries for each object.
  #
  local build_static=0
  local static_library_mtime=$(stat_mtime "$static_library")

  for source in "${objects[@]}"; do
    if ! test -f "$source"; then
      echo "$source not built.."
      exit 1
    fi
    if (( force )) || ! test -f "$static_library" || (( $(stat_mtime "$source") > "$static_library_mtime" )); then
      build_static=1
      # do not break early; we still want to validate all objects exist
    fi
  done

  if (( build_static )); then
    # Stage unique-named object entries for archiving
    local stage_dir="$output_directory/.archive_objects"
    rm -rf "$stage_dir"
    mkdir -p "$stage_dir"

    # Generate a unique filename for each object. The ordinal guarantees unique
    # archive member names without creating path-length-sensitive flattened names.
    local staged_list=()
    local stage_index=0
    for obj in "${objects[@]}"; do
      local flat=""
      flat="$(printf '%06d_%s' "$stage_index" "$(basename "$obj")")"
      local dst="$stage_dir/$flat"

      # A hard link gives ar a unique member name without duplicating the object
      # bytes. Both paths are under the same target directory, so they normally
      # share a filesystem. Fall back to a copy where hard links are unavailable.
      if ! ln "$obj" "$dst" 2>/dev/null; then
        cp -f "$obj" "$dst" || exit 1
      fi

      staged_list+=("$dst")
      ((stage_index += 1))
    done

    # Create the archive from the staged unique object files
    rm -f "$static_library"
    local archive_rc=0
    $ar crs "$static_library" "${staged_list[@]}" || archive_rc=$?

    # The archive contains the object bytes, so staging is never an incremental
    # build input. Remove it immediately, including after an archive failure.
    rm -rf "$stage_dir"

    if (( archive_rc != 0 )); then
      rm -f "$static_library"
      echo "failed to build $static_library"
      exit "$archive_rc"
    fi

    if [ -f "$static_library" ]; then
      echo "ok - built static library ($arch-$platform): $(basename "$static_library")"
      stage_runtime_compat_archives "$static_library"
    else
      echo "failed to build $static_library"
      exit 1
    fi
  else
    if [ -f "$static_library" ]; then
      echo "ok - using cached static library ($arch-$platform): $(basename "$static_library")"
      stage_runtime_compat_archives "$static_library"
    else
      echo "static library doesn't exist after cache check passed: ($arch-$platform): $(basename "$static_library")"
      exit 1
    fi
  fi

  if [[ "$platform" = "desktop" ]]; then
    if [[ "$host" = "Linux" ]] || [[ "$host" = "Darwin" ]]; then
      "$root/bin/generate-oro-runtime-pkg-config.sh"
    fi
  fi

  if [[ "$platform" == "android" ]]; then
    # This is a sanity check to confirm that the static_library is > 8 bytes
    # If an empty ${objects[@]} is provided to ar, it will still spit out a header without an error code.
    # therefore check the output size
    # This error condition should only occur after a code change
    lib_size=$(stat_size "$static_library")
    if (( lib_size < $(android_min_expected_static_lib_size "$base_lib") )); then
      echo >&2 "not ok -  $static_library size looks wrong: $lib_size, renaming as .bad"
      mv "$static_library" "$static_library.bad"
      exit 1
    fi
  fi
}

main "${args[@]}"
exit $?
