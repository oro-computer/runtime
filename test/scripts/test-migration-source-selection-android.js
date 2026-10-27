import assert from 'node:assert/strict'
import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
import { connectPublicRenderer } from './android-public-cdp.js'

// Run after SourceInstrumentation. Its deliberate public-key-loss state must remain intact.
const bundle = 'oro.runtime.qualification.sourcehistory'
const serial = process.env.ORO_TEST_ANDROID_SERIAL
const output = process.env.ORO_TEST_SOURCE_SELECTION_REPORT
assert.ok(serial, 'Select the public device explicitly')
assert.ok(output, 'Select an evidence file')
const device = (...args) => execFileSync('adb', ['-s', serial, ...args], { encoding: 'utf8', timeout: 15000 }).trim()
let page
try {
  device('shell', 'am', 'start', '-n', bundle + '/.MainActivity')
  page = await connectPublicRenderer({ serial, bundle })
  const result = await page.evaluate(`(async () => {
    const migration = (await import('oro:migration')).default
    await (await (await import('oro:application')).default.getCurrentWindow()).markReady()
    const support = await migration.getSupport()
    const errors = []
    for (const [action, options] of [
      ['inspectProtected', { profile: 'selected', sourceRef: 'public-target' }],
      ['inspectHandover', { component: 'job', sourceRef: 'public-target' }]
    ]) {
      try { await migration[action](options); errors.push('unexpected-success') }
      catch (error) { errors.push(error.code || error.name) }
    }
    return { support, errors }
  })()`)
  assert.equal(result.support.available, true)
  assert.equal(result.support.sourceSelection, 'prefer-native-records-v1')
  assert.equal(result.support.legacyProvenance, 'android-json-pairs-sha256-v1')
  assert.equal(result.support.generatedAdapters, true)
  assert.deepEqual(result.errors, ['EKEYREVOKED', 'EKEYREVOKED'])
  fs.writeFileSync(output, JSON.stringify({ bundle, passed: true, ...result }, null, 2) + '\n')
  console.log('Android public source selection support and key-loss rejection passed through JavaScript IPC')
} finally {
  await page?.close()
  device('shell', 'am', 'force-stop', bundle)
}
