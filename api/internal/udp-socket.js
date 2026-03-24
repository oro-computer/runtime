/* global ReadableStream, WritableStream */
/**
 * UDPSocket — Direct Sockets UDP wrapper (connected or bound).
 *
 * Provides Web Streams for UDP IO using UDPMessage objects.
 *
 * Permissions-Policy gating: if disallowed, `opened` rejects immediately
 * with NotAllowedError and the instance is inert.
 */
import { createSocket } from '../dgram.js'
import { toBuffer } from '../util.js'
import { OperationError, NotAllowedError } from '../errors.js'
import { isDirectSocketsAllowed } from './direct-sockets-policy.js'

function detectType (addr, hint) {
  if (hint === 'ipv6') return 'udp6'
  if (hint === 'ipv4') return 'udp4'
  if (typeof addr === 'string' && addr.includes(':')) return 'udp6'
  return 'udp4'
}

export class UDPSocket {
  #socket
  #readable
  #writable
  #opened
  #openedResolve
  #openedReject
  #closed
  #closedResolve
  #mode // 'connected' | 'bound'
  #onMessage

  /**
   * @typedef {Object} UDPSocketOptions
   * @property {string} [remoteAddress]
   * @property {number} [remotePort]
   * @property {string} [localAddress]
   * @property {number} [localPort]
   * @property {'ipv4'|'ipv6'} [dnsQueryType]
   * @property {number} [sendBufferSize]
   * @property {number} [receiveBufferSize]
   * @description Provide either remoteAddress/remotePort (connected mode) OR localAddress[/localPort] (bound mode). Options are mutually exclusive.
   */

  /**
   * @typedef {Object} UDPMessage
   * @property {BufferSource} data
   * @property {string} [remoteAddress] - Required in bound mode for send; omitted in connected mode
   * @property {number} [remotePort] - Required in bound mode for send; omitted in connected mode
   */

