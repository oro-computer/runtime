import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'

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
