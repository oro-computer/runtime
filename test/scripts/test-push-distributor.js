// Public-device qualification. Provide the two APKs, the local HTTPS server's
// certificate and an explicit Android target. No application accounts are used.
import assert from 'node:assert/strict'
import { execFile, spawn } from 'node:child_process'
import { promisify } from 'node:util'
import { readFile, writeFile } from 'node:fs/promises'
import { createCipheriv, createECDH, createHash, createHmac, randomBytes } from 'node:crypto'
import https from 'node:https'
import path from 'node:path'

const execute = promisify(execFile)
const serial = process.env.ANDROID_SERIAL
const adb = process.env.ADB || path.join(process.env.ANDROID_HOME || '', 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb')
const distributor = process.env.ORO_TEST_DISTRIBUTOR_APK
const connector = process.env.ORO_TEST_PUSH_CONNECTOR_APK
const certificate = process.env.ORO_TEST_PUSH_CA
const origin = new URL(process.env.ORO_TEST_PUSH_ORIGIN || 'https://127.0.0.1:18881')
const output = process.env.ORO_TEST_PUSH_REPORT
const application = 'dev.oro.push.qualification'
let instrumentation

function delay (milliseconds) { return new Promise(resolve => setTimeout(resolve, milliseconds)) }
async function android (args) {
  const result = await execute(adb, ['-s', serial, ...args], { timeout: 60000, maxBuffer: 2 * 1024 * 1024 })
  return result.stdout
}
async function state () {
  try { return JSON.parse(await android(['shell', 'run-as', application, 'cat', 'files/public-results.json'])) } catch { return {} }
}
async function pending () {
  const xml = await android(['shell', 'run-as', 'dev.oro.push', 'cat', 'shared_prefs/oro.push.development.v1.xml'])
  const encoded = /<string name="state">([\s\S]*?)<\/string>/.exec(xml)?.[1]
  if (!encoded) return 0
  const decoded = encoded.replaceAll('&quot;', '"').replaceAll('&apos;', "'").replaceAll('&lt;', '<').replaceAll('&gt;', '>').replaceAll('&amp;', '&')
  return JSON.parse(decoded).pending.length
}
async function waitFor (description, predicate, timeout = 20000) {
  const deadline = Date.now() + timeout
  while (Date.now() < deadline) {
    if (await predicate()) return
    await delay(150)
  }
  throw new Error(`Timed out: ${description}`)
}
async function operate (operation, name = 'alpha') {
  if (operation === 'link') await android(['shell', 'am', 'start', '-n', `${application}/.MainActivity`, '--es', 'operation', operation])
  else await android(['shell', 'am', 'broadcast', '--receiver-foreground', '-a', `${application}.OPERATE`, '-p', application, '--es', 'operation', operation, '--es', 'name', name])
}

async function register (name) {
  for (let attempt = 0; attempt < 7; attempt++) {
    await operate('register', name)
    await waitFor(`${name} registration outcome`, async () => ['registered', 'failed'].includes((await state())[name]?.phase), 30000)
    const result = (await state())[name]
    if (result.phase === 'registered') return
    assert.equal(result.reason, 'NETWORK', `Unexpected ${name} registration failure`)
    // The public connector rotates its token after failure, as required by UP.
    console.log(`Retrying ${name} after a network registration outcome`)
    await delay(1000)
  }
  throw new Error(`${name} did not register after bounded network retries`)
}

// RFC 8291 encryption of fixed public data; the fixture decrypts it using the
// runtime's unmodified PushCryptography source, with private keys on Android.
function encrypt (keys) {
  const server = createECDH('prime256v1')
  const publicKey = server.generateKeys()
  const recipient = Buffer.from(keys.p256dh, 'base64url')
  const auth = Buffer.from(keys.auth, 'base64url')
  const mac = (key, message) => createHmac('sha256', key).update(message).digest()
  const expand = (key, info, length) => mac(key, Buffer.concat([info, Buffer.from([1])])).subarray(0, length)
  const key = expand(mac(auth, server.computeSecret(recipient)), Buffer.concat([Buffer.from('WebPush: info\0'), recipient, publicKey]), 32)
  const salt = randomBytes(16)
  const secret = mac(salt, key)
  const cipher = createCipheriv('aes-128-gcm', expand(secret, Buffer.from('Content-Encoding: aes128gcm\0'), 16),
    expand(secret, Buffer.from('Content-Encoding: nonce\0'), 12))
  const record = Buffer.concat([cipher.update(Buffer.concat([Buffer.from('Public Oro push development check.'), Buffer.from([2])])), cipher.final(), cipher.getAuthTag()])
  const size = Buffer.alloc(4)
  size.writeUInt32BE(4096)
  return Buffer.concat([salt, size, Buffer.from([65]), publicKey, record])
}

async function publish (enrollment, ca) {
  const endpoint = new URL(enrollment.endpoint)
  assert.equal(endpoint.origin, origin.origin, 'Fixture endpoint must use the selected local test server')
  const bytes = encrypt(enrollment.keys)
  return await new Promise((resolve, reject) => {
    const request = https.request(endpoint, { ca, method: 'POST', headers: { 'Content-Encoding': 'aes128gcm', TTL: '60', 'Content-Length': bytes.length } }, response => {
      let body = ''
      response.on('data', chunk => { body += chunk })
      response.on('end', () => {
        try { assert.equal(response.statusCode, 200, body); resolve(JSON.parse(body)) } catch (error) { reject(error) }
      })
    })
    request.setTimeout(5000, () => request.destroy(new Error('Publish timeout')))
    request.on('error', reject)
    request.end(bytes)
  })
}

async function enable () {
  await android(['shell', 'pm', 'grant', 'dev.oro.push', 'android.permission.POST_NOTIFICATIONS'])
  if (process.env.ORO_TEST_PUSH_DEBUG_SETUP === '1') {
    await android(['shell', 'am', 'force-stop', 'dev.oro.push'])
    instrumentation = spawn(adb, ['-s', serial, 'shell', 'am', 'instrument', '-w', '-r', '-e', 'server', origin.origin, '-e', 'transportOnly', 'true', `${application}/.EnableDistributor`])
    let result = ''
    instrumentation.stdout.on('data', bytes => { result += bytes })
    instrumentation.stderr.on('data', bytes => { result += bytes })
    await waitFor('transport instrumentation setup', async () => {
      if (instrumentation.exitCode !== null) throw new Error(result)
      return /transportSetup=passed/.test(result)
    }, 60000)
  } else {
    const result = await android(['shell', 'am', 'instrument', '-w', '-r', '-e', 'server', origin.origin, `${application}/.EnableDistributor`])
    assert.match(result, /settingsAction=passed/, result)
  }
  if (process.env.ORO_TEST_PUSH_DEBUG_SETUP !== '1') await android(['shell', 'am', 'start', '-n', 'dev.oro.push/.SettingsActivity'])
}

async function main () {
  assert.ok(serial && distributor && connector && certificate, 'Set ANDROID_SERIAL, ORO_TEST_DISTRIBUTOR_APK, ORO_TEST_PUSH_CONNECTOR_APK and ORO_TEST_PUSH_CA')
  assert.equal(origin.protocol, 'https:')
  assert.equal(origin.hostname, '127.0.0.1', 'Use the local test server through the selected adb reverse mapping')
  assert.equal((await android(['shell', 'getprop', 'sys.boot_completed'])).trim(), '1', 'Android must finish booting first')
  const port = origin.port || '443'
  const ca = await readFile(certificate)
  const report = {
    serial,
    scope: 'Public standalone distributor and connector; Android OS version below',
    setup: process.env.ORO_TEST_PUSH_DEBUG_SETUP === '1'
      ? 'Signed instrumentation configures and starts the distributor; Settings UI action is unverified'
      : 'Signed instrumentation invoked the Settings action',
    checks: []
  }
  report.androidApi = (await android(['shell', 'getprop', 'ro.build.version.sdk'])).trim()
  if (process.env.ORO_TEST_PUSH_SKIP_INSTALL === '1') {
    for (const [id, apk] of [['dev.oro.push', distributor], [application, connector]]) {
      const installed = (await android(['shell', 'pm', 'path', id])).trim().replace(/^package:/, '')
      assert.match(installed, /^\/data\/app\/[A-Za-z0-9_~./+=-]+\.apk$/)
      const hash = (await android(['shell', 'sha256sum', installed])).trim().split(/\s+/)[0]
      assert.equal(hash, createHash('sha256').update(await readFile(apk)).digest('hex'), `${id} installed APK must match the selected artifact`)
    }
    console.log('Verified the already installed public APK hashes')
  } else {
    await android(['install', '-r', distributor])
    await android(['install', '-r', connector])
    console.log('Installed the public distributor and connector APKs')
  }
  await android(['reverse', `tcp:${port}`, `tcp:${port}`])
  try {
    await enable()
    console.log('Enabled the isolated development distributor')
    await android(['shell', 'am', 'force-stop', application])
    const previous = await state()
    if (process.env.ORO_TEST_PUSH_DEBUG_SETUP !== '1') {
      await operate('link')
      await waitFor('UnifiedPush selection', async () => (await state()).linkCount > (previous.linkCount || 0))
      assert.equal((await state()).linkPassed, true)
      console.log('Verified UnifiedPush selection identity')
      report.checks.push('UnifiedPush link result with immutable distributor identity')
    } else await android(['shell', 'am', 'start', '-n', `${application}/.MainActivity`, '--es', 'operation', 'unstop'])
    await operate('protocol')
    await waitFor('Kotlin wire boundaries', async () => (await state()).protocolRuns > (previous.protocolRuns || 0))
    assert.equal((await state()).protocolPassed, true)
    for (const name of ['alpha', 'beta']) {
      await register(name)
    }
    let current = await state()
    const alpha = current.alpha
    const beta = current.beta
    assert.notEqual(alpha.endpoint, beta.endpoint)
    console.log('Registered two public subscriptions')
    await operate('register', 'alpha')
    await waitFor('stable repeat registration', async () => (await state()).alpha?.phase === 'registered')
    assert.equal((await state()).alpha.endpoint, alpha.endpoint)
    report.checks.push('Distinct tokens, stable repeated endpoint, verified caller identity')
    await publish(alpha, ca)
    await waitFor('encrypted alpha delivery', async () => (await state()).alpha.count > alpha.count)
    assert.equal((await state()).alpha.plaintextMatched, true)
    await waitFor('alpha acknowledgement', async () => await pending() === 0)
    report.checks.push('HTTPS server to native receiver with RFC 8291 authenticated decryption and ACK')
    await operate('hold', 'beta')
    const published = await publish(beta, ca)
    await waitFor('unacknowledged beta delivery', async () => (await state()).beta.count > beta.count && await pending() > 0)
    assert.match(beta.token, /^[a-zA-Z0-9-]+$/)
    assert.match(published.id, /^[a-zA-Z0-9_-]+$/)
    await android(['shell', 'am', 'broadcast', '-a', 'org.unifiedpush.android.distributor.MESSAGE_ACK', '-p', 'dev.oro.push', '--es', 'token', beta.token, '--es', 'id', published.id])
    assert.ok(await pending() > 0, 'Unproven sender cannot acknowledge another app message')
    const beforeRestart = (await state()).beta.count
    await android(['shell', 'am', 'force-stop', 'dev.oro.push'])
    if (process.env.ORO_TEST_PUSH_DEBUG_SETUP === '1') await enable()
    else await android(['shell', 'am', 'start', '-n', 'dev.oro.push/.SettingsActivity'])
    await waitFor('retained ciphertext after force-stop and explicit reopen', async () => (await state()).beta.count > beforeRestart)
    assert.equal((await state()).beta.id, published.id)
    await operate('ack', 'beta')
    await waitFor('retained ciphertext acknowledged', async () => await pending() === 0)
    report.checks.push('Forged ACK rejected; pending ciphertext survives process stop; retry retains ID; ACK drains inbox')
    await operate('unregister', 'alpha')
    await waitFor('alpha unregistration', async () => (await state()).alpha.phase === 'unregistered')
    current = await state()
    await publish(alpha, ca)
    await delay(1500)
    assert.equal((await state()).alpha.count, current.alpha.count)
    assert.ok(!(await state()).failure)
    report.checks.push('Unregistration stops later delivery')
    report.distributorApkSha256 = createHash('sha256').update(await readFile(distributor)).digest('hex')
    report.connectorApkSha256 = createHash('sha256').update(await readFile(connector)).digest('hex')
    report.unverified = ['reboot and Doze', 'device-vendor battery policies', 'physical touch and permission prompt interaction', 'production native handlers and authenticated backend enrollment', 'other Android versions', 'installed ntfy Android distributor']
    if (process.env.ORO_TEST_PUSH_DEBUG_SETUP === '1') report.unverified.push('Interactive Settings enablement and UnifiedPush selection; normal uninstrumented service startup')
    if (output) await writeFile(output, JSON.stringify(report, null, 2) + '\n')
    console.log(`Passed ${report.checks.length} Android UnifiedPush development qualification groups`)
  } finally {
    for (const name of ['alpha', 'beta']) await operate('unregister', name).catch(() => {})
    await android(['reverse', '--remove', `tcp:${port}`]).catch(() => {})
    if (instrumentation) {
      await android(['shell', 'am', 'broadcast', '-a', `${application}.STOP`, '-p', 'dev.oro.push']).catch(() => {})
      instrumentation.kill('SIGTERM')
    }
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1 })
