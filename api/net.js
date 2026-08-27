/**
 * @module net
 * Node-style TCP clients and servers backed by the native runtime.
 */

import { EventEmitter } from './events.js'
import { Buffer } from './buffer.js'
import ipc from './ipc.js'
import { rand64 } from './crypto.js'

/**
 * @typedef {Object} TCPAddress
 * @property {string} address
 * @property {number} port
 */

/**
 * @typedef {Object} TCPConnectOptions
 * @property {number} port
 * @property {string} [host='127.0.0.1']
 * @property {number} [timeout=0]
 * @property {number} [writableHighWaterMark=65536]
 */

/**
 * @typedef {Object} TCPServerOptions
 * @property {boolean} [noDelay=false]
 * @property {boolean} [keepAlive=false]
 * @property {number} [keepAliveDelay=0]
 * @property {number} [timeout=0]
 * @property {number} [writableHighWaterMark=65536]
 */

/**
 * @typedef {Object} TCPListenOptions
 * @property {number} port
 * @property {string} [host='0.0.0.0']
 * @property {number} [backlog=128]
 */

/**
 * @typedef {Object} TCPSocketOptions
 * @property {string|number|bigint} [id]
 * @property {boolean} [existing=false]
 * @property {number} [writableHighWaterMark=65536]
 * @ignore
 */

/**
 * @callback TCPConnectionListener
 * @param {TCPSocket} socket
 * @returns {void}
 */

/**
 * @callback TCPCallback
 * @param {Error} [err]
 * @returns {void}
 */

const MAX_TCP_HANDLE_ID = 18446744073709551615n

function normaliseTCPHandleId (input) {
  if (input == null) return String(rand64())

  if (typeof input === 'bigint') {
    if (input < 0n || input > MAX_TCP_HANDLE_ID) {
      throw new RangeError('id must fit in an unsigned 64-bit integer')
    }
    return String(input)
  }

  if (typeof input === 'number') {
    if (!Number.isSafeInteger(input) || input < 0) {
      throw new RangeError('id must be a non-negative safe integer')
    }
    return String(input)
  }

  if (typeof input === 'string') {
    const value = input.trim()
    if (!/^[0-9]+$/.test(value)) {
      throw new TypeError('id must be a decimal string')
    }
    const id = BigInt(value)
    if (id > MAX_TCP_HANDLE_ID) {
      throw new RangeError('id must fit in an unsigned 64-bit integer')
    }
    return String(id)
  }

  throw new TypeError('id must be a bigint, number, or decimal string')
}

function normalisePort (input, allowZero = false) {
  if (!Number.isInteger(input)) {
    throw new TypeError('port must be an integer')
  }

  if (input < (allowZero ? 0 : 1) || input > 65535) {
    const range = allowZero ? '0 through 65535' : '1 through 65535'
    throw new RangeError(`port must be in the range ${range}`)
  }

  return input
}

function normaliseHost (input, fallback) {
  if (input == null) return fallback
  if (typeof input !== 'string' || input.length === 0) {
    throw new TypeError('host must be a non-empty string')
  }
  return input
}

function normaliseUnsignedInteger (input, name, fallback, maximum = 0xffffffff) {
  if (input == null) return fallback
  if (!Number.isInteger(input)) {
    throw new TypeError(`${name} must be an integer`)
  }
  if (input < 0 || input > maximum) {
    throw new RangeError(`${name} must be in the range 0 through ${maximum}`)
  }
  return input
}

function normaliseHighWaterMark (input) {
  const value = normaliseUnsignedInteger(
    input,
    'writableHighWaterMark',
    64 * 1024,
    Number.MAX_SAFE_INTEGER
  )
  if (value === 0) {
    throw new RangeError('writableHighWaterMark must be greater than zero')
  }
  return value
}

function normaliseConnectArguments (portOrOptions, host, cb) {
  let port = portOrOptions
  let timeout = 0
  let writableHighWaterMark

  if (portOrOptions && typeof portOrOptions === 'object') {
    port = portOrOptions.port
    timeout = normaliseUnsignedInteger(
      portOrOptions.timeout,
      'timeout',
      0
    )
    writableHighWaterMark = portOrOptions.writableHighWaterMark
    cb = typeof host === 'function' ? host : cb
    host = portOrOptions.host
  } else if (typeof host === 'function') {
    cb = host
    host = undefined
  }

  if (cb != null && typeof cb !== 'function') {
    throw new TypeError('callback must be a function')
  }

  return {
    port: normalisePort(port),
    host: normaliseHost(host, '127.0.0.1'),
    timeout,
    writableHighWaterMark,
    callback: cb
  }
}

