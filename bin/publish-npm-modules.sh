#!/usr/bin/env bash

declare root
root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

source "$root/bin/android-functions.sh"
source "$root/bin/functions.sh"
source "$root/bin/runtime-artifacts.sh"

declare runtime_artifact_name="$ORO_RUNTIME_ARTIFACT_NAME"
declare -a runtime_artifact_aliases=()
if [[ -n "${ORO_RUNTIME_ARTIFACT_ALIASES+x}" ]]; then
  runtime_artifact_aliases=("${ORO_RUNTIME_ARTIFACT_ALIASES[@]}")
fi

declare -a archs=()
read -r -a archs <<< "$(host_arch)"
declare platform
platform="$(uname -s | tr '[:upper:]' '[:lower:]')"

declare args=()
declare install_args=()
declare only_platforms=0
declare only_top_level=0
declare no_rebuild=0
declare remove_oro_home=1
declare do_global_link=0

function usage() {
  cat <<'EOF'
Usage: ./bin/publish-npm-modules.sh [options] [-- npm-options]

Build, stage, and pack Oro npm packages. This helper never contacts the npm
registry; verified tarballs are published only by the GitHub OIDC release job.

Options:
  -h, --help             Show this help and exit
  -n, --dry-run          Explicit compatibility alias for pack-only behavior
      --only-platforms   Process only the current platform package
      --only-top-level   Process only the Node adapter and meta-package
      --no-rebuild       Reuse an existing staged runtime
      --no-remove-oro-home
                         Preserve the npm staging directory before the run
      --yes-deps         Accept supported installer dependency prompts while
                         rebuilding the staged runtime
      --link             Link packages globally for local development
      --                 Pass all remaining options to npm pack

Set ORO_NPM_STAGING_HOME to stage under build/npm or a temporary directory.
NO_ANDROID and NO_IOS are independent source-build presence flags inherited by
the installer. A non-empty NO_ANDROID disables only Android artifacts; a
non-empty NO_IOS disables only iOS/iOS Simulator artifacts on macOS. Values
such as 0 and false still disable the named target. Set both for desktop-only
packaging; neither variable selects an oroc application-build target.
EOF
}

function _stage_platform_oroc_binary() {
  local dest="$1"
  local arch="$2"
  local cli_name="oroc"
  local cli_source=""

  if [[ "$platform" == "win32" ]]; then
    cli_name="oroc.exe"
  fi

  cli_source="$ORO_HOME/bin/$cli_name"
  if [[ ! -f "$cli_source" ]]; then
    cli_source="$root/build/$arch-desktop/bin/$cli_name"
  fi

  if [[ ! -f "$cli_source" ]]; then
    echo >&2 "not ok - missing compiled CLI binary for $platform/$arch: $cli_source"
    exit 1
  fi

  if (( do_global_link )); then
    ln -sf "$cli_source" "$dest/bin/$cli_name"
  else
    cp -f "$cli_source" "$dest/bin/$cli_name"
  fi
}

function _pack_or_link () {
  if (( !do_global_link )); then
    npm pack "${args[@]}" || exit $?
  fi
}

function should_stage_target_directory() {
  local name="${1##*/}"

  if [[ -n "${NO_ANDROID:-}" ]] && [[ "$name" == *-android ]]; then
    return 1
  fi

  if [[ -n "${NO_IOS:-}" ]]; then
    case "$name" in
      *-iPhoneOS|*-iPhoneSimulator|*-ios|*-ios-simulator)
        return 1
        ;;
    esac
  fi

  return 0
}

