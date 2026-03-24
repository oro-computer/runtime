/**
 * @module zlib
 *
 * Native zlib (v1.3.1) helpers exposed through the Oro runtime. This module
 * provides convenient buffer-based compression utilities as well as a
 * streaming API built on top of the runtime's zlib service.
 *
 * Examples:
 *
 * ```js
 * import { deflate, inflate } from 'oro:zlib'
 *
 * const compressed = await deflate(Buffer.from('hello'))
 * const plain = await inflate(compressed)
 * ```
 */

import ipc from './ipc.js'
import { Buffer } from './buffer.js'
import { rand64 } from './crypto.js'

/**
 * @typedef {'zlib'|'gzip'|'raw'} ZlibFormat
 */

/**
 * @typedef {'deflate'|'inflate'} ZlibMode
 */

/**
 * @typedef {object} ZlibOptions
 * @property {ZlibFormat} [format='zlib']
 * @property {number} [level] Compression level (0-9, zlib default when omitted)
 * @property {AbortSignal} [signal]
 * @property {number} [timeout]
 */

/**
 * @typedef {ZlibOptions & { mode?: ZlibMode }} ZlibStreamOptions
 */

/**
 * @typedef {object} ZlibChunkOptions
 * @property {boolean} [finish=false]
 * @property {AbortSignal} [signal]
 * @property {number} [timeout]
 */

const ROUTES = Object.freeze({
  capabilities: 'zlib.capabilities',
  deflate: 'zlib.deflate',
  inflate: 'zlib.inflate',
  streamOpen: 'zlib.stream.open',
  streamWrite: 'zlib.stream.write'
})

function toBuffer (input) {
  if (Buffer.isBuffer(input)) return input
  if (ArrayBuffer.isView(input)) {
    return Buffer.from(input.buffer, input.byteOffset, input.byteLength)
  }
  if (input instanceof ArrayBuffer) {
    return Buffer.from(input)
  }
  if (typeof input === 'string') {
    return Buffer.from(input)
  }
  throw new TypeError(
    'Expected Buffer, Uint8Array, ArrayBuffer, or string for zlib input'
  )
}

async function writeBinary (route, params, buffer, options = null) {
  const result = await ipc.write(route, params, buffer, {
    responseType: 'arraybuffer',
    timeout: options?.timeout,
    signal: options?.signal
  })

  if (result.err) {
    throw result.err
  }

  const contentType = result.headers?.get('content-type')
  if (contentType && contentType !== 'application/octet-stream') {
    throw new TypeError(
      `Invalid response content type from '${route}'. Received: ${contentType}`
    )
  }

  const data = result.data
  if (!data) {
    return Buffer.alloc(0)
  }

  if (ArrayBuffer.isView(data) || data instanceof ArrayBuffer) {
    return Buffer.from(data)
  }

  throw new TypeError(
    `Invalid binary response from '${route}'. Received: ${typeof data}`
  )
}

/**
 * Reports whether the zlib primitive is available in the current runtime
 * (based on native build configuration).
 * @return {Promise<boolean>}
 */
export async function isAvailable () {
  try {
    const result = await ipc.send(ROUTES.capabilities, {})
    if (result.err) {
      return false
    }
    return Boolean(result.data?.available)
  } catch {
    return false
  }
}

function normalizeFormat (format) {
  if (!format) return 'zlib'
  const value = String(format).toLowerCase()
  if (value === 'gzip' || value === 'raw' || value === 'zlib') {
    return value
  }
  throw new RangeError("format must be one of 'zlib', 'gzip', or 'raw'")
}

function normalizeLevel (level) {
  if (level == null) return undefined
  const value = Number(level)
  if (!Number.isFinite(value) || value < 0 || value > 9) {
    throw new RangeError('level must be an integer between 0 and 9')
  }
  return value
}

