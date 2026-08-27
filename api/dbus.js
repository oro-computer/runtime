import { EventEmitter } from './events.js'
import ipc from './ipc.js'

import * as exports from './dbus.js'

/**
 * Result payload returned from the native IPC bridge.
 * @template TData
 * @template TError
 * @typedef {{ data?: TData, err?: TError, source?: string }} IPCResult
 */

/**
 * DBus message body expressed as signature + values tuple.
 * @typedef {Object} DBusBody
 * @property {string} signature
 * @property {any[]} values
 */

/**
 * Signal payload forwarded from the runtime.
 * @typedef {Object} DBusSignal
 * @property {string} connectionId
 * @property {string} path
 * @property {string} interface
 * @property {string} member
 * @property {string} sender
 * @property {DBusBody | any} [body]
 * @property {string} signature
 * @property {any[]} values
 * @property {string} [callId]
 * @property {any} raw
 */

/**
 * Ensures an IPC result matches the expected format.
 * @template TData
 * @template TError
 * @param {IPCResult<TData, TError>} result
 * @param {string} source
 * @returns {TData | IPCResult<TData, TError>}
 */
function ensureResult (result, source) {
  if (result?.err) {
    throw result.err
  }

  if (result?.data !== undefined) {
    return result.data
  }

  if (result?.source === source) {
    return result
  }

  return result
}

/** @type {Map<string, Connection>} */
const connections = new Map()
const listenerSymbol = Symbol.for('oro.runtime.dbus.listener')

/**
 * Enumerates buses understood by the runtime.
 * @type {{ readonly SESSION: 'session', readonly SYSTEM: 'system', readonly STARTER: 'starter', readonly ADDRESS: 'address' }}
 */
export const BUS = Object.freeze({
  SESSION: 'session',
  SYSTEM: 'system',
  STARTER: 'starter',
  ADDRESS: 'address'
})

/**
 * Flags for `requestName` calls, mirroring `DBUS_NAME_FLAG_*` constants.
 * @type {{ readonly NONE: 0, readonly ALLOW_REPLACEMENT: 0x1, readonly REPLACE_EXISTING: 0x2, readonly DO_NOT_QUEUE: 0x4 }}
 */
export const NAME_FLAGS = Object.freeze({
  NONE: 0,
  ALLOW_REPLACEMENT: 0x1,
  REPLACE_EXISTING: 0x2,
  DO_NOT_QUEUE: 0x4
})

/**
 * Replies for `requestName`, mirroring `DBUS_REQUEST_NAME_REPLY_*` constants.
 * @type {{ readonly PRIMARY_OWNER: 1, readonly IN_QUEUE: 2, readonly EXISTS: 3, readonly ALREADY_OWNER: 4 }}
 */
export const REQUEST_NAME_REPLY = Object.freeze({
  PRIMARY_OWNER: 1,
  IN_QUEUE: 2,
  EXISTS: 3,
  ALREADY_OWNER: 4
})

/**
 * Replies for `releaseName`, mirroring `DBUS_RELEASE_NAME_REPLY_*` constants.
 * @type {{ readonly RELEASED: 1, readonly NON_EXISTENT: 2, readonly NOT_OWNER: 3 }}
 */
export const RELEASE_NAME_REPLY = Object.freeze({
  RELEASED: 1,
  NON_EXISTENT: 2,
  NOT_OWNER: 3
})

/**
 * Message type codes per the DBus specification.
 * @type {{ readonly METHOD_CALL: 1, readonly METHOD_RETURN: 2, readonly ERROR: 3, readonly SIGNAL: 4 }}
 */
export const MESSAGE_TYPE = Object.freeze({
  METHOD_CALL: 1,
  METHOD_RETURN: 2,
  ERROR: 3,
  SIGNAL: 4
})

/**
 * Common well-known names.
 * @type {{ readonly DBUS: 'org.freedesktop.DBus' }}
 */
export const WELL_KNOWN_NAMES = Object.freeze({
  DBUS: 'org.freedesktop.DBus'
})

/**
 * Common well-known object paths.
 * @type {{ readonly DBUS: '/org/freedesktop/DBus' }}
 */
export const WELL_KNOWN_PATHS = Object.freeze({
  DBUS: '/org/freedesktop/DBus'
})

/**
 * Common well-known interfaces.
 * @type {{ readonly DBUS: 'org.freedesktop.DBus' }}
 */
