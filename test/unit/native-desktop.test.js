import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const compiler = process.env.ORO_TEST_CXX || 'clang++'
const flags = spawnSync('pkg-config', ['--cflags', '--libs', 'gio-2.0'], { encoding: 'utf8' })
const available = process.platform === 'linux' && flags.status === 0 &&
  spawnSync(compiler, ['--version'], { stdio: 'ignore' }).status === 0

test('Linux native notifications use real isolated D-Bus calls, owner-bound actions and explicit sound hints', {
  skip: process.platform !== 'linux' || (!available && !process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS),
  timeout: 120000
}, () => {
  assert.ok(available, 'Requires Linux, C++20, GLib development files and dbus-run-session')
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-notifications-'))
  const binary = path.join(directory, 'notifications')
  const compiled = spawnSync(compiler, ['-std=c++20', '-Wall', '-Werror', '-I' + root, '-I' + path.join(root, 'include'),
    path.join(root, 'src/runtime/native/desktop_bus.cc'), path.join(root, 'src/runtime/native/desktop_notifications.cc'),
    path.join(root, 'test/fixtures/native-records/DesktopNotifications.cc'),
    ...flags.stdout.trim().split(/\s+/), '-pthread', '-o', binary
  ], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  fs.writeFileSync(path.join(directory, 'server.log'), '')
  const result = spawnSync('dbus-run-session', ['--', 'bash', '-c', `
set -e
"$1" server > "$2" 2>&1 &
notification_server=$!
trap 'kill "$notification_server" 2>/dev/null || true' EXIT
for attempt in {1..200}; do
  if [[ $(< "$2") == *notification-mock-ready* ]]; then break; fi
  sleep 0.02
done
"$1" client
wait "$notification_server"
`, 'oro-notification-test', binary, path.join(directory, 'server.log')], { encoding: 'utf8', timeout: 20000 })
  assert.equal(result.status, 0, result.stdout + result.stderr + fs.readFileSync(path.join(directory, 'server.log'), 'utf8'))
  assert.match(result.stdout, /desktop-notification-dbus-ownership-sound-and-actions-passed/)
})
