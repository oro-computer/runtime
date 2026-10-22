/**
 * @module credentials
 * Explicit Android 14+ password selection. Enable `[credentials] enabled = true`.
 * WebView Autofill, passkeys and account/server authorization remain separate.
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
 * @property {boolean} supported Platform implements native password selection.
 * @property {boolean} enabled Application opted into selection.
 * @property {boolean} busy A selection request is active.
 */

/**
 * Queries support without opening a provider or reading a credential.
 * Other platforms reject with ENOSYS.
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
