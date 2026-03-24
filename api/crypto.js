/* eslint-disable no-fallthrough */
/**
 * @module crypto
 *
 * Some high-level methods around the `crypto.subtle` API for getting
 * random bytes and hashing.
 *
 * Example usage:
 * ```js
 * import { randomBytes } from 'oro:crypto'
 * ```
 */

import { toBuffer } from './util.js'
import { Buffer } from './buffer.js'

import * as exports from './crypto.js'

const MURMUR3_C1 = 0xcc9e2d51
const MURMUR3_C2 = 0x1b873593
const MURMUR3_R1 = 15
const MURMUR3_R2 = 13
const MURMUR3_M = 5
const MURMUR3_N = 0xe6546b64

function rotl32 (value, bits) {
  return (value << bits) | (value >>> (32 - bits))
}

function mixHash (hash, k) {
  hash ^= k
  hash = rotl32(hash, MURMUR3_R2)
  hash = (Math.imul(hash, MURMUR3_M) + MURMUR3_N) | 0
  return hash
}

function finalizeHash (hash, length) {
  hash ^= length
  hash ^= hash >>> 16
  hash = Math.imul(hash, 0x85ebca6b)
  hash ^= hash >>> 13
  hash = Math.imul(hash, 0xc2b2ae35)
  hash ^= hash >>> 16
  return hash >>> 0
}

function murmur3StringImpl (string, seed) {
  let hash = seed
  const length = string.length
  const remainder = length & 3
  const bodyLength = length - remainder

  for (let i = 0; i < bodyLength; i += 4) {
    let k =
      (string.charCodeAt(i) & 0xff) |
      ((string.charCodeAt(i + 1) & 0xff) << 8) |
      ((string.charCodeAt(i + 2) & 0xff) << 16) |
      ((string.charCodeAt(i + 3) & 0xff) << 24)

    k = (k * MURMUR3_C1) & 0xffffffff
    k = (k << MURMUR3_R1) | (k >>> (32 - MURMUR3_R1))
    k = (k * MURMUR3_C2) & 0xffffffff

    hash ^= k
    hash =
      ((hash << MURMUR3_R2) | (hash >>> (32 - MURMUR3_R2))) * MURMUR3_M +
      MURMUR3_N
    hash = hash & 0xffffffff
  }

  let k1 = 0

  switch (remainder) {
    case 3:
      k1 ^= (string.charCodeAt(bodyLength + 2) & 0xff) << 16
    case 2:
      k1 ^= (string.charCodeAt(bodyLength + 1) & 0xff) << 8
    case 1:
      k1 ^= string.charCodeAt(bodyLength) & 0xff
      k1 = (k1 * MURMUR3_C1) & 0xffffffff
      k1 = (k1 << MURMUR3_R1) | (k1 >>> (32 - MURMUR3_R1))
      k1 = (k1 * MURMUR3_C2) & 0xffffffff
      hash ^= k1
  }

  hash ^= bodyLength
  hash ^= hash >>> 16
  hash = (hash * 0x85ebca6b) & 0xffffffff
  hash ^= hash >>> 13
  hash = (hash * 0xc2b2ae35) & 0xffffffff
  hash ^= hash >>> 16

  return hash >>> 0
}

function murmur3BytesImpl (bytes, seed) {
  let hash = seed | 0
  const length = bytes.byteLength
  const bodyLength = length & ~3

  for (let i = 0; i < bodyLength; i += 4) {
    let k =
      (bytes[i] |
        (bytes[i + 1] << 8) |
        (bytes[i + 2] << 16) |
        (bytes[i + 3] << 24)) >>>
      0

    k = Math.imul(k, MURMUR3_C1)
    k = rotl32(k, MURMUR3_R1)
    k = Math.imul(k, MURMUR3_C2)
    hash = mixHash(hash, k)
  }

  let k1 = 0

  switch (length & 3) {
    case 3:
      k1 ^= bytes[bodyLength + 2] << 16
    case 2:
      k1 ^= bytes[bodyLength + 1] << 8
    case 1:
      k1 ^= bytes[bodyLength]
      k1 = Math.imul(k1, MURMUR3_C1)
      k1 = rotl32(k1, MURMUR3_R1)
      k1 = Math.imul(k1, MURMUR3_C2)
      hash ^= k1
  }

  return finalizeHash(hash, length)
}

/**
 * @typedef {Uint8Array|Int8Array} TypedArray
 */

/**
 * WebCrypto API
 * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Crypto}
 */
export let webcrypto = globalThis.crypto?.webcrypto ?? globalThis.crypto

const pending = []

if (globalThis?.process?.versions?.node) {
  pending.push(
    import('node:crypto').then((module) => {
      webcrypto = module.webcrypto
    })
  )
}

