import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const read = name => fs.readFileSync(path.join(root, name), 'utf8')
const ci = read('.github/workflows/ci.yml')

test('release tags require native validation even when the tagged commit has an open pull request', { skip: process.platform === 'win32' }, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-release-scope-'))
  const step = ci.split('      - name: Classify changed files\n')[1]
  const script = step.split('        run: |\n')[1].split(/\n {2}[a-z][a-z0-9-]+:\n/)[0]
    .split('\n').map(line => line.replace(/^ {10}/, '')).join('\n')
  const output = path.join(directory, 'output')
  const result = spawnSync('bash', ['-e', '-o', 'pipefail', '-c', 'gh () { echo api-called >&2; printf "1\\n"; }; ' + script], {
    encoding: 'utf8',
    env: {
      ...process.env,
      EVENT_NAME: 'push',
      REF_NAME: 'v0.2.0',
      REF_TYPE: 'tag',
      BEFORE_SHA: 'a'.repeat(40),
      CURRENT_SHA: 'b'.repeat(40),
      RUNNER_TEMP: directory,
      GITHUB_OUTPUT: output,
      GITHUB_STEP_SUMMARY: path.join(directory, 'summary'),
      GITHUB_REPOSITORY: 'public/example'
    }
  })
  assert.equal(result.status, 0, result.stderr)
  assert.equal(fs.readFileSync(output, 'utf8'), 'run_ci=true\nruntime_changed=true\n')
  assert.doesNotMatch(result.stderr, /api-called/)
})

test('the artifact source gate includes native and editor workflows without duplicate standalone triggers', () => {
  const release = read('.github/workflows/release-artifacts.yml')
  const verification = release.split('  verify-release:\n')[1].split('\n  build-release-artifacts:')[0]
  assert.match(verification, /uses: \.\/\.github\/workflows\/ci\.yml/)
  const build = release.split('  build-release-artifacts:\n')[1]
  assert.match(build, /needs: \[validate-release, verify-release\]/)
  for (const name of ['native-parity', 'language-server']) {
    const block = ci.split(`  ${name}:\n`)[1].split(/\n {2}[a-z][a-z-]+:\n/)[0]
    assert.match(block, /needs: \[changes, lint\]/)
    assert.match(block, new RegExp(`uses: \\.\\/\\.github\\/workflows\\/${name}\\.yml`))
    const source = read(`.github/workflows/${name}.yml`)
    assert.match(source, /^ {2}workflow_call:/m)
    assert.doesNotMatch(source, /^ {2}(push|pull_request):/m)
  }
})

test('native host fixtures reject a poisoned cache before invoking a compiler or downloading replacements', { skip: process.platform !== 'linux' }, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "oro-fixture cache's-"))
  fs.mkdirSync(path.join(directory, 'lib'))
  fs.writeFileSync(path.join(directory, 'lib/kotlin-compiler.jar'), 'invalid cached archive')
  const result = spawnSync(process.execPath, [path.join(root, 'bin/install-native-test-tools.js'), directory], { encoding: 'utf8', timeout: 10000 })
  assert.notEqual(result.status, 0)
  assert.match(result.stderr, /Fixture SHA-256 mismatch: kotlin-compiler-embeddable/)
  assert.equal(fs.readFileSync(path.join(directory, 'lib/kotlin-compiler.jar'), 'utf8'), 'invalid cached archive')
})
