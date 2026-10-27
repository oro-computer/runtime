import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp, readFile, writeFile, mkdir, stat, copyFile, chmod } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path from 'node:path'
import net from 'node:net'
import https from 'node:https'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { gzipSync, deflateRawSync } from 'node:zlib'
import { fileURLToPath } from 'node:url'
import pushServer from '../../src/cli/push-server.cjs'

const { parseOptions, extractBinary, resolveBinary, serverConfiguration, assets, version } = pushServer
const helper = fileURLToPath(new URL('../../src/cli/push-server.cjs', import.meta.url))
const binary = process.env.ORO_TEST_NTFY_BIN
const cli = process.env.ORO_TEST_OROC

test('local server defaults are writable user state and isolated from cloud configuration', () => {
  const options = parseOptions([])
  assert.equal(options.host, '127.0.0.1')
  assert.equal(options.port, 8787)
  assert.equal(options['base-url'], 'http://127.0.0.1:8787')
  const config = serverConfiguration(options)
  assert.equal(config['listen-http'], '127.0.0.1:8787')
  assert.equal(config['listen-https'], '')
  assert.equal(config['firebase-key-file'], '')
  assert.equal(config['upstream-base-url'], '')
  assert.equal(config['web-root'], '/')
  assert.equal(config['cache-duration'], '12h')
})

test('direct TLS and HTTPS proxy configurations advertise reachable origins', () => {
  const direct = parseOptions(['--host=0.0.0.0', '--port', '9443', '--base-url=https://push.example:9443', '--cert-file=./cert.pem', '--key-file=./key.pem'])
  assert.equal(serverConfiguration(direct)['listen-http'], '')
  assert.equal(serverConfiguration(direct)['listen-https'], '0.0.0.0:9443')
  const proxy = parseOptions(['--base-url=https://push.example'])
  assert.equal(serverConfiguration(proxy)['listen-http'], '127.0.0.1:8787')
  assert.equal(parseOptions(['--host=::1'])['base-url'], 'http://[::1]:8787')
})

for (const args of [
  ['--host=0.0.0.0'], ['--host=0.0.0.0', '--base-url=http://example.com'],
  ['--host=not-an-ip'], ['--port=3.14'], ['--port=0'], ['--port=65536'], ['--port=123oops'],
  ['--base-url=https://user:pass@example.com'], ['--base-url=https://example.com/path'],
  ['--base-url=https://example.com?query'], ['--base-url=https://example.com/#fragment'],
  ['--base-url=https://0.0.0.0'], ['--base-url=ftp://example.com'],
  ['--cert-file=a'], ['--key-file=b'], ['--offline=false'], ['--port'],
  ['--data-dir='], ['--port=8787', '--port=8788'], ['--unexpected'], ['server']
]) {
  test(`invalid configuration fails: ${args.join(' ')}`, () => {
    assert.throws(() => parseOptions(args))
  })
}

function tarArchive (name, content, type = '0') {
  const header = Buffer.alloc(512)
  header.write(name)
  header.write(content.length.toString(8).padStart(11, '0') + '\0', 124)
  header.write(type, 156)
  return gzipSync(Buffer.concat([header, content, Buffer.alloc((512 - content.length % 512) % 512 + 512)]))
}

function zipArchive (name, content) {
  const compressed = deflateRawSync(content)
  const local = Buffer.alloc(30)
  local.writeUInt32LE(0x04034b50)
  local.writeUInt16LE(name.length, 26)
  const central = Buffer.alloc(46)
  central.writeUInt32LE(0x02014b50)
  central.writeUInt16LE(8, 10)
  central.writeUInt32LE(compressed.length, 20)
  central.writeUInt32LE(content.length, 24)
  central.writeUInt16LE(name.length, 28)
  const end = Buffer.alloc(22)
  end.writeUInt32LE(0x06054b50)
  end.writeUInt16LE(1, 10)
  end.writeUInt32LE(local.length + name.length + compressed.length, 16)
  return Buffer.concat([local, Buffer.from(name), compressed, central, Buffer.from(name), end])
}

test('archive extraction selects the expected executable without writing archive paths', () => {
  const content = Buffer.from('public binary fixture')
  assert.deepEqual(extractBinary(tarArchive(`ntfy_${version}_linux_amd64/ntfy`, content), `ntfy_${version}_linux_amd64.tar.gz`), content)
  assert.deepEqual(extractBinary(zipArchive(`ntfy_${version}_windows_amd64/ntfy.exe`, content), `ntfy_${version}_windows_amd64.zip`), content)
  for (const name of ['../../ntfy', `ntfy_${version}_linux_amd64/other`]) {
    assert.throws(() => extractBinary(tarArchive(name, content), `ntfy_${version}_linux_amd64.tar.gz`), /missing/)
  }
  assert.throws(() => extractBinary(tarArchive(`ntfy_${version}_linux_amd64/ntfy`, content, '2'), `ntfy_${version}_linux_amd64.tar.gz`), /missing/)
  assert.throws(() => extractBinary(Buffer.from('invalid'), 'invalid.zip'))
})

