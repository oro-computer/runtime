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


echo "Ensuring adb server running..."
$adb start-server || exit $?

temp="$(mktemp)"
${SHELL:-sh} -c "$root/scripts/bootstrap-android-emulator.sh $temp" & bootstrap_pid=$!

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
    kill "$bootstrap_pid" 2>/dev/null || true
    wait "$bootstrap_pid" 2>/dev/null || true
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
  boot_completed="$($adb shell getprop sys.boot_completed 2>/dev/null)"
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
    kill "$bootstrap_pid" 2>/dev/null || true
    wait "$bootstrap_pid" 2>/dev/null || true
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

"$adb" uninstall "$id"

echo "Removing old fixtures..."
"$adb" shell rm -rf "/data/local/tmp/oro-test-fixtures"
echo "Pushing fixtures..."

fixtures_path="$root/fixtures"
# adb on windows doesn't handle incorrect slashes (hangs)
if [[ "$host" != "Win32" ]]; then
  "$adb" push "$fixtures_path" "/data/local/tmp/oro-test-fixtures"
else
  # adb push just doesn't work under mingw (It hangs, attempts to prefix DEST path with c:\Program Files\Git...)
  fixtures_path="${fixtures_path/"$(pwd)/"/}"
  # change to a relative native path (So we don't have to fix drive letter)
  fixtures_path="${fixtures_path//\//\\}"
  "$COMSPEC" "/k $adb push $fixtures_path /data/local/tmp/oro-test-fixtures && exit "
fi

node "$root/scripts/test-android.js" || {
  rc=$?
  if [[ -n "$RUNNER_TEMP" ]]; then
    mkdir -p "$RUNNER_TEMP/android-failure-logs"
    "$adb" logcat -d -b all > "$RUNNER_TEMP/android-failure-logs/logcat.txt" 2>&1 || true
    "$adb" shell dumpsys activity activities > "$RUNNER_TEMP/android-failure-logs/activities.txt" 2>&1 || true
  fi
  echo "info: Shutting Android Emulator due to failed build."
  "$adb" devices | grep emulator | cut -f1 | while read -r line; do
    "$adb" -s "$line" emu kill
  done
  exit "$rc"
}

echo "info: Shutting Android Emulator due to poll-adb-logcat.sh finishing."
"$adb" devices | grep emulator | cut -f1 | while read -r line; do
  "$adb" -s "$line" emu kill
done

"$adb" kill-server >/dev/null 2>&1

exit $rc
