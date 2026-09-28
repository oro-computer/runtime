import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import vm from 'node:vm'

const script = readFileSync(new URL('../../bin/publish-npm-modules.sh', import.meta.url), 'utf8')
const guard = script.slice(
  script.indexOf('if [[ "$npm_staging_root" != "$expected_npm_staging_root" ]]'),
  script.indexOf('\nwhile (( $# > 0 )); do')
)
assert.ok(guard.includes('npm staging home must be'))

function resolvePath (name, value, paths, existing = new Map()) {
  const body = script.match(new RegExp(`function ${name}\\(\\) \\{\\s+node -e '([\\s\\S]*?)' "\\$1"`))?.[1]
  assert.ok(body, `${name} must contain its path resolver`)
  let output = ''
  const fs = {
    existsSync: filename => existing.has(filename) || paths.dirname(filename) === filename,
    realpathSync: { native: filename => existing.get(filename) || filename }
  }
  vm.runInNewContext(body, {
    require: name => name === 'node:path' ? paths : fs,
    process: { argv: ['node', value], stdout: { write: value => { output += value } } }
  })
  return output
}

function checkGuard (paths, { root, staging, temporary, home, existing = new Map() }) {
  const canonical = value => resolvePath('resolve_removal_path', value, paths, existing)
  const npmRoot = paths.join(root, 'build', 'npm')
  return spawnSync('bash', ['-c', `
    npm_staging_root="$1"
    expected_npm_staging_root="$2"
    repository_root="$3"
    temporary_root="$4"
    home_root="$5"
    ORO_HOME="$6"
    temporary_staging_home=0
    ${guard}
    exit 0
  `, 'staging-guard', canonical(npmRoot), resolvePath('resolve_absolute_path', npmRoot, paths),
  canonical(root), canonical(temporary), canonical(home), canonical(staging)], {
    encoding: 'utf8',
    timeout: 10000
  })
}

for (const [name, paths, root, temporary, home] of [
  ['Windows', path.win32, 'D:\\a\\runtime\\runtime', 'C:\\Users\\runner\\Temp', 'C:\\Users\\runner'],
  ['POSIX', path.posix, '/work/runtime', '/private/tmp', '/home/runner']
]) {
  const defaults = { root, temporary, home }
  test(`${name} staging accepts repository and temporary children with spaces`, () => {
    for (const staging of [paths.join(root, 'build/npm/win32-x64'), paths.join(temporary, 'npm staging')]) {
      const result = checkGuard(paths, { ...defaults, staging })
      assert.equal(result.status, 0, result.stderr)
    }
  })

  test(`${name} staging rejects roots, siblings, parent traversal, and unsafe temporary roots`, () => {
    for (const options of [
      { staging: root },
      { staging: home },
      { staging: temporary },
      { staging: paths.join(root, 'build/npm-other') },
      { staging: paths.join(root, 'build/npm/../../outside') },
      { staging: paths.join(root, 'outside'), temporary: root },
      { staging: paths.join(home, 'outside'), temporary: home },
      { staging: paths.join(paths.parse(root).root, 'outside'), temporary: paths.parse(root).root }
    ]) {
      const result = checkGuard(paths, { ...defaults, ...options })
      assert.equal(result.status, 1, JSON.stringify(options))
    }
  })

  test(`${name} staging rejects symlink escapes, including nonexistent children`, () => {
    const npmRoot = paths.join(root, 'build/npm')
    const link = paths.join(npmRoot, 'link')
    const outside = paths.join(root, 'outside')
    for (const [existing, staging] of [
      [new Map([[npmRoot, outside]]), paths.join(npmRoot, 'package')],
      [new Map([[link, outside]]), paths.join(link, 'new', 'package')]
    ]) {
      const result = checkGuard(paths, { ...defaults, existing, staging })
      assert.equal(result.status, 1, result.stderr)
    }
  })
}

test('POSIX path normalization preserves literal backslashes', () => {
  const filename = '/private/tmp/npm\\package'
  assert.equal(resolvePath('resolve_removal_path', filename, path.posix), filename)
})

test('package version checks accept CRLF and reject mismatches and CLI failures', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-package-version-'))
  mkdirSync(path.join(directory, 'bin'))
  writeFileSync(path.join(directory, 'VERSION.txt'), '0.1.4\r\n')
  const cli = path.join(directory, 'bin/oroc')
  const versionGuard = script.split('# Confirm that build CLI matches current commit\n')[1]
    .split('\ndeclare android_abis=')[0]
  for (const [output, expected] of [
    ['printf "0.1.4 (ee38c00c)\\r\\n"', 0],
    ['printf "0.1.4 (ee38c00c)\\n"', 0],
    ['printf "0.1.3 (ee38c00c)\\r\\n"', 1],
    ['exit 23', 23]
  ]) {
    writeFileSync(cli, `#!/usr/bin/env bash\n${output}\n`, { mode: 0o755 })
    const result = spawnSync('bash', ['-c', `
      root="$1"
      ORO_HOME="$1"
      platform=linux
      only_top_level=0
      ABORT_ERRORS=0
      git() { echo ee38c00c; }
      ${versionGuard}
      exit "$ABORT_ERRORS"
    `, 'version-guard', directory.split(path.sep).join('/')], {
      encoding: 'utf8',
      timeout: 10000
    })
    assert.equal(result.status, expected, result.stdout + result.stderr)
  }
})
