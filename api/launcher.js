/**
 * @module launcher
 * Android named destinations. Requires `[launcher] enabled = true`. A shortcut
 * produces routing receipts, never authorization or resource mutation. Restore
 * sign-in and resolve the exact current account/target before opening it.
 */
import request from './internal/platform-request.js'

/**
 * @typedef {object} LauncherDestination
 * @property {string} id Stable opaque reference, 1..128 letters/digits/underscores/hyphens.
 * @property {string} label User-visible label, 1..80 characters, no control characters.
 * @property {string} accountRef Opaque account identity; never credentials or tokens.
 * @property {string} targetRef Opaque saved target identity; never an executable URL or mutation.
 */
/**
 * @typedef {object} LauncherReceipt
 * @property {string} receiptId
 * @property {string} id
 * @property {string|null} accountRef Null for a compile-time static entry, to be resolved after sign-in.
 * @property {string} targetRef
 * @property {boolean} static
 * @property {number} receivedAt Unix milliseconds. At most 32 receipts, retained for seven days.
 */
/**
 * @typedef {object} LauncherStatus
 * @property {boolean} available
 * @property {boolean} pinSupported
 * @property {number} maximumPerActivity Android's quota, shared with app-owned shortcuts.
 * @property {boolean} rateLimited
 * @property {'resolve-after-authentication'} authorizationBoundary
 */

/**
 * @typedef {object} LauncherPublicationResult
 * @property {number} published
 * @property {boolean} authorizationEstablished False; apps must authenticate and resolve targets.
 */
/**
 * @typedef {object} LauncherPinResult
 * @property {boolean} requested
 * @property {boolean} pinConfirmed False; submission is not user confirmation.
 */

/**
 * Reads native launcher support without publishing destinations.
 * @returns {Promise<LauncherStatus>}
 */
export async function getStatus () { return request('launcher', 'status', {}) }

/**
 * Replaces this runtime's dynamic destinations, preserving unrelated app
 * shortcuts. Removed targets are disabled, including pinned copies. At most
 * 16 entries and Android's per-activity quota. Must run from a visible window.
 * @param {LauncherDestination[]} destinations
 * @returns {Promise<LauncherPublicationResult>}
 */
export async function replace (destinations) {
  if (!Array.isArray(destinations) || destinations.length > 16) throw new TypeError('At most 16 launcher destinations are supported')
  return request('launcher', 'replace', { destinations })
}

/**
 * Requests Android's user-controlled pin flow for an existing destination.
 * Submission is not confirmation that the user pinned the shortcut.
 * @param {string} id
 * @returns {Promise<LauncherPinResult>}
 */
export async function pin (id) { return request('launcher', 'pin', { id }) }

/**
 * Reads pending cold/warm launch identities after restoring authorization.
 * Reads do not consume receipts. Validate current IAM and handle removed
 * accounts/targets; opening a shortcut must not replay a mutation.
 * @returns {Promise<LauncherReceipt[]>}
 */
export async function getResponses () { return request('launcher', 'responses', {}) }

/**
 * Acknowledges a reconciled or rejected routing receipt. Repeat acknowledgements
 * are allowed. Call only after authenticated target resolution has completed.
 * @param {string} receiptId
 * @returns {Promise<void>}
 */
export async function acknowledgeResponse (receiptId) { await request('launcher', 'acknowledge', { receiptId }) }

export default { getStatus, replace, pin, getResponses, acknowledgeResponse }
