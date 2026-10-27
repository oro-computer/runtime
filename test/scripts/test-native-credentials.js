import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawn, spawnSync } from 'node:child_process'
import { createInterface } from 'node:readline'

const root = fileURLToPath(new URL('../../', import.meta.url))
assert.equal(process.platform, 'linux')
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-linux-password-'))
const executable = path.join(directory, 'public-password')
const pinExecutable = path.join(directory, 'public-pin')
function run (program, args, options = {}) {
  const result = spawnSync(program, args, { encoding: 'utf8', timeout: 120000, cwd: root, ...options })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
  return result.stdout
}
const flags = run('pkg-config', ['--cflags', '--libs', 'gtk+-3.0']).trim().split(/\s+/).filter(Boolean)
run(process.env.ORO_TEST_CXX || 'clang++', ['-std=c++20', '-Wall', '-Werror', '-I' + root, '-I' + path.join(root, 'include'),
  path.join(root, 'test/fixtures/native-records/DesktopCredentials.cc'), path.join(root, 'src/runtime/native/desktop_credentials.cc'),
  path.join(root, 'src/runtime/native/desktop_dialog.cc'), path.join(root, 'src/runtime/native/execution_signals.cc'),
  ...flags, '-pthread', '-ldl', '-o', executable])
run(process.env.ORO_TEST_CXX || 'clang++', ['-std=c++20', '-Wall', '-Werror', '-I' + root, '-I' + path.join(root, 'include'),
  path.join(root, 'test/fixtures/native-records/DesktopPin.cc'), path.join(root, 'src/runtime/native/desktop_dialog.cc'), path.join(root, 'src/runtime/native/execution_signals.cc'),
  ...flags, '-pthread', '-o', pinExecutable])
for (const name of ['data', 'config', 'cache', 'run', 'control']) fs.mkdirSync(path.join(directory, name), { mode: 0o700 })
const xvfb = spawn('Xvfb', ['-displayfd', '1', '-screen', '0', '1024x768x24', '-nolisten', 'tcp'], { stdio: ['ignore', 'pipe', 'pipe'] })
const output = createInterface({ input: xvfb.stdout })
const log = []
xvfb.stderr.on('data', value => log.push(value.toString()))
try {
  const display = await new Promise((resolve, reject) => {
    output.on('line', value => { if (/^\d+$/.test(value)) resolve(':' + value) })
    xvfb.once('exit', code => reject(new Error(`Xvfb exited ${code}: ${log.join('')}`)))
  })
  const result = run('dbus-run-session', ['--', 'bash', '-c', `
set -euo pipefail
printf '%s' public-password-test | gnome-keyring-daemon --unlock --components=secrets --control-directory="$GNOME_KEYRING_CONTROL"
timeout 60 "$1"
timeout 30 "$2"
`, 'oro-password-test', executable, pinExecutable], {
    env: {
      ...process.env,
      DISPLAY: display,
      XDG_DATA_HOME: path.join(directory, 'data'),
      XDG_CONFIG_HOME: path.join(directory, 'config'),
      XDG_CACHE_HOME: path.join(directory, 'cache'),
      XDG_RUNTIME_DIR: path.join(directory, 'run'),
      GNOME_KEYRING_CONTROL: path.join(directory, 'control')
    }
  })
  assert.match(result, /linux-password-selection-filter-cancel-lifecycle-retry-passed/)
  assert.match(result, /linux-native-pin-mask-cancel-focus-protected-path-retry-passed/)
  console.log(result.trim())
  console.log('Public Linux password acceptance artifacts:', directory)
} finally { xvfb.kill('SIGTERM') }
