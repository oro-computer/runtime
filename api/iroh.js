/**
 * @module iroh
 *
 * High-level bindings for the native iroh runtime. Provides helpers to create
 * endpoints, establish peer-to-peer connections, exchange datagrams, and work
 * with unidirectional/bidirectional streams.
 */

import ipc from './ipc.js'
import { Buffer } from './buffer.js'
import { rand64 } from './crypto.js'

const ROUTES = Object.freeze({
  init: 'iroh.init',
  shutdown: 'iroh.shutdown',
  status: 'iroh.status',
  setLogLevel: 'iroh.setLogLevel',
  pathToKey: 'iroh.pathToKey',
  keyToPath: 'iroh.keyToPath',
  endpointCreate: 'iroh.endpoint.create',
  endpointDestroy: 'iroh.endpoint.destroy',
  endpointBind: 'iroh.endpoint.bind',
  endpointHomeRelay: 'iroh.endpoint.homeRelay',
  endpointNodeAddr: 'iroh.endpoint.nodeAddr',
  endpointClose: 'iroh.endpoint.close',
  connectionConnect: 'iroh.connection.connect',
  connectionAccept: 'iroh.connection.accept',
  connectionAcceptAny: 'iroh.connection.acceptAny',
  connectionClose: 'iroh.connection.close',
  connectionWaitClosed: 'iroh.connection.waitClosed',
  connectionStats: 'iroh.connection.stats',
  connectionDatagramWrite: 'iroh.connection.datagram.write',
  connectionDatagramRead: 'iroh.connection.datagram.read',
  connectionOpenBi: 'iroh.connection.openBi',
  connectionOpenUni: 'iroh.connection.openUni',
  connectionAcceptBi: 'iroh.connection.acceptBi',
  connectionAcceptUni: 'iroh.connection.acceptUni',
  connectionTypeWatch: 'iroh.connectionType.watch',
  streamWrite: 'iroh.stream.write',
  streamFinish: 'iroh.stream.finish',
  streamRead: 'iroh.stream.read',
  streamReadToEnd: 'iroh.stream.readToEnd'
})

export const LOG_LEVELS = Object.freeze({
  trace: 0,
  debug: 1,
  info: 2,
  warn: 3,
  error: 4,
  off: 5
})

const LEVEL_NAMES = Object.freeze(
  Object.entries(LOG_LEVELS).reduce((map, [name, value]) => {
    map[value] = name
    return map
  }, {})
)

function ensureResult (result, source) {
  if (result?.err) {
    throw result.err
  }

  if (result?.data !== undefined) {
    return result.data
  }

  if (result?.source === source) {
    return null
  }

  return result
}

async function send (route, params = {}, transfer) {
  const result = await ipc.send(route, params, transfer)
  return ensureResult(result, route)
}

function generateId () {
  return rand64().toString(10)
}

function encodeBytes (input) {
  if (input == null) return ''
  if (typeof input === 'string') {
    return Buffer.from(input).toString('base64')
  }
  if (Buffer.isBuffer(input)) {
    return input.toString('base64')
  }
  if (typeof input.length === 'number') {
    return Buffer.from(input).toString('base64')
  }
  throw new TypeError('Expected a string, Buffer, or array-like of bytes')
}

function decodeBytes (input) {
  if (typeof input !== 'string' || input.length === 0) return new Uint8Array()
  const buffer = Buffer.from(input, 'base64')
  return new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength)
}

function serializeAlpns (alpns) {
  if (!alpns || alpns.length === 0) return ''
  const values = []
  for (const entry of alpns) {
    values.push(encodeBytes(entry))
  }
  return values.join(',')
}

const DISCOVERY_VALUES = new Set(['default', 'none'])

function normalizeDiscovery (value) {
  if (value == null) return undefined
  const normalized = String(value).trim().toLowerCase()
  if (normalized.length === 0) return undefined
  if (!DISCOVERY_VALUES.has(normalized)) {
    throw new TypeError('discovery must be "default" or "none"')
  }
  return normalized
}

function normalizeStatus (data = {}) {
  const initialized = Boolean(data?.initialized)
  const version = typeof data?.version === 'string' ? data.version : ''
  const logLevel = normalizeLogLevel(data?.logLevel)
  return { initialized, version, logLevel }
}

