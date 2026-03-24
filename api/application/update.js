import { fetch as runtimeFetch } from '../fetch/index.js'
import { Buffer } from '../buffer.js'
import { sodium, ready as cryptoReady, createDigest } from '../crypto.js'
import { TarArchive } from '../tar.js'
import ipc from '../ipc.js'
import process from '../process.js'
import * as semver from '../semver.js'

/**
 * @module application/update
 *
 * High-level helpers for fetching and verifying signed application updates.
 *
 * The wire format and transport bindings are described in
 * `docs/APPLICATION_UPDATE_PROTOCOL.md`. This module focuses on the client
 * side: downloading manifests and artifacts, verifying signatures and hashes,
 * and selecting suitable updates for the current platform.
 */

/**
 * @typedef {object} UpdateTarget
 * @property {string} platform - Target platform identifier (for example, `darwin`, `win32`, `linux`).
 * @property {string} arch - Target CPU architecture (for example, `x64`, `arm64`).
 * @property {string} artifactUrl - Absolute or relative URL for the update payload.
 * @property {number} [length] - Expected payload length in bytes.
 * @property {string} hashAlgorithm - Hash algorithm identifier (for example, `sha256`).
 * @property {string} hash - Hex or base64url encoded hash of the payload.
 * @property {string} [signatureAlgorithm] - Optional artifact signature algorithm (for example, `ed25519`).
 * @property {string} [artifactSignature] - Optional encoded signature over the artifact bytes.
 * @property {string} [osVersionRange] - Optional OS version range constraint.
 */

/**
 * @typedef {object} UpdateDescriptor
 * @property {string} id - Update identifier, unique within the manifest.
 * @property {string} version - Application version string (semantic version recommended).
 * @property {string} [channel] - Distribution channel (for example, `stable`, `beta`).
 * @property {string} [minRuntimeVersion] - Minimum Oro runtime version required.
 * @property {boolean} [critical] - Whether this update is considered critical.
 * @property {string} [notesUrl] - Optional URL to human-readable release notes.
 * @property {UpdateTarget[]} targets - Platform-specific artifacts for this update.
 */

/**
 * @typedef {object} UpdateManifest
 * @property {number} schemaVersion - Manifest schema version.
 * @property {string} appId - Application identifier (for example, reverse DNS).
 * @property {string} [generatedAt] - ISO8601 timestamp when the manifest was generated.
 * @property {string[]} [channels] - Optional list of known channels.
 * @property {UpdateDescriptor[]} updates - List of available updates.
 */

/**
 * @typedef {object} ManifestSignature
 * @property {number} schemaVersion - Signature schema version.
 * @property {string} algorithm - Signature algorithm (for example, `ed25519`).
 * @property {string} [keyId] - Optional key identifier for bookkeeping.
 * @property {Uint8Array} signature - Raw signature bytes.
 * @property {string} [encoding] - Original textual encoding (`hex`, `base64`, or `base64url`).
 */

/**
 * @typedef {Uint8Array|ArrayBuffer|import('../buffer.js').Buffer|string} KeyLike
 */

/**
 * @typedef {object} ManifestFetchOptions
 * @property {string} manifestUrl - URL of the manifest JSON document.
 * @property {string} [signatureUrl] - URL of the manifest signature JSON; defaults to `manifestUrl + '.sig'`.
 * @property {KeyLike} [publicKey] - Public key used to verify the manifest signature.
 * @property {KeyLike[]} [publicKeys] - Optional list of public keys; the manifest is accepted if any key verifies.
 * @property {string} [expectedAppId] - Optional expected appId; if provided, the manifest's appId must match.
 * @property {typeof globalThis.fetch} [fetch] - Optional custom fetch implementation.
 * @property {AbortSignal} [signal] - Optional abort signal for network requests.
 * @property {Record<string, string>} [headers] - Optional additional HTTP headers for manifest/signature requests.
 * @property {number} [maxManifestBytes] - Optional maximum manifest size in bytes; manifests larger than this are rejected.
 */