function normaliseListenArguments (portOrOptions, host, backlog, cb) {
  let port = portOrOptions

  if (portOrOptions && typeof portOrOptions === 'object') {
    port = portOrOptions.port
    cb = typeof host === 'function' ? host : cb
    host = portOrOptions.host
    backlog = portOrOptions.backlog
  } else {
    if (typeof host === 'function') {
      cb = host
      host = undefined
      backlog = undefined
    } else if (typeof backlog === 'function') {
      cb = backlog
      backlog = undefined
    }
  }

  if (cb != null && typeof cb !== 'function') {
    throw new TypeError('callback must be a function')
  }

  return {
    port: normalisePort(port, true),
    host: normaliseHost(host, '0.0.0.0'),
    backlog: normaliseUnsignedInteger(backlog, 'backlog', 128, 0x7fffffff),
    callback: cb
  }
}

function unpackQueuedEvent (event) {
  const detail = event?.detail
  const params = detail?.params || {}
  return {
    detail,
    source: params.source || detail?.source,
    data: params.data,
    err: params.err
  }
}

// Node-like TCP socket implementation bridging to the native 'tcp' service.
//
// Semantics:
// - connect(): returns immediately; emits 'connect' when native connect
//   completes. Writes queued before 'connect' are flushed after it.
// - write(): per-write callback indicates completion/error; 'drain' fires
//   after buffered bytes fall below the configured high-water mark.
// - end(): requests half-close (shutdown) and will destroy on EOF or after
//   a small fallback delay if no EOF arrives.
// - Events: 'data' (Buffer), 'end', 'error', 'drain', 'close', 'timeout'.
export class TCPSocket extends EventEmitter {
  /**
   * @param {string|number|bigint|TCPSocketOptions} [id]
   * @param {TCPSocketOptions} [opts]
   */
  constructor (id, opts) {
    super()
    if (typeof id === 'object' && id !== null) {
      opts = id
      id = opts.id
    }
    const options = opts || {}
    this.id = normaliseTCPHandleId(id)
    this._reading = false
    this._destroyed = false
    this._connected = false
    this._connecting = false
    this._ended = false
    this._remote = null
    this._local = null
    this._writing = false
    this._queue = []
    this._bufferedBytes = 0
    this._needDrain = false
    this._ending = false
    this._shutdownStarted = false
    this.writableHighWaterMark = normaliseHighWaterMark(
      options.writableHighWaterMark
    )
    this._timeoutMs = 0
    this._timeoutTimer = null
    if (!options.existing) {
      const res = ipc.sendSync('tcp.create', { id: this.id })
      if (res && res.err && res.err.message !== 'Socket already exists') {
        throw res.err
      }
    }
    // Listen for write completion events to provide accurate backpressure
    this._writeHandler = (ev) => {
      const { source, data, err } = unpackQueuedEvent(ev)
      if (source !== 'tcp.write') return
      const id = data ? data.id : err ? err.id : null
      if (id !== this.id) return
      const inflight = this._inflight
      this._inflight = null
      if (inflight) {
        this._bufferedBytes = Math.max(0, this._bufferedBytes - inflight.length)
      }
      if (err) {
        if (inflight && typeof inflight.cb === 'function') inflight.cb(err)
        else this.emit('error', err)
      } else {
        if (inflight && typeof inflight.cb === 'function') inflight.cb()
      }
      this._bumpTimeout()
      this._writing = false
      this._emitDrainIfNeeded()
      this._flushQueue()
    }
    globalThis.addEventListener('data', this._writeHandler)
  }

  _bumpTimeout () {
    if (!this._timeoutMs || this._timeoutMs <= 0) return
    if (this._timeoutTimer) {
      try {
        clearTimeout(this._timeoutTimer)
      } catch {}
      this._timeoutTimer = null
    }
    this._timeoutTimer = setTimeout(() => {
      this._timeoutTimer = null
      this.emit('timeout')
    }, this._timeoutMs)
  }