function levelNameFromValue (value) {
  return LEVEL_NAMES[value] ?? 'info'
}

function clampLogLevel (value) {
  const min = LOG_LEVELS.trace
  const max = LOG_LEVELS.off
  if (value < min) return min
  if (value > max) return max
  return value
}

export function normalizeLogLevel (level) {
  if (typeof level === 'number' && Number.isInteger(level)) {
    const clamped = clampLogLevel(level)
    return {
      value: clamped,
      name: levelNameFromValue(clamped)
    }
  }

  if (typeof level === 'string') {
    const normalized = level.toLowerCase().trim()
    if (Object.prototype.hasOwnProperty.call(LOG_LEVELS, normalized)) {
      const value = LOG_LEVELS[normalized]
      return { value, name: normalized }
    }
  }

  if (typeof level?.value === 'number') {
    return normalizeLogLevel(level.value)
  }

  if (typeof level?.name === 'string') {
    return normalizeLogLevel(level.name)
  }

  return { value: LOG_LEVELS.info, name: 'info' }
}

function serializeLogLevel (level) {
  const normalized = normalizeLogLevel(level)
  return normalized.name
}

export async function init () {
  const data = await send(ROUTES.init)
  return Boolean(data?.initialized ?? true)
}

export async function shutdown () {
  const data = await send(ROUTES.shutdown)
  return !(data?.initialized ?? false)
}

export async function status () {
  const data = (await send(ROUTES.status)) || {}
  return normalizeStatus(data)
}

export async function ensureInitialized () {
  const current = await status()
  if (current.initialized) {
    return current
  }

  await init()
  return status()
}

export async function version () {
  const info = await status()
  return info.version
}

export async function setLogLevel (level) {
  const value = serializeLogLevel(level)
  const data = await send(ROUTES.setLogLevel, { level: value })

  if (data?.logLevel) {
    return normalizeLogLevel(data.logLevel)
  }

  return normalizeLogLevel(value)
}

export function getLogLevelName (level) {
  return normalizeLogLevel(level).name
}

export function getLogLevelValue (level) {
  return normalizeLogLevel(level).value
}

export async function pathToKey (path, options = {}) {
  if (typeof path !== 'string' || path.length === 0) {
    throw new TypeError('path must be a non-empty string')
  }

  const params = { path, encoding: 'base64' }
  if (typeof options.prefix === 'string') params.prefix = options.prefix
  if (typeof options.root === 'string') params.root = options.root

  const data = await send(ROUTES.pathToKey, params)
  const encoded = data?.key
  if (typeof encoded !== 'string') {
    return new Uint8Array()
  }

  return decodeBytes(encoded)
}

export async function keyToPath (key, options = {}) {
  if (key == null) {
    throw new TypeError('key is required')
  }

  const params = {
    key: encodeBytes(key),
    encoding: 'base64'
  }

  if (typeof options.prefix === 'string') params.prefix = options.prefix
  if (typeof options.root === 'string') params.root = options.root

  const data = await send(ROUTES.keyToPath, params)
  const path = data?.path
  if (typeof path !== 'string') {
    throw new Error('invalid response: missing path')
  }
  return path
}

export class Endpoint {
  constructor (id) {
    this.id = id
    this.closed = false
  }

  static async create (options = {}) {
    const id = options.id ? String(options.id) : generateId()
    const params = { id }

    if (options.secretKey != null) {
      if (typeof options.secretKey !== 'string') {
        throw new TypeError('secretKey must be a base32 string')
      }
      params.secretKey = options.secretKey
    }
    const discovery = normalizeDiscovery(options.discovery)
    if (discovery) params.discovery = discovery
    if (options.relayMode) params.relayMode = String(options.relayMode)
    if (options.alpns && options.alpns.length > 0) {
      params.alpns = serializeAlpns(options.alpns)
    }

    await send(ROUTES.endpointCreate, params)
    return new Endpoint(id)
  }

  async bind (options = {}) {
    if (this.closed) throw new Error('Endpoint is closed')
    const params = { endpointId: this.id }
    if (options.ipv4) params.ipv4 = String(options.ipv4)
    if (options.ipv6) params.ipv6 = String(options.ipv6)
    await send(ROUTES.endpointBind, params)
  }