/**
 * @typedef {object} UpdateSelectionOptions
 * @property {string} [channel] - Desired update channel; defaults to `"stable"`.
 * @property {string} [currentVersion] - Current application version.
 * @property {string} [platform] - Target platform identifier; defaults to the runtime platform when available.
 * @property {string} [arch] - Target architecture identifier; defaults to the runtime architecture when available.
 * @property {string} [runtimeVersion] - Current Oro runtime version; defaults to `process.versions.oro` when available.
 */

/**
 * @typedef {object} UpdateSelectionResult
 * @property {UpdateManifest} manifest - The validated manifest.
 * @property {UpdateDescriptor} update - The chosen update descriptor.
 * @property {UpdateTarget} target - The chosen platform-specific target.
 */

/**
 * @typedef {object} DownloadOptions
 * @property {typeof globalThis.fetch} [fetch] - Optional custom fetch implementation.
 * @property {AbortSignal} [signal] - Optional abort signal for the download request.
 * @property {number} [maxArtifactBytes] - Optional maximum artifact size in bytes; artifacts larger than this are rejected.
 */

/**
 * @typedef {ManifestFetchOptions & UpdateSelectionOptions & DownloadOptions & { download?: boolean }} UpdateCheckOptions
 */

/**
 * @typedef {object} UpdateCheckResult
 * @property {boolean} updateAvailable - Indicates whether an update is available.
 * @property {UpdateManifest} manifest - The validated manifest.
 * @property {ManifestSignature} signature - The validated manifest signature.
 * @property {UpdateDescriptor} [update] - The chosen update descriptor, when `updateAvailable` is `true`.
 * @property {UpdateTarget} [target] - The chosen platform-specific target, when `updateAvailable` is `true`.
 * @property {Uint8Array} [artifact] - The downloaded and verified artifact bytes when `download` is `true`.
 */

const DEFAULT_CHANNEL = 'stable'
const DEFAULT_HASH_ALGORITHM = 'SHA-256'

function ensureResult (result, source) {
  if (result?.err) {
    throw result.err
  }

  if (result?.source === source || result?.data !== undefined) {
    return result.data
  }

  return result
}

function shouldFallbackToFetch (err) {
  if (!err) return false
  const code = err.code
  return (
    code === 'UPDATE_DISABLED' ||
    code === 'ERR_SODIUM_UNAVAILABLE' ||
    code === 'ERR_SODIUM_INIT' ||
    code === 'ERR_HTTP_UNAVAILABLE' ||
    code === 'ERR_HTTPS_UNAVAILABLE' ||
    code === 'ERR_HASH_ALGORITHM'
  )
}

/**
 * Creates an Error with a stable code and optional extra properties.
 * @param {string} code
 * @param {string} message
 * @param {object} [props]
 * @returns {Error}
 */
function createError (code, message, props) {
  const err = new Error(message)
  err.code = code
  if (props && typeof props === 'object') {
    Object.assign(err, props)
  }
  return err
}

function getDefaultPlatform () {
  return process?.platform || null
}

function getDefaultArch () {
  return process?.arch || null
}

function getDefaultRuntimeVersion () {
  return (
    process?.versions?.oro ||
    process?.versions?.socket ||
    process?.version ||
    null
  )
}

/**
 * Normalises a key or signature input into a Uint8Array.
 * @param {KeyLike} value
 * @param {string} name
 * @returns {Uint8Array}
 */
function toUint8Array (value, name) {
  if (value instanceof Uint8Array) {
    return value
  }

  if (value instanceof ArrayBuffer) {
    return new Uint8Array(value)
  }

  if (
    value &&
    typeof value === 'object' &&
    typeof (/** @type {any} */ (value).buffer) === 'object' &&
    typeof (/** @type {any} */ (value).byteOffset) === 'number' &&
    typeof (/** @type {any} */ (value).byteLength) === 'number'
  ) {
    const view = /** @type {ArrayBufferView} */ (value)
    return new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
  }

  if (typeof value === 'string') {
    return decodeEncodedBytes(value)
  }

  throw new TypeError(`Expected ${name} to be bytes or string`)
}

/**
 * Decodes a hex or base64/base64url string into bytes.
 * @param {string} value
 * @returns {Uint8Array}
 */