export const WELL_KNOWN_INTERFACES = Object.freeze({
  DBUS: 'org.freedesktop.DBus'
})

/**
 * Common well-known members.
 * @type {{ readonly NAME_OWNER_CHANGED: 'NameOwnerChanged', readonly LIST_NAMES: 'ListNames' }}
 */
export const WELL_KNOWN_MEMBERS = Object.freeze({
  NAME_OWNER_CHANGED: 'NameOwnerChanged',
  LIST_NAMES: 'ListNames'
})

/**
 * Common well-known error names.
 * @type {{ readonly FAILED: 'org.freedesktop.DBus.Error.Failed', readonly UNKNOWN_OBJECT: 'org.freedesktop.DBus.Error.UnknownObject', readonly UNKNOWN_METHOD: 'org.freedesktop.DBus.Error.UnknownMethod', readonly SERVICE_UNKNOWN: 'org.freedesktop.DBus.Error.ServiceUnknown' }}
 */
export const WELL_KNOWN_ERRORS = Object.freeze({
  FAILED: 'org.freedesktop.DBus.Error.Failed',
  UNKNOWN_OBJECT: 'org.freedesktop.DBus.Error.UnknownObject',
  UNKNOWN_METHOD: 'org.freedesktop.DBus.Error.UnknownMethod',
  SERVICE_UNKNOWN: 'org.freedesktop.DBus.Error.ServiceUnknown'
})

const ERROR_NAME_PATTERN = /^[A-Za-z][\w]*(\.[A-Za-z0-9_]+)+$/

/**
 * Coerces an error name into a valid DBus error identifier.
 * @param {string} name
 * @returns {string}
 */
function normaliseErrorName (name) {
  if (typeof name === 'string' && ERROR_NAME_PATTERN.test(name)) {
    return name
  }
  return WELL_KNOWN_ERRORS.FAILED
}

/**
 * Parses a DBus match rule into key/value pairs for quick comparisons.
 * @param {string} rule
 * @returns {Record<string, string>}
 */
function parseMatchRule (rule) {
  const entries = {}
  if (typeof rule !== 'string' || rule.length === 0) {
    return entries
  }

  const parts = rule.split(',')
  for (const part of parts) {
    const [rawKey, rawValue] = part.split('=')
    if (!rawKey || rawValue === undefined) continue
    const key = rawKey.trim().toLowerCase()
    const value = rawValue.trim().replace(/^'|'$/g, '')
    entries[key] = value
  }
  return entries
}

/**
 * Determines whether a parsed rule matches an incoming signal.
 * @param {Record<string, string>} parsed
 * @param {DBusSignal} signal
 * @returns {boolean}
 */
function matchesRule (parsed, signal) {
  if (!parsed) return false
  if (parsed.type && parsed.type.toLowerCase() !== 'signal') return false
  if (parsed.interface && parsed.interface !== signal.interface) return false
  if (parsed.member && parsed.member !== signal.member) return false
  if (parsed.path && parsed.path !== signal.path) return false
  if (
    parsed.path_namespace &&
    !(signal.path || '').startsWith(parsed.path_namespace)
  ) {
    return false
  }
  if (parsed.sender && parsed.sender !== signal.sender) return false
  return true
}

/**
 * Options accepted when providing a structured DBus body.
 * @typedef {Object} StructuredBody
 * @property {string} [signature]
 * @property {any[]} values
 */

/**
 * Variant container helper used by {@link variant}.
 * @typedef {Object} VariantBody
 * @property {string} signature
 * @property {any} value
 */

/**
 * Options used when invoking {@link Connection#call}.
 * @typedef {Object} MethodCallOptions
 * @property {string} member
 * @property {string} [destination]
 * @property {string} [path]
 * @property {string} [interface]
 * @property {string} [signature]
 * @property {any[] | StructuredBody | VariantBody} [body]
 * @property {number} [timeout]
 * @property {boolean} [noReply]
 */

/**
 * Options used when emitting custom signals via {@link Connection#emitSignal}.
 * @typedef {Object} SignalOptions
 * @property {string} path
 * @property {string} name
 * @property {string} [interface]
 * @property {string} [signature]
 * @property {any[] | StructuredBody | VariantBody} [body]
 */

