import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'

const builder = readFileSync(new URL('../../bin/build-runtime-library.sh', import.meta.url), 'utf8')
const launcher = builder.match(/declare runtime_compiler_launcher=""[\s\S]*?(?=declare newest_header_mtime)/)?.[0]
assert.ok(launcher, 'the runtime compiler launcher must be present')

for (const platform of ['android', 'iPhoneOS', 'iPhoneSimulator']) {
  test(`${platform} caches individual compiler invocations without splitting paths`, () => {
    const result = spawnSync('bash', ['-s'], {
      encoding: 'utf8',
      input: `
        host=Darwin
        platform=${platform}
        syntax_only=0
        VERBOSE=1
        ccache() { printf 'argument=<%s>\\n' "$@"; }
        ${launcher}
        run_runtime_compiler '/SDK with spaces/clang++' -Os -c 'source with spaces.cc' -o 'output with spaces.o'
      `
    })
    assert.equal(result.status, 0, result.stderr)
    assert.ok(result.stdout.endsWith([
      'argument=</SDK with spaces/clang++>',
      'argument=<-Os>',
      'argument=<-c>',
      'argument=<source with spaces.cc>',
      'argument=<-o>',
      'argument=<output with spaces.o>',
      ''
    ].join('\n')))
  })
}

test('cached Apple compiler errors retain their diagnostic and exit code', () => {
  const result = spawnSync('bash', ['-s'], {
    encoding: 'utf8',
    input: `
      host=Darwin
      platform=iPhoneOS
      syntax_only=0
      VERBOSE=''
      ccache() { echo 'compiler diagnostic' >&2; return 23; }
      ${launcher}
      run_runtime_compiler '/SDK/clang++' -c source.cc -o source.o
    `
  })
  assert.equal(result.status, 23)
  assert.equal(result.stderr.trim(), 'compiler diagnostic')
})
