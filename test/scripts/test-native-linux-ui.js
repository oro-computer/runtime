import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawn, spawnSync } from 'node:child_process'
import { createInterface } from 'node:readline'

const root = fileURLToPath(new URL('../../', import.meta.url))
assert.equal(process.platform, 'linux')
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-linux-native-ui-'))
const executable = path.join(directory, 'public-ui')
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
  path.join(root, 'test/fixtures/native-records/LinuxWebView.cc'), '-Wl,--start-group', archive,
  ...['libuv.a', 'libusb-1.0.a', 'libllama.a', 'libwhisper.a', 'libmbedtls.a', 'libmbedx509.a', 'libmbedcrypto.a'].map(name => path.join(libraries, name)),
  '-Wl,--end-group', ...flags('ldflags.sh'), '-o', executable])
for (const name of ['data', 'config', 'cache', 'run', 'state', 'resources']) fs.mkdirSync(path.join(directory, name), { mode: 0o700 })
fs.cpSync(path.join(root, 'api'), path.join(directory, 'resources/oro'), { recursive: true })
fs.copyFileSync(path.join(root, 'test/fixtures/native-records/public-ui.html'), path.join(directory, 'resources/index.html'))
fs.copyFileSync(path.join(root, 'test/fixtures/native-records/public-frame.html'), path.join(directory, 'resources/public-frame.html'))
const xvfb = spawn('Xvfb', ['-displayfd', '1', '-screen', '0', '1024x768x24', '-nolisten', 'tcp'], { stdio: ['ignore', 'pipe', 'pipe'] })
const output = createInterface({ input: xvfb.stdout })
const log = []
xvfb.stderr.on('data', value => log.push(value.toString()))
try {
  const display = await new Promise((resolve, reject) => {
    output.on('line', value => { if (/^\d+$/.test(value)) resolve(':' + value) })
    xvfb.once('exit', code => reject(new Error(`Xvfb exited ${code}: ${log.join('')}`)))
  })
  const result = run('dbus-run-session', ['--', executable, path.join(directory, 'resources')], {
    env: {
      ...process.env,
      DISPLAY: display,
      XDG_DATA_HOME: path.join(directory, 'data'),
      XDG_CONFIG_HOME: path.join(directory, 'config'),
      XDG_CACHE_HOME: path.join(directory, 'cache'),
      XDG_RUNTIME_DIR: path.join(directory, 'run'),
      ORO_STATE_DIR: path.join(directory, 'state'),
      WEBKIT_DISABLE_DMABUF_RENDERER: '1',
      WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS: '1'
    }
  })
  assert.match(result, /linux-webkit-isolated-native-ipc-cancellation-reload-passed/)
  assert.match(result, /linux-webkit-process-loss-user-recovery-passed/)
  console.log(result.trim())
  console.log('Public Linux WebKit acceptance artifacts:', directory)
} finally { xvfb.kill('SIGTERM') }
