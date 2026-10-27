import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, copyFileSync, existsSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const compiler = ['clang++-18', 'clang++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('Windows source checks find complete libsodium headers before MSBuild runs', {
  skip: !compiler ? 'Clang with Windows target support and Bash are required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-windows-sodium-'))
  const source = path.join(directory, 'build/libsodium')
  const headers = path.join(source, 'src/libsodium/include')
  const installed = path.join(directory, 'build/x86_64-desktop/include')
  for (const folder of ['bin', 'build/libsodium/src/libsodium/include/sodium', 'build/libsodium/builds/msvc']) {
    mkdirSync(path.join(directory, folder), { recursive: true })
  }
  copyFileSync(path.join(root, 'bin/cflags.sh'), path.join(directory, 'bin/cflags.sh'))
  // A fresh Git checkout has a version template; MSBuild supplies its concrete
  // header in a pre-build event, after the runtime's source check would run.
  writeFileSync(path.join(headers, 'sodium.h'), '#include "sodium/version.h"\n')
  writeFileSync(path.join(headers, 'sodium/version.h.in'), '#define SODIUM_VERSION_STRING "@VERSION@"\n')
  const version = '#define SODIUM_VERSION_STRING "fixture-version"\n'
  writeFileSync(path.join(source, 'builds/msvc/version.h'), version)
  writeFileSync(path.join(directory, 'probe.cc'), '#include <sodium.h>\nstatic_assert(sizeof(SODIUM_VERSION_STRING) > 1);\n')

  function compile () {
    const flags = spawnSync('bash', ['bin/cflags.sh'], {
      cwd: directory,
      env: { ...process.env, TARGET_HOST: 'Win32', ARCH: 'x86_64', ORO_EXCLUDE_BUILD_METADATA: '1' },
      encoding: 'utf8'
    })
    assert.equal(flags.status, 0, flags.stderr)
    return spawnSync(compiler, [
      '--target=x86_64-pc-windows-msvc', '-std=c++20', '-fsyntax-only',
      ...flags.stdout.trim().split(/\s+/).filter(flag => flag.startsWith('-I') || flag === '-DSODIUM_STATIC'),
      path.join(directory, 'probe.cc')
    ], { encoding: 'utf8', timeout: 10000 })
  }
  const before = compile()
  assert.notEqual(before.status, 0)
  assert.match(before.stderr, /sodium\.h.*file not found/)

  const installer = readFileSync(path.join(root, 'bin/install.sh'), 'utf8')
  const start = installer.indexOf('function _stage_libsodium_headers {')
  const end = installer.indexOf('\nfunction _compile_libsodium {', start)
  assert.ok(start >= 0 && end > start)
  const stage = spawnSync('bash', ['-s', '--', source, installed], {
    input: `set -e\nhost=Win32\ndie () { if (( $1 != 0 )); then echo "$2" >&2; exit "$1"; fi; }\n${installer.slice(start, end)}\n_stage_libsodium_headers "$1" "$2"\n`,
    encoding: 'utf8'
  })
  assert.equal(stage.status, 0, stage.stderr)
  assert.equal(readFileSync(path.join(installed, 'sodium/version.h'), 'utf8'), version)
  assert.equal(existsSync(path.join(headers, 'sodium/version.h')), false, 'source checkout stays unmodified')
  const after = compile()
  assert.equal(after.status, 0, after.stderr)

  assert.match(installer, /_stage_libsodium_headers "\$BUILD_DIR\/libsodium" "\$BUILD_DIR\/\$\(host_arch\)-desktop\/include"/)
  assert.match(installer, /_stage_libsodium_headers "\$staging_dir" "\$BUILD_DIR\/\$target-\$platform\/include"/)
})
