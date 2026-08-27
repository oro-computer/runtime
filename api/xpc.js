import { EventEmitter } from './events.js'
import ipc from './ipc.js'
import { Buffer } from './buffer.js'
import { isPlainObject } from './util.js'

const connections = new Map()
const listeners = new Map()
const listenerSymbol = Symbol.for('oro.runtime.xpc.listener')
const VALUE_SYMBOL = Symbol.for('oro.runtime.xpc.value')
const CLOSED_SYMBOL = Symbol.for('oro.runtime.xpc.connection.closed')
const DESCRIPTOR_SYMBOL = Symbol.for('oro.runtime.xpc.descriptor')
const MAX_TIMEOUT_MS = 9223372036854

/**
 * @typedef {object} XPCExplicitValue
 * @property {string} type
 * @property {any} [value]
 * @property {string} [encoding]
 */

/**
 * @typedef {'utf8'|'utf-8'|'hex'|'base64'} XPCBufferEncoding
 */

/**
 * @typedef {object} XPCMessageTimeoutDetail
 * @property {string | null} messageId
 * @property {string | null} reason
 */

/**
 * @typedef {object} XPCMessageDroppedDetail
 * @property {string | null} reason
 */

function ensureResult (result, source) {
  if (result?.err) {
    const error = new Error(
      result.err.message || `XPC request failed for ${source}`
    )
    error.type = result.err.type || 'Error'
    error.code = result.err.code
    throw error
  }

  if (result?.data !== undefined) {
    return result.data
  }

  if (result?.source === source) {
    return result
  }

  return result
}

function toBuffer (value, encoding = 'utf8') {
  if (Buffer.isBuffer(value)) return value
  if (value instanceof ArrayBuffer) return Buffer.from(value)
  if (ArrayBuffer.isView(value)) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength)
  }
  if (typeof value === 'string') return Buffer.from(value, encoding)
  throw new TypeError('Unsupported binary payload for XPC value')
}

function isTypedValue (value) {
  return value && typeof value === 'object' && value[VALUE_SYMBOL] === true
}

function markTypedValue (type, payload, extras = {}) {
  return {
    __proto__: null,
    [VALUE_SYMBOL]: true,
    type,
    value: payload,
    ...extras
  }
}

function markDescriptor (descriptor) {
  if (!descriptor || typeof descriptor !== 'object') return descriptor
  if (descriptor[DESCRIPTOR_SYMBOL]) return descriptor
  Object.defineProperty(descriptor, DESCRIPTOR_SYMBOL, {
    configurable: false,
    enumerable: false,
    value: true
  })
  return descriptor
}

function isDescriptor (value) {
  return Boolean(
    value && typeof value === 'object' && value[DESCRIPTOR_SYMBOL] === true
  )
}

function encodeTypedValue (descriptor) {
  switch (descriptor.type) {
    case 'int64':
    case 'uint64': {
      const raw =
        typeof descriptor.value === 'bigint'
          ? descriptor.value.toString()
          : String(descriptor.value)
      return {
        type: descriptor.type,
        value: raw
      }
    }

    case 'data': {
      const buffer = toBuffer(descriptor.value, descriptor.encoding)
      return {
        type: 'data',
        encoding: 'base64',
        value: buffer.toString('base64')
      }
    }

    case 'bool':
      return {
        type: 'bool',
        value: descriptor.value ? 'true' : 'false'
      }

    case 'uuid':
      return {
        type: 'uuid',
        value: String(descriptor.value ?? '')
      }

    default:
      throw new TypeError(`Unsupported explicit XPC type '${descriptor.type}'`)
  }
}

