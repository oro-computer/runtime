/**
 * @module foreground
 * Explicitly user-started Android data-sync or Linux native-process work. Requires
 * `[foreground] enabled = true` and a compiled registered executor. This does
 * not run background JavaScript, authorize accounts, retry, or prove transfers.
 * Linux can delegate to an explicitly packaged native session host using
 * `[linux.native] service_enabled = true`; login hosting needs deployment setup.
 * iOS/iPadOS, macOS and Windows backends for this API are not implemented.
 * Support probes report unavailable and operational calls reject ENOSYS on
 * missing backends; an accepted executor is not proof of completed remote work.
 */
import request from './internal/platform-request.js'

/**
 * @typedef {object} ForegroundOptions
 * @property {string} id Opaque operation ID; retained until explicitly released.
 * @property {string} kind Registered compiled native executor.
 * @property {string} authorityRef Opaque saved authority reference.
 * @property {string} accountRef Opaque saved account reference.
 * @property {string} targetRef Opaque saved target reference.
 * @property {string} operationRef Opaque compiled operation reference.
 * @property {true} userInitiated Assert that this call follows an explicit user action.
 * @property {number} [durationMs=900000] 1,000..1,800,000 milliseconds, subject to Android limits.
 */
/**
 * @typedef {object} ForegroundStatus
 * @property {string} id
 * @property {string} kind
 * @property {string} generation Exact generation required for stop/release.
 * @property {'starting'|'running'|'completed'|'unauthorized'|'unknown'|'failed'|'rejected'} phase
 * @property {'unknown'|'completed'|'unauthorized'|'failed'|'not-started'} outcome Executor assertion, not independent server verification.
 * @property {number} updatedAt Unix milliseconds.
 * @property {number} durationMs
 */
/**
 * @typedef {object} ForegroundSupport
 * @property {string} [platform] Present on an unavailable cross-platform support result.
 * @property {'missing'|'restricted'} [implementation] Missing runtime backend or platform API restriction; not a permission denial.
 * @property {string} [reason] Stable unavailable reason.
 * @property {boolean} available Configured native backend; OS admission remains separate.
 * @property {boolean} canStart Notification permission/channel policy permits an attempt; OS admission can still reject.
 * @property {string[]} registeredKinds
 * @property {number} maximumDurationMs
 * @property {number} maximumTasks Retained journal entries, including terminal outcomes.
 * @property {number} maximumConcurrentExecutors One, including a cancelled executor awaiting resource cleanup.
 * @property {false} automaticRestart
 * @property {false} backgroundJavaScript
 * @property {'application-process'} [executionScope] Linux executes only while the application process lives.
 * @property {boolean} [survivesProcessExit] False on Linux; a desktop process is not a boot service.
 */
/**
 * @typedef {object} ForegroundRequestOptions
 * @property {AbortSignal} [signal] Cancels the request, not an already admitted operation; query its durable ID after an uncertain reply.
 */

function identifier (value) {
  if (typeof value !== 'string' || !/^[a-zA-Z0-9_-]{1,128}$/.test(value)) throw new TypeError('An opaque operation reference is required')
  return value
}

/**
 * Reports configuration, registered executors and current notification policy.
 * Missing or platform-restricted backends return unavailable status; operational calls still reject with ENOSYS.
 * @returns {Promise<ForegroundSupport>}
 */
export async function getSupport () {
  return request('foreground', 'support', {})
}

/**
 * Starts from the current foreground document after an explicit user action.
 * Linux additionally requires an action-capable freedesktop notification server
 * for its public Stop control. It does not install a daemon or restart work.
 * Returns durable admission, not completion. Navigation or an aborted/lost reply
 * can race admission: inspect the ID before another attempt. The admitted native
 * operation survives Home/navigation and has its own Stop notification/deadline.
 * Task removal, OS timeout and stop record uncertainty; no work auto-restarts.
 * @param {ForegroundOptions} options
 * @param {ForegroundRequestOptions} [requestOptions]
 * @returns {Promise<ForegroundStatus>}
 */
export async function start (options, requestOptions = {}) {
  if (!options || options.userInitiated !== true ||
      Object.keys(options).some(key => !['id', 'kind', 'authorityRef', 'accountRef', 'targetRef', 'operationRef', 'durationMs', 'userInitiated'].includes(key)) ||
      typeof options.kind !== 'string' || !/^[a-z][a-z0-9.-]{0,63}$/.test(options.kind)) throw new TypeError('An explicit user action and registered executor are required')
  const durationMs = options.durationMs ?? 900000
  if (!Number.isInteger(durationMs) || durationMs < 1000 || durationMs > 1800000) throw new RangeError('Foreground duration must be 1000..1800000 milliseconds')
  const payload = { kind: options.kind, userInitiated: true, durationMs }
  for (const field of ['id', 'authorityRef', 'accountRef', 'targetRef', 'operationRef']) payload[field] = identifier(options[field])
  return request('foreground', 'start', payload, requestOptions.signal)
}

/**
 * Reads the retained status without restarting work.
 * @param {string} id
 * @returns {Promise<ForegroundStatus>}
 */
export async function getStatus (id) {
  return request('foreground', 'status', { id: identifier(id) })
}

/**
 * Lists at most sixteen retained, redacted operation statuses.
 * @returns {Promise<ForegroundStatus[]>}
 */
export async function list () {
  return request('foreground', 'list', {})
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
 * @returns {Promise<ForegroundStatus>}
 */
export async function stop (id, expectedGeneration) {
  return request('foreground', 'stop', { id: identifier(id), generation: generation(expectedGeneration) })
}

/**
 * Releases a terminal journal entry after app reconciliation. Resource cleanup
 * must finish before the operation ID can be released or a new executor started.
 * @param {string} id
 * @param {string} expectedGeneration
 * @returns {Promise<void>}
 */
export async function release (id, expectedGeneration) {
  await request('foreground', 'release', { id: identifier(id), generation: generation(expectedGeneration) })
}

export default { getSupport, start, getStatus, list, stop, release }
