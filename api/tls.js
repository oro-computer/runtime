/**
 * @module tls (experimental)
 * Native TLS client/server wrappers over the runtime TLS service.
 */

import { EventEmitter } from './events.js'
import { Buffer } from './buffer.js'
import { rand64, createDigest } from './crypto.js'
import ipc from './ipc.js'

const MAX_TLS_HANDLE_ID = 18446744073709551615n

const normaliseTlsHandleId = (input) => {
  if (input == null) {
    return String(rand64())
  }

  if (typeof input === 'bigint') {
    if (input < 0n) {
      throw new RangeError('id must be a non-negative bigint')
    }
    if (input > MAX_TLS_HANDLE_ID) {
      throw new RangeError('id must fit in an unsigned 64-bit integer')
    }
    return String(input)
  }

  if (typeof input === 'number') {
    if (!Number.isSafeInteger(input) || input < 0) {
      throw new RangeError('id must be a non-negative safe integer')
    }
    return String(input)
  }

  if (typeof input === 'string') {
    const value = input.trim()
    if (!value) {
      throw new TypeError('id must not be empty')
    }
    if (!/^[0-9]+$/.test(value)) {
      throw new TypeError('id must be a decimal string')
    }
    const canonical = value.replace(/^0+(?=\d)/, '')
    if (canonical.length > 20) {
      throw new RangeError('id must fit in an unsigned 64-bit integer')
    }
    const parsed = BigInt(canonical)
    if (parsed > MAX_TLS_HANDLE_ID) {
      throw new RangeError('id must fit in an unsigned 64-bit integer')
    }
    return String(parsed)
  }

  throw new TypeError('id must be a bigint, number, or decimal string')
}

/**
 * @typedef {Object} TlsHandshakeInfo
 * @property {string} [id]
 * @property {string} [clientId]
 * @property {string} [hostname]
 * @property {string} [protocol]
 * @property {string} [cipher]
 * @property {string} [alpn]
 * @property {string} [alpnProtocol]
 * @property {string} [provider]
 * @property {string} [subject]
 * @property {string[]} [sans]
 * @property {string} [peerPin]
 */

export class TLSSocket extends EventEmitter {
  /**
   * @param {string|number|bigint} [id]
   */
  constructor (id) {
    super()
    /** @type {string} */
    this.id = normaliseTlsHandleId(id)
    /** @type {TlsHandshakeInfo|null} */
    this.handshake = null
    /** @type {string|undefined} */
    this.hostname = undefined
    /** @type {string|undefined} */
    this.provider = undefined
    /** @type {string|undefined} */
    this.protocol = undefined
    /** @type {string|undefined} */
    this.cipher = undefined
    /** @type {string|undefined} */
    this.alpn = undefined
    /** @type {string|undefined} */
    this.alpnProtocol = undefined
    /** @type {string|undefined} */
    this.subject = undefined
    /** @type {string[]|undefined} */
    this.sans = undefined
    /** @type {string|undefined} */
    this.peerPin = undefined
  }

  /**
   * @param {TlsHandshakeInfo} info
   */
  _setHandshake (info) {
    if (!info || typeof info !== 'object') return
    this.handshake = info
    if (info.hostname != null) this.hostname = info.hostname
    if (info.provider != null) this.provider = info.provider
    if (info.protocol != null) this.protocol = info.protocol
    if (info.cipher != null) this.cipher = info.cipher
    if (info.alpn != null) this.alpn = info.alpn
    if (info.alpnProtocol != null) this.alpnProtocol = info.alpnProtocol
    else if (info.alpn != null) this.alpnProtocol = info.alpn
    if (info.subject != null) this.subject = info.subject
    if (info.sans != null) this.sans = info.sans
    if (info.peerPin != null) this.peerPin = info.peerPin
  }

  /**
   * @param {Buffer|Uint8Array|ArrayBuffer|DataView|string} chunk
   * @param {(err?: Error) => void} [cb]
   * @returns {boolean}
   */
  write (chunk, cb) {
    const buf = Buffer.from(chunk)
    const res = ipc.sendSync('tls.write', { id: this.id }, buf)
    if (res && res.err) {
      const err = new Error(res.err?.message || 'TLS write failed')
      if (typeof cb === 'function') cb(err)
      else this.emit('error', err)
      return false
    }
    if (typeof cb === 'function') cb()
    return true
  }

  /**
   * @param {Buffer|Uint8Array|ArrayBuffer|DataView|string|(() => void)} [chunk]
   * @param {() => void} [cb]
   */
  end (chunk, cb) {
    if (typeof chunk === 'function') {
      cb = chunk
      chunk = undefined
    }
    if (typeof chunk !== 'undefined') this.write(chunk)
    try {
      ipc.sendSync('tls.shutdown', { id: this.id })
    } catch {}
    try {
      ipc.sendSync('tls.close', { id: this.id })
    } catch {}
    if (typeof cb === 'function') cb()
    this.emit('close')
  }

  /**
   * @returns {void}
   */
  destroy () {
    try {
      ipc.sendSync('tls.readStop', { id: this.id })
    } catch {}
    try {
      ipc.sendSync('tls.close', { id: this.id })
    } catch {}
    this.emit('close')
  }
}

