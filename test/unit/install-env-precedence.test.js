import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdirSync, rmSync, existsSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')
const installScript = path.join(repoRoot, 'bin', 'install.sh')

function runInstallProbe (label, envPatch = {}, options = {}) {
  const tmpRoot = path.join(repoRoot, 'test', 'tmp', `run-44-${label}`)
  const dataHome = path.join(tmpRoot, 'data-home')

  if (!options.skipCleanup) {
    rmSync(tmpRoot, { recursive: true, force: true })
  }

  mkdirSync(dataHome, { recursive: true })

  const env = {
    PATH: process.env.PATH,
    HOME: tmpRoot,
    XDG_DATA_HOME: dataHome,
    LOCALAPPDATA: '',
    NO_ANDROID: '1',
    NO_IOS: '1',
    ORO_INSTALL_MODE: 'probe-env',
    ...envPatch
  }

  const result = spawnSync('bash', ['-lc', `"${installScript}"`], {
    cwd: repoRoot,
    env,
    encoding: 'utf8'
  })

  assert.equal(
    result.status,
    0,
    result.stderr || 'install probe exited non-zero'
  )

  const lines = result.stdout
    .split('\n')
    .map((l) => l.trim())
    .filter(Boolean)
  const kv = Object.fromEntries(
    lines
      .filter((line) => line.includes('='))
      .map((line) => {
        const idx = line.indexOf('=')
        return [line.slice(0, idx), line.slice(idx + 1)]
      })
  )

  return {
    tmpRoot,
    dataHome,
    env,
    kv,
    stdout: result.stdout,
    stderr: result.stderr
  }
}

test('install prefers ORO_HOME over defaults', () => {
  const tmpRoot = path.join(repoRoot, 'test', 'tmp', 'run-44-oro-home')
  const oroHome = path.join(tmpRoot, 'oro-home')

  const { kv } = runInstallProbe('oro-home', {
    ORO_HOME: oroHome
  })

  assert.equal(kv.ORO_HOME, oroHome)
})

test('install defaults ORO_HOME when unset', () => {
  const label = 'default'
  const tmpRoot = path.join(repoRoot, 'test', 'tmp', `run-44-${label}`)
  const dataHome = path.join(tmpRoot, 'data-home')
  const expected = path.join(dataHome, 'oro')

  const { kv } = runInstallProbe(label)

  assert.equal(kv.ORO_HOME, expected)
  assert.ok(existsSync(expected), 'runtime home should exist after install')
})
