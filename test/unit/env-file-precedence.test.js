import test from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, readFileSync, writeFileSync, rmSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { syncEnvFile } from '../scripts/run.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const testRoot = path.resolve(__dirname, '..')
const repoRoot = path.resolve(testRoot, '..')

test('syncEnvFile prefers .oro.env when present', () => {
  const oroPath = path.join(repoRoot, '.oro.env')
  if (!existsSync(oroPath)) return

  const destPath = path.join(testRoot, '.oro.env')
  const originalDestExists = existsSync(destPath)
  const originalDestContent = originalDestExists ? readFileSync(destPath) : null

  const hadOroEnvFilename = 'ORO_ENV_FILENAME' in process.env
  const originalOroEnvFilename = process.env.ORO_ENV_FILENAME

  try {
    if (originalDestExists) {
      rmSync(destPath)
    }

    delete process.env.ORO_ENV_FILENAME

    syncEnvFile()

    assert.ok(
      existsSync(destPath),
      'expected .oro.env to be synced to test root'
    )
    const src = readFileSync(oroPath, 'utf8')
    const dest = readFileSync(destPath, 'utf8')
    assert.equal(dest, src)
  } finally {
    if (hadOroEnvFilename) {
      process.env.ORO_ENV_FILENAME = originalOroEnvFilename
    } else {
      delete process.env.ORO_ENV_FILENAME
    }

    if (originalDestExists) {
      writeFileSync(destPath, originalDestContent)
    } else if (existsSync(destPath)) {
      rmSync(destPath)
    }
  }
})

test('syncEnvFile skips when preferred file is missing', () => {
  const missingName = `.run-46-missing-${Date.now()}.env`
  const missingSource = path.join(repoRoot, missingName)
  const destPath = path.join(testRoot, missingName)

  assert.equal(
    existsSync(missingSource),
    false,
    'missing source should not exist'
  )

  const originalDestExists = existsSync(destPath)
  const originalDestContent = originalDestExists ? readFileSync(destPath) : null

  const hadOroEnvFilename = 'ORO_ENV_FILENAME' in process.env
  const originalOroEnvFilename = process.env.ORO_ENV_FILENAME

  try {
    if (originalDestExists) {
      rmSync(destPath)
    }

    process.env.ORO_ENV_FILENAME = missingName

    syncEnvFile()

    assert.equal(
      existsSync(destPath),
      false,
      'expected no env file to be synced'
    )
  } finally {
    if (hadOroEnvFilename) {
      process.env.ORO_ENV_FILENAME = originalOroEnvFilename
    } else {
      delete process.env.ORO_ENV_FILENAME
    }

    if (originalDestExists) {
      writeFileSync(destPath, originalDestContent)
    } else if (existsSync(destPath)) {
      rmSync(destPath)
    }
  }
})
