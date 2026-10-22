import ipc from './ipc.js'
import { Buffer } from './buffer.js'

/**
 * @module secure-storage
 */

/**
 * @typedef {object} SecureStorageOptions
 * @property {string|null} [scope] Origin string identifying the storage namespace.
 */

/**
 * @typedef {SecureStorageOptions & { encoding?: 'utf8' | 'base64' | 'hex' }} SetItemOptions
 */

/**
 * @typedef {SecureStorageOptions & { encoding?: 'utf8' | 'base64' | 'hex' | 'buffer' }} GetItemOptions
 */

const DEFAULT_SCOPE =
  typeof globalThis.location?.origin === 'string'
    ? globalThis.location.origin
    : null

/**
 * Normalises a user supplied scope.
 * @param {string|null|undefined} scope
 * @returns {string|null}
 */
function resolveScope (scope) {
  if (scope == null) return DEFAULT_SCOPE
  if (typeof scope !== 'string') throw new TypeError('scope must be a string')
  return scope.trim()
}

/**
 * Ensures the key is a non-empty string.
 * @param {string} key
 * @returns {string}
 */
function normalizeKey (key) {
  if (typeof key !== 'string' || key.trim().length === 0) {
    throw new TypeError('key must be a non-empty string')
  }
  return key
}

/**
 * Converts binary inputs into a Buffer instance.
 * @param {string|Uint8Array|ArrayBuffer|Buffer} value
 * @returns {Buffer}
 */
function toBuffer (value) {
  if (typeof value === 'string') return Buffer.from(value, 'utf8')
  if (Buffer.isBuffer(value)) return value
  if (value instanceof Uint8Array) return Buffer.from(value)
  if (value instanceof ArrayBuffer) return Buffer.from(new Uint8Array(value))
  throw new TypeError(
    'value must be a string, Buffer, Uint8Array, or ArrayBuffer'
  )
}

/**
 * Stores a value inside secure storage for the given key.
 * @param {string} key
 * @param {string|Uint8Array|ArrayBuffer|Buffer} value
 * @param {SetItemOptions} [options]
 * @returns {Promise<void>}
 */
export async function setItem (key, value, options = {}) {
  key = normalizeKey(key)
  const scope = resolveScope(options.scope)

  const params = { key }
  if (scope) params.scope = scope

  let bytes = null

  if (typeof value === 'string') {
    const encoding = options.encoding || 'utf8'
    if (!['utf8', 'base64', 'hex'].includes(encoding)) {
      throw new TypeError("encoding must be 'utf8', 'base64', or 'hex'")
    }
    params.value = value
    params.encoding = encoding
  } else {
    bytes = toBuffer(value)
  }

  const result = bytes
    ? await ipc.write('secureStorage.set', params, bytes)
    : await ipc.request('secureStorage.set', params)
  if (result.err) throw result.err
}

/**
 * Retrieves a previously stored value.
 * @param {string} key
 * @param {GetItemOptions} [options]
 * @returns {Promise<string|Uint8Array|null>}
 */
export async function getItem (key, options = {}) {
  key = normalizeKey(key)
  const scope = resolveScope(options.scope)
  const encoding = options.encoding || 'utf8'

  const params = { key }
  if (scope) params.scope = scope

  let nativeEncoding = encoding
  let returnBuffer = false

  if (encoding === 'buffer') {
    nativeEncoding = 'base64'
    returnBuffer = true
  } else if (!['utf8', 'base64', 'hex'].includes(encoding)) {
    throw new TypeError("encoding must be 'utf8', 'base64', 'hex', or 'buffer'")
  }

  params.encoding = nativeEncoding

  const result = await ipc.request('secureStorage.get', params)
  if (result.err) {
    if (result.err.type === 'NotFoundError' || result.err.name === 'NotFoundError' ||
        result.err.code === 'NOT_FOUND_ERR') return null
    throw result.err
  }

  const value = result.data?.value
  if (typeof value !== 'string') return null

  if (returnBuffer) {
    const buffer = Buffer.from(value, 'base64')
    return new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength)
  }

  return value
}

/**
 * Removes a single key from secure storage.
 * @param {string} key
 * @param {SecureStorageOptions} [options]
 * @returns {Promise<void>}
 */
export async function removeItem (key, options = {}) {
  key = normalizeKey(key)
  const scope = resolveScope(options.scope)

  const params = { key }
  if (scope) params.scope = scope

  const result = await ipc.request('secureStorage.remove', params)
  if (result.err) throw result.err
}

/**
 * Clears all keys for the provided scope (or default scope).
 * @param {SecureStorageOptions} [options]
 * @returns {Promise<void>}
 */
export async function clear (options = {}) {
  const scope = resolveScope(options.scope)
  const params = {}
  if (scope) params.scope = scope

  const result = await ipc.request('secureStorage.clear', params)
  if (result.err) throw result.err
}

/**
 * Lists the stored keys for the provided scope.
 * @param {SecureStorageOptions} [options]
 * @returns {Promise<string[]>}
 */
export async function keys (options = {}) {
  const scope = resolveScope(options.scope)
  const params = {}
  if (scope) params.scope = scope

  const result = await ipc.request('secureStorage.keys', params)
  if (result.err) throw result.err
  return Array.isArray(result.data?.keys) ? result.data.keys : []
}

/**
 * @typedef {object} SecureStorageStatus
 * @property {boolean} available Whether the Android Keystore metadata query succeeded.
 * @property {boolean} keyPresent Whether an existing runtime key was found.
 * @property {'unknown'|'not-created'|'key-missing'|'software'|'hardware'|'tee'|'strongbox'} protection
 * @property {boolean} requiresRecovery Encrypted values exist without their key.
 * @property {boolean|null} userAuthenticationRequired Existing key policy, or null when unknown.
 * @property {'not-probed'} readability No encrypted values were decrypted by this inspection.
 */

/**
 * Inspects existing Android Keystore protection without creating a key, exporting
 * key material, or decrypting application values. A present key does not prove
 * every stored value is readable. Other platforms reject with ENOSYS.
 * @returns {Promise<SecureStorageStatus>}
 */
export async function getStatus () {
  const { data, err } = await ipc.request('secureStorage.status')
  if (err) throw err
  return data
}

/**
 * @typedef {Object} SecureStorageModule
 * @property {typeof setItem} setItem
 * @property {typeof getItem} getItem
 * @property {typeof removeItem} removeItem
 * @property {typeof clear} clear
 * @property {typeof keys} keys
 * @property {typeof getStatus} getStatus
 */

/** @type {SecureStorageModule} */
const api = {
  setItem,
  getItem,
  removeItem,
  clear,
  keys,
  getStatus
}

export default api