  constructor (options) {
    if (!options || typeof options !== 'object') {
      throw new TypeError('UDPSocket constructor requires an options object')
    }

    // Initialize promises early to support policy rejection path
    this.#opened = new Promise((resolve, reject) => {
      this.#openedResolve = resolve
      this.#openedReject = reject
    })
    this.#closed = new Promise((resolve) => {
      this.#closedResolve = resolve
    })

    // Permissions-Policy gate
    if (!isDirectSocketsAllowed()) {
      const rej = this.#openedReject
      this.#openedReject = null
      if (typeof rej === 'function') {
        rej(new NotAllowedError('Operation not permitted'))
      }
      return
    }

    // Determine mode from options. Connected mode uses a fixed remote
    // endpoint; Bound mode binds to a local endpoint and requires an
    // explicit remote per write.
    const hasRemote =
      options.remoteAddress !== undefined || options.remotePort !== undefined
    const hasLocal =
      options.localAddress !== undefined || options.localPort !== undefined
    if (hasRemote && hasLocal) {
      throw new TypeError(
        'UDPSocket: remoteAddress/remotePort and localAddress/localPort are mutually exclusive'
      )
    }

    if (!hasRemote && !hasLocal) {
      throw new TypeError(
        'UDPSocket: must specify either connected or bound options'
      )
    }

    const type = detectType(
      options.remoteAddress || options.localAddress,
      options.dnsQueryType
    )
    const sock = createSocket({ type })
    this.#socket = sock

    // Already initialized above; keep resolve/reject handles

    // Build streams upon bind/connect ready
    const setupStreams = (openInfo) => {
      // Readable exposes UDPMessage objects
      this.#onMessage = null
      const readable = new ReadableStream({
        start: (controller) => {
          this.#onMessage = (msg, rinfo) => {
            try {
              const data = msg instanceof Uint8Array ? msg : new Uint8Array(msg)
              if (this.#mode === 'connected') {
                controller.enqueue({ data })
              } else {
                controller.enqueue({
                  data,
                  remoteAddress: rinfo.address,
                  remotePort: rinfo.port >>> 0
                })
              }
            } catch (err) {
              try {
                controller.error(err)
              } catch {}
            }
          }
          sock.on('message', this.#onMessage)
        },
        cancel: () => {
          try {
            sock.close()
          } catch {}
        }
      })

      const writable = new WritableStream({
        write: async (msg) => {
          // Normalize to UDPMessage
          let data
          let remoteAddress
          let remotePort
          if (msg && typeof msg === 'object' && msg.data !== undefined) {
            data = msg.data
            remoteAddress = msg.remoteAddress
            remotePort = msg.remotePort
          } else {
            data = msg
          }
          const buf = toBuffer(data)
          return new Promise((resolve, reject) => {
            try {
              if (this.#mode === 'connected') {
                // In connected mode, address/port must not be provided
                if (remoteAddress !== undefined || remotePort !== undefined) {
                  return reject(
                    new TypeError(
                      'UDPSocket writable expects no remoteAddress/remotePort in connected mode'
                    )
                  )
                }
                sock.send(buf, (err) => (err ? reject(err) : resolve()))
              } else {
                // bound mode
                if (
                  typeof remoteAddress !== 'string' ||
                  !Number.isFinite(remotePort)
                ) {
                  return reject(
                    new TypeError(
                      'UDPSocket writable requires UDPMessage { data, remoteAddress, remotePort } in bound mode'
                    )
                  )
                }
                sock.send(buf, remotePort >>> 0, remoteAddress, (err) =>
                  err ? reject(err) : resolve()
                )
              }
            } catch (err) {
              reject(err)
            }
          })
        },
        close: () => {
          try {
            sock.close()
          } catch {}
          return Promise.resolve()
        },
        abort: () => {
          try {
            sock.close()
          } catch {}
        }
      })

      this.#readable = readable
      this.#writable = writable
      const res = this.#openedResolve
      this.#openedResolve = null
      if (typeof res === 'function') res(openInfo)
    }

    if (hasRemote) {
      // connected mode
      const remotePort = options.remotePort >>> 0
      const remoteAddress = String(options.remoteAddress)
      this.#mode = 'connected'
      sock.once('connect', () => {
        const local = sock.address() || { address: '', port: 0 }
        const openInfo = {
          readable: null,
          writable: null,
          remoteAddress,
          remotePort,
          localAddress: local.address,
          localPort: local.port >>> 0
        }
        setupStreams(openInfo)
      })
      sock.connect(remotePort, remoteAddress)
    } else {
      // bound mode
      const localAddress = String(options.localAddress)
      const localPort = Number.isFinite(options.localPort)
        ? options.localPort >>> 0
        : 0
      this.#mode = 'bound'
      sock.once('listening', () => {
        const local = sock.address() || {
          address: localAddress,
          port: localPort
        }
        const openInfo = {
          readable: null,
          writable: null,
          remoteAddress: '',
          remotePort: 0,
          localAddress: local.address,
          localPort: local.port >>> 0
        }
        setupStreams(openInfo)
      })
      sock.bind({ port: localPort, address: localAddress })
    }

    sock.once('close', () => {
      try {
        if (this.#onMessage) this.#socket?.off?.('message', this.#onMessage)
      } catch {}
      const res = this.#closedResolve
      this.#closedResolve = null
      if (typeof res === 'function') res()
    })

    // Optional buffer sizes
    if (Number.isFinite(options?.sendBufferSize)) {
      try {
        sock.setSendBufferSize(options.sendBufferSize >>> 0)
      } catch {}
    }
    if (Number.isFinite(options?.receiveBufferSize)) {
      try {
        sock.setRecvBufferSize(options.receiveBufferSize >>> 0)
      } catch {}
    }
  }

  /** @type {Promise<{ readable: ReadableStream<any>, writable: WritableStream<any>, remoteAddress: string, remotePort: number, localAddress: string, localPort: number }>} */
  get opened () {
    return this.#opened
  }

  /** @type {Promise<void>} */
  get closed () {
    return this.#closed
  }

  async close () {
    if (this.#readable?.locked || this.#writable?.locked) {
      throw new OperationError(
        'UDPSocket.close() requires unlocked readable and writable streams'
      )
    }
    try {
      this.#socket?.close()
    } catch {}
    await this.#closed
  }
}

export default UDPSocket
