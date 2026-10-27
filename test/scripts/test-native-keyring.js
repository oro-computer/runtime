// Linux OS-service acceptance, separate from portable fixture-key checks.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
assert.equal(process.platform, 'linux', 'This acceptance check requires Linux Secret Service')
const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
const libraries = path.join(root, 'build', `${architecture}-desktop/lib`)
const archive = process.env.ORO_TEST_NATIVE_RUNTIME_ARCHIVE || path.join(libraries, 'liboro-runtime.a')
assert.ok(fs.existsSync(archive), 'Rebuild the native runtime archive from current source first')
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-keyring-'))
const executable = path.join(directory, 'keyring-check')

function run (program, args, options = {}) {
  const result = spawnSync(program, args, { cwd: root, encoding: 'utf8', timeout: 120000, ...options })
  assert.equal(result.status, 0, `${program}: ${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
  return result.stdout
}

function flags (name) {
  return run('bash', [path.join(root, 'bin', name)]).trim().split(/\s+/).filter(Boolean)
}

run(process.env.ORO_TEST_CXX || 'clang++', [
  ...flags('cflags.sh'), '-I' + root, path.join(root, 'test/fixtures/native-records/LinuxKeyring.cc'),
  '-Wl,--start-group', archive,
  ...['libuv.a', 'libusb-1.0.a', 'libllama.a', 'libwhisper.a', 'libmbedtls.a', 'libmbedx509.a', 'libmbedcrypto.a']
    .map(name => path.join(libraries, name)),
  '-Wl,--end-group', ...flags('ldflags.sh'), '-o', executable
])

for (const name of ['data', 'config', 'run', 'control', 'state']) {
  fs.mkdirSync(path.join(directory, name), { mode: 0o700 })
}
const env = {
  ...process.env,
  XDG_DATA_HOME: path.join(directory, 'data'),
  XDG_CONFIG_HOME: path.join(directory, 'config'),
  XDG_RUNTIME_DIR: path.join(directory, 'run'),
  ORO_STATE_DIR: path.join(directory, 'state'),
  GNOME_KEYRING_CONTROL: path.join(directory, 'control')
}
// The private D-Bus and XDG directories isolate this public test keyring from
// the desktop session. No user keyring, account or credential is inspected.
const output = run('dbus-run-session', ['--', 'bash', '-c', `
set -euo pipefail
printf '%s' public-runtime-test-password | gnome-keyring-daemon --unlock --components=secrets --control-directory="$GNOME_KEYRING_CONTROL"
timeout 25 "$1" "$2" write
timeout 25 "$1" "$2" reopen
`, 'oro-keyring-check', executable, directory], { env })
assert.match(output, /linux-secret-service-native-records-passed/)
assert.match(output, /linux-secret-service-process-reopen-passed/)
const unavailable = run(executable, [directory, 'unavailable'], {
  env: { ...env, DBUS_SESSION_BUS_ADDRESS: 'unix:path=' + path.join(directory, 'unavailable-bus') },
  timeout: 30000
})
assert.match(unavailable, /linux-secret-service-unavailable-fails-closed/)
console.log(output.trim())
console.log(unavailable.trim())
console.log('Public acceptance artifacts:', directory)