function decodeEncodedBytes (value) {
  const trimmed = value.trim()

  if (/^[0-9a-fA-F]+$/.test(trimmed) && trimmed.length % 2 === 0) {
    const buf = Buffer.from(trimmed, 'hex')
    return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength)
  }

  let s = trimmed.replace(/-/g, '+').replace(/_/g, '/')
  while (s.length % 4 !== 0) s += '='

  const buf = Buffer.from(s, 'base64')
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength)
}

/**
 * Parses a manifest JSON document from bytes and performs minimal structural validation.
 * @param {Uint8Array} bytes
 * @returns {UpdateManifest}
 */
function parseManifest (bytes) {
  const text = Buffer.from(bytes).toString('utf8')
  let json

  try {
    json = JSON.parse(text)
  } catch (err) {
    throw createError('ERR_MANIFEST_JSON', 'Invalid manifest JSON', {
      cause: err
    })
  }

  if (!json || typeof json !== 'object') {
    throw createError('ERR_MANIFEST_JSON', 'Manifest JSON must be an object')
  }

  if (typeof json.schemaVersion !== 'number') {
    throw createError(
      'ERR_MANIFEST_SCHEMA',
      'Manifest is missing numeric schemaVersion'
    )
  }

  if (typeof json.appId !== 'string' || json.appId.length === 0) {
    throw createError(
      'ERR_MANIFEST_SCHEMA',
      'Manifest is missing non-empty appId'
    )
  }

  if (!Array.isArray(json.updates)) {
    throw createError(
      'ERR_MANIFEST_SCHEMA',
      'Manifest "updates" field must be an array'
    )
  }

  return /** @type {UpdateManifest} */ (json)
}

/**
 * Verifies a manifest signature using libsodium.
 * @param {Uint8Array} manifestBytes
 * @param {Uint8Array} signatureBytes
 * @param {Uint8Array} publicKeyBytes
 * @returns {Promise<boolean>} - true when the signature matches; false otherwise.
 */
async function verifyManifestBytes (
  manifestBytes,
  signatureBytes,
  publicKeyBytes
) {
  await cryptoReady

  if (typeof sodium.crypto_sign_verify_detached !== 'function') {
    throw createError(
      'ERR_SODIUM_SIGNATURE_API',
      'libsodium signature API unavailable'
    )
  }

  return sodium.crypto_sign_verify_detached(
    signatureBytes,
    manifestBytes,
    publicKeyBytes
  )
}

/**
 * Maps a manifest hash algorithm identifier to a WebCrypto algorithm name.
 * @param {string|undefined} algorithm
 * @returns {string}
 */
function normalizeHashAlgorithm (algorithm) {
  if (!algorithm) return DEFAULT_HASH_ALGORITHM

  const upper = algorithm.toUpperCase()

  if (upper === 'SHA256' || upper === 'SHA-256') {
    return 'SHA-256'
  }

  if (upper === 'SHA384' || upper === 'SHA-384') {
    return 'SHA-384'
  }

  if (upper === 'SHA512' || upper === 'SHA-512') {
    return 'SHA-512'
  }

  throw createError(
    'ERR_HASH_ALGORITHM',
    `Unsupported hash algorithm: ${algorithm}`
  )
}

/**
 * Performs a case-insensitive string equality check.
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
function equalsIgnoreCase (a, b) {
  return a.length === b.length && a.toLowerCase() === b.toLowerCase()
}

/**
 * Compares two semantic version strings.
 * @param {string} a
 * @param {string} b
 * @returns {number} -1 if a < b, 0 if a == b, 1 if a > b
 */
function compareSemVer (a, b) {
  try {
    return semver.compare(String(a ?? ''), String(b ?? ''))
  } catch {
    // Preserve legacy behaviour for non-SemVer strings by treating them
    // as equal so they neither win nor lose comparisons outright.
    return 0
  }
}

/**
 * Selects a suitable update for the given options.
 * @param {UpdateManifest} manifest
 * @param {UpdateSelectionOptions} [options]
 * @returns {UpdateSelectionResult|null}
 */
