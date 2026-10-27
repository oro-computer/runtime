#!/usr/bin/env node

// A local ntfy server supplies the server half of an existing UnifiedPush
// distributor. Downloads are pinned; Android registration stays in oro:push.
const fs = require('node:fs/promises')
const path = require('node:path')
const os = require('node:os')
const net = require('node:net')
const { createHash, randomUUID } = require('node:crypto')
const { gunzipSync, inflateRawSync } = require('node:zlib')
const { spawn } = require('node:child_process')

const version = '2.33.1'
const assets = {
  'linux-x64': ['linux_amd64.tar.gz', '224aa4c8bd1b1d5ede011283c56eddf72893ba787cfcc5988e0c6bf07d84cc3f'],
  'linux-arm64': ['linux_arm64.tar.gz', 'e211baed18313b868d6304267d887d9602ee920b0962c36d80f2a346795abca2'],
  'win32-x64': ['windows_amd64.zip', 'b2dd70a74732151ed1f7945bc3146d4bd4d73a7c9e592ad7b3fbe59f7a24e6eb']
}
const archiveLimit = 100 * 1024 * 1024
const expandedLimit = 256 * 1024 * 1024

function digest (buffer) {
  return createHash('sha256').update(buffer).digest('hex')
}

function dataDirectory (env = process.env) {
  if (env.ORO_CONFIG_HOME) return path.join(env.ORO_CONFIG_HOME, 'push')
  if (process.platform === 'win32') return path.join(env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'oro', 'push')
  if (process.platform === 'darwin') return path.join(os.homedir(), 'Library', 'Application Support', 'oro', 'push')
  return path.join(env.XDG_DATA_HOME || path.join(os.homedir(), '.local', 'share'), 'oro', 'push')
}

function parseOptions (args) {
  const values = new Set(['host', 'port', 'base-url', 'data-dir', 'cert-file', 'key-file', 'ntfy-bin'])
  const options = { host: '127.0.0.1', port: '8787', 'data-dir': dataDirectory(), offline: false }
  const seen = new Set()
  for (let index = 0; index < args.length; index++) {
    const match = /^--([a-z-]+)(?:=(.*))?$/.exec(args[index])
    if (!match || (!values.has(match[1]) && match[1] !== 'offline')) throw new Error(`Unknown push server option: ${args[index]}`)
    const [, name, inline] = match
    if (seen.has(name)) throw new Error(`Duplicate --${name}`)
    seen.add(name)
    if (name === 'offline') {
      if (inline !== undefined) throw new Error('--offline takes no value')
      options.offline = true
    } else {
      const value = inline === undefined ? args[++index] : inline
      if (!value || value.startsWith('--')) throw new Error(`--${name} requires a value`)
      options[name] = value
    }
  }
  if (!/^\d+$/.test(options.port) || Number(options.port) < 1 || Number(options.port) > 65535) throw new Error('--port must be between 1 and 65535')
  options.port = Number(options.port)
  if (!net.isIP(options.host) && options.host !== 'localhost') throw new Error('--host must be an IP address or localhost')
  const tls = Boolean(options['cert-file'] || options['key-file'])
  if (Boolean(options['cert-file']) !== Boolean(options['key-file'])) throw new Error('--cert-file and --key-file must be supplied together')
  const local = options.host === 'localhost' || options.host === '::1' || options.host.startsWith('127.')
  if (!local && !options['base-url']) throw new Error('A LAN listener requires --base-url=https://<reachable-host>:<port>')
  const host = net.isIP(options.host) === 6 ? `[${options.host}]` : options.host
  const supplied = options['base-url'] || `${tls ? 'https' : 'http'}://${host}:${options.port}`
  let base
  try { base = new URL(supplied) } catch { throw new Error('--base-url must be an absolute HTTP(S) origin') }
  if (!['http:', 'https:'].includes(base.protocol) || base.username || base.password || base.pathname !== '/' || base.search || base.hash || !base.hostname || ['0.0.0.0', '[::]'].includes(base.hostname)) {
    throw new Error('--base-url must be an HTTP(S) origin without credentials, path, query or fragment')
  }
  if ((tls || !local) && base.protocol !== 'https:') throw new Error('TLS and LAN listeners require an HTTPS --base-url')
  options['base-url'] = base.origin
  options['data-dir'] = path.resolve(options['data-dir'])
  for (const name of ['cert-file', 'key-file', 'ntfy-bin']) {
    if (options[name]) options[name] = path.resolve(options[name])
  }
  return options
}

