import assert from 'node:assert/strict'
import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
import { connectPublicRenderer } from './android-public-cdp.js'

// Only the isolated fixture and its separately signed public seeder are addressed.
const serial = process.env.ORO_TEST_ANDROID_SERIAL
const output = process.env.ORO_TEST_MIGRATION_REPORT
const bundle = 'oro.runtime.qualification.migration'
assert.ok(serial)
assert.ok(output)
const device = (...args) => execFileSync('adb', ['-s', serial, ...args], { encoding: 'utf8', timeout: 20000 }).trim()
const delay = ms => new Promise(resolve => setTimeout(resolve, ms))
const evidence = []
let page
const scope = { authorityRef: 'public-authority', accountRef: 'public-account', targetRef: 'public-target' }
const call = (method, options = {}) => page.evaluate(`window.publicMigration(${JSON.stringify(method)},${JSON.stringify(options)})`)
async function until (work, valid, label, budget = 15000) {
  const end = Date.now() + budget
  while (Date.now() < end) {
    const result = await work()
    if (valid(result)) return result
    await delay(150)
  }
  throw new Error(label)
}
async function ready () {
  device('shell', 'am', 'start', '-n', bundle + '/.MainActivity')
  page = await connectPublicRenderer({ serial, bundle })
  await until(() => page.evaluate('window.publicMigrationReady && document.hasFocus() && document.visibilityState === "visible"'), Boolean, 'Public foreground document unavailable')
  await delay(500)
}
async function code (method, options) {
  return page.evaluate(`window.publicMigration(${JSON.stringify(method)},${JSON.stringify(options)}).then(()=>"accepted",e=>e.code||e.name)`)
}
try {
  device('reverse', 'tcp:45132', 'tcp:45132')
  device('shell', 'pm', 'grant', bundle, 'android.permission.POST_NOTIFICATIONS')
  await ready()
  const support = await call('getSupport')
  assert.equal(support.protectedImport, 'protected-import-v1')
  assert.equal(support.retainedComponents, 'retained-components-v1')
  assert.equal(support.nativeIntegrationRequired, false)
  evidence.push({ check: 'zero-application-native runtime migration support', support })
  const source = { profile: 'private', sourceRef: 'public-target' }
  const before = await call('inspectProtected', source)
  assert.equal(await code('importProtected', { ...source, ...scope, sourceSha256: '0'.repeat(64), userInitiated: true }), 'ESTALE')
  const injected = { ...source, ...scope, sourceSha256: before.sourceSha256, userInitiated: true, alias: 'not-permitted' }
  assert.equal(await code('importProtected', injected), 'EINVAL')
  evidence.push({ check: 'stale source and arbitrary alias injection rejected before prompts' })
  // The user completes the two cryptographic prompts; no authentication is synthesized.
  console.log('Public historical decrypt and runtime encrypt prompts are ready')
  const recovery = process.env.ORO_TEST_MIGRATION_RECOVERY_REF
  const imported = await call('importProtected', { ...source, ...scope, sourceSha256: before.sourceSha256, userInitiated: true, ...(recovery ? { recoveryRef: recovery } : {}) })
  assert.equal(imported.valueExported, false)
  assert.equal(imported.originalsPreserved, true)
  assert.ok(!('value' in imported))
  assert.equal((await call('inspectProtected', source)).sourceSha256, before.sourceSha256)
  const rediscovered = await call('discoverProtected', { ...source, ...scope })
  assert.ok(rediscovered.some(value => value.handle === imported.handle))
  evidence.push({ check: 'authenticated opaque rewrap preserves original and is rediscoverable', imported })
  console.log('Public migrated-value unlock prompt is ready')
  const opened = await call('openProtected', { handle: imported.handle, userInitiated: true })
  assert.equal(opened.value, Buffer.from('public historical value 🎈\0').toString('base64'))
  evidence.push({ check: 'new per-operation protected key decrypts exact public UTF-8/NUL bytes' })
  fs.writeFileSync(output, JSON.stringify({ bundle, evidence, phase: 'protected-import-passed' }, null, 2) + '\n')
  console.log('Protected historical import passed; original data unchanged')
} finally {
  await page?.close()
  fs.writeFileSync(output, JSON.stringify({ bundle, evidence, phase: evidence.length === 4 ? 'protected-import-passed' : 'incomplete' }, null, 2) + '\n')
}