  /**
   * Connects this socket to a remote TCP endpoint.
   * @param {number|TCPConnectOptions} port
   * @param {string|(() => void)} [host]
   * @param {() => void} [cb]
   * @returns {TCPSocket}
   */
  connect (port, host, cb) {
    const options = normaliseConnectArguments(port, host, cb)
    port = options.port
    host = options.host
    cb = options.callback
    if (this._destroyed) throw new Error('Socket is destroyed')
    if (this._connected || this._connecting) {
      throw new Error('Socket is already connected or connecting')
    }
    this._connecting = true
    // Wait for async tcp.connect event to mark connected
    const ondata = (ev) => {
      const { source, data, err } = unpackQueuedEvent(ev)
      if (source !== 'tcp.connect') return
      const id = data ? data.id : err ? err.id : null
      if (id !== this.id) return
      globalThis.removeEventListener('data', ondata)
      if (err) {
        this.emit('error', err)
        this._connecting = false
        return
      }
      this._connected = true
      this._connecting = false
      if (!this._reading) this._startRead()
      this._flushQueue()
      this.emit('connect')
      try {
        this.setNoDelay(true)
      } catch {}
      this._bumpTimeout()
      if (typeof cb === 'function') queueMicrotask(() => cb())
    }
    this._connectHandler = ondata
    globalThis.addEventListener('data', ondata)

    ipc.send('tcp.connect', {
      id: this.id,
      port,
      address: host
    }).then((res) => {
      if (!res?.err) return
      globalThis.removeEventListener('data', ondata)
      this._connectHandler = null
      this._connecting = false
      this.emit('error', res.err)
    }).catch((err) => {
      globalThis.removeEventListener('data', ondata)
      this._connectHandler = null
      this._connecting = false
      this.emit('error', err)
    })

    return this
  }

  _startRead () {
    if (this._reading) return
    this._reading = true
    // Subscribe to the global native event stream and filter by socket id.
    const handler = (ev) => {
      const { detail, source, data, err } = unpackQueuedEvent(ev)
      if (source !== 'tcp.read') return
      const id = data ? data.id : err ? err.id : null
      if (id !== this.id) return
      if (err) {
        this.emit('error', err)
        return
      }
      if (data && data.EOF) {
        this.emit('end')
        this._ended = true
        return
      }
      if (detail && detail.data) {
        this._bumpTimeout()
        const buf = Buffer.from(detail.data)
        this.emit('data', buf)
      }
    }
    globalThis.addEventListener('data', handler)
    this._globalHandler = handler
    ipc.send('tcp.readStart', { id: this.id }).then((res) => {
      if (!res?.err) return
      this._reading = false
      this.emit('error', res.err)
    }).catch((err) => {
      this._reading = false
      this.emit('error', err)
    })
  }

  /**
   * Queues bytes for writing.
   * @param {string|Buffer|Uint8Array|ArrayBuffer|DataView} chunk
   * @param {string|TCPCallback} [encoding]
   * @param {TCPCallback} [cb]
   * @returns {boolean}
   */
  write (chunk, encoding, cb) {
    if (typeof encoding === 'function') {
      cb = encoding
      encoding = undefined
    }
    if (cb != null && typeof cb !== 'function') {
      throw new TypeError('callback must be a function')
    }
    if (this._destroyed) {
      const err = new Error('Socket is destroyed')
      // Node returns false and schedules callback with error; we emulate
      if (typeof cb === 'function') queueMicrotask(() => cb(err))
      else this.emit('error', err)
      return false
    }
    if (this._ending) {
      const err = new Error('Write after end')
      if (typeof cb === 'function') queueMicrotask(() => cb(err))
      else this.emit('error', err)
      return false
    }
    // Normalize chunks to Buffer; callers may pass string, ArrayBufferView, etc.
    const buf = typeof chunk === 'string'
      ? Buffer.from(chunk, encoding)
      : Buffer.from(chunk)
    const task = { buf, cb }
    this._queue.push(task)
    this._bufferedBytes += buf.length
    const canContinue = this._bufferedBytes < this.writableHighWaterMark
    if (!canContinue) this._needDrain = true
    this._bumpTimeout()
    this._flushQueue()
    return canContinue
  }

