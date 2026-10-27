import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
test('native service strings round-trip every scalar and NUL and reject malformed Unicode', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-unicode-'))
  const binary = path.join(directory, process.platform === 'win32' ? 'unicode.exe' : 'unicode')
  const built = spawnSync(process.env.ORO_TEST_CXX || 'clang++', ['-std=c++20', '-O2', '-Wall', '-Werror', '-I' + root, '-I' + path.join(root, 'include'),
    path.join(root, 'src/runtime/native/unicode.cc'), path.join(root, 'test/fixtures/native-records/Unicode.cc'), '-o', binary], { encoding: 'utf8', timeout: 30000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync(binary, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  assert.match(result.stdout, /native-utf-all-scalars-nul-supplementary-malformed-and-bounds-passed/)
})
