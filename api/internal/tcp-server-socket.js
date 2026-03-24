/* global ReadableStream */
/**
 * TCPServerSocket — Direct Sockets TCP server wrapper.
 *
 * Listens on a local address and exposes a ReadableStream of accepted
 * TCPSocket (Direct Sockets) instances.
 *
 * Permissions-Policy gating: if disallowed, `opened` rejects immediately
 * with NotAllowedError and the instance is inert.
 */
import { createServer } from '../tcp.js'
import { OperationError, NotAllowedError } from '../errors.js'
import TCPSocket from './tcp-socket.js'
import { isDirectSocketsAllowed } from './direct-sockets-policy.js'

export class TCPServerSocket {
  #server
  #readable
  #opened
  #openedResolve
  #openedReject
  #closed
  #closedResolve
  #onConnection

  /**
   * @typedef {Object} TCPServerSocketOptions
   * @property {number} [localPort] - 0 to have OS pick a free port
   * @property {number} [backlog] - Size of accept queue; platform default if omitted
   */

  constructor (localAddress, options = {}) {
    if (typeof localAddress !== 'string' || !localAddress) {
      throw new TypeError(
        'TCPServerSocket constructor: localAddress must be a non-empty string'
      )
    }

    const backlog = Number.isFinite(options?.backlog)
      ? options.backlog >>> 0
      : undefined
    const localPort = Number.isFinite(options?.localPort)
      ? options.localPort >>> 0
      : 0

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

    const srv = createServer()
    this.#server = srv

    // The server's readable stream yields accepted TCPSocket instances.
    // Each item is ready to `await tcpSocket.opened` to access streams.
    const readable = new ReadableStream({
      start: (controller) => {
        this.#onConnection = (netSock) => {
          try {
            // Use internal constructor path to wrap an existing net socket
            const ds = new TCPSocket({ [TCPSocket.kFromNetSocket]: netSock })
            controller.enqueue(ds)
          } catch (err) {
            try {
              controller.error(err)
            } catch {}
          }
        }
        srv.on('connection', this.#onConnection)
      },
      cancel: async () => {
        try {
          await srv.close()
        } catch {}
      }
    })
    this.#readable = readable

    srv.once('close', () => {
      try {
        if (this.#onConnection) {
          this.#server?.off?.('connection', this.#onConnection)
        }
      } catch {}
      try {
        if (this.#readable) this.#readable.cancel?.()
      } catch {}
      const res = this.#closedResolve
      this.#closedResolve = null
      if (typeof res === 'function') res()
    })

    // Start listening; report local address/port in `opened` info.
    srv.listen(localPort, localAddress, backlog, (err) => {
      if (err) {
        const rej = this.#openedReject
        this.#openedReject = null
        if (typeof rej === 'function') rej(err)
        return
      }
      const addr = srv.address() || { address: localAddress, port: localPort }
      const openedInfo = {
        readable,
        localAddress: addr.address,
        localPort: addr.port >>> 0
      }
      const res = this.#openedResolve
      this.#openedResolve = null
      if (typeof res === 'function') res(openedInfo)
    })
  }

  /** @type {Promise<{ readable: ReadableStream<any>, localAddress: string, localPort: number }>} */
  get opened () {
    return this.#opened
  }

  /** @type {Promise<void>} */
  get closed () {
    return this.#closed
  }

  async close () {
    if (this.#readable?.locked) {
      throw new OperationError(
        'TCPServerSocket.close() requires unlocked readable stream'
      )
    }
    try {
      await this.#server?.close()
    } catch {}
    await this.#closed
  }
}

export default TCPServerSocket