  _emitDrainIfNeeded () {
    if (
      this._needDrain &&
      this._bufferedBytes < this.writableHighWaterMark
    ) {
      this._needDrain = false
      this.emit('drain')
    }
  }

  _flushQueue () {
    if (this._writing) return
    if (!this._connected) return
    if (this._queue.length === 0) {
      if (this._ending) this._shutdown()
      return
    }
    const { buf, cb } = this._queue.shift()
    this._writing = true
    const task = { cb, length: buf.length }
    this._inflight = task
    ipc.write('tcp.write', { id: this.id }, buf).then((res) => {
      if (res?.err && this._inflight === task) {
        const inflight = this._inflight
        this._inflight = null
        this._bufferedBytes = Math.max(
          0,
          this._bufferedBytes - inflight.length
        )
        if (typeof inflight?.cb === 'function') inflight.cb(res.err)
        else this.emit('error', res.err)
        this._writing = false
        this._emitDrainIfNeeded()
        this._flushQueue()
      }
    }).catch((err) => {
      if (this._inflight !== task) return
      const inflight = this._inflight
      this._inflight = null
      this._bufferedBytes = Math.max(
        0,
        this._bufferedBytes - inflight.length
      )
      if (typeof inflight?.cb === 'function') inflight.cb(err)
      else this.emit('error', err)
      this._writing = false
      this._emitDrainIfNeeded()
      this._flushQueue()
    })
  }

  /**
   * Returns this socket's local address, when available.
   * @returns {TCPAddress|null}
   */
  address () {
    const res = ipc.sendSync('tcp.getSockName', { id: this.id })
    if (res && res.data) {
      this._local = { address: res.data.address, port: res.data.port }
      return { ...this._local }
    }
    return this._local ? { ...this._local } : null
  }

  /**
   * Returns this socket's remote address, when connected.
   * @returns {TCPAddress|null}
   */
  remoteAddressInfo () {
    const res = ipc.sendSync('tcp.getPeerName', { id: this.id })
    if (res && res.data) {
      this._remote = { address: res.data.address, port: res.data.port }
      return { ...this._remote }
    }
    return this._remote ? { ...this._remote } : null
  }

  get remoteAddress () {
    const info = this.remoteAddressInfo()
    return info ? info.address : undefined
  }

  get remotePort () {
    const info = this.remoteAddressInfo()
    return info ? info.port : undefined
  }

  /** @returns {string|undefined} */
  get localAddress () {
    return this.address()?.address
  }

  /** @returns {number|undefined} */
  get localPort () {
    return this.address()?.port
  }

  /** @returns {boolean} */
  get destroyed () {
    return this._destroyed
  }

  /** @returns {boolean} */
  get connecting () {
    return this._connecting
  }

  /** @returns {boolean} */
  get pending () {
    return !this._connected
  }

  /** @returns {number} */
  get writableLength () {
    return this._bufferedBytes
  }

  /**
   * Enables or disables TCP_NODELAY.
   * @param {boolean} [on=true]
   * @returns {boolean}
   */
  setNoDelay (on = true) {
    const res = ipc.sendSync('tcp.setNoDelay', { id: this.id, on: !!on })
    if (res.err) throw res.err
    return true
  }

  /**
   * Enables or disables TCP keepalive.
   * @param {boolean} [on=true]
   * @param {number} [initialDelaySec=0]
   * @returns {boolean}
   */
  setKeepAlive (on = true, initialDelaySec = 0) {
    initialDelaySec = normaliseUnsignedInteger(
      initialDelaySec,
      'initialDelaySec',
      0
    )
    const res = ipc.sendSync('tcp.setKeepAlive', {
      id: this.id,
      on: !!on,
      delay: initialDelaySec
    })
    if (res.err) throw res.err
    return true
  }

  /**
   * Optionally writes a final chunk and half-closes the socket.
   * @param {string|Buffer|Uint8Array|ArrayBuffer|DataView} [chunk]
   * @param {string|(() => void)} [encoding]
   * @param {() => void} [cb]
   * @returns {TCPSocket}
   */
  end (chunk, encoding, cb) {
    if (typeof chunk === 'function') {
      cb = chunk
      chunk = undefined
      encoding = undefined
    } else if (typeof encoding === 'function') {
      cb = encoding
      encoding = undefined
    }
    if (cb != null && typeof cb !== 'function') {
      throw new TypeError('callback must be a function')
    }
    if (chunk != null) this.write(chunk, encoding)
    this._ending = true
    this._endCallback = cb
    this._flushQueue()
    return this
  }