export function selectUpdate (manifest, options = {}) {
  const channel = options.channel || DEFAULT_CHANNEL
  const platform = options.platform || getDefaultPlatform()
  const arch = options.arch || getDefaultArch()
  const runtimeVersion = options.runtimeVersion || getDefaultRuntimeVersion()
  const currentVersion = options.currentVersion || null

  const candidates = []

  for (const update of manifest.updates || []) {
    if (!update || typeof update !== 'object') continue

    if (update.channel && update.channel !== channel) continue
    if (!Array.isArray(update.targets) || update.targets.length === 0) {
      continue
    }

    const matchingTargets = update.targets.filter((target) => {
      if (!target || typeof target !== 'object') return false

      if (platform && target.platform && target.platform !== platform) {
        return false
      }

      if (arch && target.arch && target.arch !== arch) {
        return false
      }

      return true
    })

    if (matchingTargets.length === 0) continue

    if (update.minRuntimeVersion && runtimeVersion) {
      if (compareSemVer(runtimeVersion, update.minRuntimeVersion) < 0) {
        continue
      }
    }

    if (currentVersion && update.version) {
      if (compareSemVer(update.version, currentVersion) <= 0) {
        continue
      }
    }

    candidates.push({ update, target: matchingTargets[0] })
  }

  if (candidates.length === 0) return null

  candidates.sort((a, b) => {
    const av = a.update.version || '0.0.0'
    const bv = b.update.version || '0.0.0'
    const cmp = compareSemVer(av, bv)

    if (cmp !== 0) return cmp

    const ac = a.update.critical === true
    const bc = b.update.critical === true
    if (ac && !bc) return 1
    if (!ac && bc) return -1

    return 0
  })

  const best = candidates[candidates.length - 1]
  return {
    manifest,
    update: best.update,
    target: best.target
  }
}

/**
 * Verifies an artifact payload against the hash declared in the target.
 * @param {Uint8Array|ArrayBuffer} payload
 * @param {UpdateTarget} target
 * @returns {Promise<void>}
 */
export async function verifyArtifact (payload, target) {
  const bytes =
    payload instanceof Uint8Array ? payload : new Uint8Array(payload)

  if (typeof target.length === 'number') {
    if (bytes.byteLength !== target.length) {
      throw createError(
        'ERR_ARTIFACT_LENGTH',
        `Artifact length mismatch (expected ${target.length}, got ${bytes.byteLength})`,
        { expected: target.length, actual: bytes.byteLength }
      )
    }
  }

  if (!target.hash || typeof target.hash !== 'string') {
    throw createError('ERR_ARTIFACT_HASH', 'Target is missing hash')
  }

  const algorithm = normalizeHashAlgorithm(target.hashAlgorithm)
  const digest = await createDigest(algorithm, bytes)
  const digestHex = Buffer.from(digest).toString('hex')

  if (!equalsIgnoreCase(digestHex, target.hash)) {
    throw createError('ERR_ARTIFACT_HASH', 'Artifact hash verification failed')
  }
}

/**
 * Downloads an artifact and verifies its hash using the JS fetch path.
 * This is kept as a fallback when the native update service is unavailable.
 * @param {UpdateTarget} target
 * @param {DownloadOptions} [options]
 * @returns {Promise<Uint8Array>}
 */
