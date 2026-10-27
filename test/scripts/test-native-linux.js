import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawn, spawnSync } from 'node:child_process'
import { createInterface } from 'node:readline'
import { createECDH, createCipheriv, randomBytes, hkdfSync } from 'node:crypto'

const root = fileURLToPath(new URL('../../', import.meta.url))

function run (program, args, options = {}) {
  const result = spawnSync(program, args, { cwd: root, encoding: 'utf8', timeout: 120000, ...options })
  assert.equal(result.status, 0, `${program}: ${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
  return result.stdout
}

if (process.argv[2] !== '--driver') {
  assert.equal(process.platform, 'linux')
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-linux-native-'))
  const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
  const libraries = path.join(root, 'build', `${architecture}-desktop/lib`)
  const archive = process.env.ORO_TEST_NATIVE_RUNTIME_ARCHIVE || path.join(libraries, 'liboro-runtime.a')
  const flags = name => run('bash', [path.join(root, 'bin', name)]).trim().split(/\s+/).filter(Boolean)
  const binaries = []
  for (const fixture of ['LinuxPlatform', 'DesktopNotifications', 'Deferred']) {
    const binary = path.join(directory, fixture)
    run(process.env.ORO_TEST_CXX || 'clang++', [...flags('cflags.sh'), '-I' + root,
      path.join(root, `test/fixtures/native-records/${fixture}.cc`), '-Wl,--start-group', archive,
      ...['libuv.a', 'libusb-1.0.a', 'libllama.a', 'libwhisper.a', 'libmbedtls.a', 'libmbedx509.a', 'libmbedcrypto.a'].map(name => path.join(libraries, name)),
      '-Wl,--end-group', ...flags('ldflags.sh'), '-o', binary])
    binaries.push(binary)
  }
  console.log(run(binaries.pop(), [directory]).trim())
  for (const name of ['data', 'config', 'run', 'control', 'state']) fs.mkdirSync(path.join(directory, name), { mode: 0o700 })
  const env = {
    ...process.env,
    XDG_DATA_HOME: path.join(directory, 'data'),
    XDG_CONFIG_HOME: path.join(directory, 'config'),
    XDG_RUNTIME_DIR: path.join(directory, 'run'),
    ORO_STATE_DIR: path.join(directory, 'state'),
    GNOME_KEYRING_CONTROL: path.join(directory, 'control')
  }
  console.log(run('dbus-run-session', ['--', process.execPath, fileURLToPath(import.meta.url), '--driver', ...binaries, directory], { env }).trim())
  console.log('Public native Linux acceptance artifacts:', directory)
} else {
  const [, , , binary, notificationBinary, directory] = process.argv
  run('gnome-keyring-daemon', ['--unlock', '--components=secrets', '--control-directory=' + process.env.GNOME_KEYRING_CONTROL], {
    input: 'public-runtime-test-password'
  })

  function start (program, args, ready) {
    const child = spawn(program, args, { stdio: ['pipe', 'pipe', 'pipe'] })
    const output = createInterface({ input: child.stdout })
    const waiting = []
    const log = []
    child.stderr.on('data', chunk => { if (log.join('').length < 1048576) log.push(chunk.toString()) })
    const initialized = new Promise((resolve, reject) => {
      child.once('exit', code => reject(new Error(`Native process exited (${code}): ${log.join('')}`)))
      output.on('line', line => {
        if (line === ready) resolve()
        else if (line.startsWith('{') && waiting.length) waiting.shift()(JSON.parse(line))
        else log.push(line + '\n')
      })
    })
    return {
      child,
      initialized,
      async raw (message) {
        const result = new Promise(resolve => waiting.push(resolve))
        child.stdin.write(JSON.stringify(message) + '\n')
        return result
      },
      async request (feature, action, options = {}) {
        const result = await this.raw({ feature, action, options })
        if (result.error) throw Object.assign(new Error(result.error), { code: result.error })
        return result.data
      },
      async quit () {
        const exited = new Promise(resolve => child.once('exit', resolve))
        child.stdin.end('{"action":"__quit"}\n')
        assert.equal(await exited, 0, log.join(''))
      },
      log
    }
  }

  async function until (check) {
    const expires = Date.now() + 10000
    while (true) {
      if (await check()) return
      assert.ok(Date.now() < expires, 'Native acceptance deadline exceeded')
      await new Promise(resolve => setTimeout(resolve, 20))
    }
  }

  function encrypted (enrollment) {
    const sender = createECDH('prime256v1')
    sender.generateKeys()
    const receiver = Buffer.from(enrollment.keys.p256dh, 'base64url')
    const authentication = Buffer.from(enrollment.keys.auth, 'base64url')
    const secret = sender.computeSecret(receiver)
    const ikm = Buffer.from(hkdfSync('sha256', secret, authentication,
      Buffer.concat([Buffer.from('WebPush: info\0'), receiver, sender.getPublicKey()]), 32))
    const salt = randomBytes(16)
    const key = Buffer.from(hkdfSync('sha256', ikm, salt, Buffer.from('Content-Encoding: aes128gcm\0'), 16))
    const nonce = Buffer.from(hkdfSync('sha256', ikm, salt, Buffer.from('Content-Encoding: nonce\0'), 12))
    const cipher = createCipheriv('aes-128-gcm', key, nonce)
    const body = Buffer.concat([cipher.update(Buffer.concat([Buffer.from('public-message'), Buffer.from([2])])), cipher.final(), cipher.getAuthTag()])
    const header = Buffer.alloc(21)
    salt.copy(header)
    header.writeUInt32BE(4096, 16)
    header[20] = 65
    return Buffer.concat([header, sender.getPublicKey(), body]).toString('base64url')
  }

  const distributor = start(binary, ['server'], 'public-distributor-ready')
  const notifications = start(notificationBinary, ['server'], 'notification-mock-ready')
  let client
  try {
    await Promise.all([distributor.initialized, notifications.initialized])
    client = start(binary, [], 'public-native-client-ready')
    await client.initialized
    const network = (await client.raw({ action: '__network' })).data
    assert.equal(network.platform, 'linux')
    assert.equal(typeof network.validated, 'boolean')
    assert.deepEqual(network.transports, [])
    assert.equal(network.serverReachabilityKnown, false)
    const supported = await client.request('foreground', 'support')
    assert.equal(supported.available, true)
    assert.equal(supported.canStart, true)
    const task = { id: 'finite', kind: 'public.read', authorityRef: 'authority', accountRef: 'account', targetRef: 'target', operationRef: 'read', userInitiated: true, durationMs: 1000 }
    const admitted = await client.request('foreground', 'start', task)
    await until(async () => (await client.request('foreground', 'status', { id: task.id })).phase === 'completed')
    await client.request('foreground', 'release', { id: task.id, generation: admitted.generation })
    const monitor = { ...task, id: 'monitor', kind: 'public.monitor' }
    delete monitor.durationMs
    const monitoring = await client.request('foreground', 'continuous.start', monitor)
    await assert.rejects(client.request('foreground', 'continuous.stop', { id: monitor.id, generation: 'old' }), { code: 'ESTALE' })
    assert.equal((await client.request('foreground', 'continuous.stop', { id: monitor.id, generation: monitoring.generation })).outcome, 'unknown')
    await until(async () => {
      const result = await client.raw({ feature: 'foreground', action: 'continuous.release', options: { id: monitor.id, generation: monitoring.generation } })
      return !result.error
    })
    assert.equal((await client.raw({ action: '__cancel' })).data, true)
    assert.deepEqual(await client.request('extension', 'metadata', { extension: 'public.metadata', payload: {} }), { public: true })
    await client.request('notifications', 'configureChannels', { channels: [{ id: 'public.alerts', name: 'Public alerts', sound: 'default' }] })
    await assert.rejects(client.request('notifications', 'configureChannels', { channels: [{ id: 'public.alerts', name: 'Changed' }] }), { code: 'EEXIST' })
    assert.equal((await client.request('notifications', 'submit', { id: 'public', channel: 'public.alerts', title: 'Public fixture' })).submitted, true)
    assert.equal((await client.request('notifications', 'deliveryStatus', { channel: 'public.alerts' })).systemPolicyKnown, false)
    const redacted = await client.request('notifications', 'submit', {
      id: 'redacted',
      channel: 'public.alerts',
      title: 'Private must be discarded',
      body: 'Private must be discarded',
      publicVersion: { title: 'Public fixture', body: 'Public summary' }
    })
    assert.equal(redacted.presentationObserved, false)
    assert.equal(redacted.visibilityPolicy, 'public-version-on-all-surfaces')
    await assert.rejects(client.request('notifications', 'submit', { id: 'group', title: 'Public', groupSummary: true }), { code: 'ENOSYS' })
    await client.request('notifications', 'cancel', { id: 'public' })
    await assert.rejects(client.request('notifications', 'cancel', { id: 'oro.runtime.foreground' }), { code: 'EACCES' })
    await assert.rejects(client.request('notifications', 'submit', { id: 'oro.runtime.foreground', title: 'Public' }), { code: 'EACCES' })

    assert.equal((await client.request('launcher', 'status')).available, true)
    const destinations = [{ id: 'public', label: 'Public test destination', accountRef: 'account', targetRef: 'target' }]
    assert.equal((await client.request('launcher', 'replace', { destinations })).published, 1)
    const desktopPath = path.join(process.env.XDG_DATA_HOME, 'applications/org.oro.PublicRuntime.oro-destinations.desktop')
    const desktop = fs.readFileSync(desktopPath, 'utf8')
    run('desktop-file-validate', [desktopPath])
    assert.ok(!desktop.includes('accountRef') && !desktop.includes('targetRef'))
    const launchURI = /oro-public:\/\/oro-native-launcher\/[a-f0-9-]+\/public/.exec(desktop)?.[0]
    assert.ok(launchURI)
    assert.equal(await client.request('launcher', 'receive', { uri: launchURI }), true)
    let entries = await client.request('launcher', 'responses')
    assert.equal(entries.length, 1)
    assert.equal(entries[0].accountRef, 'account')
    await client.request('launcher', 'acknowledge', { receiptId: entries[0].receiptId })
    assert.deepEqual(await client.request('launcher', 'responses'), [])
    await client.request('launcher', 'replace', { destinations })
    assert.equal(fs.readFileSync(desktopPath, 'utf8'), desktop)
    await client.request('launcher', 'replace', { destinations: [] })
    await assert.rejects(client.request('launcher', 'receive', { uri: launchURI }), { code: 'ESTALE' })
    assert.ok(!fs.readFileSync(desktopPath, 'utf8').includes('[Desktop Action public]'))
    await client.request('launcher', 'replace', { destinations })
    const retainedLaunch = /oro-public:\/\/oro-native-launcher\/[a-f0-9-]+\/public/.exec(fs.readFileSync(desktopPath, 'utf8'))[0]
    assert.equal(await client.request('launcher', 'receive', { uri: retainedLaunch }), true)

    const deferred = { id: 'deferred', kind: 'public.reconcile', authorityRef: 'authority', accountRef: 'account', targetRef: 'target', operationRef: 'read', network: 'any', requiresBatteryNotLow: false }
    assert.equal((await client.request('deferred', 'support')).available, true)
    await client.request('deferred', 'enqueue', deferred)
    await until(async () => (await client.request('deferred', 'status', { id: deferred.id })).phase === 'completed')
    await client.request('deferred', 'release', { id: deferred.id })

    const vapid = createECDH('prime256v1')
    vapid.generateKeys()
    const registration = { id: 'public', kind: 'public.alert', authorityRef: 'authority', accountRef: 'account', targetRef: 'target', applicationServerKey: vapid.getPublicKey().toString('base64url'), description: 'Public test' }
    assert.deepEqual(await client.request('push', 'distributors'), ['org.unifiedpush.Distributor.OroPublicTest'])
    await client.request('push', 'selectDistributor', { distributor: 'org.unifiedpush.Distributor.OroPublicTest' })
    await client.request('push', 'register', registration)
    let enrollment
    await until(async () => {
      enrollment = await client.request('push', 'registration', { id: registration.id })
      return enrollment.phase === 'registered'
    })
    assert.equal(enrollment.keys.p256dh.length, 87)
    assert.equal(enrollment.keys.auth.length, 22)
    assert.equal('privateKey' in enrollment, false)
    assert.equal('token' in enrollment, false)
    const firstGeneration = enrollment.generation
    await client.request('push', 'acknowledgeRegistration', { id: registration.id, generation: firstGeneration })
    const payload = encrypted(enrollment)
    await client.raw({ action: '__forge', payload })
    await new Promise(resolve => setTimeout(resolve, 80))
    assert.equal((await client.raw({ action: '__count' })).data, 0)
    await client.raw({ action: '__deliver', payload })
    await until(async () => (await client.raw({ action: '__count' })).data === 1 && (await client.request('push', 'registration', { id: registration.id })).pendingMessages === 0)
    await client.raw({ action: '__deliver', payload })
    await new Promise(resolve => setTimeout(resolve, 80))
    assert.equal((await client.raw({ action: '__count' })).data, 1)
    await client.raw({ action: '__allow', allowed: false })
    await client.raw({ action: '__deliver', payload: encrypted(enrollment) })
    await until(async () => (await client.request('push', 'registration', { id: registration.id })).pendingMessages === 1)
    assert.equal((await client.raw({ action: '__count' })).data, 1)
    await client.raw({ action: '__allow', allowed: true })
    await client.request('push', 'retryPending')
    await until(async () => (await client.raw({ action: '__count' })).data === 2 && (await client.request('push', 'registration', { id: registration.id })).pendingMessages === 0)
    await client.raw({ action: '__endpoint', endpoint: 'https://push.invalid/rotated' })
    await until(async () => (await client.request('push', 'registration', { id: registration.id })).generation !== firstGeneration)
    await assert.rejects(client.request('push', 'acknowledgeRegistration', { id: registration.id, generation: firstGeneration }), { code: 'EAGAIN' })
    const retained = await client.request('push', 'registration', { id: registration.id })
    assert.equal((await client.raw({ action: '__restart' })).data, true)
    assert.deepEqual((await client.request('push', 'registration', { id: registration.id })).keys, retained.keys)
    assert.equal((await client.request('foreground', 'support')).registeredKinds.includes('public.read'), true)
    await client.quit()
    client = start(binary, [], 'public-native-client-ready')
    await client.initialized
    entries = await client.request('launcher', 'responses')
    assert.equal(entries.length, 1)
    assert.equal(entries[0].targetRef, 'target')
    await client.request('launcher', 'acknowledge', { receiptId: entries[0].receiptId })
    await until(async () => (await client.request('push', 'registration', { id: registration.id })).phase === 'registered')
    assert.deepEqual((await client.request('push', 'registration', { id: registration.id })).keys, retained.keys)
    assert.equal((await client.raw({ action: '__count' })).data, 0)
    const discover = () => client.request('extension', 'discover', { extension: 'public.metadata', payload: {} })
    const retire = value => client.request('extension', 'retire', { extension: 'public.metadata', payload: value })
    const [recovered] = await discover()
    assert.deepEqual(Object.keys(recovered).sort(), ['id', 'kind', 'phase', 'revision'])
    assert.equal(recovered.id, registration.id)
    await client.request('push', 'register', registration)
    await assert.rejects(retire(recovered), { code: 'EAGAIN' })
    let current = (await discover())[0]
    await until(async () => (await client.request('push', 'registration', { id: registration.id })).phase === 'registered')
    current = (await discover())[0]
    assert.equal(await retire(current), true)
    assert.equal(await retire(current), false)
    await client.request('push', 'register', registration)
    await assert.rejects(retire(current), { code: 'EAGAIN' })
    assert.equal((await discover()).length, 1)
    await client.request('push', 'unregister', { id: registration.id })
    assert.deepEqual(await discover(), [])
    await assert.rejects(client.request('push', 'registration', { id: registration.id }), { code: 'ENOENT' })
    await client.quit()
    client = null
    console.log('linux-production-keyring-execution-notifications-unifiedpush-passed')
  } finally {
    if (client) client.child.kill('SIGTERM')
    distributor.child.kill('SIGTERM')
    notifications.child.kill('SIGTERM')
    fs.writeFileSync(path.join(directory, 'public-distributor.log'), distributor.log.join(''))
    fs.writeFileSync(path.join(directory, 'public-notifications.log'), notifications.log.join(''))
  }
}
