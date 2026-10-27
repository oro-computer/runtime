import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { selectAndroidSerial, stageAndroidFixtures } from '../scripts/android-fixtures.js'

test('Android device selection refuses ambiguous, offline and unauthorized targets before mutation', () => {
  const devices = 'List of devices attached\r\npublic-phone\tdevice\r\nemulator-5564\tdevice\r\npublic-offline\toffline\r\npublic-locked\tunauthorized\r\n'
  assert.throws(() => selectAndroidSerial(devices), /Set ANDROID_SERIAL/)
  assert.equal(selectAndroidSerial(devices, 'emulator-5564'), 'emulator-5564')
  assert.equal(selectAndroidSerial(devices, 'public-phone'), 'public-phone')
  for (const serial of ['public-offline', 'public-locked', 'missing']) {
    assert.throws(() => selectAndroidSerial(devices, serial), /not connected and authorized/)
  }
  assert.equal(selectAndroidSerial('List of devices attached\npublic-phone device\n'), 'public-phone')
  assert.throws(() => selectAndroidSerial('List of devices attached\n'), /Set ANDROID_SERIAL/)
  assert.throws(() => stageAndroidFixtures('not-invoked', 'public-fixture', '.', ''), /explicit serial/)
})

function emulatorRun (exitCode, scenario = 'new') {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro emulator ownership '))
  const scripts = path.join(directory, 'test/scripts')
  const bin = path.join(directory, 'bin')
  mkdirSync(scripts, { recursive: true })
  mkdirSync(bin)
  const logfile = path.join(directory, 'adb.jsonl')
  const lifecycle = path.join(directory, 'lifecycle.txt')
  writeFileSync(path.join(directory, 'package.json'), '{"type":"module"}')
  writeFileSync(path.join(scripts, 'test-android-emulator.sh'), readFileSync(new URL('../scripts/test-android-emulator.sh', import.meta.url)))
  writeFileSync(path.join(scripts, 'bootstrap-android-emulator.sh'), `#!/usr/bin/env bash
    printf 'bootstrap-started\n' >> "$PUBLIC_LIFECYCLE"
    printf '0\n' > "$1"
    trap 'printf "bootstrap-stopped\\n" >> "$PUBLIC_LIFECYCLE"; exit 0' TERM INT
    while true; do sleep 0.05; done
  `)
  writeFileSync(path.join(scripts, 'test-android.js'), `
    import fs from 'node:fs'
    fs.appendFileSync(process.env.PUBLIC_LIFECYCLE, 'runner:' + process.env.ANDROID_SERIAL + '\\n')
    process.exit(Number(process.env.PUBLIC_RUN_EXIT))
  `)
  writeFileSync(path.join(bin, 'adb'), `#!${process.execPath}
    import fs from 'node:fs'
    const args = process.argv.slice(2)
    fs.appendFileSync(process.env.PUBLIC_ADB_LOG, JSON.stringify(args) + '\\n')
    if (args[0] === 'devices') {
      console.log('List of devices attached\\npublic-phone\\tdevice\\nemulator-5582\\tdevice')
      if (process.env.PUBLIC_EXISTING === '1') console.log('emulator-5564\\tdevice')
    } else if (args[0] === 'start-server') {
    } else if (args[0] !== '-s' || args[1] !== 'emulator-5564') process.exit(99)
    else if (args.includes('getprop')) console.log('1\\r')
    else if (args.includes('service')) console.log('Service ' + args.at(-1) + ': ' + (process.env.PUBLIC_FRAMEWORK === 'missing' ? 'not found' : 'found'))
  `, { mode: 0o755 })
  // Keep temporary test inputs for inspection; no real cleanup command runs.
  writeFileSync(path.join(bin, 'rm'), '#!/bin/sh\nexit 0\n', { mode: 0o755 })
  const env = {
    ...process.env,
    PATH: bin + path.delimiter + process.env.PATH,
    PUBLIC_LIFECYCLE: lifecycle,
    PUBLIC_ADB_LOG: logfile,
    PUBLIC_RUN_EXIT: String(exitCode),
    PUBLIC_EXISTING: scenario === 'existing' ? '1' : '0',
    PUBLIC_FRAMEWORK: scenario === 'framework' ? 'missing' : 'ready',
    ORO_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS: '1',
    ORO_ANDROID_EMULATOR_PORT: '5564',
    ANDROID_SERIAL: scenario === 'phone' ? 'public-phone' : 'emulator-5564',
    RUNNER_TEMP: path.join(directory, 'runner')
  }
  const result = spawnSync('bash', [path.join(scripts, 'test-android-emulator.sh')], {
    cwd: directory, env, encoding: 'utf8', timeout: 15000
  })
  assert.ifError(result.error)
  const calls = existsSync(logfile) ? readFileSync(logfile, 'utf8').trim().split('\n').filter(Boolean).map(line => JSON.parse(line)) : []
  const events = existsSync(lifecycle) ? readFileSync(lifecycle, 'utf8') : ''
  return { result, calls, events }
}

for (const exitCode of [0, 17]) {
  test(`emulator runner scopes all device calls and stops only its bootstrap on exit ${exitCode}`, {
    skip: process.platform === 'win32' ? 'Requires POSIX shell process signals' : false
  }, () => {
    const { result, calls, events } = emulatorRun(exitCode)
    assert.equal(result.status, exitCode, result.stderr || result.stdout)
    assert.ok(events.includes('runner:emulator-5564'))
    assert.ok(events.includes('bootstrap-stopped'))
    for (const call of calls.filter(args => !['devices', 'start-server'].includes(args[0]))) {
      assert.deepEqual(call.slice(0, 2), ['-s', 'emulator-5564'])
    }
    assert.ok(!calls.some(args => args.includes('kill-server') || args.includes('emu')))
  })
}

for (const scenario of ['existing', 'phone']) {
  test(`emulator runner refuses ${scenario} target without starting or stopping any emulator`, {
    skip: process.platform === 'win32' ? 'Requires POSIX shell fixture' : false
  }, () => {
    const { result, calls, events } = emulatorRun(0, scenario)
    assert.equal(result.status, 1)
    assert.equal(events, '')
    assert.ok(calls.every(args => args[0] === 'devices'))
  })
}

test('emulator runner refuses boot-complete without package/activity services', {
  skip: process.platform === 'win32' ? 'Requires POSIX shell fixture' : false
}, () => {
  const { result, events } = emulatorRun(0, 'framework')
  assert.equal(result.status, 124, result.stderr || result.stdout)
  assert.ok(!events.includes('runner:'))
  assert.ok(events.includes('bootstrap-stopped'))
})
