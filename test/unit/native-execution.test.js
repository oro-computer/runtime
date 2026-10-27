import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const root = fileURLToPath(new URL('../../', import.meta.url))
const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
const include = process.env.ORO_TEST_SODIUM_INCLUDE || path.join(root, 'build', `${architecture}-desktop/include`)
const sodium = process.env.ORO_TEST_SODIUM_LIBRARY || path.join(root, 'build', `${architecture}-desktop/lib/libsodium.a`)
const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const available = compiler && fs.existsSync(sodium) && fs.existsSync(path.join(include, 'sodium.h'))

test('native execution fences admission, deadlines, cleanup and durable uncertain outcomes', {
  skip: available || process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS ? false : 'Requires C++20, libsodium and SQLite',
  timeout: 120000
}, () => {
  assert.ok(available)
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-execution-'))
  const binary = path.join(directory, process.platform === 'win32' ? 'execution.exe' : 'execution')
  const compiled = spawnSync(compiler, ['-std=c++20', '-Wall', '-Werror', '-I' + root,
    '-I' + path.join(root, 'include'), '-I' + path.join(root, 'build'), '-I' + include,
    path.join(root, 'src/runtime/native/records.cc'), path.join(root, 'src/runtime/native/execution.cc'), path.join(root, 'src/runtime/native/execution_signals.cc'),
    path.join(root, 'test/fixtures/native-records/Execution.cc'), sodium,
    process.env.ORO_TEST_SQLITE_LIBRARY || '-lsqlite3', '-pthread', '-o', binary
  ], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync(binary, [directory], { encoding: 'utf8', timeout: 20000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /native-execution-cancellation-quarantine-restart-passed/)
})
