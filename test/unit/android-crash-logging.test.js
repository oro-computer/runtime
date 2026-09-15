import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const source = readFileSync(
  new URL('../scripts/poll-adb-logcat.sh', import.meta.url),
  'utf8'
)
const dumpFunctions = source.slice(
  0,
  source.indexOf('\nfunction exit_on_adb_crash_signal')
)

for (const [command, status, filter] of [
  ['dump_runtime_error', 1, 'AndroidRuntime:E'],
  ['dump_pid 1234 7', 7, '--pid=1234']
]) {
  test(`Android crash logs finish and report failure: ${command}`, () => {
    const result = spawnSync('bash', ['-s'], {
      input: `${dumpFunctions}
        adb=mock_adb
        poll_adb_watchdog_file=watchdog
        mock_adb () {
          case " $* " in
            *" -d "*) printf 'crash details: %s\\n' "$*" ;;
            *) printf 'unexpected streaming logcat\\n'; return 99 ;;
          esac
        }
        kill () { printf 'unexpected signal\\n'; exit 99; }
        sleep () { printf 'unexpected delay\\n'; }
        watchdog_file_update () { printf 'watchdog=%s\\n' "$1"; }
        ${command}
      `,
      encoding: 'utf8',
      timeout: 2000
    })

    assert.equal(result.status, status, result.stderr)
    assert.match(result.stdout, /crash details: logcat -d -b all/)
    assert.ok(result.stdout.includes(filter), result.stdout)
    assert.ok(result.stdout.includes(`watchdog=${status}`), result.stdout)
    assert.doesNotMatch(result.stdout, /unexpected/)
  })
}

for (const [mode, status, expected] of [
  ['success', 0, /# ok/],
  ['failure', 1, /# fail 1/],
  ['chromium-success', 0, /# ok/],
  ['chromium-failure', 1, /# fail 1/],
  ['exit', 7, /got process pid: 1234/],
  ['startup-timeout', 124, /No TAP output after 1s/],
  ['test-timeout', 124, /Timeout exceeded/]
]) {
  test(`Android log polling reports ${mode}`, () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro-adb-poll-'))
    const adb = path.join(directory, 'adb')
    writeFileSync(adb, `#!/usr/bin/env bash
case "$*" in
  'shell am start '*) echo 'Starting activity' ;;
  'shell ps') echo 'u0_a123 1234 1 computer.oro.runtime.tests' ;;
  'logcat -d -b all --pid=1234') echo 'application diagnostics' ;;
  'logcat --pid=1234')
    case "$ORO_MOCK_ADB_MODE" in
      success) printf 'I Console : TAP version 13\\nI Console : # ok\\n' ;;
      failure) printf 'I Console : TAP version 13\\nI Console : # fail 1\\n' ;;
      chromium-success|chromium-failure)
        echo '09-15 20:17:13.335 5483 5483 I chromium: [INFO:CONSOLE:423] "TAP version 13", source: https://app.example/oro/console.js (423)'
        if [[ "$ORO_MOCK_ADB_MODE" == chromium-success ]]; then
          echo '09-15 20:17:13.335 5483 5483 I chromium: [INFO:CONSOLE:423] "# ok", source: https://app.example/oro/console.js (423)'
        else
          echo '09-15 20:17:13.335 5483 5483 I chromium: [INFO:CONSOLE:423] "# fail 1", source: https://app.example/oro/console.js (423)'
        fi
        ;;
      exit) echo 'I Console : __EXIT_SIGNAL__=7' ;;
      test-timeout) echo 'I Console : TAP version 13' ;;
    esac
    ;;
esac
`, { mode: 0o755 })
    const result = spawnSync('bash', [fileURLToPath(new URL('../scripts/poll-adb-logcat.sh', import.meta.url))], {
      env: {
        ...process.env,
        adb,
        CI: 'true',
        TMPDIR: directory,
        ORO_MOCK_ADB_MODE: mode,
        ORO_ANDROID_TEST_STARTUP_TIMEOUT_SECONDS: '1',
        ORO_ANDROID_TEST_TIMEOUT_SECONDS: '2'
      },
      encoding: 'utf8',
      timeout: 6000
    })
    assert.equal(result.status, status, `${result.stdout}\n${result.stderr}`)
    assert.match(result.stdout, expected)
    if (status === 124) {
      assert.match(result.stdout, /application diagnostics/)
    } else {
      assert.doesNotMatch(result.stdout, /application diagnostics/)
    }
  })
}