// Extract only the named regular file into memory, never archive paths onto
// disk. The archive is SHA-256 verified before reaching this parser.
function extractBinary (archive, filename) {
  const directory = filename.replace(/\.(tar\.gz|zip)$/, '')
  const member = `${directory}/${filename.endsWith('.zip') ? 'ntfy.exe' : 'ntfy'}`
  if (filename.endsWith('.tar.gz')) {
    const tar = gunzipSync(archive, { maxOutputLength: expandedLimit })
    for (let offset = 0; offset + 512 <= tar.length;) {
      const header = tar.subarray(offset, offset + 512)
      const name = header.subarray(0, 100).toString().split('\0')[0]
      if (!name) break
      const sizeText = header.subarray(124, 136).toString().replace(/\0/g, '').trim()
      if (!/^[0-7]+$/.test(sizeText)) throw new Error('Invalid ntfy tar member size')
      const size = parseInt(sizeText, 8)
      const start = offset + 512
      if (size > expandedLimit || start + size > tar.length) throw new Error('Truncated ntfy archive')
      if (name === member && [0, 48].includes(header[156])) return Buffer.from(tar.subarray(start, start + size))
      offset = start + Math.ceil(size / 512) * 512
    }
  } else {
    let end = archive.length - 22
    while (end >= Math.max(0, archive.length - 65557) && archive.readUInt32LE(end) !== 0x06054b50) end--
    if (end < Math.max(0, archive.length - 65557)) throw new Error('Invalid ntfy ZIP directory')
    let offset = archive.readUInt32LE(end + 16)
    const count = archive.readUInt16LE(end + 10)
    for (let index = 0; index < count; index++) {
      if (offset + 46 > archive.length || archive.readUInt32LE(offset) !== 0x02014b50) throw new Error('Invalid ntfy ZIP member')
      const method = archive.readUInt16LE(offset + 10)
      const size = archive.readUInt32LE(offset + 20)
      const expanded = archive.readUInt32LE(offset + 24)
      const length = archive.readUInt16LE(offset + 28)
      const name = archive.subarray(offset + 46, offset + 46 + length).toString()
      if (name === member) {
        const local = archive.readUInt32LE(offset + 42)
        if (local + 30 > archive.length || archive.readUInt32LE(local) !== 0x04034b50) throw new Error('Invalid ntfy ZIP local header')
        const start = local + 30 + archive.readUInt16LE(local + 26) + archive.readUInt16LE(local + 28)
        if (expanded > expandedLimit || start + size > archive.length) throw new Error('Truncated ntfy ZIP payload')
        const bytes = archive.subarray(start, start + size)
        const binary = method === 0 ? bytes : method === 8 ? inflateRawSync(bytes, { maxOutputLength: expandedLimit }) : null
        if (!binary || binary.length !== expanded) throw new Error('Unsupported ntfy ZIP compression or length')
        return Buffer.from(binary)
      }
      offset += 46 + length + archive.readUInt16LE(offset + 30) + archive.readUInt16LE(offset + 32)
    }
  }
  throw new Error(`ntfy archive is missing ${member}`)
}

async function readOptional (filename) {
  try { return await fs.readFile(filename) } catch (error) {
    if (error.code !== 'ENOENT') throw error
    return null
  }
}

async function writeAtomic (filename, bytes, mode) {
  const temporary = `${filename}.${randomUUID()}.partial`
  try {
    await fs.writeFile(temporary, bytes, { mode, flag: 'wx' })
    await fs.rename(temporary, filename)
  } finally {
    await fs.rm(temporary, { force: true })
  }
}

async function download (url, signal) {
  const response = await fetch(url, { signal: AbortSignal.any([signal, AbortSignal.timeout(120000)]) })
  if (!response.ok) throw new Error(`ntfy download failed: HTTP ${response.status}`)
  const buffers = []
  let size = 0
  for await (const buffer of response.body) {
    size += buffer.length
    if (size > archiveLimit) throw new Error('ntfy download exceeds 100 MiB')
    buffers.push(buffer)
  }
  return Buffer.concat(buffers)
}

