import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
test('native children retain shared capacity and ownership until join; cancellation and deadlines are event driven', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-children-'))
  const binary = path.join(directory, process.platform === 'win32' ? 'children.exe' : 'children')
  const sources = ['callbacks.cc', 'execution_signals.cc', 'execution_children.cc'].map(name => path.join(root, 'src/runtime/native', name))
  const built = spawnSync(process.env.ORO_TEST_CXX || 'clang++', ['-std=c++20', '-Wall', '-Werror', '-I' + root, '-I' + path.join(root, 'include'),
    ...sources, path.join(root, 'test/fixtures/native-records/Children.cc'), '-pthread', '-o', binary], { encoding: 'utf8', timeout: 30000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync(binary, [], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  assert.match(result.stdout, /native-event-cancellation-owned-children-shared-capacity-joined-cleanup-and-no-replay-passed/)
})