/**
 * Options to describe an exported DBus object.
 * @typedef {Object} ExportOptions
 * @property {string} path
 * @property {string} [interface]
 * @property {string[]} [methods]
 */

/**
 * Result object accepted by {@link Connection#respond} when acknowledging a method call.
 * @typedef {Object} MethodResult
 * @property {string} [signature]
 * @property {any[] | StructuredBody | VariantBody} [body]
 */

/**
 * Error descriptor accepted by {@link Connection#respond} when rejecting a method call.
 * @typedef {Object} MethodError
 * @property {true} error
 * @property {string} [name]
 * @property {string} [message]
 * @property {any[] | StructuredBody | VariantBody | any} [body]
 */

/**
 * Type guard checking whether a payload is a structured body.
 * @param {unknown} value
 * @returns {value is StructuredBody}
 */
function isStructuredBody (value) {
  return Boolean(
    value && typeof value === 'object' && Array.isArray(value.values)
  )
}

/**
 * Coerces DBus body values so they can be serialised for IPC transport.
 * - Structured bodies (`{ values: [...] }`) are flattened to their values.
 * - Array-like views (TypedArrays) are converted to plain arrays.
 * - Primitive values pass through unchanged (a TypeError is raised later if arrays are required).
 * @param {any[] | StructuredBody | VariantBody | any} value
 * @returns {any[] | StructuredBody | VariantBody | null | undefined}
 */
function normaliseBody (value) {
  if (value === undefined) {
    return undefined
  }

  if (value === null) {
    return null
  }

  if (isStructuredBody(value)) {
    return normaliseBody(value.values)
  }

  if (Array.isArray(value)) {
    return value
  }

  if (ArrayBuffer.isView(value)) {
    return Array.from(value)
  }

  if (value instanceof ArrayBuffer) {
    return Array.from(new Uint8Array(value))
  }

  if (value && typeof value === 'object') {
    return value
  }

  return value
}

/**
 * Serialises a DBus body payload into a JSON string for IPC.
 * @param {any} value
 * @param {string} [context='DBus body']
 * @returns {string|undefined}
 */
function stringifyBody (value, context = 'DBus body') {
  if (value === undefined) {
    return undefined
  }

  let payload = value
  if (Array.isArray(value)) {
    payload = { values: value }
  }

  try {
    return JSON.stringify(payload)
  } catch (err) {
    const message = err?.message || err
    throw new TypeError(`Failed to serialise ${context}: ${message}`)
  }
}

/**
 * Normalises connection options before serialising for IPC.
 * @param {Record<string, any>} value
 * @returns {Record<string, any> | null}
 */
function normaliseConnectOptions (value) {
  if (!value || typeof value !== 'object') {
    return null
  }

  const options = {}

  if (typeof value.bus === 'string' && value.bus.length) {
    options.bus = value.bus
  }

  if (typeof value.address === 'string' && value.address.length) {
    options.address = value.address
  }

  if (Object.prototype.hasOwnProperty.call(value, 'private')) {
    options.private = Boolean(value.private)
  }

  if (Object.prototype.hasOwnProperty.call(value, 'allowPeerAuthentication')) {
    options.allowPeerAuthentication = Boolean(value.allowPeerAuthentication)
  }

  return Object.keys(options).length ? options : null
}

/**
 * Runtime DBus connection wrapper.
 */
export class Connection extends EventEmitter {
  #id = ''
  #closed = false
  #matches = new Map()
  #exports = new Map()

  /**
   * @param {string | number} id
   */
  constructor (id) {
    super()
    this.#id = String(id)
  }

  /**
   * Unique identifier of the underlying DBus connection.
   * @returns {string}
   */
  get id () {
    return this.#id
  }

  /**
   * Indicates whether the connection has been closed.
   * @returns {boolean}
   */
  get closed () {
    return this.#closed
  }

  /**
   * Fetches global DBus availability metadata.
   * @returns {Promise<{ available: boolean, reason?: string }>}
   */
  async availability () {
    return exports.availability()
  }

