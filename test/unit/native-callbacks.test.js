import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
test('compiled native callbacks retain owned resources through cancellation and cleanup without replay', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-callbacks-'))
  const binary = path.join(directory, process.platform === 'win32' ? 'callbacks.exe' : 'callbacks')
  const compiler = process.env.ORO_TEST_CXX || 'clang++'
  const built = spawnSync(compiler, ['-std=c++20', '-Wall', '-Werror', '-I' + root, '-I' + path.join(root, 'include'),
    path.join(root, 'src/runtime/native/callbacks.cc'), path.join(root, 'src/runtime/native/execution_signals.cc'), path.join(root, 'src/runtime/native/execution_children.cc'), path.join(root, 'test/fixtures/native-records/Callbacks.cc'),
    '-pthread', '-o', binary], { encoding: 'utf8', timeout: 30000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync(binary, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  assert.match(result.stdout, /native-callback-authorization-capacity-cancellation-cleanup-and-no-replay-passed/)
})
