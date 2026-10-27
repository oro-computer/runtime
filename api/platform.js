/**
 * @module platform
 * Inspects platform backend availability and calls explicitly
 * configured compiled Android and Linux extensions through document-owned IPC. No
 * JavaScript module or executable path is loaded natively.
 * Compiled extension request backends for iOS/iPadOS, macOS and Windows are not
 * implemented and reject ENOSYS. `getSupport(feature)` returns the native inventory's
 * missing/restricted reason; an implemented descriptor is not OS/hardware or
 * deployment qualification. Shared encrypted-record support is a separate API.
 */
import platformRequest from './internal/platform-request.js'
import ipc from './ipc.js'

/**
 * @typedef {object} PlatformRequestOptions
 * @property {AbortSignal} [signal] Abort native work and discard a late result.
 */

/**
 * Invokes a narrowly named native operation with a JSON object payload. Requests
 * require a foreground interactive document, expire after 90 seconds and share
 * native capacity limits. Results are limited to 512 KiB. The extension must
 * implement its own feature authorization; trusted origin is not server IAM.
 * Unregistered extensions and unimplemented platforms reject with ENOSYS.
 * @param {string} extension Configured extension name.
 * @param {string} action Operation name.
 * @param {object} [payload]
 * @param {PlatformRequestOptions} [options]
 * @returns {Promise<any>}
 */
export async function request (extension, action, payload = {}, options = {}) {
  if (typeof extension !== 'string' || !/^[a-z][a-z0-9_.-]{0,63}$/.test(extension) ||
      typeof action !== 'string' || !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(action) ||
      !payload || typeof payload !== 'object' || Array.isArray(payload)) {
    throw new TypeError('Invalid native extension operation')
  }
  return platformRequest('extension', action, { extension, payload }, options.signal)
}

/**
 * @typedef {'authentication'|'credentials'|'foreground'|'foreground.continuous'|'deferred'|'push'|'launcher'|'extension'|'back'|'gestures'|'notifications'|'records'|'coordination'|'secure-storage'|'background'|'workflows'} PlatformFeature
 */
/**
 * @typedef {object} PlatformFeatureSupport
 * @property {number} schema Descriptor schema version, currently 1.
 * @property {'android'|'ios'|'macos'|'linux'|'windows'|'unknown'} platform iPadOS uses the ios backend.
 * @property {PlatformFeature} feature
 * @property {'implemented'|'missing'|'restricted'} implementation Missing is unimplemented runtime work; restricted is a platform API limitation.
 * @property {string} backend Native implementation family, empty when absent.
 * @property {string} reason Stable unavailable reason, empty for an implemented backend.
 * @property {boolean} requiresRuntimeCheck An implemented backend still needs the feature's runtime support, permission, hardware and configuration checks.
 */
/**
 * @typedef {object} PlatformCapabilities
 * @property {number} schema Currently 1.
 * @property {string} platform
 * @property {Object<string, PlatformFeatureSupport>} features Inventory of the new native operation interfaces. This does not describe every existing Oro API.
 */

/**
 * Reads the compiled backend inventory on every platform without prompting,
 * loading extensions or accessing saved data. An implemented backend is not
 * proof of hardware, OS admission, current permission or device qualification.
 * @returns {Promise<PlatformCapabilities>}
 */
export async function getCapabilities () {
  const { data, err } = await ipc.request('runtime.platform.support')
  if (err) throw err
  return data
}

/**
 * Reads one compiled native feature descriptor without prompting. Query the
 * feature's own support/status API before starting an operation. Missing and
 * OS-restricted features are separate results; operational calls still reject.
 * @param {PlatformFeature} feature
 * @returns {Promise<PlatformFeatureSupport>}
 */
export async function getSupport (feature) {
  if (typeof feature !== 'string' || !/^[a-z][a-z.-]{0,63}$/.test(feature)) throw new TypeError('A native feature name is required')
  const { data, err } = await ipc.request('runtime.platform.support', { feature })
  if (err) throw err
  return data
}

export default { request, getCapabilities, getSupport }
