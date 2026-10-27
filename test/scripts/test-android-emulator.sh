#!/usr/bin/env bash

root="$(CDPATH='' cd -- "$(dirname "$(dirname -- "$0")")" && pwd)"

if [[ -n $1 ]]; then
  host=$1
else
  host="$(uname -s)"
fi

if [[ "$host" = "Linux" ]]; then
  if [ -n "$WSL_DISTRO_NAME" ] || uname -r | grep 'Microsoft'; then
    echo >&2 "error: WSL is not supported."
    exit 1
  fi
elif [[ "$host" == *"MINGW64_NT"* ]]; then
  host="Win32"
elif [[ "$host" == *"MSYS_NT"* ]]; then
  # handle error: /bin/bash: C:/oro/test/scripts/bootstrap-android-emulator.sh: No such file or directory
  echo >&2 "MSYS bash is not supported, please add Git\\bin to path."
  exit 1
fi

oro_env=""
for candidate in ".oro.env"; do
  if [ -f "$root/$candidate" ]; then
    oro_env="$root/$candidate"
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
  export ANDROID_HOME
  export ANDROID_SDK_MANAGER
  export JAVA_HOME
fi

id="computer.oro.runtime.tests"
adb="$(which adb 2>/dev/null)"
[[ -z "$adb" ]] && adb="$ANDROID_HOME/platform-tools/adb"
if [[ ! -f "$adb" ]]; then
  echo "adb not in path or ANDROID_HOME not set."
  exit 1
fi
export adb

port="${ORO_ANDROID_EMULATOR_PORT:-${ANDROID_SERIAL#emulator-}}"
port="${port:-5554}"
if [[ ! "$port" =~ ^[0-9]{4}$ ]] || (( port < 5554 || port > 5584 || port % 2 != 0 )); then
  echo >&2 "Use an even ORO_ANDROID_EMULATOR_PORT from 5554 through 5584."
  exit 1
fi
if [[ -n "$ANDROID_SERIAL" && "$ANDROID_SERIAL" != "emulator-$port" ]]; then
  echo >&2 "ANDROID_SERIAL must match emulator-$port for an emulator test run."
  exit 1
fi
export ANDROID_SERIAL="emulator-$port"
export ORO_ANDROID_EMULATOR_PORT="$port"
if "$adb" devices | awk 'NR > 1 {print $1}' | grep -Fx "$ANDROID_SERIAL" >/dev/null; then
  echo >&2 "Android target $ANDROID_SERIAL already exists; choose a free ORO_ANDROID_EMULATOR_PORT."
  exit 1
fi

bootstrap_pid=""
function stop_owned_emulator() {
  if [[ -n "$bootstrap_pid" ]]; then
    kill "$bootstrap_pid" 2>/dev/null || true
    for attempt in {1..100}; do
      kill -0 "$bootstrap_pid" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "$bootstrap_pid" 2>/dev/null; then
      kill -KILL "$bootstrap_pid" 2>/dev/null || true
    fi
    wait "$bootstrap_pid" 2>/dev/null || true
    bootstrap_pid=""
  fi
}
trap stop_owned_emulator EXIT
trap 'exit 143' TERM
trap 'exit 130' INT

echo "Ensuring adb server running..."
"$adb" start-server || exit $?

temp="$(mktemp)"
bash "$root/scripts/bootstrap-android-emulator.sh" "$temp" & bootstrap_pid=$!

bootstrap_exit_code=""
bootstrap_wait_count=0
bootstrap_timeout="${ORO_ANDROID_EMULATOR_SETUP_TIMEOUT_SECONDS:-600}"
while [ -z "$bootstrap_exit_code" ]; do
  bootstrap_exit_code="$(cat "$temp")"
  if [[ -n "$bootstrap_exit_code" ]]; then
    break
  fi

  if ! kill -0 "$bootstrap_pid" 2>/dev/null; then
    wait "$bootstrap_pid"
    bootstrap_exit_code=$?
    if (( bootstrap_exit_code == 0 )); then
      bootstrap_exit_code=1
    fi
    echo "Android emulator setup exited before reporting readiness: $bootstrap_exit_code"
    rm "$temp"
    exit "$bootstrap_exit_code"
  fi

  if (( bootstrap_wait_count >= bootstrap_timeout )); then
    echo "Android emulator setup timed out after ${bootstrap_timeout}s."
    stop_owned_emulator
    rm "$temp"
    exit 124
  fi

  if (( bootstrap_wait_count > 0 && bootstrap_wait_count % 30 == 0 )); then
    echo "Waiting for Android emulator setup: ${bootstrap_wait_count}s/${bootstrap_timeout}s"
  fi

  sleep 1
  (( bootstrap_wait_count++ ))
done

if [[ "$bootstrap_exit_code" != "0" ]]; then
  rm "$temp"
  exit "$bootstrap_exit_code"
fi

# reset bootstrap exit code, we will use it again
bootstrap_exit_code=""
echo > "$temp"

echo "info: Waiting for Android Emulator to boot"

boot_completed=""
boot_wait_count=0
boot_timeout="${ORO_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS:-300}"
while [[ "$boot_completed" != "1" ]] ; do
  if ! kill -0 "$bootstrap_pid" 2>/dev/null; then
    wait "$bootstrap_pid"
    bootstrap_exit_code=$?
    bootstrap_pid=""
    (( bootstrap_exit_code == 0 )) && bootstrap_exit_code=1
    echo "Android Emulator exited before framework readiness."
    rm "$temp"
    exit "$bootstrap_exit_code"
  fi
  boot_completed="$("$adb" -s "$ANDROID_SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')"
  if [[ "$boot_completed" == "1" ]]; then
    for service in package activity; do
      if ! "$adb" -s "$ANDROID_SERIAL" shell service check "$service" 2>/dev/null | grep -E ': found[[:space:]]*$' >/dev/null; then
        boot_completed=""
        break
      fi
    done
  fi
  # reliable cross platform method of waiting for background process asynchronously
  bootstrap_exit_code="$(cat "$temp")"
  if [[ -n "$bootstrap_exit_code" ]] && [[ "$bootstrap_exit_code" != "0" ]]; then
    # emulator already exited
    echo "Android Emulator failed to boot."
    wait "$bootstrap_pid" 2>/dev/null || true
    rm "$temp"
    exit "$bootstrap_exit_code"
  fi

  if (( boot_wait_count >= boot_timeout )); then
    echo "Android Emulator boot timed out after ${boot_timeout}s."
    "$adb" devices || true
    stop_owned_emulator
    rm "$temp"
    exit 124
  fi

  if (( boot_wait_count > 0 && boot_wait_count % 30 == 0 )); then
    echo "Waiting for Android Emulator to boot: ${boot_wait_count}s/${boot_timeout}s"
  fi

  sleep 1
  (( boot_wait_count++ ))
done
echo "info: Android Emulator booted"

rm "$temp"

node "$root/scripts/test-android.js" || {
  rc=$?
  if [[ -n "$RUNNER_TEMP" ]]; then
    mkdir -p "$RUNNER_TEMP/android-failure-logs"
    "$adb" -s "$ANDROID_SERIAL" logcat -d -b all > "$RUNNER_TEMP/android-failure-logs/logcat.txt" 2>&1 || true
    "$adb" -s "$ANDROID_SERIAL" shell dumpsys activity activities > "$RUNNER_TEMP/android-failure-logs/activities.txt" 2>&1 || true
  fi
  echo "info: Shutting Android Emulator due to failed build."
  exit "$rc"
}

echo "info: Shutting Android Emulator due to poll-adb-logcat.sh finishing."
exit 0