function encodeValue (value) {
  if (isTypedValue(value)) {
    return encodeTypedValue(value)
  }

  if (isDescriptor(value)) {
    return value
  }

  if (value === null || value === undefined) {
    return { type: 'null' }
  }

  if (typeof value === 'string') {
    return { type: 'string', value }
  }

  if (typeof value === 'boolean') {
    return { type: 'bool', value: value ? 'true' : 'false' }
  }

  if (typeof value === 'number') {
    if (Number.isNaN(value) || !Number.isFinite(value)) {
      return { type: 'string', value: String(value) }
    }

    if (Number.isInteger(value)) {
      return {
        type: value >= 0 ? 'uint64' : 'int64',
        value: String(value)
      }
    }

    return { type: 'double', value: String(value) }
  }

  if (typeof value === 'bigint') {
    const type = value >= 0n ? 'uint64' : 'int64'
    return { type, value: value.toString() }
  }

  if (value instanceof Date) {
    return { type: 'string', value: value.toISOString() }
  }

  if (
    Buffer.isBuffer(value) ||
    value instanceof ArrayBuffer ||
    ArrayBuffer.isView(value)
  ) {
    const buffer = toBuffer(value)
    return {
      type: 'data',
      encoding: 'base64',
      value: buffer.toString('base64')
    }
  }

  if (Array.isArray(value)) {
    return {
      type: 'array',
      value: value.map(encodeValue)
    }
  }

  if (value instanceof Map) {
    const entries = {}
    for (const [key, entryValue] of value.entries()) {
      if (typeof key !== 'string') continue
      entries[key] = encodeValue(entryValue)
    }
    return {
      type: 'dictionary',
      value: entries
    }
  }

  if (isPlainObject(value)) {
    const entries = {}
    for (const [key, entryValue] of Object.entries(value)) {
      entries[key] = encodeValue(entryValue)
    }
    return {
      type: 'dictionary',
      value: entries
    }
  }

  return { type: 'string', value: String(value) }
}

function decodeValue (descriptor) {
  if (!descriptor || typeof descriptor !== 'object') return descriptor

  const type = descriptor.type || ''
  const value = descriptor.value

  switch (type) {
    case 'dictionary': {
      if (!value || typeof value !== 'object') {
        return {}
      }
      const output = {}
      for (const [key, entry] of Object.entries(value)) {
        output[key] = decodeValue(entry)
      }
      return output
    }

    case 'array':
      if (!Array.isArray(value)) return []
      return value.map(decodeValue)

    case 'string':
      return String(value ?? '')

    case 'bool':
      return value === true || value === 'true' || value === '1'

    case 'int64':
    case 'uint64': {
      try {
        const bigint = typeof BigInt === 'function' ? BigInt(value) : null
        if (bigint !== null) return bigint
      } catch {}
      const num = Number(value)
      return Number.isNaN(num) ? String(value ?? '') : num
    }

    case 'double': {
      const num = Number(value)
      return Number.isNaN(num) ? String(value ?? '') : num
    }

    case 'data': {
      const encoding = descriptor.encoding || 'base64'
      if (typeof value !== 'string') return Buffer.alloc(0)
      return Buffer.from(value, encoding)
    }

    case 'uuid':
      return String(value ?? '')

    case 'null':
      return null

    default:
      return descriptor.value ?? null
  }
}