async function downloadUpdateViaFetch (target, options = {}) {
  const fetchImpl =
    options.fetch || runtimeFetch || /** @type {any} */ (globalThis.fetch)

  if (typeof fetchImpl !== 'function') {
    throw createError('ERR_FETCH_UNAVAILABLE', 'fetch API unavailable')
  }

  const url = target.artifactUrl || /** @type {any} */ (target).url

  if (typeof url !== 'string' || url.length === 0) {
    throw createError('ERR_ARTIFACT_URL', 'Target is missing artifactUrl')
  }

  let res
  try {
    res = await fetchImpl(url, { signal: options.signal })
  } catch (err) {
    throw createError('ERR_ARTIFACT_DOWNLOAD', 'Failed to download artifact', {
      cause: err
    })
  }

  if (!res || res.status < 200 || res.status >= 300) {
    throw createError(
      'ERR_ARTIFACT_DOWNLOAD',
      `Artifact download failed with status ${res?.status}`,
      { status: res?.status ?? null }
    )
  }

  if (
    typeof options.maxArtifactBytes === 'number' &&
    options.maxArtifactBytes > 0
  ) {
    const rawLength = res.headers?.get?.('content-length')
    if (rawLength != null) {
      const len = Number.parseInt(rawLength, 10)
      if (Number.isFinite(len) && len > options.maxArtifactBytes) {
        throw createError(
          'ERR_ARTIFACT_TOO_LARGE',
          `Artifact exceeds maxArtifactBytes limit (${len} > ${options.maxArtifactBytes})`,
          { length: len, maxArtifactBytes: options.maxArtifactBytes }
        )
      }
    }
  }

  const buf = new Uint8Array(await res.arrayBuffer())

  if (
    typeof options.maxArtifactBytes === 'number' &&
    options.maxArtifactBytes > 0 &&
    buf.byteLength > options.maxArtifactBytes
  ) {
    throw createError(
      'ERR_ARTIFACT_TOO_LARGE',
      `Artifact exceeds maxArtifactBytes limit (${buf.byteLength} > ${options.maxArtifactBytes})`,
      { length: buf.byteLength, maxArtifactBytes: options.maxArtifactBytes }
    )
  }
  await verifyArtifact(buf, target)

  return buf
}

/**
 * Opens a verified artifact as a tar archive using the native tar service.
 * This is a convenience helper that wraps the artifact bytes in a TarArchive
 * so callers can inspect and extract entries using the `oro:tar` API surface.
 *
 * @param {Uint8Array|ArrayBuffer|import('../buffer.js').Buffer} artifact
 * @returns {Promise<import('../tar.js').TarArchive>}
 */
export async function openArtifactArchive (artifact) {
  let bytes = artifact

  if (artifact instanceof ArrayBuffer) {
    bytes = new Uint8Array(artifact)
  }

  return await TarArchive.fromBuffer(bytes)
}

/**
 * Normalizes a manifest signature object returned by the native update
 * service into the ManifestSignature shape expected by callers.
 * @param {any} nativeSignature
 * @returns {ManifestSignature}
 */
function normalizeManifestSignatureFromNative (nativeSignature) {
  if (!nativeSignature || typeof nativeSignature !== 'object') {
    throw createError(
      'ERR_SIGNATURE_JSON',
      'Invalid manifest signature from native update service'
    )
  }

  const encoding =
    typeof nativeSignature.encoding === 'string'
      ? nativeSignature.encoding
      : 'hex'

  const encoded = String(nativeSignature.signature || '')
  const signatureBytes = decodeEncodedBytes(encoded)

  return {
    schemaVersion:
      typeof nativeSignature.schemaVersion === 'number'
        ? nativeSignature.schemaVersion
        : 1,
    algorithm: String(nativeSignature.algorithm || 'ed25519'),
    keyId:
      typeof nativeSignature.keyId === 'string'
        ? nativeSignature.keyId
        : undefined,
    signature: signatureBytes,
    encoding
  }
}

/**
 * Downloads an artifact via the native update service, which performs
 * hash verification and size checks in the runtime.
 * @param {UpdateTarget} target
 * @param {DownloadOptions} [options]
 * @returns {Promise<Uint8Array>}
 */
async function downloadUpdateNative (target, options = {}) {
  const url = target.artifactUrl || /** @type {any} */ (target).url

  if (typeof url !== 'string' || url.length === 0) {
    throw createError('ERR_ARTIFACT_URL', 'Target is missing artifactUrl')
  }

  const params = {
    artifactUrl: url,
    hashAlgorithm: target.hashAlgorithm || DEFAULT_HASH_ALGORITHM,
    hash: target.hash
  }

  if (typeof target.length === 'number' && Number.isFinite(target.length)) {
    params.length = String(target.length)
  }

  if (
    typeof options.maxArtifactBytes === 'number' &&
    options.maxArtifactBytes > 0
  ) {
    params.maxArtifactBytes = String(options.maxArtifactBytes)
  }

  const result = await ipc.request('application.update.download', params, {
    responseType: 'arraybuffer',
    signal: options.signal
  })

  if (result.err) {
    throw result.err
  }

  const buf = result.data
  if (!buf) return new Uint8Array(0)

  if (buf instanceof ArrayBuffer) {
    return new Uint8Array(buf)
  }

  if (ArrayBuffer.isView(buf)) {
    return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength)
  }

  return new Uint8Array(Buffer.from(buf))
}