test('offline misses and corrupted downloads fail before executing anything', async t => {
  const asset = assets[`${process.platform}-${process.arch}`]
  if (!asset) return t.skip('No pinned release for this host')
  const directory = await mkdtemp(path.join(tmpdir(), 'oro-push-cache-'))
  const options = parseOptions([`--data-dir=${directory}`, '--offline'])
  const signal = new AbortController().signal
  let requests = 0
  const download = async () => { requests++; return Buffer.from('untrusted download') }
  await assert.rejects(resolveBinary(options, signal, download), /No verified ntfy/)
  assert.equal(requests, 0)
  options.offline = false
  await assert.rejects(resolveBinary(options, signal, download), /SHA-256 mismatch/)
  assert.equal(requests, 1)
  const archive = path.join(directory, 'tools', version, `${process.platform}-${process.arch}`, `ntfy_${version}_${asset[0]}`)
  await assert.rejects(stat(archive), { code: 'ENOENT' })
  await writeFile(archive, 'corrupted cached archive')
  options.offline = true
  await assert.rejects(resolveBinary(options, signal, download), /No verified ntfy/)
  assert.equal(requests, 1)
})

test('verified cached release repairs a corrupted executable without network access', { skip: !process.env.ORO_TEST_NTFY_ARCHIVE }, async () => {
  const directory = await mkdtemp(path.join(tmpdir(), 'oro-push-verified-cache-'))
  const options = parseOptions([`--data-dir=${directory}`, '--offline'])
  const tool = path.join(directory, 'tools', version, `${process.platform}-${process.arch}`)
  await mkdir(tool, { recursive: true })
  const [suffix] = assets[`${process.platform}-${process.arch}`]
  await writeFile(path.join(tool, `ntfy_${version}_${suffix}`), await readFile(process.env.ORO_TEST_NTFY_ARCHIVE))
  const signal = new AbortController().signal
  const noDownload = () => { throw new Error('Network forbidden') }
  const executable = await resolveBinary(options, signal, noDownload)
  const expected = await readFile(executable)
  assert.ok(expected.length > 1024)
  await writeFile(executable, 'corrupted executable')
  assert.equal(await resolveBinary(options, signal, noDownload), executable)
  assert.deepEqual(await readFile(executable), expected)
})

async function freePort () {
  const server = net.createServer()
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const port = server.address().port
  await new Promise(resolve => server.close(resolve))
  return port
}

async function start (t, executable, args, env = {}) {
  const child = spawn(executable, args, { env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] })
  let output = ''
  child.stdout.on('data', bytes => { output += bytes })
  child.stderr.on('data', bytes => { output += bytes })
  const closed = once(child, 'close')
  t.after(async () => {
    if (child.exitCode === null && child.signalCode === null) child.kill('SIGTERM')
    await closed
  })
  return { child, closed, output: () => output }
}

async function ready (server, origin) {
  const deadline = Date.now() + 10000
  while (Date.now() < deadline) {
    if (server.child.exitCode !== null) assert.fail(server.output())
    try {
      const response = await fetch(`${origin}/v1/health`, { signal: AbortSignal.timeout(500) })
      if (response.ok && (await response.json()).healthy) return
    } catch {}
    await new Promise(resolve => setTimeout(resolve, 50))
  }
  assert.fail(`Server did not become healthy: ${server.output()}`)
}

