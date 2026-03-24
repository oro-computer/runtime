/**
 * @module ipfs
 *
 * High-level bindings for the native libipfs integration. The API exposes
 * helpers to start and stop the embedded daemon, add and retrieve UnixFS
 * content, manage pins, and update peer connectivity from JavaScript.
 */

import ipc from './ipc.js'

const ROUTES = Object.freeze({
  start: 'ipfs.start',
  stop: 'ipfs.stop',
  status: 'ipfs.status',
  add: 'ipfs.add',
  get: 'ipfs.get',
  pin: 'ipfs.pin',
  unpin: 'ipfs.unpin',
  garbageCollect: 'ipfs.gc',
  peerId: 'ipfs.peerId',
  addPeer: 'ipfs.addPeer',
  removePeer: 'ipfs.removePeer'
})

function ensureResult (result, source) {
  if (!result) return null

  if (result?.err) {
    const message =
      typeof result.err?.message === 'string'
        ? result.err.message
        : `ipfs operation failed (${source})`

    const error = new Error(message)
    if (typeof result.err?.type === 'string') {
      error.name = result.err.type
    }
    if (typeof result.err?.code === 'string') {
      error.code = result.err.code
    }
    throw error
  }

  if (result?.data !== undefined) {
    return result.data
  }

  if (result?.source === source) {
    return null
  }

  return result
}

async function send (route, params = {}) {
  const payload = await ipc.send(route, params)
  return ensureResult(payload, route)
}

function coercePort (value) {
  const port = Number(value)
  if (!Number.isInteger(port)) return 0
  if (port <= 0 || port > 65535) return 0
  return port
}

function normalizeBoolean (value, fallback = false) {
  if (typeof value === 'boolean') return value
  if (typeof value === 'number') return value !== 0
  if (typeof value === 'string') {
    const normalized = value.trim().toLowerCase()
    if (normalized === 'true') return true
    if (normalized === 'false') return false
  }
  return fallback
}

function normalizeObject (value) {
  if (value && typeof value === 'object') return value
  return {}
}

function normalizeStart (data = {}) {
  const info = normalizeObject(data)
  const port = coercePort(info.port)
  const repoPath = typeof info.repoPath === 'string' ? info.repoPath : ''
  const peerId = typeof info.peerId === 'string' ? info.peerId : ''
  return { repoPath, port, peerId }
}

function normalizeStatus (data = {}) {
  const info = normalizeObject(data)
  return {
    available: normalizeBoolean(info.available, false),
    started: normalizeBoolean(info.started, false),
    repoPath: typeof info.repoPath === 'string' ? info.repoPath : '',
    port: coercePort(info.port),
    peerId: typeof info.peerId === 'string' ? info.peerId : ''
  }
}

function normalizeAddResult (data = {}) {
  const info = normalizeObject(data)
  const cid = typeof info.cid === 'string' ? info.cid : ''
  if (!cid) {
    throw new Error('ipfs.add returned an invalid CID')
  }
  return { cid }
}

function normalizeGetResult (data = {}) {
  const info = normalizeObject(data)
  const cid = typeof info.cid === 'string' ? info.cid : ''
  const path = typeof info.path === 'string' ? info.path : ''
  const pinned = normalizeBoolean(info.pinned, false)
  return { cid, path, pinned }
}

function normalizePeerResult (data = {}, key) {
  const info = normalizeObject(data)
  const peer = typeof info.peer === 'string' ? info.peer : ''
  const flag = normalizeBoolean(info[key], true)
  return { peer, [key]: flag }
}

/**
 * Start the embedded IPFS node.
 *
 * @param {Object} [options]
 * @param {string} [options.repoPath] Absolute path to the repo directory.
 * @param {number|string} [options.port] Swarm port to bind.
 * @returns {Promise<{repoPath: string, port: number, peerId: string}>}
 */
export async function start (options = {}) {
  const params = {}
  if (options.repoPath) {
    params.repoPath = String(options.repoPath)
  }
  if (options.port !== undefined) {
    const port = coercePort(options.port)
    if (!port) {
      throw new TypeError('options.port must be an integer between 1 and 65535')
    }
    params.port = String(port)
  }
  const data = await send(ROUTES.start, params)
  return normalizeStart(data)
}

/**
 * Stop the running IPFS node.
 * @returns {Promise<boolean>} Resolves to true when the node is stopped.
 */
export async function stop () {
  const data = await send(ROUTES.stop)
  const info = normalizeObject(data)
  return normalizeBoolean(info.stopped, true)
}

