#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
source "$root/bin/runtime-artifacts.sh"

declare canonical_runtime_lib
canonical_runtime_lib="$(oro_runtime_canonical_lib_prefix)"
declare -a runtime_artifact_aliases=()
if [[ -n "${ORO_RUNTIME_ARTIFACT_ALIASES+x}" ]]; then
  runtime_artifact_aliases=("${ORO_RUNTIME_ARTIFACT_ALIASES[@]}")
fi
declare runtime_artifact_name="$ORO_RUNTIME_ARTIFACT_NAME"

declare arch=""
declare platform=""
declare do_full_clean=0
declare do_clean_env_only=0
declare do_clean_llama=0
declare do_clean_staging=0
declare do_clean_cache=0
declare dry_run=0
declare target_filter_requested=0

function usage () {
  echo "usage: ./bin/clean.sh [--dry-run] [--staging] [--cache]"
  echo "       ./bin/clean.sh [--dry-run] --full"
  echo "       ./bin/clean.sh [--dry-run] [--arch <arch>] [--platform <platform>] [--llama]"
  echo "       ./bin/clean.sh [--dry-run] --only-env"
}

if (( TARGET_OS_IPHONE )); then
  arch="arm64"
  platform="iPhoneOS"
elif (( TARGET_IPHONE_SIMULATOR )); then
  arch="x86_64"
  platform="iPhoneSimulator"
fi

