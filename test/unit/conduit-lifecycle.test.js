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
const libuv = spawnSync('pkg-config', ['--cflags', '--libs', 'libuv'], { encoding: 'utf8' })
const available = compiler && libuv.status === 0 && process.platform !== 'win32'

function read (filename) {
  return readFileSync(path.join(root, filename), 'utf8').replace(/^#include ".*"\n/gm, '')
}

test('Lifecycle services stop without waiting on their loop and Conduit closes before restart', {
  skip: process.platform === 'win32' || (!available && !process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS) ? 'A Unix C++20 compiler and libuv development files are required' : false
}, () => {
  assert.ok(available, 'A Unix C++20 compiler and pkg-config libuv are required')
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-conduit-lifecycle-'))
  writeFileSync(path.join(directory, 'loop-header.hh'), read('src/runtime/loop.hh'))
  writeFileSync(path.join(directory, 'loop-methods.hh'), read('src/runtime/loop/loop.cc'))
  writeFileSync(path.join(directory, 'conduit-header.hh'), read('src/runtime/core/services/conduit.hh'))
  const conduit = read('src/runtime/core/services/conduit.cc')
  const terminate = conduit.slice(conduit.indexOf('  struct ClientCloseContext'), conduit.indexOf('  void Conduit::Client::close ('))
  writeFileSync(path.join(directory, 'conduit-methods.hh'), `namespace oro::runtime::core::services {\n${terminate}\n${conduit.slice(conduit.indexOf('  bool Conduit::start ()'))}`)
  writeFileSync(path.join(directory, 'timers-header.hh'), read('src/runtime/core/services/timers.hh'))
  writeFileSync(path.join(directory, 'timers-methods.hh'), read('src/runtime/core/services/timers.cc'))
  const fs = read('src/runtime/core/services/fs.cc')
  const udp = read('src/runtime/core/services/udp.cc')
  const tcp = read('src/runtime/core/services/tcp.hh')
  writeFileSync(path.join(directory, 'service-stop-methods.hh'), [
    'namespace oro::runtime::core::services {',
    fs.slice(fs.indexOf('  bool FS::stop ()'), fs.indexOf('  JSON::Object getStatsJSON')),
    udp.slice(udp.indexOf('  bool UDP::stop ()'), udp.indexOf('  void UDP::capabilities')),
    tcp.slice(tcp.indexOf('      bool stop () override'), tcp.indexOf('      // API surface'))
      .replace('bool stop () override', 'bool TCP::stop ()'),
    '}'
  ].join('\n'))
  const executable = path.join(directory, 'replay')
  const platformFlags = process.platform === 'darwin' ? ['-fblocks', '-DORO_TEST_APPLE_LOOP=1'] : []
  const compiled = spawnSync(compiler, [
    '-std=c++20', '-Wall', '-pthread', ...platformFlags, '-I' + directory,
    path.join(root, 'test/fixtures/conduit-lifecycle/Replay.cc'),
    ...libuv.stdout.trim().split(/\s+/), '-o', executable
  ], { encoding: 'utf8', timeout: 30000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
})
