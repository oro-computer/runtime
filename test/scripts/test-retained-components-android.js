import assert from 'node:assert/strict'
import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { connectPublicRenderer } from './android-public-cdp.js'

const serial = process.env.ORO_TEST_ANDROID_SERIAL
const output = process.env.ORO_TEST_MIGRATION_REPORT
const bundle = 'oro.runtime.qualification.migration'
assert.ok(serial)
assert.ok(output)
const device = (...args) => execFileSync('adb', ['-s', serial, ...args], { encoding: 'utf8', timeout: 20000 }).trim()
const delay = ms => new Promise(resolve => setTimeout(resolve, ms))
const scope = { authorityRef: 'public-authority', accountRef: 'public-account', targetRef: 'public-target' }
const evidence = []
const channels = JSON.parse(fs.readFileSync(new URL('../fixtures/migration/public/workflows.json', import.meta.url), 'utf8')).workflows[0].channels
let page
let binding
const call = (method, options = {}) => page.evaluate(`window.publicMigration(${JSON.stringify(method)},${JSON.stringify(options)})`)
const workflow = (method, options = {}) => page.evaluate(`import('oro:workflows').then(m=>m.${method}(${JSON.stringify(options)}))`)
const code = (method, options) => page.evaluate(`window.publicMigration(${JSON.stringify(method)},${JSON.stringify(options)}).then(()=>"accepted",e=>e.code||e.name)`)
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
  await until(() => page.evaluate('window.publicMigrationReady && document.hasFocus() && document.visibilityState === "visible"'), Boolean, 'Public foreground unavailable')
  await delay(500)
}
async function send () {
  await page.close()
  const result = device('shell', 'am', 'instrument', '-w', '-e', 'action', 'send', bundle + '.test/public.oro.migration.SeedInstrumentation')
  assert.match(result, /INSTRUMENTATION_CODE: -1/)
  await ready()
}
async function prepare (component) {
  const source = await call('inspectHandover', { component, sourceRef: 'public-target' })
  const existing = (await call('discoverHandovers', scope)).find(value => value.component === component && value.sourceRef === 'public-target')
  console.log('Public native handover consent ready for ' + component)
  return call('prepareHandover', {
    component,
    sourceRef: 'public-target',
    ...source,
    ...scope,
    bindingId: binding.id,
    bindingGeneration: binding.generation,
    expectedGeneration: existing?.generation ?? '',
    userInitiated: true
  })
}
const ref = value => ({ id: value.id, generation: value.generation })
try {
  device('reverse', 'tcp:45132', 'tcp:45132')
  await ready()
  await page.evaluate(`import('oro:notification').then(m=>m.configureChannels(${JSON.stringify(channels)}))`)
  binding = await workflow('bind', { workflow: 'public-read', ...scope, userInitiated: true })
  for (const existing of await call('discoverHandovers', scope)) {
    if (!['receiver', 'job'].includes(existing.component)) continue
    const current = existing.phase !== 'stopped' ? await call('stopHandover', ref(existing)) : existing
    for (const receipt of await call('getRetainedReceipts', ref(current))) {
      const released = await code('releaseRetainedReceipt', { ...ref(current), receiptId: receipt.receiptId })
      if (released === 'accepted') continue
      assert.equal(released, 'EBUSY')
      // This isolated server has no mutation endpoint; old public receipts can
      // be reconciled as not-applied without affecting historical source data.
      await call('resolveRetainedReceipt', {
        ...ref(current),
        receiptId: receipt.receiptId,
        outcome: 'not-applied',
        userInitiated: true,
        receiptSha256: createHash('sha256').update('public application-verified test receipt').digest('hex')
      })
      await call('releaseRetainedReceipt', { ...ref(current), receiptId: receipt.receiptId })
    }
  }
  const original = await call('inspectHandover', { component: 'receiver', sourceRef: 'public-target' })
  await send()
  const receiver = await prepare('receiver')
  assert.equal(receiver.phase, 'prepared')
  assert.equal(await code('activateHandover', { id: receiver.id, generation: 'stale-public-generation' }), 'ESTALE')
  const activeReceiver = await call('activateHandover', ref(receiver))
  assert.equal(activeReceiver.phase, 'active')
  const initial = await call('getRetainedReceipts', ref(receiver))
  assert.equal(initial.length, 1)
  assert.equal(initial[0].outcome, 'unknown')
  assert.ok(!JSON.stringify(initial).includes('public-legacy-token'))
  assert.ok(!('payload' in initial[0]))
  evidence.push({ check: 'valid historical sender is retained before handover, with private payload redacted and unknown outcome' })
  assert.equal(await code('releaseRetainedReceipt', { ...ref(receiver), receiptId: initial[0].receiptId }), 'EBUSY')
  const resolution = await call('resolveRetainedReceipt', {
    ...ref(receiver),
    receiptId: initial[0].receiptId,
    outcome: 'not-applied',
    userInitiated: true,
    receiptSha256: createHash('sha256').update('public application-verified test receipt').digest('hex')
  })
  assert.equal(resolution.remoteOutcomeVerifiedByRuntime, false)
  evidence.push({ check: 'uncertain receipt cannot be released before explicit application resolution' })
  await send()
  assert.equal((await workflow('getStatus', { id: binding.id })).pendingRead, true)
  evidence.push({ check: 'same-source authorized retained receiver queues a durable fresh-read hint without replaying payload' })
  const beforeForged = (await call('getRetainedReceipts', ref(receiver))).length
  device('shell', 'am', 'broadcast', '-n', bundle + '/public.oro.migration.LegacyReceiver', '-a', 'public.oro.migration.MESSAGE', '--es', 'token', 'public-legacy-token', '--es', 'id', 'public-forged-message')
  await delay(600)
  assert.equal((await call('getRetainedReceipts', ref(receiver))).length, beforeForged)
  evidence.push({ check: 'forged sender without matching UID/creator proof is rejected' })
  const job = await prepare('job')
  await call('activateHandover', ref(job))
  device('shell', 'cmd', 'jobscheduler', 'run', '-f', bundle, '23101')
  const completed = await until(() => call('getRetainedReceipts', ref(job)), rows => rows.some(value => value.outcome === 'read-completed'), 'Retained job did not complete its authorized fixed read', 30000)
  assert.ok(completed.every(value => !('payload' in value)))
  assert.equal((await workflow('getStatus', { id: binding.id })).retainedEvents, 1)
  assert.equal((await call('inspectHandover', { component: 'receiver', sourceRef: 'public-target' })).sourceSha256, original.sourceSha256)
  evidence.push({ check: 'queued legacy job ID dispatches the fixed native HTTPS/checkpoint/notification workflow after conditional activation' })
  const stopped = await call('stopHandover', ref(job))
  await until(() => call('getHandoverStatus', ref(stopped)), value => value.drained, 'Stopped retained work did not drain')
  assert.equal(await code('activateHandover', ref(job)), 'ESTALE')
  const found = await call('discoverHandovers', scope)
  assert.ok(found.some(value => value.id === stopped.id && value.generation === stopped.generation))
  evidence.push({ check: 'Stop fences the old generation and exact owned state is rediscoverable' })
  await call('stopHandover', ref(receiver))
  await workflow('stop', { id: binding.id })
  console.log('Retained public components passed')
} finally {
  await page?.close()
  fs.writeFileSync(output, JSON.stringify({ bundle, binding, evidence, phase: evidence.length === 6 ? 'passed' : 'incomplete' }, null, 2) + '\n')
}
