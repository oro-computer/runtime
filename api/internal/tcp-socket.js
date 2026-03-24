/* global ReadableStream, WritableStream */
/**
 * TCPSocket — Direct Sockets TCP client wrapper.
 *
 * Creates a TCP client connection and exposes Web Streams for IO.
 *
 * Permissions-Policy gating: if disallowed, `opened` rejects immediately
 * with NotAllowedError and the instance is inert.
 */
import { connect } from '../tcp.js'
import { toBuffer } from '../util.js'
import { OperationError, NotAllowedError } from '../errors.js'
import { isDirectSocketsAllowed } from './direct-sockets-policy.js'

// Direct Sockets-style TCPSocket wrapper implemented on top of oro:tcp
// (which internally uses the runtime TCP primitives).
export class TCPSocket {
  #socket = null
  #readable = null
  #writable = null
  #_controller = null
  #opened
  #openedResolve
  #openedReject
  #closed
  #closedResolve
  #onData
  #onEnd
  #onError
  static kFromNetSocket = Symbol.for(
    'oro.runtime.directsockets.TCPSocket.fromNet'
  )

  /**
   * @typedef {Object} TCPSocketOptions
   * @property {boolean} [noDelay=false] - Enable/disable Nagle’s algorithm
   * @property {boolean} [keepAlive] - Alias for enabling TCP keepalive
   * @property {number} [keepAliveDelay] - Seconds between TCP keepalive probes
   * @property {number} [sendBufferSize] - Not currently used by the runtime
   * @property {number} [receiveBufferSize] - Not currently used by the runtime
   * @property {'ipv4'|'ipv6'} [dnsQueryType] - Hint for name resolution
   */
  #initWithSocket (sock, options, remoteAddress, remotePort) {
    try {
      if (options && Object.prototype.hasOwnProperty.call(options, 'noDelay')) {
        sock.setNoDelay(!!options.noDelay)
      } else {
        sock.setNoDelay(true)
      }
    } catch {}

    try {
      const keepAliveOn =
        options?.keepAlive === true || Number.isFinite(options?.keepAliveDelay)
      if (keepAliveOn) {
        const delay = options?.keepAliveDelay >>> 0 || 0
        sock.setKeepAlive(true, delay)
      }
    } catch {}

    const localInfo = sock.address() || { address: '', port: 0 }
    const remoteInfo = sock.remoteAddressInfo() || {
      address: remoteAddress,
      port: remotePort >>> 0
    }

    let controllerRef = null
    const readable = new ReadableStream({
      start: (controller) => {
        controllerRef = controller
        this.#onData = (buf) => {
          try {
            const chunk = buf instanceof Uint8Array ? buf : new Uint8Array(buf)
            controller.enqueue(chunk)
          } catch (err) {
            try {
              controller.error(err)
            } catch {}
          }
        }
        this.#onEnd = () => {
          try {
            controller.close()
          } catch {}
        }
        sock.on('data', this.#onData)
        sock.once('end', this.#onEnd)
      },
      cancel: () => {
        try {
          sock.destroy()
        } catch {}
      }
    })

    const writable = new WritableStream({
      write: (data) => {
        const buf = toBuffer(data)
        return new Promise((resolve, reject) => {
          try {
            const ok = sock.write(buf, (err) => (err ? reject(err) : resolve()))
            if (ok === false) {
              /* backpressure handled by callback */
            }
          } catch (err) {
            reject(err)
          }
        })
      },
      close: () =>
        new Promise((resolve) => {
          try {
            sock.end()
          } catch {}
          resolve()
        }),
      abort: () => {
        try {
          sock.destroy()
        } catch {}
      }
    })