async function resolveBinary (options, signal, fetchArchive = download) {
  if (options['ntfy-bin']) {
    const stat = await fs.stat(options['ntfy-bin'])
    if (!stat.isFile()) throw new Error('--ntfy-bin must point to an executable file')
    return options['ntfy-bin']
  }
  const asset = assets[`${process.platform}-${process.arch}`]
  if (!asset) throw new Error('Automatic ntfy server installation supports Linux x64/arm64 and Windows x64; provide --ntfy-bin=<server-enabled executable> on other hosts (the official macOS ntfy binary is client-only)')
  const [suffix, expected] = asset
  const filename = `ntfy_${version}_${suffix}`
  const directory = path.join(options['data-dir'], 'tools', version, `${process.platform}-${process.arch}`)
  await fs.mkdir(directory, { recursive: true, mode: 0o700 })
  const archivePath = path.join(directory, filename)
  let archive = await readOptional(archivePath)
  if (!archive || digest(archive) !== expected) {
    if (options.offline) throw new Error(`No verified ntfy ${version} archive is cached; run once online or use --ntfy-bin=<path>`)
    console.error(`oroc push server: downloading ntfy ${version} from its official GitHub release`)
    archive = await fetchArchive(`https://github.com/binwiederhier/ntfy/releases/download/v${version}/${filename}`, signal)
    if (digest(archive) !== expected) throw new Error('ntfy archive SHA-256 mismatch; refusing to execute it')
    await writeAtomic(archivePath, archive, 0o600)
  }
  signal.throwIfAborted()
  const binary = extractBinary(archive, filename)
  const executable = path.join(directory, process.platform === 'win32' ? 'ntfy.exe' : 'ntfy')
  const cached = await readOptional(executable)
  if (!cached || digest(cached) !== digest(binary)) await writeAtomic(executable, binary, 0o700)
  if (process.platform !== 'win32') await fs.chmod(executable, 0o700)
  return executable
}

function serverConfiguration (options) {
  const address = `${net.isIP(options.host) === 6 ? `[${options.host}]` : options.host}:${options.port}`
  const tls = Boolean(options['cert-file'])
  return {
    'base-url': options['base-url'],
    'listen-http': tls ? '' : address,
    'listen-https': tls ? address : '',
    'cert-file': options['cert-file'] || '',
    'key-file': options['key-file'] || '',
    'cache-file': path.join(options['data-dir'], 'messages.db'),
    'cache-duration': '12h',
    'cache-startup-queries': 'PRAGMA journal_size_limit=16777216;',
    'attachment-cache-dir': '',
    'firebase-key-file': '',
    'upstream-base-url': '',
    // ntfy's UnifiedPush capability discovery shares the topic web route.
    'web-root': '/',
    'enable-signup': false,
    'enable-login': false,
    'auth-default-access': 'read-write',
    'log-level': 'WARN'
  }
}

async function runServer (options, signal) {
  // Do not inherit a system ntfy config, relay credentials, log settings or
  // NTFY_* environment overrides into this isolated development server.
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) => !key.toUpperCase().startsWith('NTFY_')))
  for (const name of ['cert-file', 'key-file']) {
    if (options[name] && !(await fs.stat(options[name])).isFile()) throw new Error(`--${name} must point to a file`)
  }
  await fs.mkdir(options['data-dir'], { recursive: true, mode: 0o700 })
  const binary = await resolveBinary(options, signal)
  const directory = await fs.mkdtemp(path.join(options['data-dir'], 'session-'))
  try {
    const config = path.join(directory, 'server.yml')
    await fs.writeFile(config, JSON.stringify(serverConfiguration(options)), { mode: 0o600 })
    signal.throwIfAborted()
    console.error(`oroc push server: starting local ntfy at ${options['base-url']} (listen ${options.host}:${options.port})`)
    console.error(`oroc push server: development topics allow anonymous access; data: ${options['data-dir']}`)
    console.error('oroc push server: configure Oro Push Dev or the Android ntfy distributor with this base URL; Ctrl-C stops the server')
    return await new Promise((resolve, reject) => {
      const child = spawn(binary, ['serve', '--config', config], { env, stdio: 'inherit', windowsHide: true })
      let timer
      function stop () {
        child.kill('SIGTERM')
        timer = setTimeout(() => child.kill('SIGKILL'), 5000)
        timer.unref()
      }
      signal.addEventListener('abort', stop, { once: true })
      child.once('error', reject)
      child.once('close', (code, termination) => {
        signal.removeEventListener('abort', stop)
        clearTimeout(timer)
        resolve(termination ? 1 : code ?? 1)
      })
      if (signal.aborted) stop()
    })
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
}

async function main () {
  const controller = new AbortController()
  let interrupted = 0
  function stop (signal) {
    if (interrupted) return
    interrupted = signal === 'SIGINT' ? 130 : 143
    controller.abort(new Error('Push server interrupted'))
  }
  process.on('SIGINT', stop)
  process.on('SIGTERM', stop)
  try {
    if (Number(process.versions.node.split('.')[0]) < 22) throw new Error('Node.js 22 or later is required')
    const result = await runServer(parseOptions(process.argv.slice(2)), controller.signal)
    process.exitCode = interrupted || result
  } catch (error) {
    if (!interrupted) console.error(`oroc push server: ${error.message}`)
    process.exitCode = interrupted || 1
  } finally {
    process.removeListener('SIGINT', stop)
    process.removeListener('SIGTERM', stop)
  }
}

module.exports = { assets, digest, extractBinary, parseOptions, resolveBinary, runServer, serverConfiguration, version }
if (require.main === module) main()