function installListener () {
  if (globalThis[listenerSymbol]) return

  const handler = (event) => {
    const detail = event?.detail
    if (!detail) return

    const source = detail.source || detail.type || detail.params?.source
    const params = detail.params || detail.data
    if (!source || !params) return

    if (source === 'xpc.message') {
      const data = params.data || params
      if (!data) return
      const id = String(data.connectionId ?? '')
      const connection = connections.get(id)
      if (!connection) return

      const rawMessage = markDescriptor(data.message)
      const decoded = decodeValue(rawMessage)
      const messageId = data.messageId ? String(data.messageId) : null
      const expectsReply =
        data.expectsReply === true || data.expectsReply === 'true'
      const listenerId = data.listenerId ? String(data.listenerId) : null

      const envelope = {
        connection,
        connectionId: id,
        listenerId,
        expectsReply,
        messageId,
        raw: rawMessage,
        data: decoded,
        respond:
          expectsReply && messageId
            ? async (payload) => respond(messageId, payload, false)
            : undefined,
        respondError:
          expectsReply && messageId
            ? async (payload) => respond(messageId, payload, true)
            : undefined
      }

      connection.emit('message', envelope)
      return
    }

    if (source === 'xpc.listener.connection') {
      const data = params.data || params
      if (!data) return
      const listenerId = String(data.listenerId ?? '')
      const listener = listeners.get(listenerId)
      if (!listener) return

      const connectionId = String(data.connectionId ?? '')
      if (!connectionId.length) return

      if (connections.has(connectionId)) {
        const existing = connections.get(connectionId)
        listener.emit('connection', existing)
        return
      }

      const connection = new XPCConnection(connectionId, {
        parent: listener,
        service: data.service,
        label: data.label
      })

      connections.set(connectionId, connection)
      listener.emit('connection', connection)
      return
    }

    if (source === 'xpc.error') {
      const data = params.data || params
      if (!data) return
      const id = String(data.connectionId ?? '')
      const connection = connections.get(id)
      if (!connection) return
      const error = new Error(data.message || 'Unknown XPC error')
      error.code = data.code
      connection.emit('error', error)
      return
    }

    if (source === 'xpc.state') {
      const data = params.data || params
      if (!data) return
      const id = String(data.connectionId ?? '')
      const connection = connections.get(id)
      if (!connection) return

      const state = {
        state: data.state,
        reason: data.reason
      }

      connection.emit('state', state)

      if (
        state.state === 'invalid' ||
        state.state === 'terminationImminent' ||
        state.state === 'closed'
      ) {
        connection._emitClose(state)
        connections.delete(id)
        if (connection.isListener) {
          listeners.delete(id)
        }
      }

      return
    }

    if (source === 'xpc.message.timeout') {
      const data = params.data || params
      if (!data) return
      const id = String(data.connectionId ?? '')
      const connection = connections.get(id)
      if (!connection) return

      const detail = {
        messageId: data.messageId ? String(data.messageId) : null,
        reason: data.reason || null
      }

      connection.emit('message-timeout', detail)
      return
    }

    if (source === 'xpc.message.dropped') {
      const data = params.data || params
      if (!data) return
      const id = String(data.connectionId ?? '')
      const connection = connections.get(id)
      if (!connection) return

      const detail = {
        reason: data.reason || null
      }

      const error = new Error(detail.reason || 'XPC message dropped')
      error.code = 'XPC_MESSAGE_DROPPED'
      connection.emit('error', error)
      connection.emit('message-dropped', detail)
    }
  }

  globalThis.addEventListener('data', handler)
  globalThis[listenerSymbol] = handler
}

installListener()

/**
 * @typedef {import('./events.js').EventEmitter & {
 *   id: string,
 *   send(message: any, options?: any): Promise<any>,
 *   sendAndForget(message: any): Promise<any>,
 *   suspend(): Promise<boolean>,
 *   resume(): Promise<boolean>,
 *   close(): Promise<boolean>
 * }} Connection
 */

/**
 * @typedef {Connection} Listener
 */

/**
 * @typedef {new (id: string | number, options?: any) => Connection} ConnectionConstructor
 */

/**
 * @typedef {new (id: string | number, options?: any) => Listener} ListenerConstructor
 */

/**
 * Emitted when a pending XPC request exceeds its deadline.
 * @event Connection#message-timeout
 * @type {XPCMessageTimeoutDetail}
 */

/**
 * Emitted when an incoming message is dropped before reaching listeners.
 * @event Connection#message-dropped
 * @type {XPCMessageDroppedDetail}
 */

/**
 * Represents a live XPC connection.
 * @extends EventEmitter
 * @event Connection#message
 * @event Connection#state
 * @event Connection#close
 * @event Connection#error
 * @event Connection#message-timeout
 * @event Connection#message-dropped
 */
class XPCConnection extends EventEmitter {
  #id

  constructor (id, options = {}) {
    super()
    this.#id = String(id)
    this.isListener = Boolean(options?.listener)
    this.service = options?.service || null
    this.label = options?.label || null
    this.parentListener = options?.parent || null
    this[CLOSED_SYMBOL] = false
  }

  get id () {
    return this.#id
  }