export class TLSServer extends EventEmitter {
  constructor (options = {}, connectionListener) {
    super()
    /** @type {string} */
    this.id = normaliseTlsHandleId()
    /** @type {boolean} */
    this._listening = false
    /** @type {Set<TLSSocket>} */
    this._clients = new Set()
    /** @type {Map<string, TLSSocket>} */
    this._clientById = new Map()
    /** @type {any} */
    this._options = options || {}
    if (typeof connectionListener === 'function') {
      this.on('secureConnection', connectionListener)
    }
    ipc.sendSync('tls.server.create', {
      id: this.id,
      cert: this._options.cert || '',
      key: this._options.key || '',
      keyPassphrase:
        this._options.keyPassphrase || this._options.passphrase || '',
      ca: this._options.ca || '',
      requestClientCert: !!this._options.requestCert,
      alpn: Array.isArray(this._options.alpnProtocols)
        ? this._options.alpnProtocols.join(',')
        : '',
      minVersion: this._options.minVersion || '',
      maxVersion: this._options.maxVersion || '',
      ciphers: Array.isArray(this._options.ciphers)
        ? this._options.ciphers.join(',')
        : ''
    })
  }

  listen (port, host = '0.0.0.0', backlog = 128, cb) {
    let res = ipc.sendSync('tls.server.bind', {
      id: this.id,
      port,
      address: host
    })
    if (res && res.err) {
      queueMicrotask(() => this.emit('error', res.err))
      if (typeof cb === 'function') queueMicrotask(() => cb(res.err))
      return this
    }
    res = ipc.sendSync('tls.server.listen', { id: this.id, backlog })
    if (res && res.err) {
      queueMicrotask(() => this.emit('error', res.err))
      if (typeof cb === 'function') queueMicrotask(() => cb(res.err))
      return this
    }
    this._listening = true
    const ondata = (ev) => {
      const { detail } = ev || {}
      const params = detail?.params || {}
      const source = params.source || detail?.source
      const { data } = params
      if (source !== 'tls.server.connection' || !data) return
      if (!this._listening || data.id !== this.id) return
      const clientId = normaliseTlsHandleId()
      const r = ipc.sendSync('tls.server.accept', {
        serverId: this.id,
        clientId
      })
      if (!r.err) {
        const socket = new TLSSocket(clientId)
        this._clientById.set(socket.id, socket)
        // Attach read listener specific to this socket
        const onread = (ev) => {
          const { detail } = ev || {}
          const params = detail?.params || {}
          const source = params.source || detail?.source
          const { data, err } = params
          if (source !== 'tls.read') return
          const id = data ? data.id : err ? err.id : null
          if (id !== socket.id) return
          if (err) return socket.emit('error', err)
          if (data && data.EOF) {
            socket.emit('end')
            try {
              ipc.sendSync('tls.readStop', { id: socket.id })
            } catch {}
            socket.destroy()
            return
          }
          if (detail && detail.data) socket.emit('data', detail.data)
        }
        globalThis.addEventListener('data', onread)
        socket.once('close', () =>
          globalThis.removeEventListener('data', onread)
        )
        try {
          ipc.sendSync('tls.readStart', { id: clientId })
        } catch {}
        this._clients.add(socket)
        socket.once('close', () => {
          this._clients.delete(socket)
          this._clientById.delete(socket.id)
        })
        this.emit('connection', socket)
      }
    }
    const onsecure = (ev) => {
      const { detail } = ev || {}
      const params = detail?.params || {}
      const source = params.source || detail?.source
      const { data } = params
      if (source !== 'tls.server.secureConnection') return
      if (!data || data.serverId !== this.id) return
      // Emit rich event with negotiated info, and a simple ready event for back-compat
      const info = {
        clientId: data.clientId,
        provider: data.provider,
        protocol: data.protocol,
        cipher: data.cipher,
        subject: data.subject,
        sans: data.sans,
        alpnProtocol: data.alpn || undefined
      }
      const socket = this._clientById.get(data.clientId)
      if (socket) socket._setHandshake(info)
      this.emit('secureConnection', info)
      this.emit('secureConnectionReady', { clientId: data.clientId })
    }
    globalThis.addEventListener('data', ondata)
    globalThis.addEventListener('data', onsecure)
    this._ondata = ondata
    this._onsecure = onsecure
    if (typeof cb === 'function') queueMicrotask(() => cb())
    return this
  }

  async close (cb) {
    this._listening = false
    if (this._ondata) {
      globalThis.removeEventListener('data', this._ondata)
      this._ondata = null
    }
    if (this._onsecure) {
      globalThis.removeEventListener('data', this._onsecure)
      this._onsecure = null
    }
    ipc.sendSync('tls.server.close', { id: this.id })
    for (const c of this._clients) c.destroy()
    await new Promise((resolve) => setTimeout(resolve, 10))
    if (typeof cb === 'function') cb()
    this.emit('close')
  }
}

export function createServer (options, connectionListener) {
  if (typeof options === 'function') return new TLSServer({}, options)
  return new TLSServer(options, connectionListener)
}

