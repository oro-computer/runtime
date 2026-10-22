/**
 * @module background/deferred
 * Durable Android read/sync reconciliation through compiled native executors.
 * Requires `[background] deferred_enabled = true` and native registration from
 * Application.onCreate. Executors resolve references and recheck current server
 * IAM at every attempt. No credentials, request bodies, JS closures or queued
 * mutations are accepted. Private origins remain subject to LAN/VPN reachability.
 */
import request from '../internal/platform-request.js'

/**
 * @typedef {object} DeferredReadOptions
 * @property {string} id Stable task identity, 1..128 letters/digits/underscores/hyphens.
 * @property {string} kind Name registered by compiled native app code.
 * @property {string} authorityRef Opaque configured server identity, never a URL or credential.
 * @property {string} accountRef Opaque account identity; authorization is rechecked at execution.
 * @property {string} targetRef Opaque resource identity.
 * @property {string} operationRef Opaque operation identity for read-only status/reconciliation.
 * @property {'any'|'unmetered'} [network='unmetered'] Android scheduling constraint, not server availability.
 * @property {boolean} [requiresCharging=false]
 * @property {boolean} [requiresBatteryNotLow=true]
 * @property {number} [delayMs=0] Minimum delay, at most seven days. Timing is controlled by Android.
 */
/**
 * @typedef {DeferredReadOptions & { schema: number, generation: string, jobId: number, phase: string, attempts: number, updatedAt: number }} DeferredReadState
 */
/**
 * @typedef {object} DeferredReadSupport
 * @property {boolean} available
 * @property {string[]} registeredKinds
 * @property {number} maximumTasks 32, including terminal records until explicitly released.
 * @property {number} maximumAttempts 5 with Android exponential backoff starting at 30 seconds.
 * @property {boolean} readOnly True. Queued writes need a separate approved/idempotent application executor contract.
 * @property {boolean} queuedWrites False.
 * @property {boolean} backgroundJavaScript False.
 * @property {boolean} survivesForceStop False. The app must relaunch, reconcile interrupted state and explicitly resume supported work.
 */

/** @returns {Promise<DeferredReadSupport>} */
export async function getSupport () { return request('deferred', 'support', {}) }

/**
 * Schedules an authorized native read/reconciliation. Reusing the same task ID
 * with identical options returns its current state, without duplicate delivery.
 * Changing an existing task returns EEXIST. Each attempt has a 60-second result
 * deadline; a blocked native executor can still occupy the single worker.
 * @param {DeferredReadOptions} options
 * @returns {Promise<DeferredReadState>}
 */
export async function enqueue (options) { return request('deferred', 'enqueue', options) }

/** @returns {Promise<DeferredReadState[]>} */
export async function list () { return request('deferred', 'list', {}) }

/**
 * Reads retained OS scheduling state and the durable last outcome. Interrupted
 * state is explicit and is never silently rescheduled.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export async function status (id) { return request('deferred', 'status', { id }) }

/**
 * Explicitly resumes an interrupted task after app reconciliation. Preserves
 * the five-attempt budget and replaces its generation, so obsolete callbacks
 * cannot complete the resumed task. The native executor reauthorizes it again.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export async function resume (id) { return request('deferred', 'resume', { id }) }

/**
 * Cancels scheduling and signals a running native executor. An already-running
 * read can finish; its result cannot complete a cancelled or replaced task.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export async function cancel (id) { return request('deferred', 'cancel', { id }) }

/**
 * Cancels and releases a retained task record. Preserve any required application
 * receipt before releasing. A later enqueue of this ID starts a new generation.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export async function release (id) { return request('deferred', 'release', { id }) }

export default { getSupport, enqueue, list, status, resume, cancel, release }