test('real ntfy supports discovery, streaming binary push, durable replay and clean shutdown', { skip: !binary, timeout: 30000 }, async t => {
  const directory = await mkdtemp(path.join(tmpdir(), 'oro push real '))
  const port = await freePort()
  const origin = `http://127.0.0.1:${port}`
  const command = cli || process.execPath
  const prefix = cli ? ['push', 'server'] : [helper]
  const args = [...prefix, `--port=${port}`, `--data-dir=${directory}`, `--ntfy-bin=${binary}`]
  let server = await start(t, command, args, { NTFY_LISTEN_HTTP: ':1', NTFY_FIREBASE_KEY_FILE: '/must/not/read', NTFY_CONFIG_FILE: '/must/not/read', NTFY_UPSTREAM_BASE_URL: 'https://must-not-contact.invalid' })
  await ready(server, origin)
  const endpoint = `${origin}/up123456789012`
  assert.deepEqual(await (await fetch(`${endpoint}?up=1`)).json(), { unifiedpush: { version: 1 } })
  const controller = new AbortController()
  const stream = await fetch(`${endpoint}/json`, { signal: controller.signal })
  const reader = stream.body.getReader()
  assert.match(Buffer.from((await reader.read()).value).toString(), /"event":"open"/)
  const body = Buffer.from([0, 255, 128, 1, 2, 3])
  const published = await fetch(`${endpoint}?up=1`, { method: 'POST', headers: { 'Content-Encoding': 'aes128gcm' }, body })
  assert.equal(published.status, 200, await published.text())
  const received = JSON.parse(Buffer.from((await reader.read()).value).toString())
  assert.equal(received.encoding, 'base64')
  assert.deepEqual(Buffer.from(received.message, 'base64'), body)
  controller.abort()
  const socket = new globalThis.WebSocket(`${origin.replace('http:', 'ws:')}/up123456789013/ws`)
  t.after(() => socket.close())
  await once(socket, 'open')
  const message = new Promise(resolve => {
    socket.addEventListener('message', event => {
      const value = JSON.parse(event.data)
      if (value.event === 'message') resolve(value)
    })
  })
  const socketPublish = await fetch(`${origin}/up123456789013?up=1`, { method: 'POST', body })
  assert.equal(socketPublish.status, 200)
  assert.deepEqual(Buffer.from((await message).message, 'base64'), body)
  socket.close()
  server.child.kill('SIGTERM')
  assert.deepEqual(await server.closed, [143, null])
  server = await start(t, command, args)
  await ready(server, origin)
  const replay = await (await fetch(`${endpoint}/json?poll=1&since=all`)).text()
  assert.equal(JSON.parse(replay.trim()).id, received.id)
  const occupied = await start(t, command, [...prefix, `--port=${port}`, `--data-dir=${directory}/other`, `--ntfy-bin=${binary}`])
  assert.equal((await occupied.closed)[0], 1)
  assert.match(occupied.output(), /address already in use|Only one usage/i)
  server.child.kill('SIGINT')
  assert.deepEqual(await server.closed, [130, null])
})

test('shutdown kills an unresponsive server within the five-second deadline', { skip: process.platform === 'win32', timeout: 12000 }, async t => {
  const directory = await mkdtemp(path.join(tmpdir(), 'oro-push-stop-'))
  const executable = path.join(directory, 'unresponsive.cjs')
  await writeFile(executable, `#!${process.execPath}\nprocess.on('SIGTERM', () => {}); console.log('public-server-pid=' + process.pid); setInterval(() => {}, 1000)\n`, { mode: 0o700 })
  const server = await start(t, process.execPath, [helper, `--ntfy-bin=${executable}`, `--data-dir=${directory}`])
  const deadline = Date.now() + 3000
  while (!server.output().includes('public-server-pid=') && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 20))
  const match = /public-server-pid=(\d+)/.exec(server.output())
  assert.ok(match, server.output())
  const started = Date.now()
  server.child.kill('SIGTERM')
  assert.deepEqual(await server.closed, [143, null])
  assert.ok(Date.now() - started < 7000, 'Server shutdown exceeded its bounded deadline')
  assert.throws(() => process.kill(Number(match[1]), 0), { code: 'ESRCH' })
})

test('real HTTPS server presents the supplied certificate and supports discovery', { skip: !binary, timeout: 30000 }, async t => {
  const { spawnSync } = await import('node:child_process')
  if (spawnSync('openssl', ['version']).status !== 0) return t.skip('Requires openssl to create a public test certificate')
  const directory = await mkdtemp(path.join(tmpdir(), 'oro-push-tls-'))
  const certificate = path.join(directory, 'cert.pem')
  const key = path.join(directory, 'key.pem')
  const generated = spawnSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1', '-keyout', key, '-out', certificate], { encoding: 'utf8' })
  assert.equal(generated.status, 0, generated.stderr)
  const port = await freePort()
  const server = await start(t, cli || process.execPath, [...(cli ? ['push', 'server'] : [helper]), `--port=${port}`, `--data-dir=${directory}`, `--ntfy-bin=${binary}`, `--cert-file=${certificate}`, `--key-file=${key}`])
  const ca = await readFile(certificate)
  const get = () => new Promise((resolve, reject) => {
    const request = https.get(`https://127.0.0.1:${port}/up123456789012?up=1`, { ca, timeout: 500 }, response => {
      let body = ''
      response.on('data', bytes => { body += bytes })
      response.on('end', () => resolve(JSON.parse(body)))
    })
    request.on('timeout', () => request.destroy(new Error('TLS timeout')))
    request.on('error', reject)
  })
  const deadline = Date.now() + 10000
  let discovery
  while (Date.now() < deadline) {
    try { discovery = await get(); break } catch {}
    if (server.child.exitCode !== null) assert.fail(server.output())
    await new Promise(resolve => setTimeout(resolve, 50))
  }
  assert.deepEqual(discovery, { unifiedpush: { version: 1 } }, server.output())
  server.child.kill('SIGTERM')
  assert.deepEqual(await server.closed, [143, null])
})

