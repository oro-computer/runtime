/**
 * @module hci
 *
 * Provides a low-level interface to the Linux Bluetooth Host Controller Interface
 * (HCI) via the `HCI_CHANNEL_USER` socket. This API is intended for advanced use
 * cases where direct access to the controller is required; higher-level Bluetooth
 * features should prefer the standard runtime Bluetooth services when possible.
 */

import { EventEmitter } from './events.js'
import { Buffer } from './buffer.js'
import ipc, { maybeMakeError } from './ipc.js'
import { rand64 } from './crypto.js'

/**
 * @typedef {object} HCIAdapter
 * @property {number} devId Numeric adapter identifier (e.g. `0` for `hci0`)
 * @property {string} name System name reported by the controller
 * @property {string} bdaddr Controller Bluetooth address in canonical form
 * @property {number} flags Raw adapter flags as reported by the kernel
 * @property {(string|number)} type Primary/AMP type label or numeric fallback
 * @property {(string|number)} bus Transport the controller is attached to
 * @property {boolean} powered Indicates whether the adapter is currently powered
 */

const sockets = new Map()
let nativeListenerInstalled = false

function installNativeListener () {
  if (nativeListenerInstalled) return
  nativeListenerInstalled = true

  globalThis.addEventListener('data', (event) => {
    const { detail } = event || {}
    const { source, params } = detail || {}
    if (!source || !params) return
    if (!source.startsWith('hci.')) return

    const { data, err } = params
    const id = data?.id ?? err?.id
    if (id == null) return

    const socket = sockets.get(id)
    if (!socket) return

    if (source === 'hci.data') {
      if (detail?.data) {
        const payload = Buffer.from(detail.data)
        if (payload.length > 0) socket.emit('data', payload)
      }
      return
    }

    if (source === 'hci.close') {
      if (err) {
        socket.emit('error', maybeMakeError(err, HCISocket.prototype.close))
      }
      sockets.delete(id)
      socket._closed = true
      socket.emit('close')
      return
    }

    if (source === 'hci.error' && err) {
      socket.emit('error', maybeMakeError(err, HCISocket))
    }
  })
}

function toDevId (value) {
  if (value == null) return undefined
  const num = Number(value)
  if (!Number.isInteger(num) || num < 0 || num > 0xffff) {
    throw new TypeError('devId must be an integer between 0 and 65535')
  }
  return num
}

function parseResult (res, caller) {
  if (res?.err) throw maybeMakeError(res.err, caller)
  return res?.data
}

/**
 * Enumerate available HCI adapters on the host.
 * @return {HCIAdapter[]}
 */
export function listAdapters () {
  installNativeListener()
  const res = ipc.sendSync('hci.listAdapters')
  const data = parseResult(res, listAdapters)
  return Array.isArray(data?.devices) ? data.devices : []
}

/**
 * Retrieve information for a specific adapter.
 * @param {number} devId Adapter identifier (e.g. `0` for `hci0`)
 * @return {HCIAdapter}
 */
export function getAdapter (devId) {
  installNativeListener()
  const id = toDevId(devId)
  const params = id == null ? {} : { devId: id }
  const res = ipc.sendSync('hci.getAdapterInfo', params)
  return parseResult(res, getAdapter)
}

/**
 * Enable or disable an adapter by bringing it up or down.
 * @param {number} devId Adapter identifier
 * @param {boolean} up When `true`, powers the adapter; otherwise powers it off
 * @return {{ devId: number, up: boolean }}
 */
export function setAdapterState (devId, up) {
  installNativeListener()
  const id = toDevId(devId)
  const payload = { up: up ? 'true' : 'false' }
  if (id != null) payload.devId = id
  const res = ipc.sendSync('hci.setAdapterState', payload)
  return parseResult(res, setAdapterState)
}

/**
 * Low-level socket for interacting with a Bluetooth controller via HCI.
 * @extends EventEmitter
 */
export class HCISocket extends EventEmitter {
  /**
   * @param {(number|{ devId?: number })} [options] Optional adapter identifier or configuration object.
   */
  constructor (options) {
    super()
    installNativeListener()

    let devId
    if (typeof options === 'number') {
      devId = toDevId(options)
    } else if (options && typeof options === 'object') {
      devId = toDevId(options.devId)
    }

    this.id = rand64()
    this.devId = devId
    this._closed = false

    const res = ipc.sendSync(
      'hci.open',
      devId == null ? { id: this.id } : { id: this.id, devId }
    )
    const data = parseResult(res, HCISocket)
    if (data?.devId != null) {
      this.devId = data.devId
    }

    sockets.set(this.id, this)
    try {
      parseResult(ipc.sendSync('hci.readStart', { id: this.id }), HCISocket)
    } catch (err) {
      sockets.delete(this.id)
      this._closed = true
      ipc.sendSync('hci.close', { id: this.id })
      throw err
    }
  }

  /**
   * Indicates whether the socket has been closed.
   * @return {boolean}
   */
  get closed () {
    return this._closed
  }

  /**
   * Write an HCI packet to the controller.
   * @param {ArrayBufferView|ArrayBuffer|Buffer|string|number[]} chunk HCI packet bytes.
   * @return {number} Number of bytes written.
   */
  write (chunk) {
    if (this._closed) throw new Error('HCISocket is closed')
    const buf = Buffer.from(chunk)
    if (buf.length === 0 || buf.length > 1028) {
      throw new RangeError('HCI packet size must be between 1 and 1028 bytes')
    }
    const res = ipc.sendSync('hci.write', { id: this.id }, buf)
    const data = parseResult(res, this.write)
    return data?.bytes ?? buf.length
  }

  /**
   * Stop receiving data and close the underlying socket.
   */
  close () {
    if (this._closed) return
    try {
      ipc.sendSync('hci.readStop', { id: this.id })
    } catch {
      // Ignore readStop errors; still attempt to close the handle
    }
    try {
      parseResult(ipc.sendSync('hci.close', { id: this.id }), this.close)
    } finally {
      sockets.delete(this.id)
      this._closed = true
      this.emit('close')
    }
  }

  /**
   * Convenience helper mirroring {@link listAdapters}.
   * @return {HCIAdapter[]}
   */
  static listAdapters () {
    return listAdapters()
  }

  /**
   * Convenience helper mirroring {@link getAdapter}.
   * @param {number} devId
   * @return {HCIAdapter}
   */
  static getAdapter (devId) {
    return getAdapter(devId)
  }

  /**
   * Convenience helper mirroring {@link setAdapterState}.
   * @param {number} devId
   * @param {boolean} up
   * @return {{ devId: number, up: boolean }}
   */
  static setAdapterState (devId, up) {
    return setAdapterState(devId, up)
  }
}

export default HCISocket