  async send (message, options = {}) {
    const expectReply = options.expectReply !== false
    let timeout = 0

    if (expectReply && options.timeout !== undefined) {
      const numericTimeout = Number(options.timeout)
      if (!Number.isFinite(numericTimeout) || numericTimeout < 0) {
        throw new RangeError(
          'options.timeout must be a non-negative finite number'
        )
      }
      if (numericTimeout > MAX_TIMEOUT_MS) {
        throw new RangeError(`options.timeout must be <= ${MAX_TIMEOUT_MS}`)
      }
      timeout = Math.trunc(numericTimeout)
    }

    const payload = {
      connectionId: this.#id,
      message: JSON.stringify(encodeValue(message))
    }

    if (expectReply) {
      payload.expectReply = 'true'
    }

    if (timeout > 0) {
      payload.timeout = String(timeout)
    }

    const response = await ipc.request('xpc.send', payload)
    const result = ensureResult(response, 'xpc.send')

    if (result?.error) {
      const error = new Error(
        result.reason || result.message || 'XPC send failed'
      )
      error.code = result.type || 'XPC_ERROR'
      error.connectionId = this.#id
      throw error
    }

    if (!expectReply) {
      return result?.status === 'ok'
    }

    const messageDescriptor = result?.message
    if (!messageDescriptor) {
      throw new Error('XPC response missing message payload')
    }
    const descriptor = markDescriptor(messageDescriptor)
    return {
      raw: descriptor,
      data: decodeValue(descriptor),
      connection: this
    }
  }

  async sendAndForget (message) {
    return this.send(message, { expectReply: false })
  }