/**
 * Downloads an artifact and verifies its hash.
 * Prefers the native update service and falls back to the JS fetch-based
 * implementation when the service is not available in this build.
 * @param {UpdateTarget} target
 * @param {DownloadOptions} [options]
 * @returns {Promise<Uint8Array>}
 */
export async function downloadUpdate (target, options = {}) {
  try {
    return await downloadUpdateNative(target, options)
  } catch (err) {
    if (shouldFallbackToFetch(err)) {
      return await downloadUpdateViaFetch(target, options)
    }
    throw err
  }
}

/**
 * Fetches and verifies a manifest + signature pair.
 * @param {ManifestFetchOptions} options
 * @returns {Promise<{ manifest: UpdateManifest, raw: Uint8Array, signature: ManifestSignature }>}
 */
export async function fetchManifest (options) {
  const manifestUrl = options?.manifestUrl
  const signatureUrl = options?.signatureUrl || `${manifestUrl}.sig`

  if (typeof manifestUrl !== 'string' || manifestUrl.length === 0) {
    throw createError('ERR_MANIFEST_URL_REQUIRED', 'manifestUrl is required')
  }

  const hasPublicKey =
    options &&
    (options.publicKey ||
      (Array.isArray(options.publicKeys) && options.publicKeys.length > 0))

  if (!hasPublicKey) {
    throw createError(
      'ERR_PUBLIC_KEY_REQUIRED',
      'publicKey or publicKeys is required'
    )
  }

  const fetchImpl =
    options.fetch || runtimeFetch || /** @type {any} */ (globalThis.fetch)

  if (typeof fetchImpl !== 'function') {
    throw createError('ERR_FETCH_UNAVAILABLE', 'fetch API unavailable')
  }

  const headers = options.headers || {}

  let manifestRes
  let signatureRes

  try {
    ;[manifestRes, signatureRes] = await Promise.all([
      fetchImpl(manifestUrl, { signal: options.signal, headers }),
      fetchImpl(signatureUrl, { signal: options.signal, headers })
    ])
  } catch (err) {
    throw createError(
      'ERR_MANIFEST_DOWNLOAD',
      'Failed to download manifest or signature',
      { cause: err }
    )
  }

  if (!manifestRes || manifestRes.status < 200 || manifestRes.status >= 300) {
    throw createError(
      'ERR_MANIFEST_DOWNLOAD',
      `Manifest download failed with status ${manifestRes?.status}`,
      { status: manifestRes?.status ?? null }
    )
  }

  if (
    !signatureRes ||
    signatureRes.status < 200 ||
    signatureRes.status >= 300
  ) {
    throw createError(
      'ERR_SIGNATURE_DOWNLOAD',
      `Manifest signature download failed with status ${signatureRes?.status}`,
      { status: signatureRes?.status ?? null }
    )
  }

  if (
    typeof options.maxManifestBytes === 'number' &&
    options.maxManifestBytes > 0
  ) {
    const rawLength = manifestRes.headers?.get?.('content-length')
    if (rawLength != null) {
      const len = Number.parseInt(rawLength, 10)
      if (Number.isFinite(len) && len > options.maxManifestBytes) {
        throw createError(
          'ERR_MANIFEST_TOO_LARGE',
          `Manifest exceeds maxManifestBytes limit (${len} > ${options.maxManifestBytes})`,
          { length: len, maxManifestBytes: options.maxManifestBytes }
        )
      }
    }
  }

  const manifestBytes = new Uint8Array(await manifestRes.arrayBuffer())

  if (
    typeof options.maxManifestBytes === 'number' &&
    options.maxManifestBytes > 0 &&
    manifestBytes.byteLength > options.maxManifestBytes
  ) {
    throw createError(
      'ERR_MANIFEST_TOO_LARGE',
      `Manifest exceeds maxManifestBytes limit (${manifestBytes.byteLength} > ${options.maxManifestBytes})`,
      {
        length: manifestBytes.byteLength,
        maxManifestBytes: options.maxManifestBytes
      }
    )
  }
  const signatureText = await signatureRes.text()

  let signatureJson

  try {
    signatureJson = JSON.parse(signatureText)
  } catch (err) {
    throw createError('ERR_SIGNATURE_JSON', 'Invalid manifest signature JSON', {
      cause: err
    })
  }

  if (
    !signatureJson ||
    typeof signatureJson !== 'object' ||
    typeof signatureJson.signature !== 'string'
  ) {
    throw createError(
      'ERR_SIGNATURE_JSON',
      'Manifest signature JSON missing "signature" field'
    )
  }

  const algorithmRaw =
    typeof signatureJson.algorithm === 'string'
      ? signatureJson.algorithm
      : 'ed25519'
  const algorithm = algorithmRaw.toLowerCase()

  if (algorithm !== 'ed25519') {
    throw createError(
      'ERR_UNSUPPORTED_SIGNATURE_ALGORITHM',
      `Unsupported manifest signature algorithm: ${signatureJson.algorithm}`
    )
  }

  const signatureBytes = decodeEncodedBytes(signatureJson.signature)

  /** @type {Uint8Array[]} */
  const publicKeys = []

  if (Array.isArray(options.publicKeys)) {
    for (const key of options.publicKeys) {
      publicKeys.push(toUint8Array(key, 'publicKeys[]'))
    }
  }

  if (options.publicKey && publicKeys.length === 0) {
    publicKeys.push(toUint8Array(options.publicKey, 'publicKey'))
  }

  let verified = false

  for (const publicKeyBytes of publicKeys) {
    const ok = await verifyManifestBytes(
      manifestBytes,
      signatureBytes,
      publicKeyBytes
    )
    if (ok) {
      verified = true
      break
    }
  }

  if (!verified) {
    throw createError(
      'ERR_SIGNATURE_VERIFICATION',
      'Manifest signature verification failed'
    )
  }

  const manifest = parseManifest(manifestBytes)

  if (manifest.schemaVersion !== 1) {
    throw createError(
      'ERR_MANIFEST_SCHEMA',
      `Unsupported manifest schemaVersion: ${manifest.schemaVersion}`
    )
  }

  if (
    typeof options.expectedAppId === 'string' &&
    options.expectedAppId.length > 0 &&
    manifest.appId !== options.expectedAppId
  ) {
    throw createError(
      'ERR_APP_ID_MISMATCH',
      `Manifest appId mismatch (expected "${options.expectedAppId}", got "${manifest.appId}")`,
      { expectedAppId: options.expectedAppId, appId: manifest.appId }
    )
  }

  /** @type {ManifestSignature} */
  const signature = {
    schemaVersion:
      typeof signatureJson.schemaVersion === 'number'
        ? signatureJson.schemaVersion
        : 1,
    algorithm: String(signatureJson.algorithm || 'ed25519'),
    keyId:
      typeof signatureJson.keyId === 'string' ? signatureJson.keyId : undefined,
    signature: signatureBytes,
    encoding:
      typeof signatureJson.encoding === 'string'
        ? signatureJson.encoding
        : undefined
  }

  return { manifest, raw: manifestBytes, signature }
}

