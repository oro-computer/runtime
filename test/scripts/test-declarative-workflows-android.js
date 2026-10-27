import assert from 'node:assert/strict'
import https from 'node:https'
import { execFileSync } from 'node:child_process'
import { connectPublicRenderer } from './android-public-cdp.js'

// Run only against the bundled public example and an explicitly selected local
// public TLS fixture. It never discovers another app or uses provider accounts.
const serial = process.env.ORO_TEST_ANDROID_SERIAL
const port = Number(process.env.ORO_TEST_WORKFLOW_PORT)
const bundle = 'oro.runtime.qualification.workflows'
assert.ok(serial, 'Select the public Android device explicitly')
assert.ok(Number.isSafeInteger(port) && port > 0 && port < 65536)
const device = (...args) => execFileSync('adb', ['-s', serial, ...args], { encoding: 'utf8', timeout: 20000 }).trim()
const delay = ms => new Promise(resolve => setTimeout(resolve, ms))
async function until (work, predicate, label, timeout = 15000) {
  const deadline = Date.now() + timeout
  let value
  do {
    value = await work()
    if (predicate(value)) return value
    await delay(200)
  } while (Date.now() < deadline)
  assert.fail(label + ': ' + JSON.stringify(value))
}
async function control (mode, generation) {
  return new Promise((resolve, reject) => {
    // This exception is confined to the public loopback test server. The app's
    // actual HTTPS request must independently pass the packaged certificate pin.
    https.get({ hostname: '127.0.0.1', port, path: '/control?' + new URLSearchParams({ mode, generation }), rejectUnauthorized: false }, response => {
      let text = ''
      response.setEncoding('utf8')
      response.on('data', chunk => { text += chunk })
      response.on('end', () => { try { resolve(JSON.parse(text)) } catch (error) { reject(error) } })
    }).on('error', reject)
  })
}
let page = await connectPublicRenderer({ serial, bundle })
await until(() => page.evaluate('typeof window.publicWorkflowCheck'), value => value === 'function', 'Public page not initialized')
const call = (action, options = {}) => page.evaluate('window.publicWorkflowCheck(' + JSON.stringify(action) + ',' + JSON.stringify(options) + ')')
const evidence = []
async function read () {
  const admitted = await call('run')
  await until(() => page.evaluate('import("oro:foreground").then(m=>m.getStatus(' + JSON.stringify(admitted.id) + '))'), value => value.generation === admitted.generation && value.phase === 'completed', 'Current read did not complete')
  const status = await until(() => call('status'), value => value.drained && value.lastOutcome === 'completed', 'Read did not drain')
  await until(() => page.evaluate('(async()=>{const m=await import("oro:foreground");try{await m.release(' + JSON.stringify(admitted.id) + ',' + JSON.stringify(admitted.generation) + ');return true}catch(e){if(e.code==="EBUSY")return false;throw e}})()'), Boolean, 'Completed controller did not drain')
  return status
}
async function stop () {
  await call('stop')
  return until(() => call('status'), value => value.drained && !value.enabled, 'Stop did not drain')
}
async function release () {
  await stop()
  const events = await call('events')
  if (events.length) await call('releaseEvents', { eventHandles: events.map(value => value.eventHandle) })
  await call('release')
}
try {
  const support = await call('support')
  assert.equal(support.available, true)
  assert.equal(support.nativeIntegrationRequired, false)
  assert.equal(support.backgroundJavaScript, false)
  assert.ok(support.workflows.includes('public-self-hosted'))
  evidence.push('bundled-runtime-executors-without-application-native-sources')

  await control('normal', '101')
  const first = await call('bind')
  assert.equal((await read()).retainedEvents, 1)
  assert.equal((await read()).retainedEvents, 1)
  const active = await page.evaluate('import("oro:notification").then(m=>m.getActive())')
  const own = active.filter(value => value.id.includes(first.id))
  assert.equal(own.length, 1)
  assert.ok(own.every(value => !('targetRef' in value)))
  const discovered = await call('discover')
  assert.ok(discovered.bindings.some(value => value.id === first.id))
  assert.ok(!JSON.stringify(discovered).includes('public-test-bearer'))
  evidence.push('pinned-https-read-checkpoint-notification-dedup-and-private-correlation-redaction')
  await call('schedule', { intervalMs: 900000 })
  await stop()
  evidence.push('shared-os-periodic-admission-and-local-stop')
  await release()

  await control('normal', '102')
  await call('bind')
  await call('monitor')
  await until(() => call('status'), value => value.retainedEvents === 1 && value.running, 'Monitor did not read')
  device('shell', 'input', 'keyevent', 'KEYCODE_HOME')
  const before = await control('normal', '103')
  await until(() => control('normal', '103'), value => value.requests > before.requests, 'Background monitor did not read', 12000)
  device('shell', 'am', 'start', '-n', bundle + '/.MainActivity')
  await until(() => call('status'), value => value.retainedEvents === 2, 'Background checkpoint missing')
  await release()
  evidence.push('continuous-user-session-reads-with-background-webview-and-stop-drain')

  await control('normal', '104')
  const credential = await call('credential')
  assert.equal(credential.valueExported, false)
  assert.ok(!('value' in credential))
  const scopeError = await page.evaluate('(async()=>{try{await window.publicWorkflowCheck("bind",' + JSON.stringify({ workflow: 'public-authorized', credentialHandle: credential.credentialHandle, targetRef: 'wrong-public-target' }) + ');return "accepted"}catch(e){return e.code||e.name}})()')
  assert.notEqual(scopeError, 'accepted')
  await call('bind', { workflow: 'public-authorized', credentialHandle: credential.credentialHandle })
  await read()
  assert.ok((await control('slow', '104')).bearer > 0)
  await call('run')
  await until(() => call('status'), value => value.running, 'Slow read not active')
  const revoked = await call('revoke', { credentialHandle: credential.credentialHandle })
  assert.equal(revoked.revokedLocally, true)
  assert.equal(revoked.serverRevocationConfirmed, false)
  await until(() => call('status'), value => value.drained, 'Revoked request did not release resources')
  await release()
  await call('releaseCredential', { credentialHandle: credential.credentialHandle })
  evidence.push('opaque-credential-scope-rejection-revocation-and-owned-https-cancellation')

  await control('wrong-scope', '105')
  await call('bind')
  await call('run')
  const denied = await until(() => call('status'), value => value.drained && value.lastOutcome !== 'not-started', 'Scope denial did not finish')
  assert.equal(denied.retainedEvents, 0)
  assert.notEqual(denied.lastOutcome, 'completed')
  await release()
  evidence.push('response-identity-mismatch-denies-presentation')

  await control('slow', '106')
  const interruptedBinding = await call('bind')
  await call('run')
  await until(() => call('status'), value => value.running, 'Process-loss read not active')
  device('shell', 'am', 'force-stop', bundle)
  await page.close()
  await control('normal', '106')
  device('shell', 'am', 'start', '-n', bundle + '/.MainActivity')
  page = await connectPublicRenderer({ serial, bundle })
  await until(() => page.evaluate('typeof window.publicWorkflowCheck'), value => value === 'function', 'Restored public page not initialized')
  const invokeRestored = (action, options) => page.evaluate('import("oro:workflows").then(m=>m.' + action + '(' + JSON.stringify(options) + '))')
  const interrupted = await until(() => invokeRestored('getStatus', { id: interruptedBinding.id }), value => value.drained, 'Process-loss state unavailable')
  const restored = await call('discover')
  assert.ok(restored.bindings.some(value => value.id === interruptedBinding.id && value.state === 'unknown'))
  assert.equal(interrupted.state, 'unknown')
  assert.equal(interrupted.interruptedOutcome, 'unknown')
  const noReplay = await control('normal', '106')
  await delay(2000)
  assert.equal((await control('normal', '106')).requests, noReplay.requests)
  await invokeRestored('stop', { id: interruptedBinding.id })
  const final = await invokeRestored('getStatus', { id: interruptedBinding.id })
  await invokeRestored('release', { id: final.id, generation: final.generation })
  evidence.push('process-loss-unknown-retention-scoped-rediscovery-and-no-automatic-monitor-or-read-replay')
  console.log(JSON.stringify({ passed: evidence, unverified: ['cold authenticated push delivery', 'reboot/Doze scheduling', 'signed historical migration', 'other operating-system workflow backends', 'production service declaration and store policy'] }, null, 2))
} finally {
  await page.close()
}
