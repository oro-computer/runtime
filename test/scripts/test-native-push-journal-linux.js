import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const root = fileURLToPath(new URL('../../', import.meta.url))
assert.equal(process.platform, 'linux')
function run (program, args) {
  const result = spawnSync(program, args, {
    cwd: root,
    encoding: 'utf8',
    timeout: 120000,
    env: { ...process.env, XDG_RUNTIME_DIR: directory }
  })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  return result.stdout
}
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-push-journal-linux-'))
const libraries = path.join(root, `build/${process.arch === 'x64' ? 'x86_64' : process.arch}-desktop/lib`)
const flags = name => run('bash', [path.join(root, 'bin', name)]).trim().split(/\s+/).filter(Boolean)
const executable = path.join(directory, 'journal')
const archive = process.env.ORO_TEST_NATIVE_RUNTIME_ARCHIVE || path.join(libraries, 'liboro-runtime.a')
run(process.env.ORO_TEST_CXX || 'clang++', [...flags('cflags.sh'), '-I' + root,
  path.join(root, 'test/fixtures/native-records/PushJournalLinux.cc'), '-Wl,--start-group', archive,
  ...['libuv.a', 'libusb-1.0.a', 'libllama.a', 'libwhisper.a', 'libmbedtls.a', 'libmbedx509.a', 'libmbedcrypto.a'].map(name => path.join(libraries, name)),
  '-Wl,--end-group', ...flags('ldflags.sh'), '-o', executable])
console.log(run('dbus-run-session', ['--', executable, directory]).trim())
