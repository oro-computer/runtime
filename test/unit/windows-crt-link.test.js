import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const compiler = ['clang++-18', 'clang++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const linker = ['lld-link-18', 'lld-link'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

for (const debug of ['', '1']) {
  test(`Windows ${debug ? 'debug' : 'release'} DLL CRT links suppress Clang's static default`, {
    skip: !compiler || !linker ? 'Clang and lld-link are required' : false
  }, () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro-windows-crt-'))
    const source = path.join(directory, 'main.cc')
    writeFileSync(source, 'int main () { return 0; }\n')
    // Empty import archives isolate the driver's library selection from SDK availability.
    for (const library of ['msvcrt', 'msvcrtd', 'oldnames']) {
      writeFileSync(path.join(directory, `${library}.lib`), '!<arch>\n')
    }
    function flags (script) {
      const result = spawnSync('bash', ['-c', `
        uname () {
          case "$1" in
            -m) printf 'x86_64\\n' ;;
            *) printf 'MINGW64_NT\\n' ;;
          esac
        }
        pkg-config () { return 1; }
        export -f uname pkg-config
        bash "$1"
      `, 'windows-flags', path.join(root, 'bin', script)], {
        env: { ...process.env, DEBUG: debug, CXX: compiler, ORO_EXCLUDE_BUILD_METADATA: '1', WIN_DEBUG_LIBS: '' },
        encoding: 'utf8'
      })
      assert.equal(result.status, 0, result.stderr)
      return result.stdout.trim().split(/\s+/)
    }
    const compileFlags = flags('cflags.sh').filter(flag => flag.startsWith('-fms-runtime-lib='))
    const linkFlags = flags('ldflags.sh').filter(flag => flag.startsWith('-Wl,-NODEFAULTLIB:'))
    assert.deepEqual(compileFlags, [`-fms-runtime-lib=${debug ? 'dll_dbg' : 'dll'}`])
    assert.ok(linkFlags.length > 0)
    const args = [
      '--target=x86_64-pc-windows-msvc', `-fuse-ld=${linker}`,
      ...compileFlags, source, `-L${directory}`,
      '-Wl,-entry:main,-subsystem:console', '-o', path.join(directory, 'main.exe')
    ]
    const before = spawnSync(compiler, args, { encoding: 'utf8' })
    assert.notEqual(before.status, 0)
    assert.match(before.stderr, /libcmt\.lib/, 'the driver should reproduce the unwanted static CRT dependency')
    const after = spawnSync(compiler, [...args, ...linkFlags], { encoding: 'utf8' })
    assert.equal(after.status, 0, after.stderr)
  })
}
