import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++'].filter(Boolean).find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const libraries = spawnSync('pkg-config', ['--cflags', '--libs', 'glib-2.0', 'libuv'], { encoding: 'utf8' })
const available = process.platform === 'linux' && compiler && libraries.status === 0

test('Linux pause keeps GTK responsive with a readable libuv backend and restores queued work', {
  skip: process.platform !== 'linux' || (!available && !process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS)
    ? 'Linux, a C++20 compiler, GLib and libuv are required'
    : false
}, () => {
  assert.ok(available, 'Linux, a C++20 compiler, GLib and libuv are required')
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-linux-loop-pause-'))
  for (const [filename, output] of [
    ['src/runtime/loop.hh', 'loop-header.hh'],
    ['src/runtime/loop/loop.cc', 'loop-methods.hh']
  ]) {
    writeFileSync(path.join(directory, output), readFileSync(path.join(root, filename), 'utf8').replace(/^#include ".*"\n/gm, ''))
  }
  const executable = path.join(directory, 'replay')
  const compiled = spawnSync(compiler, [
    '-std=c++20', '-Wall', '-Werror', '-pthread',
    '-I' + path.join(root, 'include'), '-I' + directory,
    path.join(root, 'test/fixtures/lifecycle-dispatch/LinuxLoop.cc'),
    ...libraries.stdout.trim().split(/\s+/), '-o', executable
  ], { encoding: 'utf8', timeout: 30000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  assert.match(result.stdout, /Linux paused and stopped loops preserve GTK progress and resume work/)
})
