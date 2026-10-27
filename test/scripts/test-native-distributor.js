import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import net from 'node:net'
import { fileURLToPath } from 'node:url'
import { spawn, spawnSync } from 'node:child_process'
import { createInterface } from 'node:readline'
import { createECDH, createCipheriv, randomBytes, hkdfSync } from 'node:crypto'
import pushServer from '../../src/cli/push-server.cjs'

const root = fileURLToPath(new URL('../../', import.meta.url))
function run (program, args, options = {}) {
  const result = spawnSync(program, args, { cwd: root, encoding: 'utf8', timeout: 120000, ...options })
  assert.equal(result.status, 0, `${program}: ${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
  return result.stdout
}
const cli = process.env.ORO_TEST_NATIVE_CLI
const ntfy = process.env.ORO_TEST_NTFY_BIN
assert.ok(cli && ntfy, 'Set ORO_TEST_NATIVE_CLI and ORO_TEST_NTFY_BIN to rebuilt CLI and local server-enabled ntfy')
if (process.argv[2] !== '--driver') {
  assert.equal(process.platform, 'linux')
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-linux-distributor-'))
  const architecture = process.arch === 'x64' ? 'x86_64' : process.arch
  const libraries = path.join(root, 'build', `${architecture}-desktop/lib`)
  const archive = process.env.ORO_TEST_NATIVE_RUNTIME_ARCHIVE || path.join(libraries, 'liboro-runtime.a')
  const flags = name => run('bash', [path.join(root, 'bin', name)]).trim().split(/\s+/).filter(Boolean)
  const binary = path.join(directory, 'public-client')
  run(process.env.ORO_TEST_CXX || 'clang++', [...flags('cflags.sh'), '-I' + root,
    path.join(root, 'test/fixtures/native-records/LinuxPlatform.cc'), '-Wl,--start-group', archive,
    ...['libuv.a', 'libusb-1.0.a', 'libllama.a', 'libwhisper.a', 'libmbedtls.a', 'libmbedx509.a', 'libmbedcrypto.a'].map(name => path.join(libraries, name)),
    '-Wl,--end-group', ...flags('ldflags.sh'), '-o', binary])
  for (const name of ['data', 'config', 'run', 'control', 'state']) fs.mkdirSync(path.join(directory, name), { mode: 0o700 })
  const result = run('dbus-run-session', ['--', process.execPath, fileURLToPath(import.meta.url), '--driver', binary, directory], {
    env: {
      ...process.env,
      XDG_DATA_HOME: path.join(directory, 'data'),
      XDG_CONFIG_HOME: path.join(directory, 'config'),
      XDG_RUNTIME_DIR: path.join(directory, 'run'),
      ORO_CONFIG_HOME: path.join(directory, 'config/oro'),
      ORO_STATE_DIR: path.join(directory, 'state'),
      GNOME_KEYRING_CONTROL: path.join(directory, 'control')
    }
  })
  console.log(result.trim())
  console.log('Public Linux ntfy interoperability artifacts:', directory)
} else {
  const [, , , binary, directory] = process.argv
  run('gnome-keyring-daemon', ['--unlock', '--components=secrets', '--control-directory=' + process.env.GNOME_KEYRING_CONTROL], { input: 'public-distributor-test' })
  const reservation = net.createServer()
  await new Promise(resolve => reservation.listen(0, '127.0.0.1', resolve))
  const port = reservation.address().port
  await new Promise(resolve => reservation.close(resolve))
  const origin = `http://127.0.0.1:${port}`
  const children = new Set()
  function start (program, args) {
    const child = spawn(program, args, { stdio: ['pipe', 'pipe', 'pipe'] })
    children.add(child)
    child.once('exit', () => children.delete(child))
    child.log = ''
    for (const stream of [child.stdout, child.stderr]) stream.on('data', chunk => { if (child.log.length < 1048576) child.log += chunk })
    child.completed = new Promise(resolve => child.once('exit', (code, signal) => resolve({ code, signal })))
    return child
  }
  async function stop (child) { if (child.exitCode === null && child.signalCode === null) child.kill('SIGTERM'); return child.completed }
  async function until (check) {
    const deadline = Date.now() + 25000
    while (!await check()) {
      assert.ok(Date.now() < deadline, 'Local distributor acceptance timed out')
      await new Promise(resolve => setTimeout(resolve, 40))
    }
  }
  function startDistributor () {
    return start(cli, ['push', 'distributor', '--platform=linux', '--server=' + origin, '--allow-loopback-http', '--data-dir=' + path.join(directory, 'distributor')])
  }
  function startServer () {
    return start(process.execPath, [path.join(root, 'src/cli/push-server.cjs'), '--host=127.0.0.1', '--port=' + port,
      '--base-url=' + origin, '--ntfy-bin=' + ntfy, '--data-dir=' + path.join(directory, 'ntfy')])
  }
  function startBacklogServer () {
    const configuration = pushServer.serverConfiguration(pushServer.parseOptions(['--host=127.0.0.1', '--port=' + port,
      '--base-url=' + origin, '--data-dir=' + path.join(directory, 'ntfy')]))
    configuration['visitor-request-limit-burst'] = 1000
    configuration['visitor-message-daily-limit'] = 10000
    const file = path.join(directory, 'public-backlog-server.json')
    fs.writeFileSync(file, JSON.stringify(configuration), { mode: 0o600 })
    return start(ntfy, ['serve', '--config', file])
  }
  function encrypted (enrollment, padding = 0) {
    const sender = createECDH('prime256v1')
    sender.generateKeys()
    const receiver = Buffer.from(enrollment.keys.p256dh, 'base64url')
    const authentication = Buffer.from(enrollment.keys.auth, 'base64url')
    const ikm = Buffer.from(hkdfSync('sha256', sender.computeSecret(receiver), authentication,
      Buffer.concat([Buffer.from('WebPush: info\0'), receiver, sender.getPublicKey()]), 32))
    const salt = randomBytes(16)
    const key = Buffer.from(hkdfSync('sha256', ikm, salt, Buffer.from('Content-Encoding: aes128gcm\0'), 16))
    const nonce = Buffer.from(hkdfSync('sha256', ikm, salt, Buffer.from('Content-Encoding: nonce\0'), 12))
    const cipher = createCipheriv('aes-128-gcm', key, nonce)
    const body = Buffer.concat([cipher.update(Buffer.concat([Buffer.from('public-message'), Buffer.from([2]), Buffer.alloc(padding)])), cipher.final(), cipher.getAuthTag()])
    const header = Buffer.alloc(21)
    salt.copy(header)
    header.writeUInt32BE(4096, 16)
    header[20] = 65
    return Buffer.concat([header, sender.getPublicKey(), body])
  }
  let server
  let distributor
  let client
  try {
    for (const address of ['http://192.0.2.1:8787', 'http://localhost:8787', 'https://example.invalid/path']) {
      const rejected = start(cli, ['push', 'distributor', '--platform=linux', '--server=' + address, '--allow-loopback-http', '--data-dir=' + path.join(directory, 'rejected')])
      assert.equal((await rejected.completed).code, 1)
    }
    server = startServer()
    const healthy = async () => { try { return (await fetch(origin + '/v1/health', { signal: AbortSignal.timeout(500) })).ok } catch { return false } }
    await until(healthy)
    distributor = startDistributor()
    await until(() => { assert.equal(distributor.exitCode, null, distributor.log); return distributor.log.includes('Distributor2 ready') })
    client = start(binary, ['development'])
    const waiting = []
    const output = createInterface({ input: client.stdout })
    output.on('line', line => { if (line.startsWith('{') && waiting.length) waiting.shift()(JSON.parse(line)) })
    function raw (value) { return new Promise(resolve => { waiting.push(resolve); client.stdin.write(JSON.stringify(value) + '\n') }) }
    async function request (feature, action, options = {}) {
      const result = await raw({ feature, action, options })
      assert.equal(result.error, undefined, JSON.stringify(result))
      return result.data
    }
    await until(() => { assert.equal(client.exitCode, null, client.log); return client.log.includes('public-native-client-ready') })
    assert.ok((await request('push', 'distributors')).includes('org.unifiedpush.Distributor.OroDev'))
    await request('push', 'selectDistributor', { distributor: 'org.unifiedpush.Distributor.OroDev' })
    const vapid = createECDH('prime256v1')
    vapid.generateKeys()
    await request('push', 'register', {
      id: 'public',
      kind: 'public.alert',
      authorityRef: 'authority',
      accountRef: 'account',
      targetRef: 'target',
      applicationServerKey: vapid.getPublicKey().toString('base64url'),
      description: 'Public ntfy test'
    })
    let enrollment
    await until(async () => { enrollment = await request('push', 'registration', { id: 'public' }); return enrollment.phase === 'registered' })
    assert.ok(enrollment.endpoint.startsWith(origin + '/up'))
    await request('push', 'acknowledgeRegistration', { id: 'public', generation: enrollment.generation })
    async function publish (padding = 0) {
      // This exercises ntfy binary caching and encrypted transport. Product VAPID/IAM is deliberately separate.
      const response = await fetch(enrollment.endpoint, { method: 'POST', body: encrypted(enrollment, padding), headers: { 'Content-Type': 'application/octet-stream' } })
      assert.equal(response.status, 200, await response.text())
    }
    await publish()
    await until(async () => (await raw({ action: '__count' })).data === 1)
    assert.equal((await stop(distributor)).code, 0)
    await publish()
    distributor = startDistributor()
    await until(() => { assert.equal(distributor.exitCode, null, distributor.log); return distributor.log.includes('Distributor2 ready') })
    await until(async () => (await raw({ action: '__count' })).data === 2)
    assert.equal((await stop(server)).code, 143)
    await new Promise(resolve => setTimeout(resolve, 2200))
    server = startServer()
    await until(healthy)
    await publish()
    await until(async () => (await raw({ action: '__count' })).data === 3)
    assert.equal((await stop(distributor)).code, 0)
    assert.equal((await stop(server)).code, 143)
    server = startBacklogServer()
    await until(healthy)
    for (let message = 0; message < 50; message++) await publish(3900)
    distributor = startDistributor()
    await until(() => { assert.equal(distributor.exitCode, null, distributor.log); return distributor.log.includes('Distributor2 ready') })
    await until(async () => (await raw({ action: '__count' })).data === 53)
    await request('push', 'unregister', { id: 'public' })
    await publish()
    await new Promise(resolve => setTimeout(resolve, 2400))
    assert.equal((await raw({ action: '__count' })).data, 53)
    client.stdin.write('{"action":"__quit"}\n')
    assert.equal((await client.completed).code, 0, client.log)
    console.log('linux-real-ntfy-distributor-encryption-restart-reconnect-unregister-passed')
    console.log('linux-development-distributor-bounded-backlog-passed')
  } catch (error) {
    for (const child of [server, distributor, client]) if (child) console.error(child.log)
    throw error
  } finally {
    await Promise.all([...children].map(stop))
  }
}
