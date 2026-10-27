/**
 * @module background/deferred
 * Durable Android and Linux reads and registered cleanup/revocation through native executors.
 * Requires `[background] deferred_enabled = true` and native registration from
 * application startup. Linux scheduling requires the native process to remain
 * alive; an explicitly configured native session host can outlive the GUI.
 * Installing/enabling its user service is a deployment step, and interrupted
 * work still needs explicit reconciliation/resume. Executors recheck current server
 * IAM at every attempt. No credentials, request bodies, JS closures or queued
 * arbitrary mutations are accepted. Private origins remain subject to LAN/VPN reachability.
 * iOS/iPadOS, macOS and Windows backends for this API are not implemented.
 * Support probes report unavailable with a platform/reason; operational calls
 * reject ENOSYS. Schedule acceptance does not guarantee OS dispatch or completion.
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
 * @property {'any'|'unmetered'} [network='unmetered'] OS path constraint, not server availability.
 * @property {boolean} [requiresCharging=false]
 * @property {boolean} [requiresBatteryNotLow=true]
 * @property {number} [delayMs=0] Minimum delay, at most seven days. OS/process availability controls timing.
 * @property {number} [intervalMs=0] Read recurrence: 0 for one-shot, or 15 minutes through seven days. Requires delayMs=0.
 * @property {number} [flexMs] Periodic execution window: at least five minutes or 5% of intervalMs, whichever is larger. Defaults to that minimum; at most intervalMs. Omit for one-shot reads.
 */
/**
 * @typedef {DeferredReadOptions & { schema: number, generation: string, jobId: number, mode?: 'read'|'maintenance', phase: string, attempts: number, updatedAt: number, lastOutcome?: string, completedRuns?: number }} DeferredReadState
 */
/**
 * @typedef {object} DeferredReadSupport
 * @property {string} [platform] Present on an unavailable cross-platform support result.
 * @property {'missing'|'restricted'} [implementation] Missing runtime backend or platform API restriction; not a permission denial.
 * @property {string} [reason] Stable unavailable reason.
 * @property {boolean} available
 * @property {string[]} registeredKinds
 * @property {number} maximumTasks 32, including terminal records until explicitly released.
 * @property {number} maximumAttempts 5 with exponential backoff starting at 30 seconds.
 * @property {boolean} readOnly True for enqueue. Maintenance uses a separate explicitly registered contract.
 * @property {boolean} queuedWrites False.
 * @property {boolean} backgroundJavaScript False.
 * @property {boolean} survivesForceStop False. The app must relaunch, reconcile interrupted state and explicitly resume supported work.
 * @property {boolean} periodicReads True. Successful cycles reset their attempt budget; terminal failures suspend recurrence.
 * @property {number} minimumIntervalMs 900000.
 * @property {number} minimumFlexMs 300000.
 * @property {string[]} registeredMaintenanceKinds Explicitly registered native maintenance executors.
 * @property {Object<string, string[]>} maintenanceOperations Allowed cleanup/revoke operations per maintenance kind.
 * @property {'application-process'} [executionScope] Linux scheduling scope.
 * @property {boolean} [survivesProcessExit] False on Linux; durable records require explicit interruption reconciliation.
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

/**
 * @typedef {Omit<DeferredReadOptions, 'operationRef'|'intervalMs'|'flexMs'> & { operationRef: 'cleanup'|'revoke' }} DeferredMaintenanceOptions
 */

/**
 * Schedules one registered cleanup/revocation task. Registration must explicitly
 * assert retry safety in native code. The executor reauthorizes every attempt
 * and owns idempotency, prerequisites and uncertain-outcome reconciliation.
 * General mutations, credentials, request bodies and periodic maintenance are
 * rejected. Uses the same journal, five-attempt budget and cancellation rules.
 * @param {DeferredMaintenanceOptions} options
 * @returns {Promise<DeferredReadState>}
 */
export async function enqueueMaintenance (options) { return request('deferred', 'enqueueMaintenance', options) }

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

export default { getSupport, enqueue, enqueueMaintenance, list, status, resume, cancel, release }