  /**
   * Terminates the connection and removes all local bookkeeping.
   * @returns {Promise<boolean>}
   */
  async close () {
    if (this.#closed) return false
    try {
      ensureResult(
        await ipc.request('dbus.disconnect', { id: this.#id }),
        'dbus.disconnect'
      )
    } finally {
      this.#closed = true
      connections.delete(this.#id)
      this.#matches.clear()
      this.#exports.clear()
    }
    return true
  }

  /**
   * Requests the provided bus name on the connection.
   * @param {string} name
   * @param {number} [flags]
   * @returns {Promise<void>}
   */
  async requestName (name, flags = 0) {
    if (typeof name !== 'string' || !name.length) {
      throw new TypeError('name must be a non-empty string')
    }
    ensureResult(
      await ipc.request('dbus.requestName', {
        id: this.#id,
        name,
        flags
      }),
      'dbus.requestName'
    )
  }

  /**
   * Releases a previously requested bus name.
   * @param {string} name
   * @returns {Promise<void>}
   */
  async releaseName (name) {
    if (typeof name !== 'string' || !name.length) {
      throw new TypeError('name must be a non-empty string')
    }
    ensureResult(
      await ipc.request('dbus.releaseName', {
        id: this.#id,
        name
      }),
      'dbus.releaseName'
    )
  }

  /**
   * Adds a match rule for DBus signals.
   * @param {string} rule
   * @param {(signal: DBusSignal, matchId: string) => void} [handler]
   * @returns {Promise<string>}
   */
  async addMatch (rule, handler) {
    if (typeof rule !== 'string' || !rule.length) {
      throw new TypeError('rule must be a non-empty string')
    }

    const result = ensureResult(
      await ipc.request('dbus.addMatch', {
        id: this.#id,
        rule
      }),
      'dbus.addMatch'
    )

    const matchId = String(result?.matchId ?? result?.id ?? '')
    if (!matchId.length) {
      throw new Error('Failed to register DBus match rule')
    }

    const parsedRule = parseMatchRule(rule)
    this.#matches.set(matchId, { rule, parsed: parsedRule, handler })
    return matchId
  }

  /**
   * Removes a previously installed match rule.
   * @param {string | number} matchId
   * @returns {Promise<void>}
   */
  async removeMatch (matchId) {
    const id = String(matchId)
    ensureResult(
      await ipc.request('dbus.removeMatch', { matchId: id }),
      'dbus.removeMatch'
    )
    this.#matches.delete(id)
  }

  /**
   * Invokes a DBus method on the remote peer.
   * @param {MethodCallOptions} options
   * @returns {Promise<any>}
   */
  async call (options) {
    if (!options || typeof options !== 'object') {
      throw new TypeError('options must be an object')
    }

    const {
      destination = '',
      path = '',
      interface: iface = '',
      member,
      signature = '',
      body = [],
      timeout,
      noReply = false
    } = options

    if (typeof member !== 'string' || member.length === 0) {
      throw new TypeError('options.member must be a non-empty string')
    }

    const payload = normaliseBody(body)
    if (
      payload !== undefined &&
      payload !== null &&
      typeof payload !== 'object'
    ) {
      throw new TypeError(
        'options.body must resolve to an array or JSON object'
      )
    }

    const params = {
      id: this.#id,
      destination,
      path,
      interface: iface,
      member,
      signature,
      noReply: noReply ? 'true' : 'false'
    }

    const serialisedBody = stringifyBody(payload, 'DBus call body')
    if (serialisedBody !== undefined) {
      params.body = serialisedBody
    }

    if (Number.isInteger(timeout)) {
      params.timeout = timeout
    }

    const result = ensureResult(
      await ipc.request('dbus.call', params),
      'dbus.call'
    )

    if (noReply) {
      return undefined
    }

    return result?.body ?? result
  }

  /**
   * Emits a custom signal to the bus.
   * @param {SignalOptions} options
   * @returns {Promise<void>}
   */
  async emitSignal (options) {
    if (!options || typeof options !== 'object') {
      throw new TypeError('options must be an object')
    }

    const {
      path,
      interface: iface = '',
      name,
      signature = '',
      body = []
    } = options

    if (typeof path !== 'string' || !path.length) {
      throw new TypeError('options.path must be a non-empty string')
    }

    if (typeof name !== 'string' || !name.length) {
      throw new TypeError('options.name must be a non-empty string')
    }

    const payload = normaliseBody(body)
    if (
      payload !== undefined &&
      payload !== null &&
      typeof payload !== 'object'
    ) {
      throw new TypeError(
        'options.body must resolve to an array or JSON object'
      )
    }

    const params = {
      id: this.#id,
      path,
      interface: iface,
      name,
      signature
    }

    const serialisedBody = stringifyBody(payload, 'DBus signal body')
    if (serialisedBody !== undefined) {
      params.body = serialisedBody
    }

    ensureResult(await ipc.request('dbus.signal', params), 'dbus.signal')
  }

  _handleSignal (signal) {
    this.emit('signal', signal)

    for (const [matchId, entry] of this.#matches.entries()) {
      if (
        typeof entry.handler === 'function' &&
        matchesRule(entry.parsed, signal)
      ) {
        try {
          entry.handler(signal, matchId)
        } catch (err) {
          queueMicrotask(() => {
            throw err
          })
        }
      }
    }
  }

  /**
   * Exports an object path so native method calls are forwarded to JS listeners.
   * @param {ExportOptions} options
   * @returns {Promise<string>}
   */
  async exportObject (options) {
    if (!options || typeof options !== 'object') {
      throw new TypeError('options must be an object')
    }

    const { path, interface: iface = '', methods } = options
    if (typeof path !== 'string' || !path.length) {
      throw new TypeError('options.path must be a non-empty string')
    }

    const params = {
      id: this.#id,
      path,
      interface: iface
    }

    const definition = {}
    if (Array.isArray(methods) && methods.length) {
      definition.methods = methods.filter(
        (value) => typeof value === 'string' && value.length
      )
    }

    if (Object.keys(definition).length > 0) {
      params.definition = definition
    }

    const result = ensureResult(
      await ipc.request('dbus.exportObject', params),
      'dbus.exportObject'
    )
    const exportIdValue =
      result?.exportId ?? result?.id ?? result?.data?.exportId
    const exportId =
      typeof exportIdValue === 'string'
        ? exportIdValue
        : exportIdValue != null
          ? String(exportIdValue)
          : ''
    if (!exportId.length) {
      throw new Error('Failed to export DBus object')
    }

    this.#exports.set(exportId, { path, interface: iface })
    return exportId
  }

  /**
   * Removes a previously exported object path.
   * @param {string | number} exportId
   * @returns {Promise<void>}
   */
  async unexportObject (exportId) {
    const id = String(exportId)
    if (!id.length) {
      throw new TypeError('exportId must be provided')
    }
    ensureResult(
      await ipc.request('dbus.unexportObject', { exportId: id }),
      'dbus.unexportObject'
    )
    this.#exports.delete(id)
  }

  /**
   * Replies to a pending method call originating from the runtime.
   * @param {string | number} callId
   * @param {MethodResult | MethodError | Error} result
   * @returns {Promise<void>}
   */
  async respond (callId, result) {
    const id = String(callId)
    if (!id.length) {
      throw new TypeError('callId must be provided')
    }

    const params = { callId: id }
    let isError = false
    let name = ''
    let signature = ''
    /** @type {any[] | StructuredBody | VariantBody | undefined} */
    let body

    if (result instanceof Error) {
      isError = true
      name = normaliseErrorName(result.name)
      signature = result.message || ''
    } else if (result && typeof result === 'object' && result.error) {
      isError = true
      name = normaliseErrorName(result.name)
      signature = result.message || ''
      body = result.body
    } else if (result && typeof result === 'object') {
      signature = result.signature || ''
      body = result.body
    }

    if (isError) {
      params.error = 'true'
      if (name) params.name = name
      if (signature) params.signature = signature
      const payload = normaliseBody(body)
      const serialised = stringifyBody(payload, 'DBus error body')
      if (serialised !== undefined) {
        params.body = serialised
      }
    } else {
      params.name = name
      if (signature) params.signature = signature
      let payload = normaliseBody(body)
      if (payload === undefined && signature) {
        payload = []
      }
      if (
        payload !== undefined &&
        payload !== null &&
        typeof payload !== 'object'
      ) {
        throw new TypeError(
          'result.body must resolve to an array or JSON object'
        )
      }
      const serialised = stringifyBody(payload, 'DBus response body')
      if (serialised !== undefined) {
        params.body = serialised
      }
    }

    ensureResult(await ipc.send('dbus.respond', params), 'dbus.respond')
  }

  /**
   * Convenience helper to send an error response.
   * @param {string | number} callId
   * @param {string} [name]
   * @param {string} [message]
   * @returns {Promise<void>}
   */
  async respondError (callId, name, message) {
    return this.respond(callId, { error: true, name, message })
  }

  /**
   * Internal handler invoked when the runtime forwards a method call into JS.
   * @param {DBusSignal & { callId: string }} payload
   * @returns {void}
   */
  _handleMethodCall (payload) {
    this.emit('methodCall', payload)
  }
}

/**
 * Installs the shared window level listener that bridges runtime DBus events into JS.
 * @returns {void}
 */
function installListener () {
  if (globalThis[listenerSymbol]) return

  const handler = (event) => {
    const detail = event?.detail
    const source = detail?.source || detail?.params?.source
    const params = detail?.params
    if (!params) return

    if (source === 'dbus.signal') {
      const data = params.data || params
      if (!data) return
      const id = String(data.connectionId ?? data.id ?? '')
      const connection = connections.get(id)
      if (!connection) return
      connection._handleSignal({
        connectionId: id,
        path: data.path || '',
        interface: data.interface || '',
        member: data.member || '',
        sender: data.sender || '',
        body: data.body,
        signature: data.body?.signature ?? '',
        values: Array.isArray(data.body?.values) ? data.body.values : data.body,
        raw: data
      })
    } else if (source === 'dbus.methodCall') {
      const data = params.data || params
      if (!data) return
      const id = String(data.connectionId ?? '')
      const connection = connections.get(id)
      if (!connection) return
      connection._handleMethodCall({
        connectionId: id,
        callId: String(data.callId ?? ''),
        path: data.path || '',
        interface: data.interface || '',
        member: data.member || '',
        sender: data.sender || '',
        body: data.body,
        signature: data.body?.signature ?? '',
        values: Array.isArray(data.body?.values) ? data.body.values : data.body,
        raw: data
      })
    }
  }

  globalThis.addEventListener('data', handler)
  globalThis[listenerSymbol] = handler
}

installListener()

/**
 * Retrieves global DBus availability metadata.
 * @returns {Promise<{ available: boolean, reason?: string }>}
 */
export async function availability () {
  const result = ensureResult(
    await ipc.request('dbus.availability', {}),
    'dbus.availability'
  )
  return {
    available: result?.available !== false,
    reason: result?.reason
  }
}

/**
 * Establishes a DBus connection via the runtime.
 * @param {Record<string, any>} [options]
 * @returns {Promise<Connection>}
 */
export async function connect (options = {}) {
  const info = await availability()
  if (info && info.available === false) {
    const message =
      typeof info.reason === 'string' && info.reason.length
        ? info.reason
        : 'DBus support is not available on this platform'
    const error = new Error(message)
    error.code = 'ERR_NOT_SUPPORTED'
    throw error
  }

  const normalised = normaliseConnectOptions(options)
  const payload = {}

  if (normalised) {
    try {
      payload.options = JSON.stringify(normalised)
    } catch (err) {
      const error = new Error('Failed to serialise DBus options')
      error.cause = err
      throw error
    }
  }

  const result = ensureResult(
    await ipc.request('dbus.connect', payload),
    'dbus.connect'
  )
  const idValue = result?.id ?? result?.connectionId ?? result?.data?.id
  const id =
    typeof idValue === 'string'
      ? idValue
      : idValue != null
        ? String(idValue)
        : ''
  if (!id.length) {
    throw new Error('Failed to create DBus connection')
  }

  if (connections.has(id)) {
    return connections.get(id)
  }

  const connection = new Connection(id)
  connections.set(id, connection)
  return connection
}

/**
 * Closes every tracked DBus connection.
 * @returns {Promise<void>}
 */
export async function disconnectAll () {
  const active = Array.from(connections.values())
  connections.clear()
  await Promise.allSettled(
    active.map(async (connection) => {
      try {
        await connection.close()
      } catch {}
    })
  )
}

/**
 * Helper for constructing DBus variant payloads.
 * @param {string} signature
 * @param {any} value
 * @returns {VariantBody}
 */
export function variant (signature, value) {
  if (typeof signature !== 'string' || !signature.length) {
    throw new TypeError('signature must be a non-empty string')
  }
  return { signature, value }
}

/**
 * Helper for constructing DBus dictionary entries.
 * @param {any} key
 * @param {any} value
 * @returns {{ key: any, value: any }}
 */
export function dictEntry (key, value) {
  return { key, value }
}

export default exports
