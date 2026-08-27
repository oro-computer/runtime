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

if [[ -z "$ANDROID_SDK_PLATFORM" ]]; then
  ANDROID_SDK_PLATFORM="37.0"
fi

case "$(uname -m)" in
  arm64|aarch64) android_system_image_arch="arm64-v8a" ;;
  x86_64|amd64) android_system_image_arch="x86_64" ;;
  *)
    echo "not ok - Unsupported Android emulator host architecture: $(uname -m)"
    exit_and_write_code 1
    ;;
esac

avd_name="OROAVD_API_${ANDROID_SDK_PLATFORM//./_}_$android_system_image_arch"
pkg="system-images;android-$ANDROID_SDK_PLATFORM;google_apis;$android_system_image_arch"
avd_exists=""

if "$avdmanager" list avd | grep -F "Name: $avd_name" >/dev/null; then
  avd_exists=1
fi

if [[ ! -f "$emulator" ]] || [[ -z "$avd_exists" ]]; then
  echo "Ensuring the Android emulator and $pkg are installed..."
  yes | "$sdkmanager" "emulator" "$pkg"
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

if [[ -z "$avd_exists" ]]; then
  echo "Creating AVD..."
  "$avdmanager" --clear-cache create avd -n "$avd_name" -k "$pkg" -d 1 --force
  rc=$?
  (( rc != 0 )) && exit_and_write_code $rc
fi

[[ -z "$EMULATOR_FLAGS" ]] && EMULATOR_FLAGS=()

EMULATOR_FLAGS+=("-gpu" "swiftshader_indirect")
# fixes adb: failed to install cmd: Can't find service: package

write_code 0
echo "Starting Android emulator..."
if [[ -z "$CI" ]]; then
  "$emulator" "@$avd_name"    \
    "${EMULATOR_FLAGS[@]}"    \
    -camera-back none         \
    -no-boot-anim             \
    -no-window                \
    -noaudio                  \
    >/dev/null
else
  "$emulator" "@$avd_name"    \
    "${EMULATOR_FLAGS[@]}"    \
    -camera-back none         \
    -no-boot-anim             \
    -no-window                \
    -noaudio                  \
    | grep -v "\[CAMetalLayer nextDrawable\] returning nil because device is nil." \
    2>&1
fi

rc=$?
exit_and_write_code $rc