/**
 * @typedef {'append'|'replace'} TlsPinsMode
 *
 * @typedef {Object} TlsPinsOptions
 * @property {TlsPinsMode} [mode='append']
 *
 * @typedef {Object} TlsConnectOptions
 * @property {string} host
 * @property {number} port
 * @property {string|number|bigint} [id]
 * @property {string} [servername] TLS SNI server name (defaults to `host`)
 * @property {string} [serverName] Alias for `servername`
 * @property {boolean} [rejectUnauthorized=true]
 * @property {string} [ca] PEM-encoded CA bundle
 * @property {string} [cert] PEM-encoded client certificate
 * @property {string} [key] PEM-encoded client private key
 * @property {string} [keyPassphrase] Passphrase for `key` (if encrypted)
 * @property {string} [passphrase] Alias for `keyPassphrase`
 * @property {string[]} [alpnProtocols]
 * @property {string} [minVersion]
 * @property {string} [maxVersion]
 * @property {string[]} [ciphers]
 * @property {string|string[]} [pins] TLS pin lines or pin tokens
 * @property {TlsPinsMode} [pinsMode='append'] Pin merge mode
 */

const normalisePinsMode = (rawMode, label = 'mode') => {
  if (rawMode == null) return 'append'
  if (typeof rawMode !== 'string') {
    throw new TypeError(`${label} must be 'append' or 'replace'`)
  }
  const mode = rawMode.trim().toLowerCase()
  if (mode !== 'append' && mode !== 'replace') {
    throw new RangeError(`${label} must be 'append' or 'replace'`)
  }
  return mode
}

const normaliseTlsPinToken = (token) => {
  let value = String(token || '').trim()
  if (!value) return ''

  if (value.toLowerCase().startsWith('sha256/')) {
    value = value.slice('sha256/'.length)
  }

  if (!/^[A-Za-z0-9+/_-]+={0,2}$/.test(value)) {
    return ''
  }

  value = value.replace(/-/g, '+').replace(/_/g, '/')

  const remainder = value.length % 4
  if (remainder !== 0) {
    value += '='.repeat(4 - remainder)
  }

  let decoded
  try {
    decoded = Buffer.from(value, 'base64')
  } catch {
    return ''
  }

  if (value && decoded.length === 0) return ''
  if (decoded.length !== 32) return ''
  return `sha256/${decoded.toString('base64')}`
}

/**
 * Establish a TLS client connection.
 *
 * @param {number|TlsConnectOptions} options
 * @param {(err?: Error) => void} [cb] Called once on success or failure.
 * @returns {TLSSocket}
 */