  _shutdown () {
    if (this._shutdownStarted || this._destroyed) return
    this._shutdownStarted = true
    // Destroy after remote EOF or as a fallback after a short delay
    const destroyOnce = () => {
      if (this._destroyed) return
      this.destroy()
      if (typeof this._endCallback === 'function') this._endCallback()
      this._endCallback = null
    }
    const onEnd = () => {
      globalThis.removeEventListener('data', onShutdown)
      destroyOnce()
    }
    const onShutdown = (ev) => {
      const { source, data, err } = unpackQueuedEvent(ev)
      if (source !== 'tcp.shutdown') return
      const id = data ? data.id : err ? err.id : null
      if (id !== this.id) return
      globalThis.removeEventListener('data', onShutdown)
      // If shutdown completed but no EOF will arrive (peer may already be gone), destroy
      queueMicrotask(destroyOnce)
    }
    this.once('end', onEnd)
    this._onShutdown = onShutdown
    globalThis.addEventListener('data', onShutdown)
    // Request half-close; do not destroy immediately to allow graceful FIN
    ipc.send('tcp.shutdown', { id: this.id }).then((res) => {
      if (res?.err) this.emit('error', res.err)
    }).catch((err) => this.emit('error', err))
    // Fallback: force close after 500ms if nothing arrives
    this._endTimer = setTimeout(() => {
      globalThis.removeEventListener('data', onShutdown)
      destroyOnce()
    }, 500)
  }

  /**
   * Sets the inactivity timeout.
   * @param {number} ms
   * @param {() => void} [cb]
   * @returns {TCPSocket}
   */
  setTimeout (ms, cb) {
    const timeout = normaliseUnsignedInteger(ms, 'timeout', 0)
    if (cb != null && typeof cb !== 'function') {
      throw new TypeError('callback must be a function')
    }
    this._timeoutMs = timeout
    if (typeof cb === 'function') this.on('timeout', cb)
    if (!timeout) {
      if (this._timeoutTimer) {
        try {
          clearTimeout(this._timeoutTimer)
        } catch {}
        this._timeoutTimer = null
      }
      return this
    }
    this._bumpTimeout()
    return this
  }

  /**
   * Closes the socket and releases its native handle.
   * @returns {TCPSocket}
   */
  destroy () {
    if (this._destroyed) return this
    this._destroyed = true
    this._connected = false
    this._ended = true
    this._queue.length = 0
    this._bufferedBytes = 0
    this._needDrain = false
    this._writing = false
    if (this._endTimer) {
      try {
        clearTimeout(this._endTimer)
      } catch {}
      this._endTimer = null
    }
    if (this._timeoutTimer) {
      try {
        clearTimeout(this._timeoutTimer)
      } catch {}
      this._timeoutTimer = null
    }
    if (this._globalHandler) {
      globalThis.removeEventListener('data', this._globalHandler)
      this._globalHandler = null
    }
    if (this._connectHandler) {
      globalThis.removeEventListener('data', this._connectHandler)
      this._connectHandler = null
    }
    if (this._writeHandler) {
      globalThis.removeEventListener('data', this._writeHandler)
      this._writeHandler = null
    }
    if (this._onShutdown) {
      globalThis.removeEventListener('data', this._onShutdown)
      this._onShutdown = null
    }
    this._connecting = false
    ipc.sendSync('tcp.readStop', { id: this.id })
    ipc.sendSync('tcp.close', { id: this.id })
    this.emit('close')
    return this
  }
}

