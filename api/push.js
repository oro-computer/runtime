/**
 * @module push
 * Optional Android UnifiedPush transport. Requires `[push] enabled = true`,
 * `provider = "unifiedpush"` and a compiled NativePushHandler registered at
 * Application startup. Native handlers authorize and process encrypted payloads
 * without a WebView. Server enrollment, IAM and notification policy are app-owned.
 */
import request from './internal/platform-request.js'

/**
 * @typedef {object} PushSupport
 * @property {boolean} available Whether the compiled transport is enabled.
 * @property {string} provider unifiedpush.
 * @property {string} selectedDistributor Selected Android package, or an empty string.
 * @property {string[]} registeredKinds Compiled native handler names.
 * @property {number} maximumRegistrations 16.
 * @property {number} maximumPendingMessages 64 across all registrations.
 * @property {number} maximumMessageBytes 4096 encrypted bytes.
 * @property {boolean} backgroundJavaScript False.
 * @property {boolean} survivesForceStop False.
 */
/**
 * @typedef {object} PushRegistrationOptions
 * @property {string} id Stable application registration identity, 1..128 letters/digits/underscores/hyphens.
 * @property {string} kind Registered compiled NativePushHandler name.
 * @property {string} authorityRef Opaque configured server identity, never a URL or credential.
 * @property {string} accountRef Opaque account identity.
 * @property {string} targetRef Opaque native application state identity.
 * @property {string} applicationServerKey Base64url uncompressed P-256 VAPID public key.
 * @property {string} [description] Short public registration label, at most 128 UTF-8 bytes.
 */
/**
 * @typedef {object} PushEnrollmentKeys
 * @property {string} p256dh Base64url P-256 public key.
 * @property {string} auth Base64url authentication secret. Send only to the authenticated enrollment server.
 */
/**
 * @typedef {object} PushRegistration
 * @property {string} id
 * @property {string} kind
 * @property {string} phase registering, registered, registration-failed, unregistered or temporarily-unavailable.
 * @property {string} endpoint HTTPS push endpoint, empty until registration succeeds.
 * @property {string} distributor Selected Android package.
 * @property {string} applicationServerKey
 * @property {string} generation Endpoint generation used to acknowledge server enrollment.
 * @property {boolean} enrollmentPending Whether this endpoint still needs application server enrollment.
 * @property {PushEnrollmentKeys} keys Enrollment material. Never include in logs, assistant arguments or diagnostics.
 * @property {number} pendingMessages Retained messages awaiting native handling.
 * @property {number} exhaustedMessages Retained messages that reached their five-attempt budget.
 */
/**
 * @typedef {object} PushSubmission
 * @property {boolean} submitted Native processing or chooser launch was submitted, not completed.
 */

/** @returns {Promise<PushSupport>} */
export async function getSupport () { return request('push', 'support', {}) }

/**
 * Opens the system distributor chooser. Existing registrations must first be
 * unregistered. Returns after launch; listen for pushchange and reread support
 * when the activity resumes. Selection is bound to the originating document and
 * expires after 90 seconds. Cancellation leaves the previous selection intact.
 * @returns {Promise<PushSubmission>}
 */
export async function selectDistributor () { return request('push', 'selectDistributor', {}) }

/**
 * Begins or renews transport registration. An endpoint arrives asynchronously;
 * read getRegistration after foreground resume or pushchange. Identical IDs and
 * references reuse keys. Different references reject EEXIST. Private decryption
 * keys and connection tokens never leave native storage. Authenticate enrollment
 * with your server before enabling unattended product delivery.
 * @param {PushRegistrationOptions} options
 * @returns {Promise<PushRegistration>}
 */
export async function register (options) { return request('push', 'register', options) }

/**
 * Returns sensitive server enrollment material and bounded inbox counts.
 * @param {string} id
 * @returns {Promise<PushRegistration>}
 */
export async function getRegistration (id) { return request('push', 'registration', { id }) }

/**
 * Marks the current endpoint enrolled after an authenticated server confirmation.
 * A changed endpoint generation rejects EAGAIN and must be reconciled again.
 * Native handlers may instead implement onRegistrationChanged and return true
 * after confirming enrollment, including when a receiver starts a cold process.
 * @param {string} id
 * @param {string} generation From the enrollment result sent to your server.
 * @returns {Promise<void>}
 */
export async function acknowledgeRegistration (id, generation) {
  return request('push', 'acknowledgeRegistration', { id, generation })
}

/**
 * Stops future local delivery and signals in-flight handling before notifying
 * the distributor. A running handler may already have produced application
 * effects and must cooperate with cancellation. Revoke server grants separately
 * through an application-authorized maintenance executor.
 * @param {string} id
 * @returns {Promise<void>}
 */
export async function unregister (id) { return request('push', 'unregister', { id }) }

/**
 * Explicitly retries retained messages, resetting exhausted attempt budgets.
 * Automatic drains occur on application startup and new transport input.
 * @returns {Promise<PushSubmission>}
 */
export async function retryPending () { return request('push', 'retryPending', {}) }

export default { getSupport, selectDistributor, register, getRegistration, acknowledgeRegistration, unregister, retryPending }