export function connect (options, cb) {
  const opts = typeof options === 'object' && options ? options : null

  let host = '127.0.0.1'
  let servername = ''
  let port = 0
  let id

  if (typeof options === 'number') {
    port = options
  } else if (opts) {
    port = opts.port
    host = opts.host || opts.servername || opts.serverName || host
    servername = opts.servername || opts.serverName || ''
    id = opts.id
  } else {
    throw new TypeError('options must be a number or an object')
  }

  port = Number(port)
  if (!Number.isSafeInteger(port) || port <= 0 || port > 65535) {
    throw new RangeError('port must be a valid TCP port number')
  }

  const hostNormalized = normaliseTlsPinHostLoose(host)
  if (!hostNormalized) {
    throw new TypeError('options.host must be a valid hostname')
  }
  host = hostNormalized

  if (servername) {
    const servernameNormalized = normaliseTlsPinHostLoose(servername)
    if (!servernameNormalized) {
      throw new TypeError('options.servername must be a valid hostname')
    }
    servername = servernameNormalized
  }

  const hasPinsInput = (pins) => {
    if (pins == null) return false

    const lines = Array.isArray(pins) ? pins : String(pins).split('\n')

    for (const rawLine of lines) {
      let line = String(rawLine || '').trim()
      if (!line) continue

      const cidx = commentIndex(line)
      if (cidx !== -1) line = line.slice(0, cidx).trim()
      if (line) return true
    }

    return false
  }

  const normalizePins = (pins, pinHost) => {
    if (pins == null) return ''

    const lines = Array.isArray(pins) ? pins : String(pins).split('\n')
    const out = []

    for (const rawLine of lines) {
      let line = String(rawLine || '').trim()
      if (!line) continue
      const cidx = commentIndex(line)
      if (cidx !== -1) line = line.slice(0, cidx).trim()
      if (!line) continue
      const parts = line.split(/\s+/).filter(Boolean)
      if (parts.length === 0) continue

      const firstToken = parts[0]
      const firstPin = normaliseTlsPinToken(firstToken)
      if (firstPin || String(firstToken).toLowerCase().startsWith('sha256/')) {
        for (const token of parts) {
          const normalized = normaliseTlsPinToken(token)
          if (!normalized) {
            throw new TypeError(
              `Invalid TLS pin token in options.pins: ${token}`
            )
          }
          out.push(`${pinHost} ${normalized}`)
        }
        continue
      }

      out.push(line)
    }

    return out.join('\n')
  }

  const pinHostBase = servername || host
  const pinHost =
    pinHostBase.includes(':') && !pinHostBase.startsWith('[')
      ? `[${pinHostBase}]:${port}`
      : `${pinHostBase}:${port}`
  const pinsInputProvided = hasPinsInput(opts?.pins)
  const pins = pinsInputProvided ? normalizePins(opts?.pins, pinHost) : ''
  let pinsMode
  if (opts && opts.pinsMode != null) {
    pinsMode = normalisePinsMode(opts.pinsMode, 'options.pinsMode')
  }

  if (pinsMode === 'replace' && !pinsInputProvided) {
    throw new TypeError(
      "options.pins must be provided when options.pinsMode is 'replace'"
    )
  }

  if (pinsInputProvided) {
    if (!pins.trim()) {
      throw new TypeError(
        'options.pins must include at least one valid sha256 pin token'
      )
    }
    validateTlsPinsConfigValue(pins, 'options.pins')

    if (pinsMode === 'replace') {
      let applies = false
      for (const rawLine of pins.split(/\r?\n/)) {
        let line = String(rawLine || '').trim()
        if (!line) continue
        const cidx = commentIndex(line)
        if (cidx !== -1) line = line.slice(0, cidx).trim()
        if (!line) continue

        const parts = line.split(/\s+/).filter(Boolean)
        if (parts.length < 2) continue
        const lineHost = normaliseTlsPinEndpointLoose(parts[0])
        if (lineHost !== pinHost && lineHost !== pinHostBase) continue

        applies = true
        break
      }

      if (!applies) {
        throw new TypeError(
          `options.pins must include at least one pin for '${pinHost}' when options.pinsMode is 'replace'`
        )
      }
    }
  }

  const socket = new TLSSocket(id)
  // Await async secureConnect event once native implementation is available
  const onread = (ev) => {
    const { detail } = ev || {}
    const params = detail?.params || {}
    const source = params.source || detail?.source
    const { data, err } = params
    if (source !== 'tls.read') return
    const id = data ? data.id : err ? err.id : null
    if (id !== socket.id) return
    if (err) return socket.emit('error', err)
    if (data && data.EOF) {
      socket.emit('end')
      try {
        ipc.sendSync('tls.readStop', { id: socket.id })
      } catch {}
      return
    }
    if (detail && detail.data) {
      socket.emit('data', detail.data)
    }
  }
  const ondata = (ev) => {
    const { detail } = ev || {}
    const params = detail?.params || {}
    const source = params.source || detail?.source
    const { data, err } = params
    if (source !== 'tls.connect') return
    const eid = data ? data.id : err ? err.id : null
    if (eid !== socket.id) return
    globalThis.removeEventListener('data', ondata)
    if (err) {
      const e = new Error(err?.message || 'TLS connect failed')
      if (err && typeof err === 'object') {
        if (err.code) e.code = err.code
        if (err.hostname) e.hostname = err.hostname
        if (err.provider) e.provider = err.provider
        if (err.subject) e.subject = err.subject
        if (err.sans) e.sans = err.sans
        if (err.peerPin) e.peerPin = err.peerPin
        if (err.expectedPins) e.expectedPins = err.expectedPins
      }
      socket.emit('error', e)
      globalThis.removeEventListener('data', onread)
      if (typeof cb === 'function') queueMicrotask(() => cb(e))
    } else {
      // include handshake info payload for consumers
      const info = data || {}
      if (info && info.alpn) info.alpnProtocol = info.alpn
      socket._setHandshake(info)
      socket.emit('secureConnect', info)
      try {
        ipc.sendSync('tls.readStart', { id: socket.id })
      } catch {}
      if (typeof cb === 'function') queueMicrotask(() => cb())
    }
  }

  // Attach listeners before initiating the connection so we never miss
  // an immediate tls.connect error emitted by the runtime.
  const cleanupListeners = () => {
    globalThis.removeEventListener('data', ondata)
    globalThis.removeEventListener('data', onread)
  }

  globalThis.addEventListener('data', ondata)
  globalThis.addEventListener('data', onread)
  socket.once('close', () => {
    cleanupListeners()
  })

  const params = {
    id: socket.id,
    host,
    port,
    rejectUnauthorized: options?.rejectUnauthorized !== false,
    servername,
    ca: options?.ca || '',
    cert: options?.cert || '',
    key: options?.key || '',
    keyPassphrase: options?.keyPassphrase || options?.passphrase || '',
    alpn: Array.isArray(options?.alpnProtocols)
      ? options.alpnProtocols.join(',')
      : '',
    minVersion: options?.minVersion || '',
    maxVersion: options?.maxVersion || '',
    ciphers: Array.isArray(options?.ciphers) ? options.ciphers.join(',') : ''
  }

  if (pinsMode) {
    params.pinsMode = pinsMode
  }

  let res
  const pinsBody = pins && pins.length > 1024 ? pins : null
  if (!pinsBody && pins) {
    params.pins = pins
  }

  try {
    res = ipc.sendSync('tls.connect', params, null, pinsBody)
  } catch (err) {
    const e = err instanceof Error ? err : new Error('TLS connect failed')
    queueMicrotask(() => {
      cleanupListeners()
      if (typeof cb === 'function') cb(e)
      socket.emit('error', e)
    })
    return socket
  }

  if (res && res.err) {
    const e = new Error(res.err?.message || 'TLS connect failed')
    if (res.err && typeof res.err === 'object') {
      if (res.err.code) e.code = res.err.code
      if (res.err.hostname) e.hostname = res.err.hostname
      if (res.err.provider) e.provider = res.err.provider
      if (res.err.subject) e.subject = res.err.subject
      if (res.err.sans) e.sans = res.err.sans
      if (res.err.peerPin) e.peerPin = res.err.peerPin
      if (res.err.expectedPins) e.expectedPins = res.err.expectedPins
    }
    queueMicrotask(() => {
      cleanupListeners()
      if (typeof cb === 'function') cb(e)
      socket.emit('error', e)
    })
    return socket
  }

  return socket
}