export class TCPServer extends EventEmitter {
  /**
   * @param {TCPServerOptions} [options]
   * @param {TCPConnectionListener} [connectionListener]
   */
  constructor (options = {}, connectionListener) {
    super()
    if (!options || typeof options !== 'object') {
      throw new TypeError('options must be an object')
    }
    if (connectionListener != null && typeof connectionListener !== 'function') {
      throw new TypeError('connectionListener must be a function')
    }
    this.id = normaliseTCPHandleId()
    this._listening = false
    this._clients = new Set()
    this._timeoutMs = 0
    this._timeoutHandler = null
    this._defaults = {
      noDelay: !!options.noDelay,
      keepAlive: !!options.keepAlive,
      keepAliveDelay: normaliseUnsignedInteger(
        options.keepAliveDelay,
        'keepAliveDelay',
        0
      ),
      writableHighWaterMark: normaliseHighWaterMark(
        options.writableHighWaterMark
      )
    }
    this._timeoutMs = normaliseUnsignedInteger(options.timeout, 'timeout', 0)
    if (typeof connectionListener === 'function') {
      this.on('connection', connectionListener)
    }
    const result = ipc.sendSync('tcp.create', { id: this.id })
    if (result?.err) throw result.err
  }

  /**
   * Starts accepting connections.
   * @param {number|TCPListenOptions} port
   * @param {string|TCPCallback} [host]
   * @param {number|TCPCallback} [backlog]
   * @param {TCPCallback} [cb]
   * @returns {TCPServer}
   */
  listen (port, host, backlog, cb) {
    const options = normaliseListenArguments(port, host, backlog, cb)
    port = options.port
    host = options.host
    backlog = options.backlog
    cb = options.callback
    if (this._listening) throw new Error('Server is already listening')
    const res = ipc.sendSync('tcp.bind', { id: this.id, port, address: host })
    if (res && res.err) {
      queueMicrotask(() => this.emit('error', res.err))
      if (typeof cb === 'function') queueMicrotask(() => cb(res.err))
      return this
    }
    // Event-driven accept: subscribe to tcp.connection
    const ondata = (ev) => {
      const { source, data, err } = unpackQueuedEvent(ev)
      if (source !== 'tcp.connection') return
      if (err && err.id === this.id) {
        // Surface listen/accept readiness errors
        this.emit('error', err)
        return
      }
      if (!data) return
      if (!this._listening) return
      if (data.id !== this.id) return
      const clientId = normaliseTCPHandleId()
      const r = ipc.sendSync('tcp.accept', { serverId: this.id, clientId })
      if (!r.err) {
        const socket = new TCPSocket(clientId, {
          existing: true,
          writableHighWaterMark: this._defaults.writableHighWaterMark
        })
        socket._connected = true
        socket._startRead()
        // Apply server defaults
        if (this._defaults.noDelay) {
          try {
            socket.setNoDelay(true)
          } catch {}
        }
        if (this._defaults.keepAlive) {
          try {
            socket.setKeepAlive(true, this._defaults.keepAliveDelay)
          } catch {}
        }
        if (this._timeoutMs > 0) {
          try {
            socket.setTimeout(this._timeoutMs)
          } catch {}
          const onTimeout = () => {
            this.emit('timeout', socket)
            if (typeof this._timeoutHandler === 'function') {
              try {
                this._timeoutHandler(socket)
              } catch {}
            }
          }
          socket.on('timeout', onTimeout)
          socket.once('close', () => socket.off('timeout', onTimeout))
        }
        this._clients.add(socket)
        socket.once('close', () => this._clients.delete(socket))
        this.emit('connection', socket)
      } else {
        this.emit('error', r.err)
      }
    }
    globalThis.addEventListener('data', ondata)
    this._ondata = ondata
    ipc.send('tcp.listen', { id: this.id, backlog }).then((result) => {
      if (result?.err) {
        this._listening = false
        globalThis.removeEventListener('data', ondata)
        this._ondata = null
        this.emit('error', result.err)
        if (typeof cb === 'function') cb(result.err)
        return
      }
      this._listening = true
      this.emit('listening')
      if (typeof cb === 'function') cb()
    }).catch((err) => {
      this._listening = false
      globalThis.removeEventListener('data', ondata)
      this._ondata = null
      this.emit('error', err)
      if (typeof cb === 'function') cb(err)
    })
    return this
  }

  /**
   * Sets the inactivity timeout applied to subsequently accepted sockets.
   * @param {number} ms
   * @param {(socket: TCPSocket) => void} [cb]
   * @returns {TCPServer}
   */
  setTimeout (ms, cb) {
    const timeout = normaliseUnsignedInteger(ms, 'timeout', 0)
    if (cb != null && typeof cb !== 'function') {
      throw new TypeError('callback must be a function')
    }
    this._timeoutMs = timeout
    if (typeof cb === 'function') this._timeoutHandler = cb
    return this
  }

