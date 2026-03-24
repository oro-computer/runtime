import test from 'node:test'
import assert from 'node:assert/strict'

import { resolve as loaderResolve } from '../../api/node-esm-loader.js'

test('node-esm-loader resolves oro: specifiers by default', async () => {
  const hadModulesDir = 'ORO_MODULES_DIR' in process.env
  const previousModulesDir = process.env.ORO_MODULES_DIR

  try {
    delete process.env.ORO_MODULES_DIR

    const seen = []
    const result = await loaderResolve(
      'oro:modules/example',
      {},
      async (resolved) => {
        seen.push(resolved)
        return resolved
      }
    )

    assert.equal(result, 'node_modules/example.js')
    assert.deepEqual(seen, ['node_modules/example.js'])
  } finally {
    if (hadModulesDir) {
      process.env.ORO_MODULES_DIR = previousModulesDir
    } else {
      delete process.env.ORO_MODULES_DIR
    }
  }
})

test('node-esm-loader resolves oro:* specifiers via runtime package scope', async () => {
  const seen = []
  const result = await loaderResolve('oro:os', {}, async (resolved) => {
    seen.push(resolved)
    return resolved
  })

  assert.equal(result, '@orocomputer/runtime/os.js')
  assert.deepEqual(seen, ['@orocomputer/runtime/os.js'])
})
