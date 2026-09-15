#!/usr/bin/env bash

function dump_runtime_error() {
  "$adb" logcat -d -b all -s AndroidRuntime:E '*:S'
  if [[ -n "$poll_adb_watchdog_file" ]]; then
    watchdog_file_update 1
  fi
  exit 1
}

function dump_pid() {
  local app_pid="$1"
  local exit_code="$2"
  [[ -z "$exit_code" ]] && exit_code=1
  "$adb" logcat -d -b all --pid="$app_pid"
  if [[ -n "$poll_adb_watchdog_file" ]]; then
    watchdog_file_update "$exit_code"
  fi
  exit "$exit_code"
}

function exit_on_adb_crash_signal() {
  while read -r line; do
    echo "'--------- beginning of crash' occurred in adb output, exiting."
    dump_runtime_error
  done < <($adb logcat | grep -- "--------- beginning of crash")
}

declare poll_adb_watchdog_file
function watchdog_file_set() {
  poll_adb_watchdog_file="$(mktemp)"
}

function watchdog_file_update() {
  printf '%s\n' "$1" > "$poll_adb_watchdog_file"
}

function watchdog_file_exists() {
  if [ -f "$poll_adb_watchdog_file" ]; then
    data="$(cat "$poll_adb_watchdog_file")"
    if [[ -z "$data" || "$data" == "running" ]]; then
      echo "0"
      return
    fi
  fi

  echo "1"
}

id="computer.oro.runtime.tests"

## Start application
[[ -z "$adb" ]] && adb="$(which adb 2>/dev/null)"
if [[ ! -f "$adb" ]]; then
  echo "adb not in path or ANDROID_HOME not set."
  exit 1
fi

"$adb" logcat -c
trap 'kill "$adb_crash_pid" "${logcat_pid:-}" 2>/dev/null || true' EXIT
exit_on_adb_crash_signal & adb_crash_pid=$!
"$adb" shell am start -n "$id/.MainActivity" || exit $?


echo "polling for '$id' PID in adb"
# Additional timeout check to catch situation where app crashes before $pid is set
count=0
count_output=""
timeout=5
[[ -n "$CI" ]] && timeout=30
echo "App start timeout: $timeout"
echo ""
while [ -z "$pid" ] && (( count < timeout )); do
  count_output="$count_output$count..." # Ouput doesn't flush in CI with echo -n, rebuild line

  [[ -n "$CI" ]] && echo "$count..."
  [[ -z "$CI" ]] && echo -e "\e[1A\e[K$count_output" # Replace previous line, doesn't work in CI
  pid="$($adb shell ps | grep "$id" | awk '{print $2}' 2>/dev/null)"
  ## Probe for application process ID
  sleep 1
  (( count++ ))
done

if (( count >= timeout )); then
  echo "timeout exceeded, assuming app crashed."
  dump_runtime_error
fi

echo "# got process pid: $pid"

## Process logs from 'adb logcat'
watchdog_file_set
# kill $adb_crash_pid
while read -r line; do

  if echo "$line" | grep "E AndroidRuntime: FATAL EXCEPTION: main" | grep "$pid"; then
    # dump_runtime_error terminates
    echo "dump_runtime_error"
    dump_runtime_error
  fi

  if echo "$line" | grep 'Fatal signal'; then
    # dump_pid terminates
    echo "dump_pid"
    watchdog_file_update 1
    dump_pid "$pid"
  fi

  if [[ "$line" =~ [DEIW][[:space:]]Console[[:space:]]*: ]]; then
    line="$(echo "$line" | sed 's/.*[DEIW] Console *: *//')"
  elif [[ "$line" =~ chromium:.*\[INFO:CONSOLE:[0-9]+\] ]]; then
    line="$(echo "$line" | sed 's/.*\[INFO:CONSOLE:[0-9]*\] "//; s/", source: .*//')"
  else
    continue
  fi

  if [[ "$line" =~ __EXIT_SIGNAL__ ]]; then
    exit_signal="${line/__EXIT_SIGNAL__=/}"
    watchdog_file_update "$exit_signal"
    exit "$exit_signal"
  fi

  echo "$line"

  if [[ "$line" == "TAP version 13" ]]; then
    watchdog_file_update running
  fi

  if [[ "$line" == "# ok" ]]; then
    watchdog_file_update 0
    exit
  fi

  if [[ "$line" == "# fail" || "$line" == "# fail "* ]]; then
    watchdog_file_update 1
    exit 1
  fi
done < <($adb logcat --pid="$pid") & logcat_pid=$!

# Handle situation where logcat loop doesn't catch exit, because eg an unexpected error condition occurred that isn't handled by the text processing above
# This section dumps the entire process log after it has gone away, note that we don't want the entire log on failed tests that were successfully reported
count=0
timeout=600
[[ -z "$CI" ]] && timeout=30
timeout="${ORO_ANDROID_TEST_TIMEOUT_SECONDS:-$timeout}"
startup_timeout="${ORO_ANDROID_TEST_STARTUP_TIMEOUT_SECONDS:-120}"
[[ -z "$CI" ]] && echo "Waiting 30s before aborting tests..."
[[ -n "$CI" ]] && echo "Waiting 10m before aborting tests..."

# while [[ "$(watchdog_file_exists)" == "0" ]]; do
while (( count < timeout )) ; do
  if [[ "$(watchdog_file_exists)" != "0" ]]; then
    break
  fi

  if (( count >= startup_timeout )) && [[ ! -s "$poll_adb_watchdog_file" ]]; then
    echo "No TAP output after ${startup_timeout}s; dumping application logs."
    dump_pid "$pid" 124
  fi

  (( count > 0 )) && (( count % 30 == 0 )) && echo "Timeout count: $count/$timeout"
  sleep 1
  (( count++ ))
done

if [[ "$(watchdog_file_exists)" == "0" ]]; then
  echo "Timeout exceeded."
  dump_pid "$pid" 124
else
  exit_code="$(cat "$poll_adb_watchdog_file" 2>/dev/null)"
  if [[ "$exit_code" =~ ^[0-9]+$ ]]; then
    exit "$exit_code"
  fi
fi

echo "poll-adb-logcat.sh exiting without signal from adb loop"
[[ -n "$pid" ]] && dump_pid $pid
wait $logcat_pid
exit 254
