#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

source "$root/bin/android-functions.sh"
source "$root/bin/functions.sh"
source "$root/bin/runtime-artifacts.sh"

declare runtime_artifact_name="$ORO_RUNTIME_ARTIFACT_NAME"
declare -a runtime_artifact_aliases=()
if [[ -n "${ORO_RUNTIME_ARTIFACT_ALIASES+x}" ]]; then
  runtime_artifact_aliases=("${ORO_RUNTIME_ARTIFACT_ALIASES[@]}")
fi

declare archs=($(host_arch))
declare platform="$(uname -s | tr '[[:upper:]]' '[[:lower:]]')"

declare args=()
declare dry_run=0
declare only_platforms=0
declare only_top_level=0
declare no_rebuild=0
declare remove_oro_home=1
declare do_global_link=0

function _stage_platform_oroc_binary() {
  local dest="$1"
  local arch="$2"
  local cli_name="oroc"
  local cli_source=""

  if [[ "$platform" == "Win32" ]]; then
    cli_name="oroc.exe"
    cli_source="$root/build/$arch-desktop/bin/$cli_name"
  else
    cli_source="$root/build/$arch-desktop/bin/$cli_name"
  fi

  if [[ ! -f "$cli_source" ]]; then
    echo >&2 "not ok - missing compiled CLI binary for $platform/$arch: $cli_source"
    exit 1
  fi

  rm -f "$dest/bin/oroc" "$dest/bin/oroc.exe"

  if (( do_global_link )); then
    ln -sf "$cli_source" "$dest/bin/$cli_name"
  else
    cp -f "$cli_source" "$dest/bin/$cli_name"
  fi
}

function _publish () {
  if (( !dry_run && !do_global_link )); then
    npm publish "${args[@]}" || exit $?
  elif (( !do_global_link )); then
    # echo "# npm publish ${args[@]}"
    npm pack "${args[@]}" || exit $?
  fi
}

function resolve_global_prefix() {
  if [[ -n "${PREFIX:-}" ]]; then
    printf '%s' "$PREFIX"
    return 0
  fi

  if command -v npm >/dev/null 2>&1; then
    local detected_prefix
    detected_prefix="$(npm prefix -g 2>/dev/null | tr -d '\r')"
    if [[ -n "$detected_prefix" ]] && [[ "$detected_prefix" != "undefined" ]] && [[ "$detected_prefix" != "null" ]]; then
      printf '%s' "$detected_prefix"
      return 0
    fi
  fi

  printf '%s' "/usr/local"
}

declare -a CLI_PACKAGE_SPECS=(
  "@orocomputer:runtime"
)