/**
 * Compresses a buffer using zlib/deflate.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export async function deflate (input, options = null) {
  const buffer = toBuffer(input)
  const params = {}

  params.format = normalizeFormat(options?.format)
  const level = normalizeLevel(options?.level)
  if (typeof level === 'number') {
    params.level = level
  }

  return await writeBinary(ROUTES.deflate, params, buffer, options)
}

/**
 * Decompresses a buffer using zlib/inflate.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export async function inflate (input, options = null) {
  const buffer = toBuffer(input)
  const params = {}

  params.format = normalizeFormat(options?.format)

  return await writeBinary(ROUTES.inflate, params, buffer, options)
}

/**
 * Compresses a buffer using gzip framing.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export async function gzip (input, options = null) {
  const opts = { ...options, format: 'gzip' }
  return deflate(input, opts)
}

/**
 * Decompresses a gzip buffer.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export async function gunzip (input, options = null) {
  const opts = { ...options, format: 'gzip' }
  return inflate(input, opts)
}

/**
 * Represents a stateful zlib stream backed by the native runtime.
 */
export class ZlibStream {
  /**
   * Opens a new zlib stream.
   * @param {ZlibMode} [mode='deflate']
   * @param {ZlibStreamOptions} [options]
   * @return {Promise<ZlibStream>}
   */
  static async create (mode = 'deflate', options = null) {
    const params = {}
    const normalizedMode = String(mode) === 'inflate' ? 'inflate' : 'deflate'

    params.mode = normalizedMode
    params.format = normalizeFormat(options?.format)

    const level = normalizeLevel(options?.level)
    if (typeof level === 'number') {
      params.level = level
    }

    const result = await ipc.send(ROUTES.streamOpen, params)
    if (result.err) {
      throw result.err
    }

    const data = result.data || {}
    const id = data.id ?? data.streamId
    if (!id) {
      throw new Error('zlib.stream.open did not return a stream id')
    }

    return new ZlibStream({
      id,
      mode: normalizedMode,
      format: params.format
    })
  }

  /**
   * @ignore
   * @param {{ id: string|number|bigint, mode: ZlibMode, format: ZlibFormat }} state
   */
  constructor (state) {
    this.id = String(state.id || rand64())
    this.mode = state.mode === 'inflate' ? 'inflate' : 'deflate'
    this.format = normalizeFormat(state.format)
    this.closed = false
  }

  /**
   * Writes a chunk into the stream and returns the processed output chunk.
   * @param {Buffer|Uint8Array|ArrayBuffer|string} chunk
   * @param {ZlibChunkOptions} [options]
   * @return {Promise<Buffer>}
   */
  async write (chunk, options = null) {
    if (this.closed) {
      throw new Error('ZlibStream is closed')
    }

    const buffer = toBuffer(chunk)
    const params = {
      id: this.id
    }

    if (options?.finish) {
      params.finish = true
      this.closed = true
    }

    return await writeBinary(ROUTES.streamWrite, params, buffer, options)
  }

  /**
   * Signals the end of the stream. An optional final chunk can be provided.
   * @param {Buffer|Uint8Array|ArrayBuffer|string} [chunk]
   * @param {ZlibChunkOptions} [options]
   * @return {Promise<Buffer>}
   */
  async end (chunk = null, options = null) {
    if (this.closed) {
      return Buffer.alloc(0)
    }

    const opts = { ...options, finish: true }
    const buffer = chunk != null ? toBuffer(chunk) : Buffer.alloc(0)
    return await this.write(buffer, opts)
  }

  /**
   * Closes the stream without sending additional data.
   * Subsequent writes will throw.
   */
  close () {
    this.closed = true
  }
}

/**
 * Convenience helper for creating a deflate stream.
 * @param {ZlibStreamOptions} [options]
 * @return {Promise<ZlibStream>}
 */
export function createDeflateStream (options = null) {
  return ZlibStream.create('deflate', options)
}

/**
 * Convenience helper for creating an inflate stream.
 * @param {ZlibStreamOptions} [options]
 * @return {Promise<ZlibStream>}
 */
export function createInflateStream (options = null) {
  return ZlibStream.create('inflate', options)
}

const api = Object.freeze({
  isAvailable,
  deflate,
  inflate,
  gzip,
  gunzip,
  ZlibStream,
  createDeflateStream,
  createInflateStream
})

export default api
