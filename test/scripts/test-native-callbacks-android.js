import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'
import { randomUUID, createHash } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { selectAndroidSerial } from './android-fixtures.js'

// This driver operates only on the checked-in, isolated public-data fixture.
const bundle = 'oro.runtime.qualification.callbacks'
const adb = process.env.ADB || 'adb'
const serial = selectAndroidSerial(run(adb, ['devices']), process.env.ANDROID_SERIAL)
const checks = []
function run (command, args, timeout = 120000) {
  const result = spawnSync(command, args, { encoding: 'utf8', timeout, maxBuffer: 1024 * 1024 })
  assert.equal(result.status, 0, `${command}: ${result.error || ''}\n${result.stdout}${result.stderr}`)
  return result.stdout
}
function device (...args) { return run(adb, ['-s', serial, ...args]) }
function pause () { return new Promise(resolve => setTimeout(resolve, 250)) }
function decode (text) {
  return text.replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&')
}
async function receipt (mode) {
  const request = randomUUID()
  device('shell', 'am', 'broadcast', '--include-stopped-packages', '-n', `${bundle}/.PublicCallbackReceiver`,
    '--es', 'mode', mode, '--es', 'requestId', request)
  const until = Date.now() + 60000
  while (Date.now() < until) {
    const raw = spawnSync(adb, ['-s', serial, 'shell', 'run-as', bundle, 'cat', 'shared_prefs/oro.public.callbacks.xml'], { encoding: 'utf8', timeout: 10000 })
    const match = raw.stdout?.match(/<string name="receipt">([\s\S]*?)<\/string>/)
    if (match) {
      const result = JSON.parse(decode(match[1]))
      if (result.requestId === request) {
        assert.notEqual(result.failed, true, `Public fixture failed: ${mode}`)
        return result
      }
    }
    await pause()
  }
  throw new Error(`No fresh public receipt for ${mode}`)
}
async function poll (predicate, description, timeout = 30000) {
  const until = Date.now() + timeout
  while (Date.now() < until) {
    const value = await receipt('read')
    if (predicate(value)) return value
    await pause()
  }
  throw new Error(description)
}
const apk = process.argv[2] && path.resolve(process.argv[2])
const bootDeadline = Date.now() + 600000
while (device('shell', 'getprop', 'sys.boot_completed').trim() !== '1') {
  assert.ok(Date.now() < bootDeadline, 'Selected Android device did not complete boot within ten minutes')
  await new Promise(resolve => setTimeout(resolve, 1000))
}
if (apk) {
  assert.ok(fs.statSync(apk).isFile())
  const packageName = run(process.env.AAPT || 'aapt', ['dump', 'badging', apk]).match(/package: name='([^']+)'/)?.[1]
  assert.equal(packageName, bundle)
  run(adb, ['-s', serial, 'install', '--no-incremental', '-r', '-g', apk], 180000)
}
device('shell', 'svc', 'power', 'stayon', 'true')
device('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP')
device('shell', 'am', 'force-stop', bundle)
assert.equal((await receipt('read')).initialized, true)
checks.push('cold Application startup and native encrypted records without an Activity')
assert.equal((await receipt('interop-outside')).outsideInteropDenied, true)
checks.push('JNI Application access denied outside an owned callback')
assert.equal((await receipt('push')).pushPassed, true)
checks.push('public native push callback dispatch through JNI; no transport claim')
await receipt('recovery-seed')
assert.equal((await receipt('recovery-discover')).redactedRecoveryPassed, true)
await receipt('recovery-seed')
assert.equal((await receipt('recovery-stale')).staleRecoveryPassed, true)
assert.equal((await receipt('recovery-remove')).recoveryCleanupPassed, true)
await receipt('recovery-seed')
assert.equal((await receipt('recovery-stale')).staleRecoveryPassed, true)
assert.equal((await receipt('recovery-remove')).recoveryCleanupPassed, true)
checks.push('scoped native recovery, renewal fencing, ID reuse and conditional local cleanup')
const initial = await receipt('read')
await receipt('push-drain-seed')
await poll(value => value.authorizing > initial.authorizing, 'Native push authorizer did not start')
assert.equal((await receipt('push-drain-stop')).authorizationStopSubmitted, true)
await poll(value => value.authorizationExited > initial.authorizationExited, 'Unregister did not interrupt push authorization')
checks.push('unregister cancels an in-flight native push authorizer')
if (process.env.ORO_ANDROID_NATIVE_COLD_ONLY !== '1') {
  device('shell', 'am', 'start', '-n', `${bundle}/.MainActivity`)
  const running = await poll(value => value.starts >= initial.starts + 3 && value.ticks > initial.ticks && value.deferred > initial.deferred,
    'Public foreground, continuous and deferred native callbacks did not execute', 90000)
  checks.push('real shared foreground, continuous and JobScheduler deferred controllers dispatch compiled C++')
  checks.push('bundled page dispatches compiled native extension with scoped Application JNI access')
  if (process.env.ORO_ANDROID_NATIVE_ROTATION_LOSS === '1') {
    const automatic = device('shell', 'settings', 'get', 'system', 'accelerometer_rotation').trim()
    const rotation = device('shell', 'settings', 'get', 'system', 'user_rotation').trim()
    assert.match(automatic, /^[01]$/)
    assert.match(rotation, /^[0-3]$/)
    const generation = (await receipt('status')).generation
    async function windowState (predicate, description) {
      const until = Date.now() + 30000
      while (Date.now() < until) {
        const state = await receipt('lifecycle')
        if (state.activityAvailable && predicate(state)) return state
        await pause()
      }
      throw new Error(description)
    }
    try {
      device('shell', 'settings', 'put', 'system', 'accelerometer_rotation', '0')
      for (const [angle, orientation] of [['1', 2], ['0', 1]]) {
        const before = await receipt('read')
        device('shell', 'settings', 'put', 'system', 'user_rotation', angle)
        await windowState(state => state.orientation === orientation && state.windows === 1 && state.visibleWindows === 1,
          'Rotation did not retain exactly one visible public window')
        const after = await poll(value => value.ticks > before.ticks + 2 && !value.cleanupFailed,
          'Native execution or encrypted records stopped during rotation')
        assert.equal(after.starts, running.starts)
        assert.equal((await receipt('status')).generation, generation)
      }
    } finally {
      device('shell', 'settings', 'put', 'system', 'user_rotation', rotation)
      device('shell', 'settings', 'put', 'system', 'accelerometer_rotation', automatic)
    }
    checks.push('portrait and landscape retain one visible public window without replaying native execution')
    checks.push('native callbacks and Application-bound encrypted records continue through rotation')
    const activity = await receipt('recreate')
    assert.equal(activity.activityAvailable, true)
    await windowState(state => state.activityInstance !== activity.activityInstance && state.windows === 1 && state.visibleWindows === 1,
      'Activity recreation did not restore one public window')
    const recreated = await poll(value => value.ticks > running.ticks + 5 && !value.cleanupFailed,
      'Native callbacks failed after Activity recreation')
    assert.equal(recreated.starts, running.starts)
    const active = await receipt('status')
    assert.equal(active.generation, generation)
    assert.equal(active.phase, 'running')
    checks.push('Activity recreation preserves the native generation, worker and key binding without duplicate execution')
    device('shell', 'am', 'force-stop', bundle)
    const interrupted = await receipt('status')
    assert.equal(interrupted.generation, generation)
    assert.equal(interrupted.phase, 'unknown')
    assert.equal(interrupted.outcome, 'unknown')
    const retained = await receipt('read')
    for (let i = 0; i < 4; i++) await pause()
    const later = await receipt('read')
    assert.equal(later.ticks, retained.ticks)
    assert.equal(later.starts, retained.starts)
    checks.push('process loss during active work retains encrypted records and an uncertain generation without automatic replay')
  } else {
    device('shell', 'input', 'keyevent', 'KEYCODE_HOME')
    await poll(value => value.ticks > running.ticks + 2 && !value.cleanupFailed, 'Native records or callbacks failed during Activity pause')
    checks.push('Activity pause retains callback execution and Application-bound Keystore access')
    const stop = await receipt('stop')
    assert.equal(stop.phase, 'unknown')
    assert.equal(stop.outcome, 'unknown')
    const stopped = await poll(value => value.closed >= initial.closed + 3 && !value.cleanupFailed, 'Owned callback cleanup did not finish')
    for (let i = 0; i < 4; i++) await pause()
    assert.equal((await receipt('read')).ticks, stopped.ticks)
    checks.push('exact-generation stop drains callback resources and stops execution')
    device('shell', 'am', 'force-stop', bundle)
    const retained = await receipt('read')
    assert.equal(retained.ticks, stopped.ticks)
    assert.equal(retained.pushes, stopped.pushes)
    checks.push('native encrypted records persist across process loss without replaying stopped work')
    const outcome = await receipt('status')
    assert.equal(outcome.generation, stop.generation)
    assert.equal(outcome.phase, 'unknown')
    assert.equal(outcome.outcome, 'unknown')
    checks.push('explicit stop retains the same generation and uncertain outcome across process loss')
  }
}
assert.equal((await receipt('key-lifetime')).keyLifetimePassed, true)
checks.push('retained native record handle loses key access after native backend shutdown')
device('shell', 'am', 'force-stop', bundle)
const report = {
  qualificationMode: process.env.ORO_ANDROID_NATIVE_COLD_ONLY === '1' ? 'cold' : process.env.ORO_ANDROID_NATIVE_ROTATION_LOSS === '1' ? 'rotation-recreation-process-loss' : 'normal-lifecycle',
  scope: 'isolated public Android fixture; account policy, historical product migration, remote transport and reboot not established',
  serial,
  api: device('shell', 'getprop', 'ro.build.version.sdk').trim(),
  checks,
  apk: apk || null,
  apkSha256: apk ? createHash('sha256').update(fs.readFileSync(apk)).digest('hex') : null
}
console.log(JSON.stringify(report, null, 2))
