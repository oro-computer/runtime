import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const arch = process.arch === 'x64' ? 'x86_64' : process.arch
const include = process.env.ORO_TEST_SODIUM_INCLUDE || path.join(root, `build/${arch}-desktop/include`)
const sodium = process.env.ORO_TEST_SODIUM_LIBRARY || path.join(root, `build/${arch}-desktop/lib/libsodium.a`)
const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const available = compiler && fs.existsSync(sodium) && fs.existsSync(path.join(include, 'sodium.h'))

test('native push journal preserves ciphertext and fences explicit installation recovery, retries and failed switches', {
  skip: available || process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS ? false : 'Requires a native C++20 compiler and rebuilt libsodium headers/archive',
  timeout: 120000
}, () => {
  assert.ok(available, 'Required native compiler/libsodium prerequisites are unavailable')
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-push-journal-'))
  const executable = path.join(directory, process.platform === 'win32' ? 'journal.exe' : 'journal')
  const built = spawnSync(compiler, ['-std=c++20', '-Wall', '-Werror', '-I' + root,
    '-I' + path.join(root, 'include'), '-I' + include, path.join(root, 'src/runtime/native/push_journal.cc'),
    path.join(root, 'test/fixtures/native-records/PushJournal.cc'), sodium,
    process.env.ORO_TEST_SQLITE_LIBRARY || '-lsqlite3', '-pthread', '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync(executable, [directory], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /push-journal-consent-ciphertext-retention-stale-retry-rollback-and-installation-fences-passed/)
})
