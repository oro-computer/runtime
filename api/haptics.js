/**
 * @module haptics
 * Optional Android semantic feedback. Applications must retain a user setting
 * and pass it explicitly; system feedback preferences remain authoritative.
 */
import ipc from './ipc.js'

/**
 * @typedef {object} HapticOptions
 * @property {boolean} [enabled=false] The application's saved user preference.
 */

/**
 * Requests one feedback pulse. Android respects system/view preferences and
 * suppresses pulses less than 100 ms apart. `true` means accepted by the view,
 * not independently measured physical feedback. Other platforms reject `ENOSYS`.
 * @param {'selection'|'confirmation'|'rejection'} kind
 * @param {HapticOptions} [options]
 * @returns {Promise<boolean>}
 */
export async function perform (kind, options = {}) {
  if (!['selection', 'confirmation', 'rejection'].includes(kind)) {
    throw new TypeError('Unknown semantic feedback kind')
  }
  if (options.enabled !== true) return false
  const { data, err } = await ipc.request('haptics.perform', { kind })
  if (err) throw err
  return data?.accepted === true
}

export default { perform }