  async homeRelay () {
    if (this.closed) throw new Error('Endpoint is closed')
    const result = await send(ROUTES.endpointHomeRelay, { endpointId: this.id })
    return String(result?.url ?? '')
  }

  async nodeAddr () {
    if (this.closed) throw new Error('Endpoint is closed')
    const result = await send(ROUTES.endpointNodeAddr, { endpointId: this.id })
    return String(result?.nodeAddr ?? '')
  }

  async connect (options) {
    if (this.closed) throw new Error('Endpoint is closed')
    if (!options || typeof options.nodeAddr !== 'string') {
      throw new TypeError('nodeAddr is required to connect')
    }

    const connectionId = options.connectionId
      ? String(options.connectionId)
      : generateId()
    const params = {
      endpointId: this.id,
      connectionId,
      nodeAddr: options.nodeAddr,
      alpn: encodeBytes(options.alpn || '')
    }

    await send(ROUTES.connectionConnect, params)
    return new Connection(this, connectionId)
  }

  async accept (options = {}) {
    if (this.closed) throw new Error('Endpoint is closed')
    const connectionId = options.connectionId
      ? String(options.connectionId)
      : generateId()
    const params = {
      endpointId: this.id,
      connectionId
    }
    if (options.expectedAlpn) {
      params.expectedAlpn = encodeBytes(options.expectedAlpn)
    }

    const result = await send(ROUTES.connectionAccept, params)

    const connection = new Connection(this, connectionId)
    if (result?.alpn) {
      connection.remoteAlpn = decodeBytes(result.alpn)
    }
    return connection
  }

  async acceptAny (options = {}) {
    if (this.closed) throw new Error('Endpoint is closed')
    const connectionId = options.connectionId
      ? String(options.connectionId)
      : generateId()
    const result = await send(ROUTES.connectionAcceptAny, {
      endpointId: this.id,
      connectionId
    })
    const connection = new Connection(this, connectionId)
    if (result?.alpn) {
      connection.remoteAlpn = decodeBytes(result.alpn)
    }
    return connection
  }

  async close () {
    if (this.closed) return
    await send(ROUTES.endpointClose, { endpointId: this.id })
    this.closed = true
  }
}

export class Connection {
  constructor (endpoint, id) {
    this.endpoint = endpoint
    this.id = id
    this.closed = false
    this.remoteAlpn = null
  }

  async close () {
    if (this.closed) return
    await send(ROUTES.connectionClose, { connectionId: this.id })
    this.closed = true
  }

  async waitClosed () {
    if (this.closed) return
    await send(ROUTES.connectionWaitClosed, { connectionId: this.id })
    this.closed = true
  }

  async stats () {
    const result = await send(ROUTES.connectionStats, { connectionId: this.id })
    return {
      connectionId: this.id,
      maxDatagramSize: Number(result?.maxDatagramSize ?? 0),
      rtt: Number(result?.rtt ?? 0),
      packetLoss: Number(result?.packetLoss ?? 0)
    }
  }

  async writeDatagram (data, options = {}) {
    const params = {
      connectionId: this.id,
      data: encodeBytes(data)
    }
    if (options.timeoutMs != null) params.timeoutMs = String(options.timeoutMs)
    await send(ROUTES.connectionDatagramWrite, params)
  }

  async readDatagram (options = {}) {
    const params = { connectionId: this.id }
    if (options.timeoutMs != null) params.timeoutMs = String(options.timeoutMs)
    const result = await send(ROUTES.connectionDatagramRead, params)
    return decodeBytes(result?.data ?? '')
  }

  async openBidirectionalStream (options = {}) {
    const sendStreamId = options.sendStreamId
      ? String(options.sendStreamId)
      : generateId()
    const recvStreamId = options.recvStreamId
      ? String(options.recvStreamId)
      : generateId()
    await send(ROUTES.connectionOpenBi, {
      connectionId: this.id,
      sendStreamId,
      recvStreamId
    })
    return [
      new SendStream(this, sendStreamId),
      new RecvStream(this, recvStreamId)
    ]
  }

  async openUnidirectionalStream (options = {}) {
    const streamId = options.streamId ? String(options.streamId) : generateId()
    await send(ROUTES.connectionOpenUni, {
      connectionId: this.id,
      sendStreamId: streamId
    })
    return new SendStream(this, streamId)
  }

