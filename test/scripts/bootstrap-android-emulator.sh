#!/usr/bin/env bash

# 'namespaced' root
bae_root="$(CDPATH='' cd -- "$(dirname "$(dirname -- "$0")")" && pwd)"

# enable async operation by writing exit code to a file for callee to test
# call this to identify that emulator is about to be called, but don't exit script
function write_code() {
  local exit_code="$1"
  [[ -n "$error_file" ]] && echo "$exit_code" > "$error_file"
}

# Call this on error, script will terminate
error_file=$1
function exit_and_write_code() {
  local exit_code="$1"
  write_code "$exit_code"
  exit "$exit_code"
}

port="${ORO_ANDROID_EMULATOR_PORT:-5554}"
if [[ ! "$port" =~ ^[0-9]{4}$ ]] || (( port < 5554 || port > 5584 || port % 2 != 0 )); then
  echo >&2 "Use an even ORO_ANDROID_EMULATOR_PORT from 5554 through 5584."
  exit_and_write_code 1
fi

declare rc

oro_env=""
for candidate in ".oro.env"; do
  if [ -f "$bae_root/$candidate" ]; then
    oro_env="$bae_root/$candidate"
    break
  elif [ -f "$candidate" ]; then
    oro_env="$candidate"
    break
  elif [ -f "../$candidate" ]; then
    oro_env="../$candidate"
    break
  fi
done

if [ -n "$oro_env" ]; then
  echo "# Sourcing $oro_env"
  source "$oro_env"
fi

if [ -n "$ANDROID_SDK_MANAGER" ]; then
  sdkmanager="$ANDROID_HOME/$ANDROID_SDK_MANAGER"
  bn="$(basename "$sdkmanager")"
  ext=""
  # Only add the result of extension filter if basename contains '.', otherwise it will contain 'sdkmanager'
  [[ "$bn" == *"."* ]] && [[ "$ext" != "$sdkmanager" ]] && ext=".${bn##*.}"
  avdmanager="$(dirname "$sdkmanager")/avdmanager$ext"
fi

if [ -z "$ANDROID_HOME" ]; then
  if test -d "$HOME/android"; then
    ANDROID_HOME="$HOME/android"
  elif test -d "$HOME/Android"; then
    ANDROID_HOME="$HOME/Android"
  elif test -d "$HOME/Library/Android/sdk"; then
    ANDROID_HOME="$HOME/Library/Android/sdk"
  elif test -d "$HOME/Library/Android"; then
    ANDROID_HOME="$HOME/Library/Android"
  elif test -d "$HOME/.android/sdk"; then
    ANDROID_HOME="$HOME/.android/sdk"
  elif test -d "$HOME/.android"; then
    ANDROID_HOME="$HOME/.android"
  fi
fi

# Keep avdmanager and emulator on the same user-data directory. Hosted Android
# toolchains may otherwise create the AVD under the SDK while emulator searches
# the runner account's default ~/.android directory.
if [[ -z "${ANDROID_USER_HOME:-}" ]]; then
  ANDROID_USER_HOME="$HOME/.android"
fi
if [[ -z "${ANDROID_AVD_HOME:-}" ]]; then
  ANDROID_AVD_HOME="$ANDROID_USER_HOME/avd"
fi
export ANDROID_USER_HOME ANDROID_AVD_HOME
mkdir -p "$ANDROID_AVD_HOME"

emulator="$(which emulator 2>/dev/null)"
[[ -z "$avdmanager" ]] && avdmanager="$(which avdmanager 2>/dev/null)"
[[ -z "$sdkmanager" ]] && sdkmanager="$(which sdkmanager 2>/dev/null)"

if [ -z "$emulator" ]; then
  emulator="$ANDROID_HOME/emulator/emulator"
fi

if [ -z "$avdmanager" ]; then
  avdmanager="$ANDROID_HOME/cmdline-tools/tools/bin/avdmanager"
fi

if [ -z "$sdkmanager" ]; then
  sdkmanager="$ANDROID_HOME/cmdline-tools/tools/bin/sdkmanager"
fi

if [ ! -f "$avdmanager" ]; then
  echo "not ok - Unable to locate avdmanager: $avdmanager"
  exit_and_write_code 1
fi

if [ ! -f "$sdkmanager" ]; then
  echo "not ok - Unable to locate sdkmanager."
  exit_and_write_code 1
fi

# The API 37 image aborts in mapper.ranchu during color-buffer readback,
# restarting SurfaceFlinger and the framework. Keep the guest independent of
# the build SDK so tests can use API 36 while applications compile with API 37.
emulator_api_level="${ORO_ANDROID_EMULATOR_API_LEVEL:-36}"