function validate_platform_target_selection() {
  local dest="$1"
  local arch="$2"

  if [[ -n "${NO_ANDROID:-}" ]] && compgen -G "$dest/lib/*-android" > /dev/null; then
    echo >&2 "not ok - NO_ANDROID excluded Android, but the staged package contains Android libraries"
    exit 1
  fi

  if [[ -z "${NO_ANDROID:-}" ]] && [[ -n "${ORO_ANDROID_CI:-}" ]]; then
    for abi in $(android_supported_abis); do
      if [[ ! -d "$dest/lib/$abi-android" ]]; then
        echo >&2 "not ok - Android packaging is enabled, but the staged package is missing required ABI $abi"
        exit 1
      fi
    done
  fi

  if [[ -n "${NO_IOS:-}" ]]; then
    for ios_dir in "$dest"/lib/*-iPhoneOS "$dest"/lib/*-iPhoneSimulator "$dest"/lib/*-ios "$dest"/lib/*-ios-simulator; do
      if [[ -e "$ios_dir" ]]; then
        echo >&2 "not ok - NO_IOS excluded Apple-mobile targets, but the staged package contains $(basename "$ios_dir")"
        exit 1
      fi
    done
  elif [[ "$platform" = "darwin" ]]; then
    local required_ios_targets=(
      "arm64-iPhoneOS"
      "x86_64-iPhoneSimulator"
    )

    if [[ "$arch" = "arm64" ]]; then
      required_ios_targets+=("arm64-iPhoneSimulator")
    fi

    for target in "${required_ios_targets[@]}"; do
      if [[ ! -d "$dest/lib/$target" ]]; then
        echo >&2 "not ok - NO_IOS is empty, but the staged package is missing required iOS target $target"
        exit 1
      fi
    done
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

function resolve_removal_path() {
  node -e '
    const fs = require("node:fs")
    const path = require("node:path")
    let current = path.resolve(process.argv[1])
    const suffix = []
    while (!fs.existsSync(current)) {
      const parent = path.dirname(current)
      if (parent === current) break
      suffix.unshift(path.basename(current))
      current = parent
    }
    const canonical = fs.realpathSync.native(current)
    process.stdout.write(path.resolve(canonical, ...suffix))
  ' "$1"
}

declare -a CLI_PACKAGE_SPECS=(
  "@oro-computer:runtime"
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

  if [[ -e "$dest" ]]; then
    echo >&2 "not ok - package staging destination already exists: $dest"
    exit 1
  fi

  if (( do_global_link )); then
    mkdir -p "$dest/bin" "$dest/docs"
    cp -rf "$root/npm/bin"/* "$dest/bin"

    ln -sf "$source"/* "$dest"
    rm -rf "$dest/src"
    ln -sf "$root/npm/src" "$dest/src"
    ln -sf "$root/LICENSE.txt" "$dest"
    ln -sf "$root/NOTICE" "$dest"
    ln -sf "$root/THIRD_PARTY_NOTICES.md" "$dest"
    ln -sf "$root/README.md" "$dest/README-RUNTIME.md"
    ln -sf "$root/docs/BUILD_ENVIRONMENT.md" "$dest/docs/BUILD_ENVIRONMENT.md"
    ln -sf "$root/api"/* "$dest"
  else
    mkdir -p "$dest/docs"
    cp -rf "$source"/. "$dest"
    mkdir -p "$dest/bin"
    cp -rf "$root/npm/bin"/* "$dest/bin"
    cp -rf "$root/npm/src" "$dest/src"
    cp -f "$root/LICENSE.txt" "$dest"
    cp -f "$root/NOTICE" "$dest"
    cp -f "$root/THIRD_PARTY_NOTICES.md" "$dest"
    cp -f "$root/README.md" "$dest/README-RUNTIME.md"
    cp -f "$root/docs/BUILD_ENVIRONMENT.md" "$dest/docs/BUILD_ENVIRONMENT.md"
    cp -rf "$root/api"/* "$dest"
    cp -f "$root/api/README.md" "$dest/API.md"
    cp -f "$root/README.md" "$dest/README.md"
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

  if [[ -e "$dest" ]]; then
    echo >&2 "not ok - package staging destination already exists: $dest"
    exit 1
  fi

  if (( do_global_link )); then
    mkdir -p "$dest/assets" "$dest/bin" "$dest/docs" "$dest/include" "$dest/lib" "$dest/objects" "$dest/src"
    cp -rf "$root/npm/src"/* "$dest/src"
    cp -rf "$root/npm/bin"/* "$dest/bin"

    ln -sf "$source"/* "$dest"
    ln -sf "$root/assets"/* "$dest/assets"
    ln -sf "$root/LICENSE.txt" "$dest"
    ln -sf "$root/NOTICE" "$dest"
    ln -sf "$root/THIRD_PARTY_NOTICES.md" "$dest"
    ln -sf "$root/README.md" "$dest"
    ln -sf "$root/docs/BUILD_ENVIRONMENT.md" "$dest/docs/BUILD_ENVIRONMENT.md"

    ln -sf "$ORO_HOME/bin"/* "$dest/bin"
    ln -sf "$ORO_HOME/src"/* "$dest/src"
    ln -sf "$ORO_HOME/uv" "$dest/uv"
    ln -sf "$ORO_HOME/include"/* "$dest/include"

    if test -d "$ORO_HOME/pkgconfig"; then
      ln -sf "$ORO_HOME/pkgconfig" "$dest/pkgconfig"
    fi

    for lib_dir in "$ORO_HOME"/lib/"$arch"-*; do
      if [[ -e "$lib_dir" ]] && should_stage_target_directory "$lib_dir"; then
        ln -sf "$lib_dir" "$dest/lib"
      fi
    done
    for objects_dir in "$ORO_HOME"/objects/"$arch"-*; do
      if [[ -e "$objects_dir" ]] && should_stage_target_directory "$objects_dir"; then
        ln -sf "$objects_dir" "$dest/objects"
      fi
    done

    if [[ -z "${NO_ANDROID:-}" ]]; then
      for abi in $(android_supported_abis); do
        if test -d "$ORO_HOME/lib/$abi-android"; then
          ln -sf "$ORO_HOME/lib/$abi-android" "$dest/lib"
        fi

        if test -d "$ORO_HOME/objects/$abi-android"; then
          ln -sf "$ORO_HOME/objects/$abi-android" "$dest/objects"
        fi
      done
    fi

    if [[ "$platform" = "darwin" ]] && [[ -z "${NO_IOS:-}" ]]; then
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
    mkdir -p "$dest/uv" "$dest/bin" "$dest/docs" "$dest/src" "$dest/include" "$dest/lib" "$dest/objects"
    cp -rf "$source"/. "$dest"

    cp -rf "$root/npm/bin"/* "$dest/bin"
    cp -rf "$root/npm/src"/* "$dest/src"
    cp -f "$root/LICENSE.txt" "$dest"
    cp -f "$root/NOTICE" "$dest"
    cp -f "$root/THIRD_PARTY_NOTICES.md" "$dest"
    cp -f "$root/README.md" "$dest"
    cp -f "$root/docs/BUILD_ENVIRONMENT.md" "$dest/docs/BUILD_ENVIRONMENT.md"

    mkdir -p "$dest/assets"
    cp -rf "$root/assets"/* "$dest/assets"

    cp -rf "$ORO_HOME/uv"/* "$dest/uv"
    cp -rf "$ORO_HOME/bin"/* "$dest/bin"
    cp -rf "$ORO_HOME/src"/* "$dest/src"
    cp -rf "$ORO_HOME/include"/* "$dest/include"

    if test -d "$ORO_HOME/pkgconfig"; then
      cp -rf "$ORO_HOME/pkgconfig" "$dest/pkgconfig"
    fi

    if [[ -z "${NO_ANDROID:-}" ]]; then
      for android_lib_dir in "$ORO_HOME"/lib/*-android; do
        if [[ -d "$android_lib_dir" ]]; then
          cp -rf "$android_lib_dir" "$dest/lib"
          rm -rf "$dest/lib/$(basename "$android_lib_dir")/objs-debug"
        fi
      done
    fi

    for lib_dir in "$ORO_HOME"/lib/"$arch"-*; do
      if [[ -e "$lib_dir" ]] && should_stage_target_directory "$lib_dir"; then
        cp -rf "$lib_dir" "$dest/lib"
      fi
    done
    for objects_dir in "$ORO_HOME"/objects/"$arch"-*; do
      if [[ -e "$objects_dir" ]] && should_stage_target_directory "$objects_dir"; then
        cp -rf "$objects_dir" "$dest/objects"
      fi
    done

    if [[ "$platform" = "darwin" ]] && [[ -z "${NO_IOS:-}" ]]; then
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

  validate_platform_target_selection "$dest" "$arch"
  _stage_platform_oroc_binary "$dest" "$arch"

  if [ "$platform" = "win32" ]; then
    cp -rap "$ORO_HOME/bin"/.vs* "$dest/bin"
  fi

  cd "$dest" || exit $?
  echo "# in directory: '$dest'"

  _pack_or_link

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

  _pack_or_link

  if (( do_global_link )); then
    for arch in "${archs[@]}"; do
      local normalized_arch="${arch/x86_64/x64}"
      local platform_package="$scope/${name}-$platform-$normalized_arch"
      npm link --no-fund --no-audit --offline --force "$platform_package"
    done

    npm link --no-fund --no-audit --offline --force
  fi
}

function publish_node_adapter () {
  local package_dir="$root/npm/packages/@oro-computer/runtime-node"

  if [[ ! -f "$package_dir/package.json" ]]; then
    echo >&2 "not ok - missing package template: $package_dir"
    exit 1
  fi

  cd "$package_dir" || exit $?
  echo "# in directory: '$package_dir'"
  _pack_or_link
}

if [[ "$platform" = "linux" ]]; then
  if [ -n "$WSL_DISTRO_NAME" ] || uname -r | grep 'Microsoft'; then
    platform="win32"
  fi
elif [[ "$(uname -s)" == *"MINGW64_NT"* ]]; then
  platform="win32"
elif [[ "$(uname -s)" == *"MSYS_NT"* ]]; then
  platform="win32"
fi

declare ORO_HOME
ORO_HOME="${ORO_NPM_STAGING_HOME:-$root/build/npm/$platform}"
declare global_prefix
global_prefix="$(resolve_global_prefix)"
declare expected_npm_staging_root
expected_npm_staging_root="$(node -e 'process.stdout.write(require("node:path").resolve(process.argv[1]))' "$root/build/npm")"
declare npm_staging_root
npm_staging_root="$(resolve_removal_path "$root/build/npm")"
declare temporary_root
temporary_root="$(resolve_removal_path "${TMPDIR:-/tmp}")"
declare home_root=""
if [[ -n "${HOME:-}" ]]; then
  home_root="$(resolve_removal_path "$HOME")"
fi
ORO_HOME="$(resolve_removal_path "$ORO_HOME")"
declare PREFIX="$ORO_HOME"
declare temporary_staging_home=0

if [[ "$npm_staging_root" != "$expected_npm_staging_root" ]]; then
  echo >&2 "not ok - repository npm staging root must not resolve through a symlink: $npm_staging_root"
  exit 1
fi

if
  [[ "$temporary_root" != "/" ]] &&
  [[ "$temporary_root" != "$root" ]] &&
  [[ -z "$home_root" || "$temporary_root" != "$home_root" ]] &&
  [[ "$ORO_HOME" == "$temporary_root/"* ]]
then
  temporary_staging_home=1
fi

if
  [[ "$ORO_HOME" != "$npm_staging_root" ]] &&
  [[ "$ORO_HOME" != "$npm_staging_root/"* ]] &&
  (( ! temporary_staging_home ))
then
  echo >&2 "not ok - npm staging home must be build/npm or a child of the temporary directory: $ORO_HOME"
  exit 1
fi

while (( $# > 0 )); do
  declare arg="$1"; shift
  if [[ "$arg" = "--help" ]] || [[ "$arg" = "-h" ]]; then
    usage
    exit 0
  fi

  if [[ "$arg" = "--" ]]; then
    args+=("$@")
    break
  fi

  if [[ "$arg" = "--dry-run" ]] || [[ "$arg" = "-n" ]]; then
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

  if [[ "$arg" = "--yes-deps" ]]; then
    install_args+=("--yes-deps")
    continue
  fi

  if [[ "$arg" = "--link" ]]; then
    do_global_link=1
    continue
  fi

  args+=("$arg")
done

if (( only_platforms && only_top_level )); then
  echo >&2 "not ok - --only-platforms and --only-top-level cannot be used together"
  exit 2
fi

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
  if (( do_global_link )); then
    "$root/bin/install.sh" --link "${install_args[@]}" || exit $?
  else
    "$root/bin/install.sh" "${install_args[@]}" || exit $?
  fi
fi

declare ABORT_ERRORS=0

# Confirm that build CLI matches current commit

if (( ! only_top_level )); then
  REPO_VERSION="$(cat "$root/VERSION.txt") ($(git rev-parse --short=8 HEAD))"
  declare built_cli="$ORO_HOME/bin/oroc"
  if [[ "$platform" == "win32" ]]; then
    built_cli="$ORO_HOME/bin/oroc.exe"
  fi
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
elif [[ -n "${NO_ANDROID:-}" ]]; then
  :
elif [[ "arm64" == "$(host_arch)" ]] && [[ "linux" == "$platform" ]]; then
  echo "warn - Android not supported on $platform-$(uname -m)"
else
  read -r -a android_abis <<< "$(android_supported_abis)"
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

if (( !only_top_level )); then
  for spec in "${CLI_PACKAGE_SPECS[@]}"; do
    IFS=':' read -r scope name <<< "$spec"
    for arch in "${archs[@]}"; do
      package_platform_variant "$scope" "$name" "$arch"
    done
  done
fi

if (( !only_platforms || only_top_level )); then
  publish_node_adapter

  for spec in "${CLI_PACKAGE_SPECS[@]}"; do
    IFS=':' read -r scope name <<< "$spec"
    stage_cli_package "$scope" "$name"
  done

  for spec in "${CLI_PACKAGE_SPECS[@]}"; do
    IFS=':' read -r scope name <<< "$spec"
    publish_cli_package "$scope" "$name"
  done
fi
