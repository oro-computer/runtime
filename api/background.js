/**
 * @module background
 * Explicit native durable tasks. Android supports expired share-cache cleanup;
 * arbitrary JavaScript, deferred sync and unattended push are not implemented.
 */
import ipc from './ipc.js'
import os from './os.js'

/**
 * @typedef {object} BackgroundScheduleOptions
 * @property {'share-prune'} kind Native cleanup only; serialized payloads are rejected.
 * @property {number} [delayMs=0] Earliest execution delay, from 0 through seven days.
 * @property {boolean} [requiresCharging=false]
 * @property {boolean} [requiresBatteryNotLow=true]
 */

/**
 * @typedef {object} BackgroundTaskState
 * @property {number} [schema]
 * @property {'share-prune'} [kind]
 * @property {'scheduling'|'scheduled'|'running'|'stopped'|'interrupted'|'completed'|'cancelled'|'rejected'|'failed'|'idle'} [phase]
 * @property {string} [generation] Opaque scheduling identity, not an authorization token.
 * @property {number} [attempts] At most three native cleanup attempts.
 * @property {number} [notBefore] Earliest Unix execution time; never a deadline guarantee.
 * @property {number} [updatedAt]
 */

/**
 * @typedef {object} BackgroundTaskSupport
 * @property {'share-prune'} kind
 * @property {boolean} available
 * @property {number} maximumAttempts
 * @property {boolean} exactTiming False; Android controls execution time.
 * @property {boolean} survivesForceStop False; the app must be launched again.
 */

/**
 * @typedef {object} BackgroundTaskStatus
 * @property {boolean} available Requires explicit background.enabled and generated manifest declarations.
 * @property {'share-prune'} kind
 * @property {string[]} supportedKinds
 * @property {boolean} pending Android currently retains the job.
 * @property {BackgroundTaskState} state Durable last scheduling record.
 * @property {boolean} backgroundJavaScript False.
 * @property {boolean} push False.
 */

function kind (value) {
  if (value !== 'share-prune') throw new TypeError('Only native share-prune tasks are supported')
  return value
}

async function request (action, options) {
  const { data, err } = await ipc.request('background.task', { action, options: JSON.stringify(options) })
  if (err) throw err
  return data
}

/**
 * Checks the supported native task kind. This does not register a JS handler or
 * authorize future application work. Other platforms reject with ENOSYS.
 * @param {'share-prune'} taskKind
 * @returns {Promise<BackgroundTaskSupport>}
 */
export async function register (taskKind) {
  return request('register', { kind: kind(taskKind) })
}

/**
 * Schedules one unique, persisted Android share-prune job. Scheduling replaces
 * the previous cleanup of that kind. Android decides timing and boot recovery;
 * force-stop prevents execution until relaunch. The task removes only expired
 * runtime-owned share files; it never executes JS, contacts servers, or retries
 * application mutations. Failed native cleanup has at most three attempts with
 * Android exponential backoff starting at 30 seconds. Requires background.enabled.
 * @param {BackgroundScheduleOptions} options
 * @returns {Promise<BackgroundTaskState>}
 */
export async function schedule (options) {
  if (!options || typeof options !== 'object') throw new TypeError('Task options are required')
  kind(options.kind)
  const delayMs = options.delayMs ?? 0
  if (!Number.isSafeInteger(delayMs) || delayMs < 0 || delayMs > 604800000) throw new TypeError('delayMs must be 0..604800000')
  for (const field of ['requiresCharging', 'requiresBatteryNotLow']) {
    if (options[field] !== undefined && typeof options[field] !== 'boolean') throw new TypeError(`${field} must be a boolean`)
  }
  if (Object.keys(options).some(key => !['kind', 'delayMs', 'requiresCharging', 'requiresBatteryNotLow'].includes(key))) {
    throw new TypeError('Native cleanup does not accept application payloads')
  }
  return request('schedule', { ...options, delayMs })
}

/**
 * Cancels the unique native cleanup. A running deletion may finish its current
 * file before observing cancellation. Already removed expired files stay removed.
 * @param {'share-prune'} [taskKind='share-prune']
 * @returns {Promise<BackgroundTaskState>}
 */
export async function cancel (taskKind = 'share-prune') {
  return request('cancel', { kind: kind(taskKind) })
}

/**
 * Reads actual native scheduling availability and durable state. An interrupted
 * record with no retained OS job is explicit; it is not silently re-enqueued.
 * Other platforms reject with ENOSYS.
 * @param {'share-prune'} [taskKind='share-prune']
 * @returns {Promise<BackgroundTaskStatus>}
 */
export async function status (taskKind = 'share-prune') {
  return request('status', { kind: kind(taskKind) })
}

/**
 * Whether this document requested the native Android cleanup capability.
 * Read status() for actual manifest and scheduler availability.
 * @type {boolean}
 */
export const available = os.platform() === 'android' && globalThis.__args?.config?.background_enabled === 'true'

/**
 * @typedef {object} BackgroundModule
 * @property {typeof available} available
 * @property {typeof register} register
 * @property {typeof schedule} schedule
 * @property {typeof cancel} cancel
 * @property {typeof status} status
 */

/** @type {BackgroundModule} Native task entry points and the document's opt-in flag. */
export const background = { available, register, schedule, cancel, status }
export default background
