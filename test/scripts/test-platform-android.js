import assert from 'node:assert/strict'
import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
import { connectPublicRenderer } from './android-public-cdp.js'

const bundle = 'oro.runtime.qualification.platform'
const serial = process.env.ORO_TEST_ANDROID_SERIAL
const output = process.env.ORO_TEST_PLATFORM_REPORT
assert.ok(serial, 'Select the public Android device explicitly')
assert.ok(output, 'Select an evidence file')
const device = (...args) => execFileSync('adb', ['-s', serial, ...args], { encoding: 'utf8', timeout: 15000 }).trim()
let page
try {
  device('shell', 'am', 'start', '-n', bundle + '/.MainActivity')
  page = await connectPublicRenderer({ serial, bundle })
  let result
  const deadline = Date.now() + 20000
  while (Date.now() < deadline) {
    result = await page.evaluate('window.publicPlatformResult')
    if (result) break
    await new Promise(resolve => setTimeout(resolve, 150))
  }
  assert.ok(result, 'Public platform checks did not settle')
  assert.ok(!result.error, result.error)
  assert.equal(result.inventory.schema, 1)
  assert.equal(result.inventory.platform, 'android')
  assert.equal(result.extension.feature, 'extension')
  assert.equal(result.extension.implementation, 'implemented')
  assert.equal(result.missing, 'ENOSYS')
  assert.equal(result.removedModuleUnavailable, true)
  fs.writeFileSync(output, JSON.stringify({ bundle, passed: true, ...result }, null, 2) + '\n')
  console.log('Android platform import, inventory, missing-registration error and removed-module rejection passed')
} finally {
  await page?.close()
  device('shell', 'am', 'force-stop', bundle)
}