function stage_cli_package () {
  local scope="$1"
  local name="$2"
  local package="$scope/$name"
  local source="$root/npm/packages/$package"
  local dest="$ORO_HOME/packages/$package"

  if [[ ! -d "$source" ]]; then
    echo >&2 "not ok - missing package template: $source"
    exit 1
  fi

  rm -rf "$dest"

  if (( do_global_link )); then
    mkdir -p "$dest/bin"
    cp -rf "$root/npm/bin"/* "$dest/bin"

    ln -sf "$source"/* "$dest"
    rm -rf "$dest/src"
    ln -sf "$root/npm/src" "$dest/src"
    ln -sf "$root/LICENSE.txt" "$dest"
    ln -sf "$root/README.md" "$dest/README-RUNTIME.md"
    ln -sf "$root/api"/* "$dest"
  else
    cp -rf "$source" "$dest"
    mkdir -p "$dest/bin"
    cp -rf "$root/npm/bin"/* "$dest/bin"
    cp -rf "$root/npm/src" "$dest/src"
    cp -f "$root/LICENSE.txt" "$dest"
    cp -f "$root/README.md" "$dest/README-RUNTIME.md"
    cp -rf "$root/api"/* "$dest"
  fi

  rm -f "$dest/global.d.ts"
}

function package_platform_variant () {
  local scope="$1"
  local name="$2"
  local arch="$3"
  local normalized_arch="${arch/x86_64/x64}"
  local package="$scope/${name}-$platform-$normalized_arch"
  local source="$root/npm/packages/$package"
  local dest="$ORO_HOME/packages/$package"

  if [[ ! -d "$source" ]]; then
    echo >&2 "not ok - missing package template: $source"
    exit 1
  fi

  rm -rf "$dest"

  if (( do_global_link )); then
    mkdir -p "$dest/assets" "$dest/bin" "$dest/include" "$dest/lib" "$dest/objects" "$dest/src"
    cp -rf "$root/npm/src"/* "$dest/src"
    cp -rf "$root/npm/bin"/* "$dest/bin"

    ln -sf "$source"/* "$dest"
    ln -sf "$root/assets"/* "$dest/assets"
    ln -sf "$root/LICENSE.txt" "$dest"
    ln -sf "$root/README.md" "$dest"

    ln -sf "$ORO_HOME/bin"/* "$dest/bin"
    ln -sf "$ORO_HOME/src"/* "$dest/src"
    ln -sf "$ORO_HOME/uv" "$dest/uv"
    ln -sf "$ORO_HOME/include"/* "$dest/include"

    if test -d "$ORO_HOME/pkgconfig"; then
      ln -sf "$ORO_HOME/pkgconfig" "$dest/pkgconfig"
    fi

    rm -rf $ORO_HOME/lib/*-android/objs-debug

    ln -sf "$ORO_HOME/lib/"$arch-* "$dest/lib"
    ln -sf "$ORO_HOME/objects/"$arch-* "$dest/objects"

    for abi in $(android_supported_abis); do
      if test -d "$ORO_HOME/lib/$abi-android"; then
        ln -sf "$ORO_HOME/lib/$abi-android" "$dest/lib"
      fi

      if test -d "$ORO_HOME/objects/$abi-android"; then
        ln -sf "$ORO_HOME/objects/$abi-android" "$dest/objects"
      fi
    done

    if [ "$platform" = "darwin" ]; then
      if [ "$(uname -m)" == "arm64" ]; then
        ln -sf "$ORO_HOME/lib/x86_64-iPhoneSimulator" "$dest/lib"
        ln -sf "$ORO_HOME/objects/x86_64-iPhoneSimulator" "$dest/objects"
      fi
      if [ "$(uname -m)" == "x86_64" ]; then
        ln -sf "$ORO_HOME/lib/arm64-iPhoneOS" "$dest/lib"
        ln -sf "$ORO_HOME/objects/arm64-iPhoneOS" "$dest/objects"
      fi
    fi
  else
    mkdir -p "$dest/uv" "$dest/bin" "$dest/src" "$dest/include" "$dest/lib" "$dest/objects"
    cp -rf "$source" "$dest"

    cp -rf "$root/npm/bin"/* "$dest/bin"
    cp -rf "$root/npm/src"/* "$dest/src"
    cp -f "$root/LICENSE.txt" "$dest"
    cp -f "$root/README.md" "$dest"

    mkdir -p "$dest/assets"
    cp -rf "$root/assets"/* "$dest/assets"

    cp -rf "$ORO_HOME/uv"/* "$dest/uv"
    cp -rf "$ORO_HOME/bin"/* "$dest/bin"
    cp -rf "$ORO_HOME/src"/* "$dest/src"
    cp -rf "$ORO_HOME/include"/* "$dest/include"

    if test -d "$ORO_HOME/pkgconfig"; then
      cp -rf "$ORO_HOME/pkgconfig" "$dest/pkgconfig"
    fi

    rm -rf $ORO_HOME/lib/*-android/objs-debug
    cp -rf $ORO_HOME/lib/*-android "$dest/lib"

    cp -rf "$ORO_HOME/lib/"$arch-* "$dest/lib"
    cp -rf "$ORO_HOME/objects/"$arch-* "$dest/objects"

    if [ "$platform" = "darwin" ]; then
      if [ "$(uname -m)" == "arm64" ]; then
        cp -rf "$ORO_HOME/lib/x86_64-iPhoneSimulator" "$dest/lib"
        cp -rf "$ORO_HOME/objects/x86_64-iPhoneSimulator" "$dest/objects"
      fi
      if [ "$(uname -m)" == "x86_64" ]; then
        cp -rf "$ORO_HOME/lib/arm64-iPhoneOS" "$dest/lib"
        cp -rf "$ORO_HOME/objects/arm64-iPhoneOS" "$dest/objects"
      fi
    fi
  fi

  _stage_platform_oroc_binary "$dest" "$arch"

  if [ "$platform" = "Win32" ]; then
    cp -rap "$ORO_HOME/bin"/.vs* "$dest/bin"
  fi

  cd "$dest" || exit $?
  echo "# in directory: '$dest'"

  _publish

  if (( do_global_link )); then
    npm link --no-fund --no-audit --offline --force || exit $?
  fi
}

function publish_cli_package () {
  local scope="$1"
  local name="$2"
  local package="$scope/$name"
  local dest="$ORO_HOME/packages/$package"

  if [[ ! -d "$dest" ]]; then
    echo >&2 "not ok - package not staged: $dest"
    exit 1
  fi

  cd "$dest" || exit $?
  echo "# in directory: '$dest'"

  _publish

  if (( do_global_link )); then
    for arch in "${archs[@]}"; do
      local normalized_arch="${arch/x86_64/x64}"
      local platform_package="$scope/${name}-$platform-$normalized_arch"
      npm link --no-fund --no-audit --offline --force "$platform_package"
    done

    npm link --no-fund --no-audit --offline --force
  fi
}
if [[ "$platform" = "linux" ]]; then
  if [ -n "$WSL_DISTRO_NAME" ] || uname -r | grep 'Microsoft'; then
    platform="Win32"
  fi
elif [[ "$(uname -s)" == *"MINGW64_NT"* ]]; then
  platform="Win32"
elif [[ "$(uname -s)" == *"MSYS_NT"* ]]; then
  platform="Win32"
fi

declare ORO_HOME="$root/build/npm/$platform"
declare global_prefix="$(resolve_global_prefix)"
declare PREFIX="$ORO_HOME"

while (( $# > 0 )); do
  declare arg="$1"; shift
  if [[ "$arg" = "--dry-run" ]] || [[ "$arg" = "-n" ]]; then
    dry_run=1
    continue
  fi

  if [[ "$arg" = "--only-platforms" ]]; then
    only_platforms=1
    continue
  fi

  if [[ "$arg" = "--only-top-level" ]]; then
    only_top_level=1
    continue
  fi

  if [[ "$arg" = "--no-remove-oro-home" ]]; then
    remove_oro_home=0
    continue
  fi

  if [[ "$arg" = "--no-rebuild" ]]; then
    no_rebuild=1
    continue
  fi

  if [[ "$arg" = "--link" ]]; then
    do_global_link=1
    continue
  fi

  args+=("$arg")
done

if (( do_global_link )); then
  # Keep staging under build/npm, but let install.sh recreate the global CLI symlink.
  PREFIX="$global_prefix"
fi

if (( remove_oro_home )); then
  rm -rf "$ORO_HOME"
fi

mkdir -p "$ORO_HOME"

export ORO_HOME
export PREFIX

if (( !only_top_level && !no_rebuild )) ; then
  if (( do_global_link && !dry_run )); then
    "$root/bin/install.sh" --link || exit $?
  else
    "$root/bin/install.sh" || exit $?
  fi
fi

if (( do_global_link && !dry_run )); then
  dry_run=1
fi

declare ABORT_ERRORS=0

# Confirm that build CLI matches current commit

if (( ! only_top_level )); then
  REPO_VERSION="$(cat "$root/VERSION.txt") ($(git rev-parse --short=8 HEAD))"
  declare built_cli="$ORO_HOME/bin/oroc"
  if [[ ! -x "$built_cli" ]]; then
    echo "Repo $REPO_VERSION and $built_cli does not exist or is not executable."
    ABORT_ERRORS=1
  else
    BUILD_VERSION=$("$built_cli" --version)

    if [[ "$REPO_VERSION" != "$BUILD_VERSION" ]]; then
      echo "Repo $REPO_VERSION and $built_cli $BUILD_VERSION don't match."
      ABORT_ERRORS=1
    fi
  fi
fi

declare android_abis=()


if (( !only_platforms || only_top_level )); then
  : #npm run gen
elif [[ "arm64" == "$(host_arch)" ]] && [[ "linux" == "$platform" ]]; then
  echo "warn - Android not supported on $platform-"$(uname -m)""
else
  android_abis+=($(android_supported_abis))
fi

if (( ! do_global_link )); then
  for abi in "${android_abis[@]}"; do
    lib_path="$ORO_HOME/lib/$abi-android"
    if [[ ! -f "$lib_path/libuv.a" ]]; then
      ABORT_ERRORS=1
      echo >&2 "not ok - $lib_path/libuv.a missing - check build process."
    fi

    declare runtime_static="$lib_path/lib${runtime_artifact_name}.a"
    if [[ ! -f "$runtime_static" ]]; then
      for alias in "${runtime_artifact_aliases[@]}"; do
        if [[ "$alias" == "$runtime_artifact_name" ]]; then
          continue
        fi
        if [[ -f "$lib_path/lib$alias.a" ]]; then
          runtime_static="$lib_path/lib$alias.a"
          break
        fi
      done
    fi

    if [[ ! -f "$runtime_static" ]]; then
      ABORT_ERRORS=1
      echo >&2 "not ok - $lib_path/lib${runtime_artifact_name}.a missing - check build process."
    fi
    export ABORT_ERRORS
  done

  if (( ABORT_ERRORS )); then
    echo >&2 "not ok - Refusing to publish due to errors."
    exit 1
  fi
fi

for spec in "${CLI_PACKAGE_SPECS[@]}"; do
  IFS=':' read -r scope _ <<< "$spec"
  mkdir -p "$ORO_HOME/packages/$scope"
done

if (( !only_platforms || only_top_level )); then
  for spec in "${CLI_PACKAGE_SPECS[@]}"; do
    IFS=':' read -r scope name <<< "$spec"
    stage_cli_package "$scope" "$name"
  done
fi

if (( !only_top_level )); then
  for spec in "${CLI_PACKAGE_SPECS[@]}"; do
    IFS=':' read -r scope name <<< "$spec"
    for arch in "${archs[@]}"; do
      package_platform_variant "$scope" "$name" "$arch"
    done
  done
fi

if (( !only_platforms || only_top_level )); then
  for spec in "${CLI_PACKAGE_SPECS[@]}"; do
    IFS=':' read -r scope name <<< "$spec"
    publish_cli_package "$scope" "$name"
  done
fi
