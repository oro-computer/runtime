/**
 * @module authentication
 * Android 11+ authentication-gated Keystore operations. Enable
 * `[permissions] allow_biometrics = true`. User authentication authorizes one
 * cryptographic operation; account authority must still be verified by a server.
 */
import { Buffer } from './buffer.js'
import request from './internal/platform-request.js'

/**
 * @typedef {'biometric'|'biometric-or-device'} AuthenticationPolicy
 */
/**
 * @typedef {object} AuthenticationOptions
 * @property {AuthenticationPolicy} [policy='biometric'] Strong biometric, optionally device credential fallback. The fallback policy does not invalidate on biometric enrollment changes.
 * @property {AbortSignal} [signal] Aborting, navigation, renderer loss or backgrounding cancels the prompt.
 */
/**
 * @typedef {AuthenticationOptions & { id: string }} ProtectedKeyOptions
 */
/**
 * @typedef {ProtectedKeyOptions & { kind?: 'secret'|'signing' }} ProtectedKeyRemovalOptions
 */
/**
 * @typedef {object} ProtectedSecret
 * @property {number} schema
 * @property {string} id
 * @property {AuthenticationPolicy} policy
 * @property {string} iv Base64 AES-GCM IV.
 * @property {string} ciphertext Base64 ciphertext and authentication tag. Store this sealed record, not plaintext.
 */
/**
 * @typedef {object} AuthenticationStatus
 * @property {boolean} supported Android API 30+.
 * @property {boolean} enabled Explicit capability and manifest permission.
 * @property {boolean} canAuthenticate Current enrollment/hardware/policy result, not authenticated identity.
 * @property {number} statusCode Native BiometricManager result or -1 when unavailable.
 * @property {AuthenticationPolicy} policy
 * @property {boolean} cryptographicOperationsOnly
 * @property {boolean} serverVerificationRequired
 * @property {boolean} busy At most one native prompt per process.
 */
/**
 * @typedef {ProtectedKeyOptions & { authority: string, accountRef: string, challenge: string, expiresAt: number }} ChallengeOptions
 * `authority` is an exact HTTPS origin, including a self-hosted IP origin when
 * properly secured. `accountRef` is an opaque account reference. `challenge` is
 * canonical Base64 of 16..1024 server-generated random bytes; `expiresAt` is a
 * server challenge expiry in Unix milliseconds, at most five minutes ahead.
 */
/**
 * @typedef {object} ChallengeSignature
 * @property {'ECDSA_P256_SHA256'} algorithm
 * @property {string} publicKey Base64 SubjectPublicKeyInfo. Enrollment must be authenticated by the authority.
 * @property {string} payload Base64 exact UTF-8 JSON bytes signed, binding application, authority, account, key, nonce and expiry.
 * @property {string} signature Base64 ASN.1 DER ECDSA signature. The server must verify it, nonce single use, current account/key authorization and expiry.
 */

function parameters (options) {
  if (!options || typeof options.id !== 'string' || !/^[a-zA-Z0-9_-]{1,128}$/.test(options.id)) {
    throw new TypeError('An opaque key id of 1..128 letters, digits, underscores or hyphens is required')
  }
  const policy = options.policy ?? 'biometric'
  if (!['biometric', 'biometric-or-device'].includes(policy)) throw new TypeError('Unsupported authentication policy')
  return { id: options.id, policy }
}

/**
 * Reads policy/enrollment availability without prompting or creating keys.
 * Other platforms reject with ENOSYS.
 * @param {AuthenticationOptions} [options]
 * @returns {Promise<AuthenticationStatus>}
 */
export async function getStatus (options = {}) {
  return request('authentication', 'status', { policy: options.policy ?? 'biometric' }, options.signal)
}

/**
 * Encrypts at most 64 KiB after a native cryptographic authentication prompt.
 * At most 64 protected keys are retained. Existing keys are never silently
 * replaced after enrollment changes, removal or invalidation. Keep the returned
 * sealed record and handle EKEYREVOKED/ENOENT through explicit app recovery.
 * @param {string|Uint8Array|ArrayBuffer} value
 * @param {ProtectedKeyOptions} options
 * @returns {Promise<ProtectedSecret>}
 */
export async function seal (value, options) {
  const bytes = Buffer.from(value)
  if (bytes.length > 65536) throw new RangeError('Protected values are limited to 64 KiB')
  return request('authentication', 'seal', { ...parameters(options), value: bytes.toString('base64') }, options.signal)
}

/**
 * Decrypts only through a user-authenticated native Cipher operation. Returns
 * bytes; decoding or restoring an application session remains app-owned.
 * @param {ProtectedSecret} sealed
 * @param {ProtectedKeyOptions} options
 * @returns {Promise<Uint8Array>}
 */
export async function open (sealed, options) {
  const data = await request('authentication', 'open', { ...parameters(options), sealed }, options.signal)
  return Buffer.from(data.value, 'base64')
}

/**
 * Creates or reuses a non-exportable P-256 key and signs an enrollment challenge
 * after cryptographic authentication. This does not enroll the key on a server.
 * @param {ChallengeOptions} options
 * @returns {Promise<ChallengeSignature>}
 */
export async function createSigningKey (options) {
  return challenge('createSigningKey', options)
}

/**
 * Signs a fresh authority/account-bound server challenge using an existing
 * authentication-gated key. No success boolean establishes server authority.
 * @param {ChallengeOptions} options
 * @returns {Promise<ChallengeSignature>}
 */
export async function signChallenge (options) {
  return challenge('signChallenge', options)
}

async function challenge (action, options) {
  const { authority, accountRef, challenge, expiresAt } = options
  return request('authentication', action, {
    ...parameters(options), authority, accountRef, challenge, expiresAt
  }, options.signal)
}

/**
 * Explicitly removes this app's protected secret or signing key. Ciphertext and
 * server enrollment become unusable; revoke enrolled keys on the server too.
 * @param {ProtectedKeyRemovalOptions} options
 * @returns {Promise<void>}
 */
export async function removeKey (options) {
  await request('authentication', 'removeKey', { ...parameters(options), kind: options.kind ?? 'secret' }, options.signal)
}

export default { getStatus, seal, open, createSigningKey, signChallenge, removeKey }