/**
 * Set or extend runtime TLS certificate pins at runtime.
 *
 * Pins use the same format as the static `tls_pins` config:
 *
 *   <host> sha256/<base64>
 *
 * Invalid entries throw a `TypeError`.
 *
 * @param {string|string[]} pins - a string or array of pin lines
 * @param {TlsPinsOptions} [options]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function setTlsPins (pins, options = {}) {
  let value = ''

  if (Array.isArray(pins)) {
    value = pins.join('\n')
  } else if (typeof pins === 'string') {
    value = pins
  } else {
    throw new TypeError('pins must be a string or an array of strings')
  }

  const mode = normalisePinsMode(options && options.mode, 'options.mode')
  validateTlsPinsConfigValue(value, 'TLS pins')

  const result =
    value && (value.length > 1024 || value.includes('\n'))
      ? await ipc.write('tls.setPins', { mode }, value)
      : await ipc.request('tls.setPins', { value, mode })

  const { data, err } = result

  if (err) {
    const error = new Error(err?.message || 'Failed to set TLS pins')
    error.code = err?.code || 'TLS_PINS_ERROR'
    throw error
  }

  return data
}

/**
 * Get the current runtime TLS pins configuration.
 *
 * @returns {Promise<{ value: string }>}
 */
export async function getTlsPins () {
  const { data, err } = await ipc.request('tls.getPins', {})

  if (err) {
    const error = new Error(err?.message || 'Failed to get TLS pins')
    error.code = err?.code || 'TLS_PINS_ERROR'
    throw error
  }

  return data
}

/**
 * Get the active runtime TLS provider name.
 *
 * @returns {Promise<{ provider: string }>}
 */
export async function getTlsProvider () {
  const { data, err } = await ipc.request('tls.getProvider', {})

  if (err) {
    const error = new Error(err?.message || 'Failed to get TLS provider')
    error.code = err?.code || 'TLS_PROVIDER_ERROR'
    throw error
  }

  return data
}

/**
 * Clear all runtime TLS pins.
 *
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function clearTlsPins () {
  return setTlsPins('', { mode: 'replace' })
}

/**
 * Create a `sha256/<base64>` pin from a leaf certificate DER payload.
 *
 * @param {Buffer|ArrayBufferView|ArrayBuffer} der
 * @returns {Promise<string>}
 */
export async function createTlsPinFromCertificateDer (der) {
  const buf = Buffer.from(der)
  const digest = await createDigest('SHA-256', buf)
  return `sha256/${digest.toString('base64')}`
}

/**
 * Create a `sha256/<base64>` pin from a PEM-encoded certificate.
 *
 * @param {string} pem
 * @returns {Promise<string>}
 */
export async function createTlsPinFromCertificatePem (pem) {
  if (typeof pem !== 'string') {
    throw new TypeError('pem must be a string')
  }

  const match = pem.match(
    /-----BEGIN CERTIFICATE-----([\s\S]+?)-----END CERTIFICATE-----/m
  )

  const body = (match ? match[1] : pem).replace(/[\r\n\t ]+/g, '')
  return createTlsPinFromCertificateDer(Buffer.from(body, 'base64'))
}

function commentIndex (line) {
  const hash = line.indexOf('#')
  const semi = line.indexOf(';')
  if (hash === -1) return semi
  if (semi === -1) return hash
  return Math.min(hash, semi)
}

function validateTlsPinsConfigValue (value, label) {
  const input = String(value || '')
  if (!input.trim()) return

  const lines = input.split(/\r?\n/)
  for (let index = 0; index < lines.length; index++) {
    const lineNumber = index + 1
    const rawLine = lines[index]
    let line = String(rawLine || '').trim()
    if (!line) continue

    const cidx = commentIndex(line)
    if (cidx !== -1) line = line.slice(0, cidx).trim()
    if (!line) continue

    const parts = line.split(/\s+/).filter(Boolean)
    if (!parts.length) continue

    const firstToken = parts[0]
    const lowerFirstToken = String(firstToken).toLowerCase()
    if (lowerFirstToken.startsWith('sha256/')) {
      throw new TypeError(
        `${label} entries must begin with a host, not a pin token: ${firstToken} (line ${lineNumber})`
      )
    }

    const firstPin = normaliseTlsPinToken(firstToken)
    if (firstPin) {
      throw new TypeError(
        `${label} entries must begin with a host, not a pin token: ${firstToken} (line ${lineNumber})`
      )
    }

    const hostNorm = normaliseTlsPinEndpointLoose(firstToken)
    if (!hostNorm) {
      throw new TypeError(
        `Invalid host in ${label} entry: ${firstToken} (line ${lineNumber})`
      )
    }

    if (parts.length < 2) {
      throw new TypeError(
        `Pins entry for '${hostNorm}' in ${label} must include at least one pin token (line ${lineNumber})`
      )
    }

    for (const token of parts.slice(1)) {
      const normalized = normaliseTlsPinToken(token)
      if (!normalized) {
        throw new TypeError(
          `Invalid TLS pin token for '${hostNorm}' in ${label}: ${token} (line ${lineNumber})`
        )
      }
    }
  }
}

