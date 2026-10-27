import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const root = fileURLToPath(new URL('../../', import.meta.url))
assert.equal(process.platform, 'linux')
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-linux-service-'))
const executable = path.join(directory, 'public-service')
const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
const libraries = path.join(root, 'build', `${architecture}-desktop/lib`)
const archive = process.env.ORO_TEST_NATIVE_RUNTIME_ARCHIVE || path.join(libraries, 'liboro-runtime.a')
function run (program, args, options = {}) {
  const result = spawnSync(program, args, { encoding: 'utf8', timeout: 120000, cwd: root, ...options })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
  return result.stdout
}
const flags = name => run('bash', [path.join(root, 'bin', name)]).trim().split(/\s+/).filter(Boolean)
run(process.env.ORO_TEST_CXX || 'clang++', [...flags('cflags.sh'), '-I' + root,
  path.join(root, 'test/fixtures/native-records/DesktopService.cc'), '-Wl,--start-group', archive,
  ...['libuv.a', 'libusb-1.0.a', 'libllama.a', 'libwhisper.a', 'libmbedtls.a', 'libmbedx509.a', 'libmbedcrypto.a'].map(name => path.join(libraries, name)),
  '-Wl,--end-group', ...flags('ldflags.sh'), '-o', executable])
for (const name of ['data', 'config', 'cache', 'run', 'control', 'state']) fs.mkdirSync(path.join(directory, name), { mode: 0o700 })
const services = path.join(directory, 'data/dbus-1/services')
fs.mkdirSync(services, { recursive: true })
fs.writeFileSync(path.join(services, 'org.oro.PublicService.NativeRuntime.service'), `[D-BUS Service]\nName=org.oro.PublicService.NativeRuntime\nExec=${executable} host\n`)
const result = run('dbus-run-session', ['--', 'bash', '-c', `
set -euo pipefail
printf '%s' public-service-test | gnome-keyring-daemon --unlock --components=secrets --control-directory="$GNOME_KEYRING_CONTROL"
trap 'if [[ -f "$2/host.pid" ]]; then kill -TERM "$(cat "$2/host.pid")" 2>/dev/null || true; fi' EXIT
timeout 45 "$1"
if gdbus call --session --dest org.oro.PublicService.NativeRuntime --object-path /dev/oro/NativeRuntime --method dev.oro.NativeRuntime1.Probe; then
  exit 1
fi
`, 'oro-native-host-check', executable, path.join(directory, 'state')], {
  env: {
    ...process.env,
    XDG_DATA_HOME: path.join(directory, 'data'),
    XDG_CONFIG_HOME: path.join(directory, 'config'),
    XDG_CACHE_HOME: path.join(directory, 'cache'),
    XDG_RUNTIME_DIR: path.join(directory, 'run'),
    ORO_STATE_DIR: path.join(directory, 'state'),
    GNOME_KEYRING_CONTROL: path.join(directory, 'control'),
    G_MESSAGES_DEBUG: 'OroRuntimeNative'
  }
})
assert.match(result, /linux-cold-native-service-authentication-cancellation-replay-and-scope-passed/)
console.log(result.trim())
console.log('Public Linux cold service acceptance artifacts:', directory)
