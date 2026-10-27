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

test('Linux queued pauses cannot suspend a restored loop or access a destroyed runtime', {
  skip: process.platform !== 'linux' || !compiler ? 'Linux and a C++20 compiler are required' : false
}, () => {
  const source = readFileSync(path.join(root, 'src/runtime/runtime.cc'), 'utf8')
  const destructor = source.slice(source.indexOf('  Runtime::~Runtime()'), source.indexOf('  void Runtime::init ()'))
  const methods = source.slice(source.indexOf('  bool Runtime::stop ()'), source.indexOf('  bool Runtime::destroy ()'))
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-linux-pause-dispatch-'))
  writeFileSync(path.join(directory, 'linux-pause-methods.hh'), destructor + methods)
  const executable = path.join(directory, 'replay')
  const built = spawnSync(compiler, [
    '-std=c++20', '-Wall', '-Werror', '-pthread', '-DDEBUG=1',
    '-I' + path.join(root, 'include'), '-I' + directory,
    path.join(root, 'test/fixtures/lifecycle-dispatch/LinuxPause.cc'), '-o', executable
  ], { encoding: 'utf8', timeout: 30000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  assert.match(result.stdout, /Linux queued lifecycle pauses cancel on restore and shutdown/)
})
