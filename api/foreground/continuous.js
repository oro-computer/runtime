/**
 * @module foreground/continuous
 * Explicitly user-started continuous Android specialUse or Linux process work.
 * iOS/iPadOS restrict unbounded background execution; macOS and Windows backends
 * for this API are not implemented. Support probes return unavailable with a
 * restricted/missing reason; operations reject ENOSYS. No JS polling fallback
 * supplies a durable execution guarantee on those platforms.
 * Requires `[foreground] continuous_enabled = true` and a compiled executor.
 * Android also requires a public continuous_subtype purpose. This does
 * not run background JavaScript, authorize accounts, retry, or prove transfers.
 * An optional packaged Linux native session host can outlive the GUI. Host
 * interruption still leaves durable outcomes for explicit reconciliation.
 */
import request from '../internal/platform-request.js'

/**
 * @typedef {object} ContinuousOptions
 * @property {string} id Opaque operation ID; retained until explicitly released.
 * @property {string} kind Registered compiled native executor.
 * @property {string} authorityRef Opaque saved authority reference.
 * @property {string} accountRef Opaque saved account reference.
 * @property {string} targetRef Opaque saved target reference.
 * @property {string} operationRef Opaque compiled operation reference.
 * @property {true} userInitiated Assert that this call follows an explicit user action.
 */
/**
 * @typedef {object} ContinuousStatus
 * @property {string} id
 * @property {string} kind
 * @property {string} generation Exact generation required for stop/release.
 * @property {'starting'|'running'|'completed'|'unauthorized'|'unknown'|'failed'|'rejected'} phase
 * @property {'unknown'|'completed'|'unauthorized'|'failed'|'not-started'} outcome Executor assertion, not independent server verification.
 * @property {number} updatedAt Unix milliseconds.
 */
/**
 * @typedef {object} ContinuousSupport
 * @property {string} [platform] Present on an unavailable cross-platform support result.
 * @property {'missing'|'restricted'} [implementation] Missing runtime backend or platform API restriction; not a permission denial.
 * @property {string} [reason] Stable unavailable reason.
 * @property {boolean} available Configured native backend; OS admission remains separate.
 * @property {boolean} canStart Notification permission/channel policy permits an attempt; OS admission can still reject.
 * @property {string[]} registeredKinds
 * @property {null} maximumDurationMs No runtime duration cap; the OS can stop the process.
 * @property {number} maximumTasks Retained journal entries, including terminal outcomes.
 * @property {number} maximumConcurrentExecutors One, including a cancelled executor awaiting resource cleanup.
 * @property {false} automaticRestart
 * @property {false} backgroundJavaScript
 * @property {'application-process'} [executionScope] Linux work ends with the application process.
 * @property {boolean} [survivesProcessExit] False on Linux.
 */
/**
 * @typedef {object} ContinuousRequestOptions
 * @property {AbortSignal} [signal] Cancels the request, not an already admitted operation; query its durable ID after an uncertain reply.
 */

function identifier (value) {
  if (typeof value !== 'string' || !/^[a-zA-Z0-9_-]{1,128}$/.test(value)) throw new TypeError('An opaque operation reference is required')
  return value
}

/**
 * Reports configuration, registered executors and current notification policy.
 * Missing or platform-restricted backends return unavailable status; operational calls still reject with ENOSYS.
 * @returns {Promise<ContinuousSupport>}
 */
export async function getSupport () {
  return request('foreground', 'continuous.support', {})
}

/**
 * Starts from the current foreground document after an explicit user action.
 * Returns durable admission, not completion. Navigation or an aborted/lost reply
 * can race admission: inspect the ID before another attempt. The admitted native
 * operation survives Home/navigation and has its own public Stop notification.
 * Removing the Android app task keeps monitoring active. Linux uses a live application process and an
 * action-capable desktop notification server; closing that process ends work.
 * Stop or process loss records uncertainty. Reopen/reboot never restarts work;
 * a new explicit start is required.
 * @param {ContinuousOptions} options
 * @param {ContinuousRequestOptions} [requestOptions]
 * @returns {Promise<ContinuousStatus>}
 */
export async function start (options, requestOptions = {}) {
  if (!options || options.userInitiated !== true ||
      Object.keys(options).some(key => !['id', 'kind', 'authorityRef', 'accountRef', 'targetRef', 'operationRef', 'userInitiated'].includes(key)) ||
      typeof options.kind !== 'string' || !/^[a-z][a-z0-9.-]{0,63}$/.test(options.kind)) throw new TypeError('An explicit user action and registered executor are required')
  const payload = { kind: options.kind, userInitiated: true }
  for (const field of ['id', 'authorityRef', 'accountRef', 'targetRef', 'operationRef']) payload[field] = identifier(options[field])
  return request('foreground', 'continuous.start', payload, requestOptions.signal)
}

/**
 * Reads the retained status without restarting work.
 * @param {string} id
 * @returns {Promise<ContinuousStatus>}
 */
export async function getStatus (id) {
  return request('foreground', 'continuous.status', { id: identifier(id) })
}

/**
 * Lists at most sixteen retained, redacted monitoring statuses.
 * @returns {Promise<ContinuousStatus[]>}
 */
export async function list () {
  return request('foreground', 'continuous.list', {})
}

function generation (value) {
  if (typeof value !== 'string' || !/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/.test(value)) throw new TypeError('An exact native generation is required')
  return value
}

/**
 * Requests stop for one exact generation. An uncertain remote outcome stays
 * unknown. Cancellation does not undo an already completed remote operation.
 * @param {string} id
 * @param {string} expectedGeneration
 * @returns {Promise<ContinuousStatus>}
 */
export async function stop (id, expectedGeneration) {
  return request('foreground', 'continuous.stop', { id: identifier(id), generation: generation(expectedGeneration) })
}

/**
 * Releases a terminal journal entry after app reconciliation. Resource cleanup
 * must finish before the operation ID can be released or a new executor started.
 * @param {string} id
 * @param {string} expectedGeneration
 * @returns {Promise<void>}
 */
export async function release (id, expectedGeneration) {
  await request('foreground', 'continuous.release', { id: identifier(id), generation: generation(expectedGeneration) })
}

export default { getSupport, start, getStatus, list, stop, release }