  /**
   * Stops accepting connections and closes tracked client sockets.
   * @param {TCPCallback} [cb]
   * @returns {Promise<void>}
   */
  async close (cb) {
    if (cb != null && typeof cb !== 'function') {
      throw new TypeError('callback must be a function')
    }
    this._listening = false
    if (this._ondata) {
      globalThis.removeEventListener('data', this._ondata)
      this._ondata = null
    }
    for (const c of this._clients) c.destroy()
    const result = ipc.sendSync('tcp.close', { id: this.id })
    if (result?.err && result.err.message !== 'NotFound') {
      if (typeof cb === 'function') cb(result.err)
      else this.emit('error', result.err)
      return
    }
    // Wait briefly for clients to drain (bounded)
    await this.waitClose(2000)
    if (typeof cb === 'function') cb()
    this.emit('close')
  }

  /**
   * Returns the number of tracked client connections.
   * @param {(err: Error|null, count: number) => void} [cb]
   * @returns {number}
   */
  getConnections (cb) {
    if (cb != null && typeof cb !== 'function') {
      throw new TypeError('callback must be a function')
    }
    const count = this._clients.size >>> 0
    if (typeof cb === 'function') cb(null, count)
    return count
  }

  /**
   * Waits for tracked clients to close, up to a bounded timeout.
   * @param {number} [timeoutMs=2000]
   * @returns {Promise<boolean>}
   */
  async waitClose (timeoutMs = 2000) {
    timeoutMs = normaliseUnsignedInteger(timeoutMs, 'timeoutMs', 2000)
    const start = Date.now()
    while (this._clients.size > 0 && Date.now() - start < timeoutMs) {
      await new Promise((resolve) => setTimeout(resolve, 10))
    }
    return this._clients.size === 0
  }

  /**
   * Returns the server's bound address, when available.
   * @returns {TCPAddress|null}
   */
  address () {
    const res = ipc.sendSync('tcp.getSockName', { id: this.id })
    if (res && res.data) {
      return { address: res.data.address, port: res.data.port }
    }
    return null
  }

  /** @returns {boolean} */
  get listening () {
    return this._listening
  }
}

/**
 * Creates a TCP server.
 * @param {TCPServerOptions|TCPConnectionListener} [options]
 * @param {TCPConnectionListener} [connectionListener]
 * @returns {TCPServer}
 */
export function createServer (options, connectionListener) {
  if (typeof options === 'function') return new TCPServer({}, options)
  return new TCPServer(options || {}, connectionListener)
}

/**
 * Creates and connects a TCP socket.
 * @param {number|TCPConnectOptions} options
 * @param {string|(() => void)} [host]
 * @param {() => void} [cb]
 * @returns {TCPSocket}
 */
export function createConnection (options, host, cb) {
  const connectOptions = normaliseConnectArguments(options, host, cb)
  const socket = new TCPSocket({
    writableHighWaterMark: connectOptions.writableHighWaterMark
  })
  socket.connect(
    connectOptions.port,
    connectOptions.host,
    connectOptions.callback
  )
  const timeoutMs = connectOptions.timeout
  if (timeoutMs > 0) {
    let timer = null
    const clear = () => {
      if (timer) {
        try {
          clearTimeout(timer)
        } catch {}
        timer = null
      }
    }
    timer = setTimeout(() => {
      timer = null
      if (!socket._connected) {
        socket.emit('timeout')
        const err = new Error('Connection timeout')
        socket.emit('error', err)
        try {
          socket.destroy()
        } catch {}
      }
    }, timeoutMs)
    socket.once('connect', clear)
    socket.once('error', clear)
    socket.once('close', clear)
  }
  return socket
}

/**
 * Alias for {@link createConnection}.
 * @param {number|TCPConnectOptions} options
 * @param {string|(() => void)} [host]
 * @param {() => void} [cb]
 * @returns {TCPSocket}
 */
export function connect (options, host, cb) {
  return createConnection(options, host, cb)
}

export { TCPSocket as Socket, TCPServer as Server }

export default {
  connect,
  createConnection,
  createServer,
  Socket: TCPSocket,
  Server: TCPServer
}