  async acceptBidirectionalStream (options = {}) {
    const sendStreamId = options.sendStreamId
      ? String(options.sendStreamId)
      : generateId()
    const recvStreamId = options.recvStreamId
      ? String(options.recvStreamId)
      : generateId()
    await send(ROUTES.connectionAcceptBi, {
      connectionId: this.id,
      sendStreamId,
      recvStreamId
    })
    return [
      new SendStream(this, sendStreamId),
      new RecvStream(this, recvStreamId)
    ]
  }

  async acceptUnidirectionalStream (options = {}) {
    const streamId = options.streamId ? String(options.streamId) : generateId()
    await send(ROUTES.connectionAcceptUni, {
      connectionId: this.id,
      recvStreamId: streamId
    })
    return new RecvStream(this, streamId)
  }

  async watchConnectionType (nodeId, listener) {
    if (typeof nodeId !== 'string' || nodeId.length === 0) {
      throw new TypeError('nodeId must be a non-empty string')
    }
    if (typeof listener !== 'function') {
      throw new TypeError('listener must be a function')
    }

    const params = { endpointId: this.endpoint.id, nodeId }

    const handler = (event) => {
      const detail = event?.detail
      const detailParams = detail?.params || detail
      if (!detailParams || detailParams.source !== 'iroh.connectionType') return

      const endpointId = String(
        detailParams?.data?.endpointId ?? detailParams?.err?.endpointId ?? ''
      )
      if (endpointId !== this.endpoint.id) {
        return
      }

      if (detailParams.err) {
        const message = String(
          detailParams.err.message ?? 'Iroh connection type watcher failed'
        )
        const error = new Error(message)
        if (detailParams.err.code != null) {
          error.code = String(detailParams.err.code)
        }
        listener(error)
        return
      }

      const payload = detailParams.data ?? {}
      const connectionType = payload.connectionType
        ? {
            value: connectionTypeValue(payload.connectionType.value),
            name: String(payload.connectionType.name ?? '')
          }
        : null

      listener(null, {
        nodeId: String(payload.nodeId ?? nodeId),
        connectionType
      })
    }

    const data = await send(ROUTES.connectionTypeWatch, params)
    if (!data?.watching) {
      throw new Error('failed to start connection type watcher')
    }
    globalThis.addEventListener('data', handler)

    return () => {
      globalThis.removeEventListener('data', handler)
      send(ROUTES.connectionTypeWatch, { ...params, watch: 'false' }).catch(
        () => {}
      )
    }
  }
}

function connectionTypeValue (value) {
  if (typeof value === 'number') return value
  const parsed = Number(value)
  return Number.isNaN(parsed) ? 0 : parsed
}

export class SendStream {
  constructor (connection, id) {
    this.connection = connection
    this.id = id
    this.closed = false
  }

  async write (data, options = {}) {
    const params = {
      streamId: this.id,
      data: encodeBytes(data)
    }
    if (options.timeoutMs != null) params.timeoutMs = String(options.timeoutMs)
    await send(ROUTES.streamWrite, params)
  }

  async finish () {
    if (this.closed) return
    await send(ROUTES.streamFinish, { streamId: this.id })
    this.closed = true
  }
}

export class RecvStream {
  constructor (connection, id) {
    this.connection = connection
    this.id = id
  }

  async read (length, options = {}) {
    if (typeof length !== 'number' || length <= 0) {
      throw new TypeError('length must be a positive number')
    }
    const params = {
      streamId: this.id,
      length: String(length)
    }
    if (options.timeoutMs != null) params.timeoutMs = String(options.timeoutMs)
    const result = await send(ROUTES.streamRead, params)
    return decodeBytes(result?.data ?? '')
  }

  async readToEnd (sizeLimit, timeoutMs) {
    const params = {
      streamId: this.id,
      sizeLimit: String(sizeLimit),
      timeoutMs: String(timeoutMs)
    }
    const result = await send(ROUTES.streamReadToEnd, params)
    return decodeBytes(result?.data ?? '')
  }
}

const api = Object.freeze({
  LOG_LEVELS,
  init,
  shutdown,
  status,
  ensureInitialized,
  version,
  setLogLevel,
  getLogLevelName,
  getLogLevelValue,
  normalizeLogLevel,
  pathToKey,
  keyToPath,
  Endpoint,
  Connection,
  SendStream,
  RecvStream
})

export default api