test('npm launchers run from a read-only package without Node on PATH and forward SIGINT', { skip: !binary || !cli || process.platform === 'win32', timeout: 30000 }, async t => {
  const directory = await mkdtemp(path.join(tmpdir(), 'oro-push-npm-'))
  const scope = path.join(directory, 'node_modules', '@oro-computer')
  const platform = path.join(scope, `runtime-${process.platform}-${process.arch}`)
  const application = path.join(scope, 'runtime')
  const root = fileURLToPath(new URL('../../', import.meta.url))
  const files = [
    [`npm/packages/@oro-computer/runtime-${process.platform}-${process.arch}/package.json`, `${platform}/package.json`, 0o444],
    ['npm/packages/@oro-computer/runtime/package.json', `${application}/package.json`, 0o444],
    ['npm/src/index.js', `${platform}/src/index.js`, 0o444],
    ['npm/bin/oroc.js', `${application}/bin/oroc.js`, 0o444],
    ['npm/bin/oroc-platform.js', `${platform}/bin/oroc-platform.js`, 0o444],
    ['src/cli/push-server.cjs', `${platform}/src/cli/push-server.cjs`, 0o444],
    [cli, `${platform}/bin/oroc`, 0o555]
  ]
  const directories = new Set()
  for (const [source, destination, mode] of files) {
    await mkdir(path.dirname(destination), { recursive: true })
    await copyFile(path.resolve(root, source), destination)
    await chmod(destination, mode)
    directories.add(path.dirname(destination))
  }
  for (const folder of directories) await chmod(folder, 0o555)
  const port = await freePort()
  const server = await start(t, process.execPath, [`${application}/bin/oroc.js`, 'push', 'server', `--port=${port}`, `--data-dir=${directory}/data`, `--ntfy-bin=${binary}`], {
    ORO_HOME: platform,
    ORO_NODE_EXECUTABLE: '',
    ORO_CONFIG_HOME: `${directory}/config`,
    PATH: '/no/node/on/path'
  })
  await ready(server, `http://127.0.0.1:${port}`)
  server.child.kill('SIGINT')
  assert.deepEqual(await server.closed, [130, null])
  await assert.rejects(fetch(`http://127.0.0.1:${port}/v1/health`))
})

test('native process launcher preserves arguments and forwards signals to the helper', { skip: process.platform === 'win32', timeout: 30000 }, async t => {
  const { spawnSync } = await import('node:child_process')
  const directory = await mkdtemp(path.join(tmpdir(), 'oro-push-launch-'))
  const source = path.join(directory, 'main.cc')
  const executable = path.join(directory, 'launcher')
  const root = fileURLToPath(new URL('../../', import.meta.url))
  await writeFile(source, '#include "push-server.hh"\nint main(int argc, char** argv) { return oro::cli::runPushServer({argv + 1, argv + argc}); }\n')
  const compiler = spawnSync('pkg-config', ['--cflags', '--libs', 'libuv'], { encoding: 'utf8' })
  let flags = compiler.stdout?.trim().split(/\s+/) || []
  if (compiler.status !== 0) {
    const archive = path.join(root, `build/${process.arch === 'x64' ? 'x86_64' : process.arch}-desktop/lib/libuv.a`)
    try { await stat(archive) } catch { return t.skip('Requires libuv headers/library') }
    flags = [`-I${root}/build/uv/include`, archive, '-lpthread', '-ldl']
  }
  const compiled = spawnSync('c++', ['-std=c++20', `-I${root}/src/cli`, source, '-o', executable, ...flags], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const script = path.join(directory, 'child with spaces.cjs')
  await writeFile(script, "process.on('SIGTERM', () => process.exit(37)); console.log(JSON.stringify(process.argv.slice(2))); setInterval(() => {}, 1000)\n")
  const server = await start(t, executable, [process.execPath, script, 'space and $HOME; literal', '"quote"'])
  const deadline = Date.now() + 10000
  while (!server.output().includes('literal') && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 30))
  assert.deepEqual(JSON.parse(server.output().trim()), ['space and $HOME; literal', '"quote"'])
  server.child.kill('SIGTERM')
  assert.deepEqual(await server.closed, [37, null])
  const missing = spawnSync(executable, ['/no/such/node'], { encoding: 'utf8' })
  assert.notEqual(missing.status, 0)
})