const sodium = {
  ready: new Promise((resolve, reject) => {
    import('./crypto/sodium.js')
      .then((module) => module.default.libsodium)
      .then((libsodium) => libsodium.ready.then(() => libsodium))
      .then((libsodium) => Object.assign(sodium, libsodium))
      .then(resolve, reject)
  })
}

pending.push(sodium.ready)

/**
 * A promise that resolves when all internals to be loaded/ready.
 * @type {Promise}
 */
export const ready = Promise.all(pending)

/**
 * libsodium API
 * @see {@link https://doc.libsodium.org/}
 * @see {@link https://github.com/jedisct1/libsodium.js}
 */
export { sodium }

/**
 * Generate cryptographically strong random values into the `buffer`
 * @param {TypedArray} buffer
 * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Crypto/getRandomValues}
 * @return {TypedArray}
 */
export function getRandomValues (buffer, ...args) {
  if (!ArrayBuffer.isView(buffer)) {
    throw new TypeError('Expected buffer to be an instance of ArrayBufferView')
  }

  if (buffer.byteLength === 0) {
    return buffer
  }

  if (typeof webcrypto?.getRandomValues === 'function') {
    return webcrypto.getRandomValues(buffer, ...args)
  }

  if (typeof sodium.randombytes_buf === 'function') {
    const view =
      buffer instanceof Uint8Array &&
      buffer.byteOffset === 0 &&
      buffer.byteLength === buffer.buffer.byteLength
        ? buffer
        : new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength)

    view.set(sodium.randombytes_buf(buffer.byteLength))
    return buffer
  }

  throw new Error(
    'crypto.getRandomValues() unavailable: no entropy source is ready'
  )
}

// so this is re-used instead of creating new one each rand64() call
const tmp = new Uint32Array(2)

/**
 * Generate a random 64-bit number.
 * @returns {BigInt} - A random 64-bit number.
 */
export function rand64 () {
  getRandomValues(tmp)
  return (BigInt(tmp[0]) << 32n) | BigInt(tmp[1])
}

/**
 * Maximum total size of random bytes per page
 */
export const RANDOM_BYTES_QUOTA = 64 * 1024

/**
 * Maximum total size for random bytes.
 */
export const MAX_RANDOM_BYTES = 0xffff_ffff_ffff

/**
 * Maximum total amount of allocated per page of bytes (max/quota)
 */
export const MAX_RANDOM_BYTES_PAGES = MAX_RANDOM_BYTES / RANDOM_BYTES_QUOTA
// note: should it do Math.ceil() / Math.round()?

/**
 * Generate `size` random bytes.
 * @param {number} size - The number of bytes to generate. The size must not be larger than 2**31 - 1.
 * @returns {Buffer} - A `Buffer` containing random bytes.
 */
export function randomBytes (size) {
  const buffers = []

  if (size < 0 || size >= MAX_RANDOM_BYTES || !Number.isInteger(size)) {
    throw Object.assign(
      new RangeError(
        `The value of "size" is out of range. It must be >= 0 && <= ${MAX_RANDOM_BYTES}. ` +
          `Received ${size}`
      ),
      {
        code: 'ERR_OUT_OF_RANGE'
      }
    )
  }

  do {
    const length = size > RANDOM_BYTES_QUOTA ? RANDOM_BYTES_QUOTA : size
    const bytes = getRandomValues(new Int8Array(length))
    buffers.push(toBuffer(bytes))
    size = Math.max(0, size - RANDOM_BYTES_QUOTA)
  } while (size > 0)

  return Buffer.concat(buffers)
}

/**
 * @param {string} algorithm - `SHA-1` | `SHA-256` | `SHA-384` | `SHA-512`
 * @param {Buffer | TypedArray | DataView} message - A `Buffer`, TypedArray, or DataView.
 * @returns {Promise<Buffer>} - A promise that resolves to a `Buffer` containing the digest.
 */
export async function createDigest (algorithm, buf) {
  return Buffer.from(await webcrypto.subtle.digest(algorithm, buf))
}

/**
 * A murmur3 hash implementation based on https://github.com/jwerle/murmurhash.c
 * that works on strings and `ArrayBuffer` views (typed arrays)
 * @param {string|Uint8Array|ArrayBuffer} value
 * @param {number=} [seed = 0]
 * @return {number}
 */
export function murmur3 (value, seed = 0) {
  if (typeof value === 'string') {
    return murmur3StringImpl(value, seed)
  }

  if (value instanceof ArrayBuffer) {
    return murmur3BytesImpl(new Uint8Array(value), seed)
  }

  if (ArrayBuffer.isView(value)) {
    return murmur3BytesImpl(
      new Uint8Array(value.buffer, value.byteOffset, value.byteLength),
      seed
    )
  }

  throw new TypeError(
    'Expecting input to be a string, ArrayBuffer, or TypedArray'
  )
}

export default exports
