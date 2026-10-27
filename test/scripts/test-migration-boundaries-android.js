import assert from 'node:assert/strict'
import fs from 'node:fs'
import https from 'node:https'
import { X509Certificate, createHash } from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { connectPublicRenderer } from './android-public-cdp.js'

// Public fixture only. Key-loss checks intentionally remove isolated test keys.
const bundle = 'oro.runtime.qualification.migration'
const serial = process.env.ORO_TEST_ANDROID_SERIAL
const output = process.env.ORO_TEST_MIGRATION_REPORT
const phase = process.env.ORO_TEST_MIGRATION_PHASE
assert.ok(serial)
assert.ok(output)
assert.ok(['components', 'retained-interruption', 'interruption', 'key-recovery'].includes(phase))
const device = (...args) => execFileSync('adb', ['-s', serial, ...args], { encoding: 'utf8', timeout: 20000 }).trim()
const delay = ms => new Promise(resolve => setTimeout(resolve, ms))
const identity = { authorityRef: 'public-authority', accountRef: 'public-account', targetRef: 'public-target' }
const source = { profile: 'private', sourceRef: 'public-target' }
const evidence = []
const setupReceipts = []
const runId = String(Date.now())
let page
let binding
let corrupted = false
let resetServer = false
const ref = value => ({ id: value.id, generation: value.generation })
const call = (method, options = {}) => page.evaluate(`window.publicMigration(${JSON.stringify(method)},${JSON.stringify(options)})`)
const code = (method, options) => page.evaluate(`window.publicMigration(${JSON.stringify(method)},${JSON.stringify(options)}).then(()=>"accepted",e=>e.code||e.name)`)
const workflow = (method, options = {}) => page.evaluate(`import('oro:workflows').then(m=>m.${method}(${JSON.stringify(options)}))`)
async function until (work, valid, label, timeout = 30000) {
  const deadline = Date.now() + timeout
  while (Date.now() < deadline) {
    const result = await work()
    if (valid(result)) return result
    await delay(150)
  }
  throw new Error(label)
}
async function ready () {
  await page?.close()
  device('shell', 'am', 'start', '-n', bundle + '/.MainActivity')
  page = await connectPublicRenderer({ serial, bundle })
  await until(() => page.evaluate('window.publicMigrationReady && document.hasFocus() && document.visibilityState === "visible"'), Boolean, 'Public foreground unavailable')
  await delay(500)
}
async function instrument (action) {
  await page?.close()
  page = null
  const text = device('shell', 'am', 'instrument', '-w', '-e', 'action', action, bundle + '.test/public.oro.migration.SeedInstrumentation')
  assert.match(text, /INSTRUMENTATION_CODE: -1/)
  const metadata = JSON.parse(text.match(/publicEvidence=(.*)/)[1])
  await ready()
  return metadata
}
async function server (generation, mode) {
  const certificate = new X509Certificate(fs.readFileSync(process.env.ORO_TEST_MIGRATION_CA))
  return new Promise((resolve, reject) => {
    // Only the fixed loopback test controller uses this fixture certificate.
    // The Android read itself still uses the bundled origin and leaf-certificate pin.
    const request = https.get(`https://127.0.0.1:45132/control?generation=${generation}${mode ? '&mode=' + mode : ''}`, { rejectUnauthorized: false, agent: false }, response => {
      try { assert.equal(response.socket.getPeerCertificate().fingerprint256, certificate.fingerprint256) } catch (error) { response.destroy(); reject(error); return }
      let value = ''
      response.on('data', chunk => { value += chunk })
      response.on('end', () => { try { resolve(JSON.parse(value)) } catch (error) { reject(error) } })
      response.on('error', reject)
    })
    request.setTimeout(5000, () => request.destroy(new Error('Public control timeout')))
    request.on('error', reject)
  })
}
async function sourceUnchanged (before) {
  assert.equal((await call('inspectProtected', source)).sourceSha256, before)
}
async function pendingImport (scope, recoveryRef) {
  const status = await call('inspectProtected', { profile: 'registry', sourceRef: 'public-target' })
  await page.evaluate(`window.boundaryResult=null; window.boundaryAbort=new AbortController();
    window.publicMigrationReady && import('oro:migration').then(m=>m.importProtected(${JSON.stringify({ profile: 'registry', sourceRef: 'public-target', sourceSha256: status.sourceSha256, ...scope, recoveryRef, userInitiated: true })},{signal:window.boundaryAbort.signal})).then(()=>{window.boundaryResult='accepted'},e=>{window.boundaryResult=e.code||e.name}); true`)
  await delay(1200)
}
function jobFinished () {
  try { return device('shell', 'cmd', 'jobscheduler', 'get-job-state', bundle, '23109').includes('unknown') } catch (error) {
    if (error.status === 23 && String(error.stdout).includes('unknown')) return true
    throw error
  }
}
try {
  device('reverse', 'tcp:45132', 'tcp:45132')
  await ready()
  const original = (await call('inspectProtected', source)).sourceSha256
  if (phase === 'components') {
    const seeded = await instrument('seed-native')
    const nativeSource = { profile: 'records', sourceRef: 'public-target' }
    const initial = await call('inspectProtected', nativeSource)
    assert.match(initial.sourceSha256, /^[a-f0-9]{64}$/)
    binding = await workflow('bind', { workflow: 'public-read', ...identity, userInitiated: true })
    const historical = { component: 'native-job', sourceRef: 'public-target' }
    const selected = await call('inspectHandover', historical)
    const previous = (await call('discoverHandovers', identity)).find(value => value.component === 'native-job')
    if (previous) {
      if (previous.phase !== 'stopped') Object.assign(previous, await call('stopHandover', ref(previous)))
      for (const receipt of await call('getRetainedReceipts', ref(previous))) {
        assert.equal(receipt.outcome, 'read-completed', 'Resolve interrupted public history before rerunning this phase')
      }
    }
    console.log('Public native-record job handover consent is ready')
    const prepared = await call('prepareHandover', { ...historical, ...selected, ...identity, expectedGeneration: previous?.generation ?? '', bindingId: binding.id, bindingGeneration: binding.generation, userInitiated: true })
    const active = await call('activateHandover', ref(prepared))
    const priorReads = (await call('getRetainedReceipts', ref(active))).filter(row => row.outcome === 'read-completed').length
    await server(2)
    device('shell', 'cmd', 'jobscheduler', 'run', '-f', bundle, '23109')
    await until(() => call('getRetainedReceipts', ref(active)), rows => rows.filter(row => row.outcome === 'read-completed').length === priorReads + 1, 'Native-record job read did not complete')
    const inspected = await instrument('inspect')
    assert.equal(inspected.nativeRevision, seeded.nativeRevision)
    assert.equal(inspected.nativeCommitId, seeded.nativeCommitId)
    assert.equal((await call('inspectProtected', nativeSource)).sourceSha256, initial.sourceSha256)
    evidence.push({ check: 'native-v1 source job completes approved HTTPS read without changing original row/revision/commit', nativeRevision: seeded.nativeRevision })
    await instrument('native-progress')
    assert.notEqual((await call('inspectProtected', nativeSource)).sourceSha256, initial.sourceSha256)
    assert.equal((await call('activateHandover', ref(active))).phase, 'active')
    await server(3)
    device('shell', 'cmd', 'jobscheduler', 'run', '-f', bundle, '23109')
    await until(() => call('getRetainedReceipts', ref(active)), rows => rows.filter(row => row.outcome === 'read-completed').length === priorReads + 2, 'Checkpoint update rejected unchanged authorization generation')
    evidence.push({ check: 'unrelated checkpoint change preserves explicit authorization generation and permits a new fixed read' })
    await instrument('native-revoke')
    assert.equal(await code('activateHandover', ref(active)), 'EACCES')
    const before = await server(3)
    const retained = (await call('getRetainedReceipts', ref(active))).length
    device('shell', 'cmd', 'jobscheduler', 'run', '-f', bundle, '23109')
    await until(() => call('getRetainedReceipts', ref(active)), rows => rows.length > retained, 'Revoked input was not retained')
    await delay(500)
    assert.equal((await server(3)).requests, before.requests)
    evidence.push({ check: 'revoked source predicate denies activation and job network dispatch' })
    await instrument('native-epoch')
    assert.equal(await code('activateHandover', ref(active)), 'ESTALE')
    assert.ok(Array.isArray(await call('getRetainedReceipts', ref(active))))
    const stopped = await call('stopHandover', ref(active))
    await until(() => call('getHandoverStatus', ref(stopped)), state => state.drained, 'Epoch-changed handover did not drain')
    assert.equal(await code('stopHandover', ref(active)), 'ESTALE')
    evidence.push({ check: 'wrapping-key epoch fences old mapping; status, receipts and Stop remain available; stale Stop reports ESTALE' })
    await instrument('metadata')
    device('shell', 'am', 'start', '-n', bundle + '/public.oro.migration.LegacyActivity', '-a', 'public.oro.migration.OPEN', '--es', 'destination', 'public-target')
    await delay(500)
    const entries = await call('getHistoricalEntries', { component: 'activity' })
    const entry = entries.find(value => value.references.destination === 'public-target')
    assert.ok(entry)
    assert.equal(entry.untrusted, true)
    assert.equal(entry.action, 'public.oro.migration.OPEN')
    assert.deepEqual(Object.keys(entry.references), ['destination'])
    await call('acknowledgeHistoricalEntry', { component: 'activity', entryId: entry.entryId })
    assert.ok(!(await call('getHistoricalEntries', { component: 'activity' })).some(value => value.entryId === entry.entryId))
    evidence.push({ check: 'published Activity alias captures only opaque untrusted reference and supports explicit acknowledgement' })
    assert.equal(await code('readUnprotected', { ...source, userInitiated: true }), 'EACCES')
    console.log('Public historical metadata read consent is ready')
    const valid = await page.evaluate('window.publicMigration(\'readUnprotected\',{profile:\'metadata\',sourceRef:\'public-target\',userInitiated:true}).then(v=>JSON.parse(atob(v.value)).destination===\'public-target\' && v.originalsPreserved)')
    assert.equal(valid, true)
    evidence.push({ check: 'explicitly exposed unprotected metadata requires consent; protected source cannot use that route' })
    await sourceUnchanged(original)
    await workflow('stop', { id: binding.id })
  }
  if (phase === 'retained-interruption') {
    await instrument('seed-native')
    const historical = { component: 'native-job', sourceRef: 'public-target' }
    const previous = (await call('discoverHandovers', identity)).find(value => value.component === historical.component)
    const current = previous && previous.phase !== 'stopped' ? await call('stopHandover', ref(previous)) : previous
    if (current) {
      for (const receipt of await call('getRetainedReceipts', ref(current))) {
        if (receipt.outcome !== 'unknown') continue
        setupReceipts.push(receipt)
        const released = await code('releaseRetainedReceipt', { ...ref(current), receiptId: receipt.receiptId })
        if (released === 'accepted') continue
        assert.equal(released, 'EBUSY')
        // This public server implements fixed GETs only and has no mutation route.
        await call('resolveRetainedReceipt', { ...ref(current), receiptId: receipt.receiptId, outcome: 'not-applied', receiptSha256: createHash('sha256').update('public fixed GET has no remote mutation').digest('hex'), userInitiated: true })
      }
    }
    const historicalBindings = await workflow('discover', { workflow: 'public-read', ...identity })
    for (const retained of historicalBindings.bindings) {
      if (!retained.drained || retained.retainedEvents !== 0) continue
      const stopped = await workflow('stop', { id: retained.id })
      await workflow('release', ref(stopped))
    }
    binding = await workflow('bind', { workflow: 'public-read', ...identity, userInitiated: true })
    const selected = await call('inspectHandover', historical)
    const options = { ...historical, ...selected, ...identity, expectedGeneration: current?.generation ?? '', bindingId: binding.id, bindingGeneration: binding.generation, userInitiated: true }
    const prepared = await call('prepareHandover', options)
    const active = await call('activateHandover', ref(prepared))
    assert.equal(await code('prepareHandover', { ...options, expectedGeneration: 'public-stale-generation' }), 'ESTALE')
    evidence.push({ check: 'stale preparation reports ESTALE after consent without retrying it as storage contention' })
    resetServer = true
    const before = await server(4, 'timeout')
    device('shell', 'cmd', 'jobscheduler', 'run', '-f', bundle, '23109')
    await until(() => server(4), state => state.requests > before.requests, 'Retained read did not reach public server')
    await page.close()
    page = null
    device('shell', 'am', 'force-stop', bundle)
    await ready()
    const interrupted = await workflow('getStatus', { id: binding.id })
    assert.equal(interrupted.interruptedOutcome, 'unknown')
    const unknown = (await call('getRetainedReceipts', ref(active))).filter(value => value.outcome === 'unknown' && value.sourceGeneration === selected.sourceGeneration)
    assert.ok(unknown.length)
    evidence.push({ check: 'process loss during retained HTTPS read preserves unknown receipt and interrupted workflow outcome' })
    await instrument('native-progress')
    const afterLoss = await server(4, 'normal')
    device('shell', 'cmd', 'jobscheduler', 'run', '-f', bundle, '23109')
    await until(async () => jobFinished(), Boolean, 'Blocked historical callback did not finish')
    assert.equal((await server(4)).requests, afterLoss.requests)
    evidence.push({ check: 'unknown retained receipt prevents automatic same-generation network redispatch after process loss' })
    for (const receipt of unknown) {
      const resolution = await call('resolveRetainedReceipt', { ...ref(active), receiptId: receipt.receiptId, outcome: 'not-applied', receiptSha256: createHash('sha256').update('public fixed GET has no remote mutation').digest('hex'), userInitiated: true })
      assert.equal(resolution.remoteOutcomeVerifiedByRuntime, false)
    }
    evidence.push({ check: 'explicit public application reconciliation is recorded without claiming runtime server verification' })
    await instrument('native-progress')
    const beforeStop = await server(4, 'timeout')
    device('shell', 'cmd', 'jobscheduler', 'run', '-f', bundle, '23109')
    await until(() => server(4), state => state.requests > beforeStop.requests, 'Reconciled retained input did not admit public read')
    const stopped = await call('stopHandover', ref(active))
    await until(() => call('getHandoverStatus', ref(stopped)), state => state.drained, 'Retained Stop did not cancel and drain in-flight read')
    assert.equal(await code('activateHandover', ref(active)), 'ESTALE')
    assert.ok((await call('getRetainedReceipts', ref(stopped))).some(value => value.outcome === 'unknown'))
    assert.equal((await workflow('getStatus', { id: binding.id })).lastOutcome, 'cancelled-read')
    evidence.push({ check: 'Stop cancels an in-flight retained HTTPS read, drains its worker and preserves uncertain receipt' })
    await workflow('stop', { id: binding.id })
  }
  if (phase === 'interruption') {
    for (const mode of ['abort', 'rotation', 'background', 'reload', 'process-loss']) {
      const scope = { ...identity, targetRef: 'public-boundary-' + mode + '-' + runId }
      await pendingImport(scope, 'public-' + mode + '-' + runId)
      if (mode === 'abort') {
        await page.evaluate('window.boundaryAbort.abort(); true')
        assert.equal(await until(() => page.evaluate('window.boundaryResult'), Boolean, 'Abort did not settle'), 'ABORT_ERR')
      } else if (mode === 'rotation') {
        const rotation = device('shell', 'wm', 'user-rotation').trim().split(/\s+/)
        try {
          device('shell', 'wm', 'user-rotation', 'lock', '1')
          await delay(700)
          await page.evaluate('window.boundaryAbort?.abort(); true')
          await delay(400)
        } finally {
          device('shell', 'wm', 'user-rotation', ...rotation)
        }
        await ready()
      } else if (mode === 'background') {
        device('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        await delay(400)
        await ready()
        assert.equal(await page.evaluate('window.boundaryResult'), 'ABORT_ERR')
      } else if (mode === 'reload') {
        await page.evaluate('setTimeout(()=>location.reload(),0);true')
        await delay(1500)
        await ready()
      } else {
        await page.close()
        page = null
        device('shell', 'am', 'force-stop', bundle)
        await ready()
      }
      assert.deepEqual(await call('discoverProtected', { profile: 'registry', sourceRef: 'public-target', ...scope }), [])
      await until(() => page.evaluate('import(\'oro:authentication\').then(m=>m.getStatus()).then(v=>!v.busy)'), Boolean, 'Cancelled authentication worker did not release')
      await sourceUnchanged(original)
      evidence.push({ check: mode + ' discards interrupted import, releases authentication ownership and preserves original' })
    }
  }
  if (phase === 'key-recovery') {
    const imports = await call('discoverProtected', { ...source, ...identity })
    assert.ok(imports.length)
    const previous = await call('importProtected', { ...source, ...identity, sourceSha256: original, userInitiated: true })
    await page.evaluate(`import('oro:authentication').then(m=>m.removeKey({id:${JSON.stringify(previous.handle)},policy:'biometric-or-device'}))`)
    assert.equal(await code('openProtected', { handle: previous.handle, userInitiated: true }), 'EKEYREVOKED')
    const retry = await call('importProtected', { ...source, ...identity, sourceSha256: original, userInitiated: true })
    assert.equal(retry.handle, previous.handle)
    assert.equal(await code('openProtected', { handle: previous.handle, userInitiated: true }), 'EKEYREVOKED')
    evidence.push({ check: 'missing target key reports EKEYREVOKED; ordinary idempotent retry does not silently replace it' })
    console.log('Public recovery decrypt and new-key encrypt prompts are ready')
    const options = { ...source, ...identity, sourceSha256: original, recoveryRef: 'public-final-recovery-' + runId, userInitiated: true }
    const recovered = await call('importProtected', options)
    assert.notEqual(recovered.handle, previous.handle)
    assert.equal((await call('importProtected', options)).handle, recovered.handle)
    console.log('Public recovered-key unlock prompt is ready')
    assert.equal(await page.evaluate(`window.publicMigration('openProtected',{handle:${JSON.stringify(recovered.handle)},userInitiated:true}).then(v=>v.value===${JSON.stringify(Buffer.from('public historical value 🎈\0').toString('base64'))})`), true)
    await sourceUnchanged(original)
    evidence.push({ check: 'explicit recovery nonce produces a new authenticated copy, retries idempotently and preserves original and old handles' })
    await instrument('corrupt')
    corrupted = true
    const damaged = await call('inspectProtected', source)
    console.log('Public corrupt historical decrypt prompt is ready; failure must be EBADMSG')
    assert.equal(await code('importProtected', { ...options, sourceSha256: damaged.sourceSha256, recoveryRef: 'public-corruption' }), 'EBADMSG')
    await instrument('restore')
    corrupted = false
    await sourceUnchanged(original)
    evidence.push({ check: 'corrupt original GCM fails EBADMSG without replacement; public original restored byte-for-byte' })
    const lost = await instrument('lose-key')
    assert.equal(lost.privateKeyPresent, false)
    assert.equal(lost.protected, original)
    assert.equal(await code('importProtected', { ...options, recoveryRef: 'public-missing-original' }), 'EKEYREVOKED')
    assert.equal((await instrument('inspect')).privateKeyPresent, false)
    console.log('Public migrated-copy unlock after original-key loss is ready')
    assert.equal(await page.evaluate(`window.publicMigration('openProtected',{handle:${JSON.stringify(recovered.handle)},userInitiated:true}).then(v=>v.value===${JSON.stringify(Buffer.from('public historical value 🎈\0').toString('base64'))})`), true)
    evidence.push({ check: 'missing original key is never recreated; ciphertext retained and separately migrated copy remains usable' })
  }
} finally {
  if (corrupted) await instrument('restore')
  if (resetServer) await server(4, 'normal')
  await page?.close()
  const expected = { components: 6, 'retained-interruption': 5, interruption: 5, 'key-recovery': 4 }
  fs.writeFileSync(output, JSON.stringify({ bundle, phase, binding, setupReceipts, evidence, passed: evidence.length === expected[phase] }, null, 2) + '\n')
}