if (( $# == 0 )); then
  do_clean_env_only=1
fi

while (( $# > 0 )); do
  declare arg="$1"; shift
  if [[ "$arg" = "--arch" ]]; then
    if (( $# == 0 )); then
      echo "error - '--arch' requires a value" >&2
      exit 2
    fi
    arch="$1"; shift
    target_filter_requested=1
    continue
  fi

  if [[ "$arg" = "--full" ]]; then
    do_full_clean=1
    continue
  elif [[ "$arg" = "--llama" ]]; then
    do_clean_llama=1
    continue
  elif [[ "$arg" = "--staging" ]]; then
    do_clean_staging=1
    continue
  elif [[ "$arg" = "--cache" ]]; then
    do_clean_cache=1
    continue
  elif [[ "$arg" = "--dry-run" ]]; then
    dry_run=1
    continue
  elif [[ "$arg" = "--help" ]] || [[ "$arg" = "-h" ]]; then
    usage
    exit 0
  elif [[ "$arg" = "--platform" ]]; then
    if (( $# == 0 )); then
      echo "error - '--platform' requires a value" >&2
      exit 2
    fi
    target_filter_requested=1
    if [[ "$1" = "ios" ]] || [[ "$1" = "iPhoneOS" ]] || [[ "$1" = "iphoneos" ]]; then
      arch="arm64"
      platform="iPhoneOS";
    elif [[ "$1" = "ios-simulator" ]] || [[ "$1" = "iPhoneSimulator" ]] || [[ "$1" = "iphonesimulator" ]]; then
      arch="x86_64"
      platform="iPhoneSimulator";
    elif [[ "$1" = "android" ]] || [[ "$1" = "Android" ]]; then
      platform="android";
      arch="*"
    else
      platform="$1";
    fi
    shift
    continue
  elif [[ "$arg" = "--only-env" ]]; then
    do_clean_env_only=1
    continue
  fi

  echo "error - unknown option '$arg'" >&2
  usage >&2
  exit 2
done

declare targets=()

if (( do_full_clean && target_filter_requested )); then
  echo "error - cannot mix '--full' and '--arch/--platform'" >&2
  exit 1
fi

if (( do_full_clean )); then
  targets+=(
    "$root/build"
    "$root/rust/oro-iroh/target"
    "$root/.cache/go-build"
    "$root/.cache/go-mod"
    "$root/.oro.env"
  )
elif (( do_clean_env_only )); then
  :
elif [ -n "$arch" ] || [ -n "$platform" ]; then
  if [[ "$platform" = "android" ]]; then
    if [[ "$arch" = "arm64" ]]; then
      arch="arm64-v8a"
    fi
  fi

  arch="${arch:-$(uname -m | sed 's/aarch64/arm64/g')}"
  platform="${platform:-desktop}"

  if (( do_clean_llama )); then
    while IFS= read -r -d '' target; do
      targets+=("$target")
    done < <(find "$root/build" -type d -path "$root/build/$arch-$platform/llama/build" -prune -print0 2>/dev/null)
  fi

  if (( ! do_clean_llama )); then
    targets+=($(find                               \
      "$root/build/"$arch-$platform/app            \
      "$root/build/"$arch-$platform/bin            \
      "$root/build/"$arch-$platform/cli            \
      "$root/build/"$arch-$platform/runtime        \
      "$root/build/"$arch-$platform/ipc            \
      "$root/build/"$arch-$platform/objects        \
      "$root/build/"$arch-$platform/process        \
      "$root/build/"$arch-$platform/window         \
      "$root/build/"$arch-$platform/tests          \
      "$root/build/"$arch-$platform/*.o            \
      "$root/build/"$arch-$platform/**/*.o         \
      "$root/build/npm/$platform"                  \
    2>/dev/null))

    shopt -s nullglob
    for lib in "$root/build/$arch-$platform"/lib/${canonical_runtime_lib}*; do
      targets+=("$lib")
    done
    for alias in "${runtime_artifact_aliases[@]}"; do
      if [[ "$alias" == "$runtime_artifact_name" ]]; then
        continue
      fi
      for lib in "$root/build/$arch-$platform"/lib/lib${alias}*; do
        targets+=("$lib")
      done
    done
    shopt -u nullglob

    targets+=($(find "$root/build/npm/$platform" -name .oro.env 2>/dev/null))
    targets+=($(find "$root/build/$arch-$platform" -name .oro.env 2>/dev/null))
  fi
elif (( ! do_clean_staging && ! do_clean_cache && ! do_clean_llama )); then
  targets+=($(find                 \
    "$root"/build/*/app            \
    "$root"/build/*/bin            \
    "$root"/build/*/cli            \
    "$root"/build/*/runtime        \
    "$root"/build/*/ipc            \
    "$root"/build/*/objects        \
    "$root"/build/*/process        \
    "$root"/build/*/window         \
    "$root"/build/*/tests          \
    "$root"/build/*/*.o            \
    "$root"/build/*/**/*.o         \
    "$root"/build/npm              \
  2>/dev/null))

  shopt -s nullglob
  for lib in "$root"/build/*/lib/${canonical_runtime_lib}*; do
    targets+=("$lib")
  done
  for alias in "${runtime_artifact_aliases[@]}"; do
    if [[ "$alias" == "$runtime_artifact_name" ]]; then
      continue
    fi
    for lib in "$root"/build/*/lib/lib${alias}*; do
      targets+=("$lib")
    done
  done
  shopt -u nullglob

  targets+=($(find "$root/build" -name .oro.env 2>/dev/null))
fi

if (( do_clean_llama )) && [[ -z "$arch" ]] && [[ -z "$platform" ]]; then
  targets+=("$root/build/$(uname -m | sed 's/aarch64/arm64/g')-desktop/llama/build")
fi

if (( do_clean_staging )); then
  while IFS= read -r -d '' target; do
    targets+=("$target")
  done < <(
    find "$root/build" -type d \( \
      -name .archive_objects -o \
      -path '*/llama/build/bin' -o \
      -path '*/llama/build/common' -o \
      -path '*/llama/build/tools' -o \
      -path '*/llama/build/vendor' \
    \) -prune -print0 2>/dev/null
  )

  while IFS= read -r -d '' target; do
    targets+=("$target")
  done < <(find "$root/build" -mindepth 3 -maxdepth 3 -type f -path '*/bin/llama-*' -print0 2>/dev/null)
fi

if (( do_clean_cache && ! do_full_clean )); then
  targets+=(
    "$root/rust/oro-iroh/target"
    "$root/.cache/go-build"
    "$root/.cache/go-mod"
    "$root/build/.cargo"
    "$root/build/.rustup"
    "$root/build/libipfs/.gocache"
    "$root/build/libipfs/.gomodcache"
    "$root/build/cr-sqlite/core/rs/bundle_static/target"
  )
fi

function clean_path () {
  local target="$1"

  if [[ ! -e "$target" ]] && [[ ! -L "$target" ]]; then
    return 0
  fi

  if (( dry_run )); then
    local size=""
    size="$(du -sh "$target" 2>/dev/null | awk '{ print $1 }')"
    echo "would clean ${target/$root\//} (${size:-size unavailable})"
    return 0
  fi

  rm -rf -- "$target"
}

if (( do_clean_env_only )); then
  if [ -n "$arch" ] || [ -n "$platform" ]; then
    targets+=($(find "$root/build/npm/$platform" -name .oro.env 2>/dev/null))
    targets+=($(find "$root/build/$arch-$platform" -name .oro.env 2>/dev/null))
  else
    targets+=($(find "$root/build" -name .oro.env 2>/dev/null))
    targets+=("$root/.oro.env")
  fi

  if (( ${#targets[@]} > 0 )); then
    echo "# cleaning .oro.env"
    for target in "${targets[@]}"; do
      if test "$target"; then
        clean_path "$target" || exit $?
        if [[ -n "$VERBOSE" ]] && (( ! dry_run )); then
          echo "ok - cleaned ${target/$root\//}"
        fi
      fi
    done
    if (( dry_run )); then
      echo "ok - dry run complete"
    else
      echo "ok - cleaned"
    fi
  elif (( dry_run )); then
    echo "ok - nothing to clean"
  fi
elif (( ${#targets[@]} > 0 )); then
  echo "# cleaning targets"
  for target in "${targets[@]}"; do
    if test "$target"; then
      clean_path "$target" || exit $?
      if [[ -n "$VERBOSE" ]] && (( ! dry_run )); then
        echo "ok - cleaned ${target/$root\//}"
      fi
    fi
  done
  if (( dry_run )); then
    echo "ok - dry run complete"
  else
    echo "ok - cleaned"
  fi
elif (( dry_run )); then
  echo "ok - nothing to clean"
fi
