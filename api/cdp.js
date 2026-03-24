/**
 * @module cdp
 *
 * Starts/stops Oro's CDP (Chrome DevTools Protocol) server for automation and
 * debugging workflows.
 */
import ipc from './ipc.js'

/**
 * @typedef {object} CDPListenOptions
 * @property {string} [hostname='127.0.0.1'] - Bind address (defaults to loopback).
 * @property {number} [port=0] - Port to listen on. Use `0` for a random port.
 */

/**
 * @typedef {object} CDPStatus
 * @property {boolean} listening
 * @property {string} hostname
 * @property {number} port
 * @property {string} browserId
 * @property {string} wsEndpoint
 * @property {string} httpEndpoint
 */

/**
 * Starts the runtime CDP (Chrome DevTools Protocol) server.
 *
 * This exposes Chromium-style endpoints such as:
 * - `GET /json/version`
 * - `ws://<hostname>:<port>/devtools/browser/<browserId>`
 *
 * @param {CDPListenOptions} [options]
 * @returns {Promise<CDPStatus>}
 */
export async function listen (options = {}) {
  const { hostname = '127.0.0.1', port = 0 } = options || {}
  const { data, err } = await ipc.request('cdp.listen', { hostname, port })
  if (err) throw err
  return data
}

/**
 * Stops the runtime CDP server.
 * @returns {Promise<void>}
 */
export async function close () {
  const { err } = await ipc.request('cdp.close')
  if (err) throw err
}

/**
 * Gets the current CDP server status.
 * @returns {Promise<CDPStatus>}
 */
export async function status () {
  const { data, err } = await ipc.request('cdp.status')
  if (err) throw err
  return data
}

export default { listen, close, status }
