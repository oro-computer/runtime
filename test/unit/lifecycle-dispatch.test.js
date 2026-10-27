import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

function methods (file, start, end) {
  const source = readFileSync(path.join(root, file), 'utf8')
  const first = source.indexOf(start)
  const last = source.indexOf(end, first)
  assert.ok(first >= 0 && last > first)
  return source.slice(first, last)
}

test('lifecycle dispatch ignores superseded requests and repeated pauses never stop services on a paused loop', {
  skip: !compiler ? 'A C++20 compiler is required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-lifecycle-dispatch-'))
  writeFileSync(path.join(directory, 'lifecycle-methods.hh'), [
    methods('src/runtime/runtime.cc', '  bool Runtime::resume ()', '  bool Runtime::destroy ()'),
    methods('src/runtime/app/app.cc', '  void App::resume ()', '  void App::stop ()')
  ].join('\n'))
  // Compile the synchronous desktop and mobile paths with test service facades.
  // This verifies shared state transitions without claiming native OS coverage.
  for (const desktop of [0, 1]) {
    const executable = path.join(directory, `replay-${desktop}${process.platform === 'win32' ? '.exe' : ''}`)
    const compiled = spawnSync(compiler, [
      '-std=c++20', '-Wall', '-Werror',
      '-DORO_RUNTIME_PLATFORM=1', `-DORO_RUNTIME_PLATFORM_DESKTOP=${desktop}`,
      '-I' + path.join(root, 'include'), '-I' + directory,
      path.join(root, 'test/fixtures/lifecycle-dispatch/Replay.cc'), '-o', executable
    ], { encoding: 'utf8', timeout: 30000 })
    assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
    const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 })
    assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  }
})
