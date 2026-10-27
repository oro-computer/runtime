/**
 * @module credentials
 * Explicit Android 14+ or Linux Secret Service password selection. Enable `[credentials] enabled = true`.
 * WebView Autofill, passkeys and account/server authorization remain separate.
 * iOS/iPadOS, macOS and Windows explicit-selection backends are not implemented.
 * `getStatus()` reports unavailable with a reason; selection rejects ENOSYS on
 * missing backends. Android also requires an eligible installed password provider;
 * Linux requires a running unlocked Secret Service and interactive session.
 */
import request from './internal/platform-request.js'

/**
 * @typedef {object} PasswordSelectionOptions
 * @property {string} [username] Restrict results to this exact saved username.
 * @property {AbortSignal} [signal] Cancel the document-owned native prompt.
 */
/**
 * @typedef {object} SelectedPassword
 * @property {string} username Provider-selected username.
 * @property {string} password Private value for the interactive form only.
 */
/**
 * @typedef {object} PasswordSelectionStatus
 * @property {string} [platform] Present on an unavailable cross-platform support result.
 * @property {'missing'|'restricted'} [implementation] Missing runtime backend or platform API restriction; not a permission denial.
 * @property {string} [reason] Stable unavailable reason.
 * @property {boolean} supported Platform implements native password selection.
 * @property {boolean} enabled Application opted into selection.
 * @property {boolean} busy A selection request is active.
 * @property {'secret-service'} [provider] Linux provider backend.
 * @property {'application-attribute'} [association] Linux searches exact application and optional username attributes.
 * @property {boolean} [autofill] False on Linux; selection does not install WebKit Autofill.
 */

/**
 * Queries support without opening a provider or reading a credential.
 * Missing or platform-restricted backends return unavailable status; operational calls still reject with ENOSYS.
 * @returns {Promise<PasswordSelectionStatus>}
 */
export async function getStatus () {
  return request('credentials', 'status', {})
}

/**
 * Opens the native provider chooser after an explicit user action. Results bind
 * to the requesting foreground document and are never persisted or logged by
 * this API. No remote origin is asserted; providers determine app association.
 * It does not submit a form, save/update a password or authenticate a server.
 * Cancellation rejects ABORT_ERR; no available credential rejects ENOENT.
 * A provider result waits at most five seconds for the requesting Activity to
 * resume, then revalidates document ownership and the foreground window.
 * Cancellation, navigation, activity stop and the original deadline still apply.
 * Linux shows at most 64 matching Secret Service accounts in a native modal. Items
 * must carry `application=<bundle identifier>` and `username` attributes. Only
 * the chosen item is unlocked/read. These lookup attributes are not an attested
 * app association. Secret Service providers and locked-keyring prompts require
 * desktop qualification; no browser-origin association or automatic fill is added.
 * @param {PasswordSelectionOptions} [options]
 * @returns {Promise<SelectedPassword>}
 */
export async function selectPassword (options = {}) {
  const username = options.username ?? ''
  if (typeof username !== 'string' || username.length > 320 || Array.from(username).some(character => character.charCodeAt(0) < 32 || character.charCodeAt(0) === 127)) {
    throw new TypeError('Invalid password selection username')
  }
  return request('credentials', 'selectPassword', { username }, options.signal)
}

export default { getStatus, selectPassword }