function normaliseTlsPinHostLoose (rawHost) {
  let host = String(rawHost || '')
    .trim()
    .toLowerCase()
  if (!host) return ''

  const scheme = host.indexOf('://')
  if (scheme !== -1) {
    host = host.slice(scheme + 3)
  } else if (host.startsWith('//')) {
    host = host.slice(2)
  }

  const cut = host.search(/[/?#]/)
  if (cut !== -1) host = host.slice(0, cut)

  const at = host.lastIndexOf('@')
  if (at !== -1) host = host.slice(at + 1)

  host = host.trim()
  if (!host) return ''

  while (host.endsWith('.')) host = host.slice(0, -1)
  if (!host) return ''

  if (host.startsWith('[')) {
    const end = host.indexOf(']')
    if (end === -1 || end <= 1) return ''
    const inner = host.slice(1, end)
    const suffix = host.slice(end + 1)

    if (!suffix) {
      return inner
    }

    if (!suffix.startsWith(':')) return ''
    const port = suffix.slice(1)
    if (!port || !/^[0-9]+$/.test(port)) return ''

    return inner
  }

  const colon = host.lastIndexOf(':')
  if (colon !== -1) {
    const prefix = host.slice(0, colon)
    const suffix = host.slice(colon + 1)
    if (!prefix.includes(':')) {
      if (!prefix || !suffix) return ''
      if (!/^[0-9]+$/.test(suffix)) return ''
      host = prefix
      while (host.endsWith('.')) host = host.slice(0, -1)
      if (!host) return ''
    }
  }

  return host
}

function normaliseTlsPinEndpointLoose (rawHost) {
  let host = String(rawHost || '')
    .trim()
    .toLowerCase()
  if (!host) return ''

  const scheme = host.indexOf('://')
  if (scheme !== -1) {
    host = host.slice(scheme + 3)
  } else if (host.startsWith('//')) {
    host = host.slice(2)
  }

  const cut = host.search(/[/?#]/)
  if (cut !== -1) host = host.slice(0, cut)

  const at = host.lastIndexOf('@')
  if (at !== -1) host = host.slice(at + 1)

  host = host.trim()
  if (!host) return ''

  while (host.endsWith('.')) host = host.slice(0, -1)
  if (!host) return ''

  if (host.startsWith('[')) {
    const end = host.indexOf(']')
    if (end === -1 || end <= 1) return ''
    const inner = host.slice(1, end)
    const suffix = host.slice(end + 1)

    if (!suffix) {
      return inner
    }

    if (!suffix.startsWith(':')) return ''
    const port = suffix.slice(1)
    if (!port || !/^[0-9]+$/.test(port)) return ''

    return `[${inner}]:${port}`
  }

  const colon = host.lastIndexOf(':')
  if (colon !== -1) {
    const prefix = host.slice(0, colon)
    const suffix = host.slice(colon + 1)
    if (!prefix.includes(':')) {
      if (!prefix || !suffix) return ''
      if (!/^[0-9]+$/.test(suffix)) return ''
      let base = prefix
      while (base.endsWith('.')) base = base.slice(0, -1)
      if (!base) return ''
      return `${base}:${suffix}`
    }
  }

  return host
}

const normaliseTlsPinHost = (input) => {
  if (typeof input !== 'string') {
    throw new TypeError('host must be a string')
  }

  let host = input.trim()
  if (!host) {
    throw new TypeError('host must not be empty')
  }

  try {
    if (host.includes('://')) {
      const url = new URL(host)
      host = url.hostname
      if (url.port) {
        host = host.includes(':')
          ? `[${host}]:${url.port}`
          : `${host}:${url.port}`
      }
    } else if (host.startsWith('//')) {
      const url = new URL(`https:${host}`)
      host = url.hostname
      if (url.port) {
        host = host.includes(':')
          ? `[${host}]:${url.port}`
          : `${host}:${url.port}`
      }
    }
  } catch {}

  const slash = host.indexOf('/')
  if (slash !== -1) host = host.slice(0, slash)

  const normalized = normaliseTlsPinEndpointLoose(host)
  if (!normalized) {
    throw new TypeError('host must be a valid hostname or hostname:port')
  }

  return normalized
}

const normalisePinsInputForHost = (pins, hostNorm) => {
  const out = []
  const lines = Array.isArray(pins) ? pins : String(pins || '').split('\n')

  for (const rawLine of lines) {
    let line = String(rawLine || '').trim()
    if (!line) continue
    const cidx = commentIndex(line)
    if (cidx !== -1) line = line.slice(0, cidx).trim()
    if (!line) continue

    const parts = line.split(/\s+/).filter(Boolean)
    if (!parts.length) continue

    const firstToken = parts[0]
    const firstPin = normaliseTlsPinToken(firstToken)
    const tokens =
      firstPin || firstToken.toLowerCase().startsWith('sha256/')
        ? parts
        : parts.slice(1)

    if (!firstPin && !firstToken.toLowerCase().startsWith('sha256/')) {
      const lineHost = normaliseTlsPinEndpointLoose(firstToken)
      if (!lineHost) {
        throw new TypeError(`Invalid host in pin entry: ${firstToken}`)
      }
      if (lineHost !== hostNorm) {
        throw new TypeError(
          `Pins entry host '${lineHost}' does not match target host '${hostNorm}'`
        )
      }
    }

    for (const token of tokens) {
      const normalized = normaliseTlsPinToken(token)
      if (!normalized) {
        throw new TypeError(`Invalid TLS pin token: ${token}`)
      }
      out.push(normalized)
    }
  }

  if (out.length === 0) {
    throw new TypeError('pins must include at least one valid sha256 pin token')
  }

  return out
}

const uniqueStrings = (values) => {
  const seen = new Set()
  const out = []
  for (const value of values) {
    if (seen.has(value)) continue
    seen.add(value)
    out.push(value)
  }
  return out
}

const readPinsForHostFromConfigValue = (value, hostNorm) => {
  const exactPins = []
  const basePins = []
  let exactConfigured = false
  let baseConfigured = false
  const seenExact = new Set()
  const seenBase = new Set()

  const hostBase = normaliseTlsPinHostLoose(hostNorm) || hostNorm

  const lines = typeof value === 'string' ? value.split(/\r?\n/) : []
  for (const rawLine of lines) {
    let line = String(rawLine || '').trim()
    if (!line) continue

    const cidx = commentIndex(line)
    if (cidx !== -1) line = line.slice(0, cidx).trim()
    if (!line) continue

    const parts = line.split(/\s+/).filter(Boolean)
    if (!parts.length) continue

    const firstToken = parts[0]
    if (String(firstToken).toLowerCase().startsWith('sha256/')) continue
    if (normaliseTlsPinToken(firstToken)) continue

    const lineHost = normaliseTlsPinEndpointLoose(firstToken)
    if (!lineHost) continue
    if (lineHost === hostNorm) {
      exactConfigured = true
    } else if (lineHost === hostBase) {
      baseConfigured = true
    } else {
      continue
    }

    for (const token of parts.slice(1)) {
      const normalized = normaliseTlsPinToken(token)
      if (!normalized) continue

      if (lineHost === hostNorm) {
        if (seenExact.has(normalized)) continue
        seenExact.add(normalized)
        exactPins.push(normalized)
      } else {
        if (seenBase.has(normalized)) continue
        seenBase.add(normalized)
        basePins.push(normalized)
      }
    }
  }

  if (exactConfigured) {
    return { configured: true, pins: exactPins }
  }

  if (baseConfigured) {
    return { configured: true, pins: basePins }
  }

  return { configured: false, pins: [] }
}

const updatePinsConfigValueForHost = (value, hostNorm, pins) => {
  const input = typeof value === 'string' ? value : ''
  const hadTrailingNewline = input.endsWith('\n')
  const originalLines = input.split(/\r?\n/)
  const nextLines = []

  for (const rawLine of originalLines) {
    const trimmed = String(rawLine || '').trim()
    if (!trimmed) {
      nextLines.push(rawLine)
      continue
    }

    let line = trimmed
    const cidx = commentIndex(line)
    if (cidx !== -1) line = line.slice(0, cidx).trim()
    if (!line) {
      nextLines.push(rawLine)
      continue
    }

    const parts = line.split(/\s+/).filter(Boolean)
    if (!parts.length) {
      nextLines.push(rawLine)
      continue
    }

    const lineHost = normaliseTlsPinEndpointLoose(parts[0])
    if (lineHost && lineHost === hostNorm) {
      continue
    }

    nextLines.push(rawLine)
  }

  const pinLines = Array.isArray(pins) ? pins : null
  if (pinLines && pinLines.length) {
    while (nextLines.length && nextLines[nextLines.length - 1] === '') {
      nextLines.pop()
    }

    for (const pin of pinLines) {
      nextLines.push(`${hostNorm} ${pin}`)
    }
  }

  let out = nextLines.join('\n')
  if (hadTrailingNewline && !out.endsWith('\n')) out += '\n'
  return out
}

/**
 * Get configured runtime TLS pins for a host.
 *
 * @param {string} host
 * @returns {Promise<{ host: string, configured: boolean, pins: string[] }>}
 */
export async function getTlsPinsForHost (host) {
  const hostNorm = normaliseTlsPinHost(host)
  const { value } = await getTlsPins()
  const { configured, pins } = readPinsForHostFromConfigValue(value, hostNorm)
  return { host: hostNorm, configured, pins }
}

/**
 * Replace runtime TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function setTlsPinsForHost (host, pins) {
  const hostNorm = normaliseTlsPinHost(host)
  const desiredPins = uniqueStrings(normalisePinsInputForHost(pins, hostNorm))
  const { value } = await getTlsPins()
  const nextValue = updatePinsConfigValueForHost(value, hostNorm, desiredPins)
  return setTlsPins(nextValue, { mode: 'replace' })
}

/**
 * Add one or more runtime TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function addTlsPinsForHost (host, pins) {
  const hostNorm = normaliseTlsPinHost(host)
  const incomingPins = normalisePinsInputForHost(pins, hostNorm)
  const { value } = await getTlsPins()
  const current = readPinsForHostFromConfigValue(value, hostNorm)
  const combinedPins = uniqueStrings([...current.pins, ...incomingPins])
  const nextValue = updatePinsConfigValueForHost(value, hostNorm, combinedPins)
  return setTlsPins(nextValue, { mode: 'replace' })
}

/**
 * Remove one or more runtime TLS pins for a single host.
 * When `pins` is omitted, removes the host entry entirely.
 *
 * @param {string} host
 * @param {string|string[]} [pins]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function removeTlsPinsForHost (host, pins) {
  const hostNorm = normaliseTlsPinHost(host)
  const { value } = await getTlsPins()

  if (pins == null) {
    const nextValue = updatePinsConfigValueForHost(value, hostNorm, null)
    return setTlsPins(nextValue, { mode: 'replace' })
  }

  const removePins = new Set(normalisePinsInputForHost(pins, hostNorm))
  const current = readPinsForHostFromConfigValue(value, hostNorm)
  const remainingPins = current.pins.filter((pin) => !removePins.has(pin))
  const desired = remainingPins.length ? remainingPins : null
  const nextValue = updatePinsConfigValueForHost(value, hostNorm, desired)
  return setTlsPins(nextValue, { mode: 'replace' })
}

/**
 * @typedef {'append'|'replace'} WebViewTlsPinsMode
 *
 * @typedef {Object} WebViewTlsPinsOptions
 * @property {WebViewTlsPinsMode} [mode='append']
 */

/**
 * Configure WebView TLS certificate pins at runtime.
 *
 * This updates the process-wide `webview_tls_pins` configuration and refreshes
 * all active window/bridge configs so platform WebViews immediately see the
 * new pins. Pins use the same format as the static `webview_tls_pins` config:
 *
 *   <host> sha256/<base64>
 *
 * Invalid entries throw a `TypeError`.
 *
 * @param {string|string[]} pins - a string or array of pin lines
 * @param {WebViewTlsPinsOptions} [options]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function setWebViewTlsPins (pins, options = {}) {
  let value = ''

  if (Array.isArray(pins)) {
    value = pins.join('\n')
  } else if (typeof pins === 'string') {
    value = pins
  } else {
    throw new TypeError('pins must be a string or an array of strings')
  }

  const mode = normalisePinsMode(options && options.mode, 'options.mode')
  validateTlsPinsConfigValue(value, 'WebView TLS pins')

  const result =
    value && (value.length > 1024 || value.includes('\n'))
      ? await ipc.write('application.setWebviewTlsPins', { mode }, value)
      : await ipc.request('application.setWebviewTlsPins', { value, mode })

  const { data, err } = result

  if (err) {
    const error = new Error(err?.message || 'Failed to set WebView TLS pins')
    error.code = err?.code || 'WEBVIEW_TLS_PINS_ERROR'
    throw error
  }

  return data
}

/**
 * Get the current WebView TLS pins configuration.
 *
 * @returns {Promise<{ value: string }>}
 */
export async function getWebViewTlsPins () {
  const { data, err } = await ipc.request('application.getWebviewTlsPins', {})

  if (err) {
    const error = new Error(err?.message || 'Failed to get WebView TLS pins')
    error.code = err?.code || 'WEBVIEW_TLS_PINS_ERROR'
    throw error
  }

  return data
}

/**
 * Clear all WebView TLS pins.
 *
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function clearWebViewTlsPins () {
  return setWebViewTlsPins('', { mode: 'replace' })
}

/**
 * Get configured WebView TLS pins for a host.
 *
 * @param {string} host
 * @returns {Promise<{ host: string, configured: boolean, pins: string[] }>}
 */
export async function getWebViewTlsPinsForHost (host) {
  const hostNorm = normaliseTlsPinHost(host)
  const { value } = await getWebViewTlsPins()
  const { configured, pins } = readPinsForHostFromConfigValue(value, hostNorm)
  return { host: hostNorm, configured, pins }
}

/**
 * Replace WebView TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function setWebViewTlsPinsForHost (host, pins) {
  const hostNorm = normaliseTlsPinHost(host)
  const desiredPins = uniqueStrings(normalisePinsInputForHost(pins, hostNorm))
  const { value } = await getWebViewTlsPins()
  const nextValue = updatePinsConfigValueForHost(value, hostNorm, desiredPins)
  return setWebViewTlsPins(nextValue, { mode: 'replace' })
}

/**
 * Add one or more WebView TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function addWebViewTlsPinsForHost (host, pins) {
  const hostNorm = normaliseTlsPinHost(host)
  const incomingPins = normalisePinsInputForHost(pins, hostNorm)
  const { value } = await getWebViewTlsPins()
  const current = readPinsForHostFromConfigValue(value, hostNorm)
  const combinedPins = uniqueStrings([...current.pins, ...incomingPins])
  const nextValue = updatePinsConfigValueForHost(value, hostNorm, combinedPins)
  return setWebViewTlsPins(nextValue, { mode: 'replace' })
}

/**
 * Remove one or more WebView TLS pins for a single host.
 * When `pins` is omitted, removes the host entry entirely.
 *
 * @param {string} host
 * @param {string|string[]} [pins]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export async function removeWebViewTlsPinsForHost (host, pins) {
  const hostNorm = normaliseTlsPinHost(host)
  const { value } = await getWebViewTlsPins()

  if (pins == null) {
    const nextValue = updatePinsConfigValueForHost(value, hostNorm, null)
    return setWebViewTlsPins(nextValue, { mode: 'replace' })
  }

  const removePins = new Set(normalisePinsInputForHost(pins, hostNorm))
  const current = readPinsForHostFromConfigValue(value, hostNorm)
  const remainingPins = current.pins.filter((pin) => !removePins.has(pin))
  const desired = remainingPins.length ? remainingPins : null
  const nextValue = updatePinsConfigValueForHost(value, hostNorm, desired)
  return setWebViewTlsPins(nextValue, { mode: 'replace' })
}

export default {
  TLSSocket,
  TLSServer,
  createServer,
  connect,
  setTlsPins,
  getTlsPins,
  getTlsProvider,
  clearTlsPins,
  getTlsPinsForHost,
  setTlsPinsForHost,
  addTlsPinsForHost,
  removeTlsPinsForHost,
  createTlsPinFromCertificateDer,
  createTlsPinFromCertificatePem,
  setWebViewTlsPins,
  getWebViewTlsPins,
  clearWebViewTlsPins,
  getWebViewTlsPinsForHost,
  setWebViewTlsPinsForHost,
  addWebViewTlsPinsForHost,
  removeWebViewTlsPinsForHost
}
