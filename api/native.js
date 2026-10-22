/**
 * @module native
 * Calls explicitly configured compiled Android extensions through the runtime's
 * document-owned IPC. No JavaScript module or executable path is loaded natively.
 */
import platformRequest from './internal/platform-request.js'

/**
 * @typedef {object} NativeRequestOptions
 * @property {AbortSignal} [signal] Abort native work and discard a late result.
 */

/**
 * Invokes a narrowly named native operation with a JSON object payload. Requests
 * require a foreground interactive document, expire after 90 seconds and share
 * native capacity limits. Results are limited to 512 KiB. The extension must
 * implement its own feature authorization; trusted origin is not server IAM.
 * Unregistered extensions and other platforms reject with ENOSYS.
 * @param {string} extension Configured extension name.
 * @param {string} action Operation name.
 * @param {object} [payload]
 * @param {NativeRequestOptions} [options]
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

export default { request }