  async suspend () {
    const result = ensureResult(
      await ipc.request('xpc.suspend', { connectionId: this.#id }),
      'xpc.suspend'
    )
    return result?.state === 'suspended'
  }

  async resume () {
    const result = ensureResult(
      await ipc.request('xpc.resume', { connectionId: this.#id }),
      'xpc.resume'
    )
    return result?.state === 'active'
  }

  async close () {
    if (this[CLOSED_SYMBOL]) return false

    const hasConnection = connections.has(this.#id)
    if (!hasConnection) {
      if (this.isListener) {
        listeners.delete(this.#id)
      }
      this._emitClose({ state: 'closed' })
      return false
    }

    try {
      ensureResult(
        await ipc.request('xpc.disconnect', { connectionId: this.#id }),
        'xpc.disconnect'
      )
    } catch (err) {
      if (err?.type !== 'InvalidStateError') {
        throw err
      }
    }

    connections.delete(this.#id)
    if (this.isListener) {
      listeners.delete(this.#id)
    }
    this._emitClose({ state: 'closed' })
    return true
  }

  _emitClose (detail = { state: 'closed' }) {
    if (this[CLOSED_SYMBOL]) return
    this[CLOSED_SYMBOL] = true
    this.emit('close', detail)
  }
}

class XPCListener extends XPCConnection {
  constructor (id, options = {}) {
    super(id, { ...options, listener: true })
    this.isListener = true
  }
}

async function respond (messageId, payload, isError) {
  if (!messageId) {
    throw new TypeError('messageId is required when responding to XPC messages')
  }

  const route = isError ? 'xpc.respondError' : 'xpc.respond'
  const descriptor = encodeValue(payload ?? null)
  const body = {
    messageId,
    message: JSON.stringify(descriptor)
  }

  ensureResult(await ipc.request(route, body), route)
  return true
}

function normalizeConnectOptions (options = {}) {
  if (!isPlainObject(options)) {
    throw new TypeError('options must be an object')
  }

  const result = {}
  const {
    service,
    type,
    listener,
    privileged,
    flags,
    label,
    pendingReplyTimeout
  } = options

  if (typeof service !== 'string' || service.trim().length === 0) {
    throw new TypeError('options.service must be a non-empty string')
  }

  result.service = service

  if (typeof type === 'string' && type.length > 0) {
    const normalisedType = type === 'named' ? 'named' : 'mach-service'
    result.type = normalisedType
  }

  if (listener !== undefined) {
    result.listener = Boolean(listener)
  }

  if (privileged !== undefined) {
    result.privileged = Boolean(privileged)
  }

  if (label && typeof label === 'string') {
    result.label = label
  }

  if (flags !== undefined) {
    const numericFlags = Number(flags)
    if (!Number.isFinite(numericFlags) || numericFlags < 0) {
      throw new TypeError('options.flags must be a positive integer')
    }
    result.flags = Math.trunc(numericFlags)
  }

  if (pendingReplyTimeout !== undefined) {
    const numericTimeout = Number(pendingReplyTimeout)
    if (!Number.isFinite(numericTimeout) || numericTimeout < 0) {
      throw new RangeError(
        'options.pendingReplyTimeout must be a non-negative finite number'
      )
    }
    if (numericTimeout > MAX_TIMEOUT_MS) {
      throw new RangeError(
        `options.pendingReplyTimeout must be <= ${MAX_TIMEOUT_MS}`
      )
    }
    result.pendingReplyTimeout = Math.trunc(numericTimeout)
  }

  return result
}

/**
 * Retrieves XPC availability information for the current platform.
 * @returns {Promise<{ available: boolean, reason?: string }>}
 */
export async function availability () {
  const result = ensureResult(
    await ipc.request('xpc.availability', {}),
    'xpc.availability'
  )
  return {
    available: result?.available !== false,
    reason: result?.reason
  }
}

/**
 * Establishes an XPC connection.
 * @param {object} options
 * @param {string} options.service
 * @param {'mach-service' | 'named'} [options.type='mach-service']
 * @param {boolean} [options.listener=false]
 * @param {boolean} [options.privileged=false]
 * @param {number} [options.flags=0]
 * @param {string} [options.label]
 * @param {number} [options.pendingReplyTimeout=30000]
 * @returns {Promise<Connection>}
 */
export async function connect (options = {}) {
  const availabilityInfo = await availability()
  if (!availabilityInfo.available) {
    const message =
      availabilityInfo.reason || 'XPC is not available on this platform'
    const error = new Error(message)
    error.code = 'ERR_NOT_SUPPORTED'
    throw error
  }

  const normalised = normalizeConnectOptions(options)
  const payload = {
    options: JSON.stringify(normalised)
  }

  const result = ensureResult(
    await ipc.request('xpc.connect', payload),
    'xpc.connect'
  )
  const connectionId = String(
    result?.connectionId ??
      result?.id ??
      result?.data?.connectionId ??
      result?.data?.id ??
      ''
  )

  if (!connectionId.length) {
    throw new Error('Failed to establish XPC connection')
  }

  if (connections.has(connectionId)) {
    return connections.get(connectionId)
  }

  const Ctor = normalised.listener ? XPCListener : XPCConnection
  const connection = new Ctor(connectionId, {
    ...normalised,
    listener: normalised.listener === true
  })

  connections.set(connectionId, connection)

  if (connection.isListener) {
    listeners.set(connectionId, connection)
  }

  return connection
}

/**
 * Closes all tracked XPC connections.
 * @returns {Promise<void>}
 */
export async function disconnectAll () {
  const active = Array.from(connections.values())
  await Promise.allSettled(
    active.map(async (conn) => {
      try {
        await conn.close()
      } catch {}
    })
  )
  connections.clear()
  listeners.clear()
}

/**
 * Helper to encode a 64-bit signed integer.
 * @param {bigint | number | string} value
 * @returns {object}
 */
export function int64 (value) {
  return markTypedValue('int64', value)
}

/**
 * Helper to encode a 64-bit unsigned integer.
 * @param {bigint | number | string} value
 * @returns {object}
 */
export function uint64 (value) {
  return markTypedValue('uint64', value)
}

/**
 * Helper to encode binary payloads as XPC data.
 * @param {Buffer | ArrayBuffer | ArrayBufferView | string} value
 * @param {XPCBufferEncoding} [encoding='utf8']
 * @returns {object}
 */
export function data (value, encoding = 'utf8') {
  return markTypedValue('data', value, { encoding })
}

/**
 * Helper to encode UUID payloads.
 * @param {string | { toString(): string }} value
 * @returns {XPCExplicitValue}
 */
export function uuid (value) {
  const stringValue = String(value ?? '').trim()
  if (stringValue.length === 0) {
    throw new TypeError('value must be a non-empty UUID string')
  }
  return markTypedValue('uuid', stringValue)
}

/** @type {ConnectionConstructor} */
export const Connection = XPCConnection

/** @type {ListenerConstructor} */
export const Listener = XPCListener