case "$(uname -m)" in
  arm64|aarch64) android_system_image_arch="arm64-v8a" ;;
  x86_64|amd64) android_system_image_arch="x86_64" ;;
  *)
    echo "not ok - Unsupported Android emulator host architecture: $(uname -m)"
    exit_and_write_code 1
    ;;
esac

# Give Google APIs and the test APK room to initialize before installation.
# A separate AVD identity avoids reusing an older, smaller userdata image.
avd_data_size="6G"
avd_name="OROAVD_API_${emulator_api_level//./_}_${android_system_image_arch}_data${avd_data_size}"
pkg="system-images;android-$emulator_api_level;google_apis;$android_system_image_arch"
system_image_dir="$ANDROID_HOME/system-images/android-$emulator_api_level/google_apis/$android_system_image_arch"
avd_exists=""

if "$avdmanager" list avd | grep -F "Name: $avd_name" >/dev/null; then
  avd_exists=1
fi

emulator_packages=()
if [[ ! -f "$emulator" ]]; then
  emulator_packages+=("emulator")
fi
if [[ ! -f "$system_image_dir/package.xml" ]] && \
   [[ ! -f "$system_image_dir/source.properties" ]]; then
  emulator_packages+=("$pkg")
fi

if (( ${#emulator_packages[@]} > 0 )); then
  echo "Installing missing Android emulator packages: ${emulator_packages[*]}"
  yes | "$sdkmanager" "${emulator_packages[@]}"
  rc=$?
  (( rc != 0 )) && exit_and_write_code $rc

  echo "Accepting licenses..."
  yes | "$sdkmanager" --licenses
  rc=$?
  (( rc != 0 )) && exit_and_write_code $rc
fi

if [ ! -f "$emulator" ]; then
  echo "not ok - Unable to locate emulator after SDK package installation: $emulator"
  exit_and_write_code 1
fi

if [[ ! -f "$system_image_dir/package.xml" ]] && \
   [[ ! -f "$system_image_dir/source.properties" ]]; then
  echo "not ok - Android system image was not installed at $system_image_dir"
  exit_and_write_code 1
fi

if [[ -z "$avd_exists" ]]; then
  echo "Creating AVD..."
  "$avdmanager" --clear-cache create avd -n "$avd_name" -k "$pkg" -d 1 --force
  rc=$?
  (( rc != 0 )) && exit_and_write_code $rc
fi

if [[ ! -f "$ANDROID_AVD_HOME/$avd_name.ini" ]]; then
  echo "not ok - Android AVD was not created at $ANDROID_AVD_HOME/$avd_name.ini"
  exit_and_write_code 1
fi

avd_config="$ANDROID_AVD_HOME/$avd_name.avd/config.ini"
if [[ ! -f "$avd_config" ]]; then
  echo "not ok - Android AVD configuration was not created at $avd_config"
  exit_and_write_code 1
fi
avd_config_temp="$(mktemp "$avd_config.XXXXXX")" || exit_and_write_code 1
awk -v size="$avd_data_size" '
  /^[[:space:]]*disk[.]dataPartition[.]size[[:space:]]*=/ { next }
  { print }
  END { print "disk.dataPartition.size=" size }
' "$avd_config" > "$avd_config_temp" && mv "$avd_config_temp" "$avd_config"
rc=$?
(( rc != 0 )) && exit_and_write_code "$rc"

[[ -z "$EMULATOR_FLAGS" ]] && EMULATOR_FLAGS=()

EMULATOR_FLAGS+=("-gpu" "swiftshader_indirect")
EMULATOR_FLAGS+=("-port" "$port")
# fixes adb: failed to install cmd: Can't find service: package

write_code 0
echo "Starting Android emulator..."
emulator_pid=""
function stop_owned_process() {
  if [[ -n "$emulator_pid" ]]; then
    kill "$emulator_pid" 2>/dev/null || true
    for attempt in {1..50}; do
      kill -0 "$emulator_pid" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "$emulator_pid" 2>/dev/null; then
      kill -KILL "$emulator_pid" 2>/dev/null || true
    fi
    wait "$emulator_pid" 2>/dev/null || true
  fi
}
trap 'exit 143' TERM
trap 'exit 130' INT
trap stop_owned_process EXIT
if [[ -z "$CI" ]]; then
  "$emulator" "@$avd_name"    \
    "${EMULATOR_FLAGS[@]}"    \
    -camera-back none         \
    -no-boot-anim             \
    -no-window                \
    -noaudio                  \
    >/dev/null &
else
  "$emulator" "@$avd_name"    \
    "${EMULATOR_FLAGS[@]}"    \
    -camera-back none         \
    -no-boot-anim             \
    -no-window                \
    -noaudio                  \
    2>&1 &
fi

emulator_pid=$!
wait "$emulator_pid"
rc=$?
emulator_pid=""
exit_and_write_code $rc
