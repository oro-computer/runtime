import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const root = fileURLToPath(new URL('../../', import.meta.url))
const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
const include = process.env.ORO_TEST_SODIUM_INCLUDE || path.join(root, 'build', `${architecture}-desktop/include`)
const sodium = process.env.ORO_TEST_SODIUM_LIBRARY || path.join(root, 'build', `${architecture}-desktop/lib/libsodium.a`)
const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const available = compiler && fs.existsSync(sodium) && fs.existsSync(path.join(include, 'sodium.h'))

test('portable native records preserve encrypted CAS, migration, local receipts and interrupted outcomes', {
  skip: available || process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS ? false : 'Requires a native C++20 compiler and rebuilt libsodium headers/archive',
  timeout: 120000
}, () => {
  assert.ok(available, 'Required native compiler/libsodium prerequisites are unavailable')
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-portable-native-records-'))
  const executable = path.join(directory, process.platform === 'win32' ? 'records.exe' : 'records')
  const built = spawnSync(compiler, [
    '-std=c++20', '-Werror', '-Wall', '-I' + root, '-I' + path.join(root, 'include'),
    '-I' + path.join(root, 'build'), '-I' + include,
    path.join(root, 'src/runtime/native/records.cc'), path.join(root, 'test/fixtures/native-records/Regression.cc'),
    sodium, process.env.ORO_TEST_SQLITE_LIBRARY || '-lsqlite3', '-pthread', '-o', executable
  ], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stderr)
  function run (directory, mode, expected = 0) {
    const result = spawnSync(executable, [directory, mode], { encoding: 'utf8', timeout: 20000 })
    assert.equal(result.status, expected, mode + ': ' + result.stdout + result.stderr)
    return result.stdout
  }
  assert.match(run(path.join(directory, 'matrix'), 'matrix'), /native-records-matrix-passed/)
  const restart = path.join(directory, 'restart')
  assert.match(run(restart, 'seed'), /seeded-executing/)
  assert.match(run(restart, 'recover'), /recovered-without-replay/)
  const interrupted = path.join(directory, 'interrupted-key')
  run(interrupted, 'crash-key', 77)
  assert.match(run(interrupted, 'recover-key'), /recovered-reserved-key/)
  const reply = path.join(directory, 'lost-reply')
  run(reply, 'lost-reply', 78)
  assert.match(run(reply, 'read-reply'), /lost-reply-reconciled/)
})