/**
 * Fetch the current runtime status of the IPFS subsystem.
 * @returns {Promise<{available: boolean, started: boolean, repoPath: string, port: number, peerId: string}>}
 */
export async function status () {
  const data = await send(ROUTES.status)
  return normalizeStatus(data)
}

/**
 * Ensure that the embedded IPFS node has been started. When the node is not
 * already running the helper will start it with the given options.
 *
 * @param {Object} [options] Passed to {@link start} if the node is inactive.
 * @returns {Promise<{repoPath: string, port: number, peerId: string}>}
 */
export async function ensureStarted (options = {}) {
  const current = await status()
  if (current.started) {
    return current
  }
  return start(options)
}

/**
 * Add a UnixFS file or directory tree to the node and return its CID.
 *
 * @param {string} path Absolute path to the file or directory to add.
 * @returns {Promise<{cid: string}>}
 */
export async function add (path) {
  if (!path || typeof path !== 'string') {
    throw new TypeError('path must be a non-empty string')
  }
  const data = await send(ROUTES.add, { path })
  return normalizeAddResult(data)
}

/**
 * Retrieve a CID or path from the network.
 *
 * @param {string} cid CID or IPFS/IPNS path to fetch.
 * @param {Object} options
 * @param {string} options.destination Filesystem path where the payload should be written.
 * @param {boolean} [options.pin=false] Whether the fetched content should be pinned after saving.
 * @returns {Promise<{cid: string, path: string, pinned: boolean}>}
 */
export async function get (cid, { destination, pin = false } = {}) {
  if (!cid || typeof cid !== 'string') {
    throw new TypeError('cid must be a non-empty string')
  }
  if (!destination || typeof destination !== 'string') {
    throw new TypeError('options.destination must be a non-empty string')
  }
  const params = {
    cid,
    destination,
    pin: pin ? 'true' : 'false'
  }
  const data = await send(ROUTES.get, params)
  return normalizeGetResult(data)
}

/**
 * Pin a CID so it is retained locally.
 * @param {string} cid CID or path to pin.
 * @returns {Promise<{cid: string, pinned: boolean}>}
 */
export async function pin (cid) {
  if (!cid || typeof cid !== 'string') {
    throw new TypeError('cid must be a non-empty string')
  }
  const data = await send(ROUTES.pin, { cid })
  return normalizeGetResult({ ...data, cid, pinned: true })
}

/**
 * Remove a CID from the local pinset.
 * @param {string} cid CID or path to unpin.
 * @returns {Promise<{cid: string, pinned: boolean}>}
 */
export async function unpin (cid) {
  if (!cid || typeof cid !== 'string') {
    throw new TypeError('cid must be a non-empty string')
  }
  const data = await send(ROUTES.unpin, { cid })
  return normalizeGetResult({ ...data, cid, pinned: false })
}

/**
 * Trigger repository garbage collection.
 * @returns {Promise<boolean>} Resolves to true when the sweep completes.
 */
export async function garbageCollect () {
  const data = await send(ROUTES.garbageCollect)
  const info = normalizeObject(data)
  return normalizeBoolean(info.collected, true)
}

/**
 * Query the current peer ID for the running node.
 * @returns {Promise<string>} The node's peer ID (may be empty if unavailable).
 */
export async function peerId () {
  const data = await send(ROUTES.peerId)
  const info = normalizeObject(data)
  return typeof info.peerId === 'string' ? info.peerId : ''
}

/**
 * Connect to a remote peer using a multiaddress.
 * @param {string} address Multiaddress of the peer to connect.
 * @returns {Promise<{peer: string, removed: boolean}>}
 */
export async function addPeer (address) {
  if (!address || typeof address !== 'string') {
    throw new TypeError('address must be a non-empty string')
  }
  const data = await send(ROUTES.addPeer, { address })
  return normalizePeerResult(data, 'added')
}

/**
 * Disconnect a previously connected peer.
 * @param {string} address Multiaddress of the peer to remove.
 * @returns {Promise<{peer: string, added: boolean}>}
 */
export async function removePeer (address) {
  if (!address || typeof address !== 'string') {
    throw new TypeError('address must be a non-empty string')
  }
  const data = await send(ROUTES.removePeer, { address })
  return normalizePeerResult(data, 'removed')
}

export default {
  start,
  stop,
  status,
  ensureStarted,
  add,
  get,
  pin,
  unpin,
  garbageCollect,
  peerId,
  addPeer,
  removePeer
}
