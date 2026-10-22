/**
 * @module network-status
 * Android network path state. Connectivity never establishes server health,
 * account authorization, or permission to retry an uncertain operation.
 */
import ipc from './ipc.js'

/**
 * @typedef {object} NetworkStatus
 * @property {boolean} available Whether Android permits reading network state.
 * @property {boolean} connected Whether a default network exists.
 * @property {boolean} validated Whether Android validated Internet connectivity.
 * @property {boolean} captivePortal Whether Android detected a captive portal.
 * @property {boolean} metered Whether the current path is metered.
 * @property {string[]} transports Reported wifi, cellular, ethernet, bluetooth, or vpn transports.
 * @property {'online'|'offline'} name Online means the path is validated.
 */

/**
 * Reads Android's current default path without making a network request.
 * Other platforms and disabled monitoring reject with `ENOSYS`.
 * @returns {Promise<NetworkStatus>}
 */
export async function getStatus () {
  const { data, err } = await ipc.request('networkStatus.get')
  if (err) throw err
  return data
}

/**
 * Observes native network path changes. Subscribe before reading `getStatus()`
 * to obtain an initial snapshot without missing later changes. This function
 * does not start polling or probe any application server.
 * @param {(status: NetworkStatus) => void} callback
 * @returns {() => void} Removes the listener.
 */
export function onChange (callback) {
  if (typeof callback !== 'function') throw new TypeError('callback must be a function')
  const listener = event => callback(event.detail)
  globalThis.addEventListener('networkstatuschange', listener)
  return () => globalThis.removeEventListener('networkstatuschange', listener)
}

export default { getStatus, onChange }
