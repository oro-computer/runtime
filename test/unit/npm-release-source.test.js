import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const workflow = readFileSync(new URL('../../.github/workflows/publish-npm.yml', import.meta.url), 'utf8')
const step = workflow.split('      - name: Resolve immutable source and enforce publication signature\n')[1]
const script = step.split('        run: |\n')[1].split('\n      - name:')[0]
  .split('\n').map(line => line.replace(/^ {10}/, '')).join('\n')
const commit = 'a'.repeat(40)
const hasTools = ['bash', 'jq'].every(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

const cases = [
  { name: 'preflight accepts an immutable SHA', ref: commit, publish: false, ok: true, api: false },
  { name: 'publishing rejects an unsigned SHA', ref: commit, publish: true, ok: false, api: false },
  { name: 'preflight rejects a moving branch', ref: 'master', publish: false, ok: false, api: false },
  { name: 'preflight rejects a shortened SHA', ref: commit.slice(0, 8), publish: false, ok: false, api: false },
  { name: 'publishing rejects a mismatched version', ref: 'v9.9.9', publish: true, ok: false, api: false },
  { name: 'publishing rejects a branch invocation', ref: 'v1.2.3', publish: true, githubRef: 'refs/heads/master', ok: false, api: false },
  { name: 'publishing rejects a lightweight tag', ref: 'v1.2.3', publish: true, objectType: 'commit', ok: false },
  { name: 'publishing rejects an unverified signature', ref: 'v1.2.3', publish: true, verified: false, ok: false },
  { name: 'publishing rejects a tag pointing to a tag', ref: 'v1.2.3', publish: true, targetType: 'tag', ok: false },
  { name: 'publishing rejects an invalid target SHA', ref: 'v1.2.3', publish: true, targetSha: 'invalid', ok: false },
  { name: 'publishing resolves a verified signed tag', ref: 'v1.2.3', publish: true, ok: true },
  { name: 'manual inspection resolves a verified signed tag', ref: 'v1.2.3', publish: false, ok: true }
]

for (const scenario of cases) {
  test(scenario.name, { skip: !hasTools ? 'Bash and jq are required' : false }, () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro-release-source-'))
    const output = path.join(directory, 'output')
    // Substitute only GitHub's responses; execute the actual workflow guard.
    const mock = `
      gh () {
        echo api-called >&2
        case "$2" in
          */git/ref/tags/*) printf '%s' "$REF_JSON" ;;
          */git/tags/*) printf '%s' "$TAG_JSON" ;;
          *) return 1 ;;
        esac
      }
    `
    const result = spawnSync('bash', ['-e', '-o', 'pipefail', '-c', mock + script], {
      encoding: 'utf8',
      env: {
        ...process.env,
        RELEASE_REF: scenario.ref,
        RELEASE_VERSION: '1.2.3',
        PUBLISH: String(scenario.publish),
        GITHUB_REF: scenario.githubRef || 'refs/tags/v1.2.3',
        GITHUB_REPOSITORY: 'oro-computer/runtime',
        GITHUB_OUTPUT: output,
        REF_JSON: JSON.stringify({ object: { type: scenario.objectType || 'tag', sha: 'b'.repeat(40) } }),
        TAG_JSON: JSON.stringify({
          verification: { verified: scenario.verified !== false, reason: 'unsigned' },
          object: { type: scenario.targetType || 'commit', sha: scenario.targetSha || commit }
        })
      }
    })
    assert.ifError(result.error)
    assert.equal(result.status, scenario.ok ? 0 : 1, result.stderr)
    if (scenario.api === false) assert.doesNotMatch(result.stderr, /api-called/)
    if (scenario.ok) assert.equal(readFileSync(output, 'utf8'), `commit_sha=${commit}\n`)
  })
}