/**
 * High-level helper: fetches & verifies the manifest, selects an update,
 * and optionally downloads the artifact using the JS fetch path.
 * This is kept as a fallback when the native update service is unavailable.
 * @param {UpdateCheckOptions} options
 * @returns {Promise<UpdateCheckResult>}
 */
async function checkForUpdatesViaFetch (options) {
  const { manifest, signature } = await fetchManifest(options)
  const selection = selectUpdate(manifest, options)

  if (!selection) {
    return {
      updateAvailable: false,
      manifest,
      signature
    }
  }

  const result = {
    updateAvailable: true,
    manifest,
    signature,
    update: selection.update,
    target: selection.target,
    artifact: undefined
  }

  if (options.download) {
    result.artifact = await downloadUpdateViaFetch(selection.target, options)
  }

  return result
}

/**
 * High-level helper: delegates update checks to the native update service
 * when available. The service performs manifest fetch, signature
 * verification, and update selection in the runtime.
 * @param {UpdateCheckOptions} options
 * @returns {Promise<UpdateCheckResult>}
 */
async function checkForUpdatesNative (options) {
  const manifestUrl = options?.manifestUrl

  if (typeof manifestUrl !== 'string' || manifestUrl.length === 0) {
    throw createError('ERR_MANIFEST_URL_REQUIRED', 'manifestUrl is required')
  }

  const publicKeys = []

  if (Array.isArray(options.publicKeys)) {
    for (const key of options.publicKeys) {
      publicKeys.push(toUint8Array(key, 'publicKeys[]'))
    }
  }

  if (options.publicKey && publicKeys.length === 0) {
    publicKeys.push(toUint8Array(options.publicKey, 'publicKey'))
  }

  if (publicKeys.length === 0) {
    throw createError(
      'ERR_PUBLIC_KEY_REQUIRED',
      'publicKey or publicKeys is required'
    )
  }

  /** @type {Record<string, any>} */
  const params = {
    manifestUrl,
    signatureUrl: options.signatureUrl,
    expectedAppId: options.expectedAppId,
    channel: options.channel || DEFAULT_CHANNEL,
    currentVersion: options.currentVersion,
    platform: options.platform || getDefaultPlatform(),
    arch: options.arch || getDefaultArch(),
    runtimeVersion: options.runtimeVersion || getDefaultRuntimeVersion()
  }

  if (
    typeof options.maxManifestBytes === 'number' &&
    options.maxManifestBytes > 0
  ) {
    params.maxManifestBytes = String(options.maxManifestBytes)
  }

  if (
    typeof options.maxArtifactBytes === 'number' &&
    options.maxArtifactBytes > 0
  ) {
    params.maxArtifactBytes = String(options.maxArtifactBytes)
  }

  if (options.headers && typeof options.headers === 'object') {
    params.headers = JSON.stringify(options.headers)
  }

  if (publicKeys.length === 1) {
    params.publicKey = Buffer.from(publicKeys[0]).toString('hex')
  } else if (publicKeys.length > 1) {
    params.publicKeys = JSON.stringify(
      publicKeys.map((pk) => Buffer.from(pk).toString('hex'))
    )
  }

  const result = await ipc.send('application.update.check', params)
  const data = ensureResult(result, 'application.update.check')

  const manifest = /** @type {UpdateManifest} */ (data.manifest)
  const signature = normalizeManifestSignatureFromNative(data.signature)

  if (!data.updateAvailable) {
    return {
      updateAvailable: false,
      manifest,
      signature
    }
  }

  const update = /** @type {UpdateDescriptor} */ (data.update)
  const target = /** @type {UpdateTarget} */ (data.target)

  const out = {
    updateAvailable: true,
    manifest,
    signature,
    update,
    target,
    artifact: undefined
  }

  if (options.download) {
    out.artifact = await downloadUpdateNative(target, options)
  }

  return out
}

/**
 * High-level helper: fetches & verifies the manifest, selects an update,
 * and optionally downloads the artifact. Prefers the native update(service)
 * when available and falls back to the JS implementation otherwise.
 * @param {UpdateCheckOptions} options
 * @returns {Promise<UpdateCheckResult>}
 */
export async function checkForUpdates (options) {
  try {
    return await checkForUpdatesNative(options)
  } catch (err) {
    if (shouldFallbackToFetch(err)) {
      return await checkForUpdatesViaFetch(options)
    }
    throw err
  }
}

/**
 * @typedef {object} UpdateModule
 * @property {typeof selectUpdate} selectUpdate
 * @property {typeof verifyArtifact} verifyArtifact
 * @property {typeof downloadUpdate} downloadUpdate
 * @property {typeof fetchManifest} fetchManifest
 * @property {typeof checkForUpdates} checkForUpdates
 */

/** @type {UpdateModule} */
const api = {
  selectUpdate,
  verifyArtifact,
  downloadUpdate,
  fetchManifest,
  checkForUpdates
}

export default api