    this.#readable = readable
    this.#writable = writable
    this.#_controller = controllerRef
    const openedInfo = {
      readable,
      writable,
      remoteAddress: remoteInfo.address,
      remotePort: remoteInfo.port >>> 0,
      localAddress: localInfo.address,
      localPort: localInfo.port >>> 0
    }
    const res = this.#openedResolve
    this.#openedResolve = null
    if (typeof res === 'function') res(openedInfo)
  }

  /**
   * @param {string} remoteAddress - Hostname or IP.
   * @param {number} remotePort - Destination port (0..65535).
   * @param {TCPSocketOptions} [options]
   *
   * Notes
   * - Gating: if disabled by policy, `opened` rejects immediately and no
   *   underlying socket is created.
   * - 'opened' resolution: deferred until native emits 'connect'; errors
   *   before that reject `opened`.
   * - Readable semantics: enqueues Uint8Array; closes on 'end'.
   * - Writable semantics: resolves per-write callback; backpressure is
   *   handled by the underlying socket and surfaced via the callback.
   */
  constructor (remoteAddress, remotePort, options = {}) {
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
    // Internal constructor path for accepted server sockets. The
    // TCPServerSocket wrapper passes an existing net socket through a
    // well-known symbol to avoid exposing net internals publicly.
    if (
      remoteAddress &&
      typeof remoteAddress === 'object' &&
      remoteAddress[TCPSocket.kFromNetSocket]
    ) {
      const sock = remoteAddress[TCPSocket.kFromNetSocket]
      options = remotePort || {}

      this.#socket = sock
      // If an error arrives after we wrap the socket but before streams
      // become visible to the consumer, reject `opened` so callers can
      // handle failure uniformly.
      this.#onError = (err) => {
        if (this.#openedReject) {
          const rej = this.#openedReject
          this.#openedReject = null
          rej(err)
        }
        try {
          this.#_controller?.error?.(err)
        } catch {}
      }
      sock.on('error', this.#onError)

      this.#initWithSocket(sock, options)

      sock.once('close', () => {
        try {
          if (this.#onData) this.#socket?.removeListener?.('data', this.#onData)
        } catch {}
        try {
          if (this.#onEnd) this.#socket?.removeListener?.('end', this.#onEnd)
        } catch {}
        try {
          this.#socket?.removeListener?.('error', this.#onError)
        } catch {}
        const res = this.#closedResolve
        this.#closedResolve = null
        if (typeof res === 'function') res()
      })
      return
    }
    if (typeof remoteAddress !== 'string' || !remoteAddress) {
      throw new TypeError(
        'TCPSocket constructor: remoteAddress must be a non-empty string'
      )
    }
    if (!Number.isFinite(remotePort) || remotePort < 0 || remotePort > 0xffff) {
      throw new TypeError(
        'TCPSocket constructor: remotePort must be an unsigned short'
      )
    }

    // Start connecting immediately
    const sock = connect({ port: remotePort >>> 0, host: remoteAddress })
    this.#socket = sock

    // Build streams lazily on connect
    this.#onData = null
    this.#onEnd = null
    // Errors before 'connect' reject `opened`. Once connected, surface
    // via the readable's controller and `error` events.
    this.#onError = (err) => {
      // If connection hasn't opened yet, reject opened
      if (this.#openedReject) {
        const rej = this.#openedReject
        this.#openedReject = null
        rej(err)
      }
      // Propagate error on readable if present
      try {
        this.#_controller?.error?.(err)
      } catch {}
    }
    sock.on('error', this.#onError)

    sock.on('connect', () =>
      this.#initWithSocket(sock, options, remoteAddress, remotePort >>> 0)
    )

    // When the socket closes, resolve `closed` and detach listeners. This is
    // the terminal state regardless of whether `end()` was called.
    sock.once('close', () => {
      // Cleanup listeners and resolve closed
      try {
        if (this.#onData) this.#socket?.removeListener?.('data', this.#onData)
      } catch {}
      try {
        if (this.#onEnd) this.#socket?.removeListener?.('end', this.#onEnd)
      } catch {}
      try {
        this.#socket?.removeListener?.('error', this.#onError)
      } catch {}
      const res = this.#closedResolve
      this.#closedResolve = null
      if (typeof res === 'function') res()
    })
  }

  /** @type {Promise<{ readable: ReadableStream<Uint8Array>, writable: WritableStream<BufferSource>, remoteAddress: string, remotePort: number, localAddress: string, localPort: number }>} */
  get opened () {
    return this.#opened
  }

  /** @type {Promise<void>} */
  get closed () {
    return this.#closed
  }

  async close () {
    // Must succeed only if both streams are unlocked
    if (this.#readable?.locked || this.#writable?.locked) {
      throw new OperationError(
        'TCPSocket.close() requires unlocked readable and writable streams'
      )
    }
    try {
      // Try graceful half-close first; underlying close will follow
      this.#socket?.end()
    } catch {}
    try {
      // Ensure closure even if peer is gone
      this.#socket?.destroy()
    } catch {}
    await this.#closed
  }
}

export default TCPSocket
