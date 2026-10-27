import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'
import { randomUUID, createHash } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { selectAndroidSerial } from './android-fixtures.js'

const bundle = 'oro.runtime.qualification.pushjournal'
const adb = process.env.ADB || 'adb'
function run (program, args, timeout = 120000) {
  const result = spawnSync(program, args, { encoding: 'utf8', timeout, maxBuffer: 1024 * 1024 })
  assert.equal(result.status, 0, `${program}: ${result.error || ''}\n${result.stdout}${result.stderr}`)
  return result.stdout
}
const serial = selectAndroidSerial(run(adb, ['devices']), process.env.ANDROID_SERIAL)
function device (...args) { return run(adb, ['-s', serial, ...args]) }
function pause () { return new Promise(resolve => setTimeout(resolve, 200)) }
async function receipt (mode, options = {}) {
  const request = randomUUID()
  const source = JSON.stringify(options)
  assert.match(source, /^[A-Za-z0-9{}":,_ .-]*$/)
  device('shell', 'am', 'broadcast', '--include-stopped-packages', '-n', `${bundle}/.JournalReceiver`,
    '--es', 'mode', mode, '--es', 'requestId', request, '--es', 'options', `'${source}'`)
  const until = Date.now() + 60000
  while (Date.now() < until) {
    const raw = spawnSync(adb, ['-s', serial, 'shell', 'run-as', bundle, 'cat', 'shared_prefs/public.journal.qa.xml'],
      { encoding: 'utf8', timeout: 10000 })
    const match = raw.stdout?.match(/<string name="receipt">([\s\S]*?)<\/string>/)
    if (match) {
      const result = JSON.parse(match[1].replace(/&quot;/g, '"').replace(/&apos;/g, "'")
        .replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&'))
      if (result.requestId === request) {
        assert.notEqual(result.failed, true, `Public fixture failure: ${mode}`)
        return result
      }
    }
    await pause()
  }
  throw new Error(`Missing fresh receipt: ${mode}`)
}
async function quarantine (revision, recoveryId, confirmed = true, retryBusy = true) {
  const until = Date.now() + 10000
  while (true) {
    const result = await receipt('quarantine', { revision, recoveryId, confirmed })
    if (!retryBusy || result.error !== 'EBUSY' || Date.now() >= until) return result
    await pause()
  }
}
const apk = process.argv[2] && path.resolve(process.argv[2])
if (apk) {
  const packageName = run(process.env.AAPT || 'aapt', ['dump', 'badging', apk]).match(/package: name='([^']+)'/)?.[1]
  assert.equal(packageName, bundle)
  run(adb, ['-s', serial, 'install', '--no-incremental', '-r', '-g', apk], 180000)
}
const checks = []
try {
  device('shell', 'svc', 'power', 'stayon', 'true')
  device('shell', 'am', 'force-stop', bundle)
  assert.equal((await receipt('inspect')).readable, true)
  assert.equal((await receipt('seed-missing')).legacyKeyPresent, false)
  const missing = await receipt('inspect')
  assert.equal(missing.reason, 'missing-key')
  assert.equal(missing.recoverable, true)
  assert.equal((await quarantine(missing.revision, 'public-denied', false)).error, 'EACCES')
  await receipt('verify')
  checks.push('missing runtime key is detected without provisioning it; denied installation consent preserves originals')
  await receipt('mutate')
  assert.equal((await quarantine(missing.revision, 'public-stale')).error, 'EAGAIN')
  const current = await receipt('inspect')
  assert.equal((await quarantine(current.revision, 'public-missing-recovery')).changed, true)
  assert.equal((await quarantine(current.revision, 'public-missing-recovery')).changed, false)
  assert.equal((await receipt('inspect')).readable, true)
  await receipt('verify')
  checks.push('stale snapshots are refused; explicit quarantine preserves ciphertext and selects fresh state with idempotent retry')
  assert.equal((await receipt('seed-busy')).newKeyPresent, true)
  let busy
  const until = Date.now() + 10000
  do { busy = await receipt('busy-status'); if (!busy.entered) await pause() } while (!busy.entered && Date.now() < until)
  assert.equal(busy.entered, true)
  await receipt('corrupt-busy')
  const corrupt = await receipt('inspect')
  assert.equal(corrupt.reason, 'ciphertext-corrupt')
  assert.equal((await quarantine(corrupt.revision, 'public-busy-recovery', true, false)).error, 'EBUSY')
  let cancelled
  const cancellationDeadline = Date.now() + 10000
  do { cancelled = await receipt('busy-status'); if (!cancelled.cancelObserved) await pause() } while (!cancelled.cancelObserved && Date.now() < cancellationDeadline)
  assert.equal(cancelled.cancelObserved, true)
  assert.equal((await receipt('inspect')).revision, corrupt.revision)
  await receipt('release')
  assert.equal((await quarantine(corrupt.revision, 'public-busy-recovery')).changed, true)
  await receipt('verify')
  checks.push('live native work receives cancellation; state cannot switch until its quarantined worker exits')
  await receipt('seed-xml')
  const malformed = await receipt('inspect')
  assert.equal(malformed.reason, 'journal-format')
  assert.equal((await quarantine(malformed.revision, 'public-xml-recovery')).changed, true)
  checks.push('malformed XML is archived without SharedPreferences overwriting or repairing its original')
  device('shell', 'am', 'force-stop', bundle)
  assert.equal((await receipt('inspect')).readable, true)
  assert.equal((await quarantine(malformed.revision, 'public-xml-recovery')).changed, false)
  const retained = await receipt('verify')
  assert.equal(retained.originalsRetained, true)
  assert.equal(retained.originalAliasesUnchanged, true)
  assert.equal(retained.remoteUnregisterAttempts, 0)
  checks.push('fresh selector and recovery receipts survive process loss; original ciphertext/key aliases remain unchanged')
  checks.push('no distributor unregister or server-revocation success is fabricated')
  console.log(JSON.stringify({
    serial,
    api: device('shell', 'getprop', 'ro.build.version.sdk').trim(),
    checks,
    scope: 'isolated public installation recovery; no product accounts, real endpoints, server grants, upgrade or reboot acceptance',
    apk,
    apkSha256: apk ? createHash('sha256').update(fs.readFileSync(apk)).digest('hex') : null
  }, null, 2))
} finally {
  device('shell', 'am', 'force-stop', bundle)
}
