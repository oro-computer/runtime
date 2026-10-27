// Real software-TPM interoperability; this does not qualify physical devices.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
assert.equal(process.platform, 'linux')
const directory = process.argv[2] === '--compile-only'
  ? path.resolve(process.argv[3])
  : fs.mkdtempSync(path.join(os.tmpdir(), 'oro-protected-keys-'))
fs.mkdirSync(directory, { recursive: true, mode: 0o700 })
const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
const libraries = path.join(root, 'build', `${architecture}-desktop/lib`)
const sodium = process.env.ORO_TEST_SODIUM_LIBRARY || path.join(libraries, 'libsodium.a')
const crypto = process.env.ORO_TEST_MBEDCRYPTO_LIBRARY || path.join(libraries, 'libmbedcrypto.a')
const compiler = process.env.ORO_TEST_CXX || 'clang++'
function run (program, args, options = {}) {
  const result = spawnSync(program, args, { cwd: root, encoding: 'utf8', timeout: 180000, ...options })
  assert.equal(result.status, 0, `${program}: ${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
  return result.stdout
}
run('bash', ['bin/fetch-pkcs11-header.sh'])
const flags = run('pkg-config', ['--cflags', '--libs', 'webkit2gtk-4.1']).trim().split(/\s+/)
const native = ['records', 'execution', 'pkcs11_backend', 'protected_keys', 'desktop_dialog', 'web_push']
run(compiler, ['-std=c++20', '-Wall', '-Werror', '-Wno-error=reorder-ctor', '-DORO_RUNTIME_ENABLE_MBEDTLS=1',
  '-I' + root, '-I' + path.join(root, 'include'), '-I' + path.join(root, 'build/include'),
  '-I' + (process.env.ORO_TEST_SODIUM_INCLUDE || path.join(root, 'build', `${architecture}-desktop/include`)),
  '-I' + (process.env.ORO_TEST_MBEDTLS_INCLUDE || path.join(root, 'build/include')),
  ...native.map(name => path.join(root, `src/runtime/native/${name}.cc`)),
  path.join(root, 'src/runtime/native/execution_signals.cc'),
  ...fs.readdirSync(path.join(root, 'src/runtime/json')).filter(name => name.endsWith('.cc')).map(name => path.join(root, 'src/runtime/json', name)),
  path.join(root, 'src/runtime/crypto/rand.cc'), path.join(root, 'src/runtime/string/string.cc'),
  path.join(root, 'test/fixtures/native-records/ProtectedKeys.cc'), sodium, crypto,
  process.env.ORO_TEST_SQLITE_LIBRARY || '-lsqlite3', '-ldl', '-pthread', ...flags, '-o', path.join(directory, 'protected-keys')])
run(compiler, ['-std=c++20', '-Wall', '-Werror', '-shared', '-fPIC', '-I' + path.join(root, 'build/include'),
  path.join(root, 'test/fixtures/native-records/Pkcs11Probe.cc'), '-ldl', '-pthread', '-o', path.join(directory, 'public-pkcs11.so')])
fs.chmodSync(path.join(directory, 'public-pkcs11.so'), 0o755)
if (process.argv[2] !== '--compile-only') {
  console.log(run('dbus-run-session', ['--', 'bash', path.join(root, 'test/fixtures/native-records/protected-keys-run.sh'), directory]).trim())
}
console.log('Public protected-key acceptance artifacts:', directory)
