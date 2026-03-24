import { EventEmitter } from './events.js'
import { Buffer } from './buffer.js'
import ipc from './ipc.js'
import { rand64 } from './crypto.js'

// Node-like TCP socket implementation bridging to the native 'tcp' service.
//
// Semantics:
// - connect(): returns immediately; emits 'connect' when native connect
//   completes. Writes queued before 'connect' are flushed after it.
// - write(): per-write callback indicates completion/error; 'drain' fires
//   when the queue becomes empty. Boolean return is an advisory hint.
// - end(): requests half-close (shutdown) and will destroy on EOF or after
//   a small fallback delay if no EOF arrives.
// - Events: 'data' (Buffer), 'end', 'error', 'drain', 'close', 'timeout'.
class TCPSocket extends EventEmitter {
  constructor (id, opts) {
    super()
    if (typeof id === 'object' && id !== null) {
      opts = id
      id = undefined
    }
    const options = opts || {}
    this.id = id || rand64()
    this._reading = false
    this._destroyed = false
    this._connected = false
    this._connecting = false
    this._ended = false
    this._remote = null
    this._local = null
    this._writing = false
    this._queue = []
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
      const { detail } = ev || {}
      const { source, params } = detail || {}
      if (source !== 'tcp.write' || !params) return
      const { data, err } = params
      const id = data ? data.id : err ? err.id : null
      if (id !== this.id) return
      const inflight = this._inflight
      this._inflight = null
      if (err) {
        if (inflight && typeof inflight.cb === 'function') inflight.cb(err)
        else this.emit('error', err)
      } else {
        if (inflight && typeof inflight.cb === 'function') inflight.cb()
      }
      this._bumpTimeout()
      this._writing = false
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

  connect (port, host = '127.0.0.1', cb) {
    const res = ipc.sendSync('tcp.connect', {
      id: this.id,
      port,
      address: host
    })
    if (res && res.err) throw res.err
    this._connecting = true
    // Wait for async tcp.connect event to mark connected
    const ondata = (ev) => {
      const { detail } = ev
      const { source, params } = detail || {}
      if (source !== 'tcp.connect' || !params) return
      const { data, err } = params
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
    return this
  }

  _startRead () {
    if (this._reading) return
    this._reading = true
    // Subscribe to the global native event stream and filter by socket id.
    const handler = (ev) => {
      const { detail } = ev || {}
      const { source, params } = detail || {}
      if (source !== 'tcp.read' || !params) return
      const { data, err } = params
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
    try {
      const res = ipc.sendSync('tcp.readStart', { id: this.id })
      if (res && res.err) throw res.err
    } catch (err) {
      queueMicrotask(() => this.emit('error', err))
    }
  }

  write (chunk, cb) {
    if (this._destroyed) {
      const err = new Error('Socket is destroyed')
      // Node returns false and schedules callback with error; we emulate
      if (typeof cb === 'function') queueMicrotask(() => cb(err))
      else this.emit('error', err)
      return false
    }
    // Normalize chunks to Buffer; callers may pass string, ArrayBufferView, etc.
    const buf = Buffer.from(chunk)
    const task = { buf, cb }
    this._queue.push(task)
    this._bumpTimeout()
    this._flushQueue()
    return this._queue.length === 0 && !this._writing
  }

  _flushQueue () {
    if (this._writing) return
    // When queue depletes, emit 'drain' to signal writers may resume.
    if (this._queue.length === 0) {
      this.emit('drain')
      return
    }
    if (!this._connected) return
    const { buf, cb } = this._queue.shift()
    this._writing = true
    this._inflight = { cb }
    try {
      const res = ipc.sendSync('tcp.write', { id: this.id }, buf)
      if (res && res.err) {
        const err = res.err
        const inflight = this._inflight
        this._inflight = null
        if (typeof inflight?.cb === 'function') inflight.cb(err)
        else this.emit('error', err)
        this._writing = false
        this._flushQueue()
      }
      // On success, waiting for async tcp.write completion
    } catch (err) {
      const inflight = this._inflight
      this._inflight = null
      if (typeof inflight?.cb === 'function') inflight.cb(err)
      else this.emit('error', err)
      this._writing = false
      this._flushQueue()
    }
  }

  address () {
    const res = ipc.sendSync('tcp.getSockName', { id: this.id })
    if (res && res.data) {
      this._local = { address: res.data.address, port: res.data.port }
      return { ...this._local }
    }
    return this._local ? { ...this._local } : null
  }

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

  setNoDelay (on = true) {
    const res = ipc.sendSync('tcp.setNoDelay', { id: this.id, on: !!on })
    if (res.err) throw res.err
    return true
  }

  setKeepAlive (on = true, initialDelaySec = 0) {
    const res = ipc.sendSync('tcp.setKeepAlive', {
      id: this.id,
      on: !!on,
      delay: initialDelaySec >>> 0
    })
    if (res.err) throw res.err
    return true
  }

  end (chunk, cb) {
    if (chunk) this.write(chunk)
    // Request half-close; do not destroy immediately to allow graceful FIN
    try {
      ipc.sendSync('tcp.shutdown', { id: this.id })
    } catch {}
    // Destroy after remote EOF or as a fallback after a short delay
    const destroyOnce = () => {
      if (this._destroyed) return
      this.destroy()
      if (typeof cb === 'function') cb()
    }
    const onEnd = () => {
      globalThis.removeEventListener('data', onShutdown)
      destroyOnce()
    }
    const onShutdown = (ev) => {
      const { detail } = ev || {}
      const { source, params } = detail || {}
      if (source !== 'tcp.shutdown' || !params) return
      const { data, err } = params
      const id = data ? data.id : err ? err.id : null
      if (id !== this.id) return
      globalThis.removeEventListener('data', onShutdown)
      // If shutdown completed but no EOF will arrive (peer may already be gone), destroy
      queueMicrotask(destroyOnce)
    }
    this.once('end', onEnd)
    this._onShutdown = onShutdown
    globalThis.addEventListener('data', onShutdown)
    // Fallback: force close after 500ms if nothing arrives
    this._endTimer = setTimeout(() => {
      globalThis.removeEventListener('data', onShutdown)
      destroyOnce()
    }, 500)
  }

  setTimeout (ms, cb) {
    const timeout = ms >>> 0 || 0
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

  destroy () {
    if (this._destroyed) return
    this._destroyed = true
    this._queue.length = 0
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
  }
}

class TCPServer extends EventEmitter {
  constructor (options = {}, connectionListener) {
    super()
    this.id = rand64()
    this._listening = false
    this._clients = new Set()
    this._timeoutMs = 0
    this._timeoutHandler = null
    this._defaults = {
      noDelay: !!options.noDelay,
      keepAlive: !!options.keepAlive,
      keepAliveDelay: options.keepAliveDelay >>> 0 || 0
    }
    if (options && typeof options.timeout === 'number') {
      const ms = options.timeout >>> 0 || 0
      if (ms > 0) this._timeoutMs = ms
    }
    if (typeof connectionListener === 'function') {
      this.on('connection', connectionListener)
    }
    ipc.sendSync('tcp.create', { id: this.id })
  }

  listen (port, host = '0.0.0.0', backlog = 128, cb) {
    let res = ipc.sendSync('tcp.bind', { id: this.id, port, address: host })
    if (res && res.err) {
      queueMicrotask(() => this.emit('error', res.err))
      if (typeof cb === 'function') queueMicrotask(() => cb(res.err))
      return this
    }
    res = ipc.sendSync('tcp.listen', { id: this.id, backlog })
    if (res && res.err) {
      queueMicrotask(() => this.emit('error', res.err))
      if (typeof cb === 'function') queueMicrotask(() => cb(res.err))
      return this
    }
    this._listening = true
    // Event-driven accept: subscribe to tcp.connection
    const ondata = (ev) => {
      const { detail } = ev
      const { source, params } = detail || {}
      if (source !== 'tcp.connection') return
      if (!params) return
      if (params.err && params.err.id === this.id) {
        // Surface listen/accept readiness errors
        this.emit('error', params.err)
        return
      }
      if (!params.data) return
      const { data } = params
      if (!this._listening) return
      if (data.id !== this.id) return
      const clientId = rand64()
      const r = ipc.sendSync('tcp.accept', { serverId: this.id, clientId })
      if (!r.err) {
        const socket = new TCPSocket(clientId, { existing: true })
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
    if (typeof cb === 'function') queueMicrotask(() => cb())
    return this
  }

  setTimeout (ms, cb) {
    const timeout = ms >>> 0 || 0
    this._timeoutMs = timeout
    if (typeof cb === 'function') this._timeoutHandler = cb
    return this
  }

  async close (cb) {
    this._listening = false
    if (this._ondata) {
      globalThis.removeEventListener('data', this._ondata)
      this._ondata = null
    }
    ipc.sendSync('tcp.close', { id: this.id })
    for (const c of this._clients) c.destroy()
    // Wait briefly for clients to drain (bounded)
    await this.waitClose(2000)
    if (typeof cb === 'function') cb()
    this.emit('close')
  }

  getConnections (cb) {
    const count = this._clients.size >>> 0
    if (typeof cb === 'function') cb(null, count)
    return count
  }

  async waitClose (timeoutMs = 2000) {
    const start = Date.now()
    while (this._clients.size > 0 && Date.now() - start < timeoutMs) {
      await new Promise((resolve) => setTimeout(resolve, 10))
    }
    return this._clients.size === 0
  }

  address () {
    const res = ipc.sendSync('tcp.getSockName', { id: this.id })
    if (res && res.data) {
      return { address: res.data.address, port: res.data.port }
    }
    return null
  }
}

export function createServer (options, connectionListener) {
  if (typeof options === 'function') return new TCPServer({}, options)
  return new TCPServer(options, connectionListener)
}

export function createConnection (options, cb) {
  let port, host
  let timeoutMs = 0
  if (typeof options === 'number') {
    port = options
    host = '127.0.0.1'
  } else {
    port = options.port
    host = options.host || '127.0.0.1'
    timeoutMs = options.timeout >>> 0 || 0
  }

  const socket = new TCPSocket()
  socket.connect(port, host, cb)
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

export const connect = createConnection
