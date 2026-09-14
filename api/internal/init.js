/* global XMLHttpRequest, requestAnimationFrame, Blob, DataTransfer, DragEvent, FileList, MessageEvent, reportError */
/* eslint-disable import/first */
// mark when runtime did init
console.assert(
  !globalThis.__RUNTIME_INIT_NOW__,
  'oro:internal/init.js was imported twice. ' +
    'This could lead to undefined behavior.'
)
console.assert(
  !globalThis.process?.versions?.node,
  'oro:internal/init.js was imported in Node.js. ' +
    'This could lead to undefined behavior.'
)

import './primitives.js'
import ipc from '../ipc.js'
ipc.sendSync('platform.event', 'beforeruntimeinit')

import { CustomEvent, ErrorEvent } from '../events.js'
import { IllegalConstructor } from '../util.js'
import { URL, protocols } from '../url.js'
import * as asyncHooks from './async/hooks.js'
import { rand64 } from '../crypto.js'
import location from '../location.js'
import mime from '../mime.js'
import path from '../path.js'
import os from '../os.js'
import fs from '../fs/promises.js'
import {
  createFileSystemDirectoryHandle,
  createFileSystemFileHandle
} from '../fs/web.js'

const GlobalWorker = globalThis.Worker || class Worker extends EventTarget {}

// only patch a webview or worker context
if ((globalThis.window || globalThis.self) === globalThis) {
  if (typeof globalThis.queueMicrotask === 'function') {
    const originalQueueMicrotask = globalThis.queueMicrotask
    const promise = Promise.resolve()

    globalThis.queueMicrotask = queueMicrotask
    globalThis.addEventListener('queuemicrotaskerror', (e) => {
      throw e.error
    })

    function queueMicrotask (...args) {
      if (args.length === 0) {
        throw new TypeError('Not enough arguments')
      }

      const [callback] = args

      if (typeof callback !== 'function') {
        throw new TypeError(
          "Argument 1 ('callback') to globalThis.queueMicrotask " +
            'must be a function'
        )
      }

      originalQueueMicrotask(task)

      function task () {
        try {
          return asyncHooks.wrap(callback, 'Microtask').call(globalThis)
        } catch (error) {
          // XXX(@jwerle): `queueMicrotask()` is broken in WebKit WebViews
          // If an error is thrown, it does not bubble to the `globalThis`
          // object, but is instead silently discarded. This is misleading
          // and results in confusion for developers trying to debug something
          // they may have done wrong. Here we will rethrow the exception out
          // of microtask execution context with the best function we can
          // possibly use to report the exception to the `globalThis` object
          // as an `Unhandled Promise Rejection` error
          const event = new ErrorEvent('queuemicrotaskerror', { error })
          promise.then(() => globalThis.dispatchEvent(event))
        }
      }
    }
  }
}

// webview only features
if (globalThis.window === globalThis) {
  globalThis.addEventListener('platformdrop', async (event) => {
    const handles = []
    let target = globalThis
    if (
      typeof event.detail?.x === 'number' &&
      typeof event.detail?.y === 'number'
    ) {
      target =
        globalThis.document.elementFromPoint(event.detail.x, event.detail.y) ??
        globalThis
    }

    if (Array.isArray(event.detail?.files)) {
      for (const file of event.detail.files) {
        if (typeof file === 'string') {
          try {
            const stats = await fs.stat(file)
            if (stats.isDirectory()) {
              handles.push(
                await createFileSystemDirectoryHandle(file, { writable: false })
              )
            } else {
              handles.push(
                await createFileSystemFileHandle(file, { writable: false })
              )
            }
          } catch (err) {
            try {
              // try to read from navigator
              const response = await fetch(file)
              if (response.ok) {
                const lastModified = Date.now()
                const buffer = new Uint8Array(await response.arrayBuffer())
                const types = await mime.lookup(path.extname(file).slice(1))
                const type = types[0]?.mime ?? ''

                handles.push(
                  await createFileSystemFileHandle(
                    new File(buffer, { lastModified, type }),
                    { writable: false }
                  )
                )
              } else {
                console.warn('platformdrop: ', err)
              }
            } catch (err) {
              console.warn('platformdrop: ', err)
            }
          }
        }
      }
    }

    const dataTransfer = new DataTransfer()
    const files = []

    for (const handle of handles) {
      if (typeof handle.getFile === 'function') {
        const file = await handle.getFile()
        const buffer = new Uint8Array(await file.arrayBuffer())
        files.push(
          new File([buffer], file.name, {
            lastModified: file.lastModified,
            type: file.type
          })
        )
      }
    }

    const fileList = Object.create(FileList.prototype, {
      length: {
        configurable: false,
        enumerable: false,
        get: () => files.length
      },

      item: {
        configurable: false,
        enumerable: false,
        value: (index) => files[index] ?? null
      },

      [Symbol.iterator]: {
        configurable: false,
        enumerable: false,
        get: () => files[Symbol.iterator]
      }
    })

    for (let i = 0; i < handles.length; ++i) {
      const file = files[i]
      if (file) {
        dataTransfer.items.add(file)
      } else {
        dataTransfer.items.add(handles[i].name, 'text/directory')
      }
    }

    Object.defineProperties(dataTransfer, {
      files: {
        configurable: false,
        enumerable: false,
        value: fileList
      }
    })

    const dropEvent = new DragEvent('drop', { dataTransfer })
    Object.defineProperty(dropEvent, 'detail', {
      value: {
        handles
      }
    })

    let index = 0
    for (const item of dropEvent.dataTransfer.items) {
      const handle = handles[index++]
      Object.defineProperties(item, {
        getAsFileSystemHandle: {
          configurable: false,
          enumerable: false,
          value: async () => handle
        }
      })
    }

    target.dispatchEvent(dropEvent)

    if (handles.length) {
      globalThis.dispatchEvent(
        new CustomEvent('dropfiles', {
          detail: {
            handles
          }
        })
      )
    }
  })

  // TODO: move this somewhere more appropriate
  if (os.platform() === 'ios') {
    const timing = {
      duration: 346 // TODO: document this
    }

    const keyboard = {
      opened: false,
      offset: 0,
      height: 0
    }

    const bezier = {
      show (t) {
        const p1 = 0.9
        const p2 = 0.95
        return (
          3 * (1 - t) * (1 - t) * t * p1 + 3 * (1 - t) * t * t * p2 + t * t * t
        )
      },

      hide (t) {
        const p1 = 0.86
        const p2 = 0.95
        return (
          3 * (1 - t) * (1 - t) * t * p1 + 3 * (1 - t) * t * t * p2 + t * t * t
        )
      }
    }

    globalThis.addEventListener('keyboard', function (event) {
      const { detail } = event

      keyboard.height = detail.value.height

      if (keyboard.offset === 0) {
        keyboard.offset = document.body.offsetHeight
      }

      if (detail.value.event === 'will-show' && !keyboard.opened) {
        let start = null

        requestAnimationFrame(function animate (timestamp) {
          if (!start) start = timestamp
          const elapsed = timestamp - start
          const progress = Math.min(elapsed / timing.duration, 1)
          const easeProgress = bezier.show(progress)
          const currentHeight = keyboard.offset - easeProgress * keyboard.height

          globalThis.document.body.style.height = `${currentHeight}px`

          if (progress < 1) {
            keyboard.opened = true
            requestAnimationFrame(animate)
          }
        })
      }

      if (detail.value.event === 'will-hide' && keyboard.opened) {
        keyboard.opened = false
        const { offsetHeight } = globalThis.document.body

        let start = null

        requestAnimationFrame(function animate (timestamp) {
          if (!start) start = timestamp
          const elapsed = timestamp - start
          let progress = Math.min(elapsed / timing.duration, 1)
          const easeProgress = bezier.hide(progress)
          const currentHeight = offsetHeight + easeProgress * keyboard.height
          if (currentHeight <= 0) progress = 1

          globalThis.document.body.style.height = `${currentHeight}px`

          if (progress < 1) {
            requestAnimationFrame(animate)
          } else {
            keyboard.opened = false
          }
        })
      }
    })
  }
}

class RuntimeWorker extends GlobalWorker {
  /**
   * Internal worker pool
   * @ignore
   */
  static pool = null

  /**
   * Handles `Symbol.species`
   * @ignore
   */
  static get [Symbol.species] () {
    return GlobalWorker
  }

  #id = null
  #objectURL = null
  #onglobaldata = null
  #onbroadcastchannelmessage = null

  /**
   * `RuntimeWorker` class worker.
   * @ignore
   * @param {string|URL} filename
   * @param {object=} [options]
   */
  constructor (filename, options, ...args) {
    options = { ...options }

    if (
      typeof filename === 'string' &&
      !URL.canParse(filename, location.href)
    ) {
      const blob = new Blob([filename], { type: 'text/javascript' })
      filename = URL.createObjectURL(blob).toString()
    } else if (String(filename).startsWith('blob')) {
      const request = new XMLHttpRequest()
      request.open('GET', String(filename), false)
      request.send()

      const blob = new Blob([request.responseText || request.response], {
        type: 'text/javascript'
      })

      filename = URL.createObjectURL(blob).toString()
    }

    const workerType =
      options[Symbol.for('oro.runtime.internal.worker.type')] ?? 'worker'
    const url = encodeURIComponent(new URL(filename, location.href).toString())
    const id = String(rand64())

    let topClient = globalThis.__args.client
    try {
      topClient = globalThis.__args.client.top || globalThis.__args.client
    } catch {}

    const __args = { ...globalThis.__args, ...options?.args, client: {} }
    const preload = `
    Object.defineProperty(globalThis, '__args', {
      configurable: false,
      enumerable: false,
      value: ${JSON.stringify(__args)}
    })

    globalThis.__args.client.id = '${id}'
    globalThis.__args.client.type = 'worker'
    globalThis.__args.client.frameType = 'none'
    globalThis.__args.client.parent = ${JSON.stringify({
      id: globalThis.__args?.client?.id,
      top: null,
      type: globalThis.__args?.client?.type,
      parent: null,
      frameType: globalThis.__args?.client?.frameType
    })}
    globalThis.__args.client.top = ${JSON.stringify({
      id: topClient?.id,
      top: null,
      type: topClient?.type,
      parent: null,
      frameType: topClient?.frameType
    })}

    globalThis.__args.client.parent.top = globalThis.__args.client.top

    Object.defineProperty(globalThis, 'isWorkerScope', {
      configurable: false,
      enumerable: false,
      writable: false,
      value: true
    })

    Object.defineProperty(globalThis, 'isOroRuntime', {
      configurable: false,
      enumerable: false,
      writable: false,
      value: true
    })

    Object.defineProperty(globalThis, 'RUNTIME_WORKER_ID', {
      configurable: false,
      enumerable: false,
      writable: false,
      value: '${id}'
    })

    Object.defineProperty(globalThis, 'RUNTIME_WORKER_TYPE', {
      configurable: false,
      enumerable: false,
      writable: false,
      value: '${workerType}'
    })

    Object.defineProperty(globalThis, 'RUNTIME_WORKER_LOCATION', {
      configurable: false,
      enumerable: false,
      writable: true,
      value: decodeURIComponent('${url}')
    })

    Object.defineProperty(globalThis, 'RUNTIME_WORKER_MESSAGE_EVENT_BACKLOG', {
      configurable: false,
      enumerable: false,
      value: []
    })

    globalThis.addEventListener('message', onInitialWorkerMessages)

    function onInitialWorkerMessages (event) {
      RUNTIME_WORKER_MESSAGE_EVENT_BACKLOG.push(event)
    }

    try {
      await import('${location.origin}/oro/internal/init.js')
      const hooks = await import('${location.origin}/oro/hooks.js')

      hooks.onReady(() => {
        globalThis.removeEventListener('message', onInitialWorkerMessages)
      })

      await import('${location.origin}/oro/internal/worker.js?source=${url}')
    } catch (err) {
      globalThis.reportError(err)
    }
    `.trim()

    const objectURL = URL.createObjectURL(
      new Blob([preload.trim()], { type: 'text/javascript' })
    )

    // level 1 worker `EventTarget` 'message' listener
    let onmessage = null

    // events are routed through this `EventTarget`
    const eventTarget = new EventTarget()

    super(objectURL, { ...options, type: 'module' }, ...args)

    RuntimeWorker.pool.set(id, new WeakRef(this))

    this.#id = id
    this.#objectURL = objectURL

    this.#onglobaldata = (event) => {
      const data = new Uint8Array(event.detail.data).buffer
      this.postMessage(
        {
          __runtime_worker_event: {
            type: event.type,
            detail: {
              ...event.detail,
              data
            }
          }
        },
        [data]
      )
    }

    this.#onbroadcastchannelmessage = (event) => {
      this.postMessage({
        __runtime_worker_event: {
          type: 'broadcastchannelmessage',
          detail: event.detail
        }
      })
    }

    globalThis.addEventListener('data', this.#onglobaldata)
    globalThis.addEventListener(
      'broadcastchannelmessage',
      this.#onbroadcastchannelmessage
    )

    const postMessage = this.postMessage.bind(this)
    const addEventListener = this.addEventListener.bind(this)
    const removeEventListener = this.removeEventListener.bind(this)

    const postMessageQueue = []
    let isReady = false

    this.postMessage = (...args) => {
      if (!isReady) {
        postMessageQueue.push(args)
      } else {
        return postMessage(...args)
      }
    }

    this.addEventListener = (eventName, ...args) => {
      if (eventName === 'message') {
        return eventTarget.addEventListener(eventName, ...args)
      }

      return addEventListener(eventName, ...args)
    }

    this.removeEventListener = (eventName, ...args) => {
      if (eventName === 'message') {
        return eventTarget.removeEventListener(eventName, ...args)
      }

      return removeEventListener(eventName, ...args)
    }

    Object.defineProperty(this, 'onmessage', {
      configurable: false,
      get: () => onmessage,
      set: (value) => {
        if (typeof onmessage === 'function') {
          eventTarget.removeEventListener('message', onmessage)
        }

        if (value === null || typeof value === 'function') {
          onmessage = value
          eventTarget.addEventListener('message', onmessage)
        }
      }
    })

    queueMicrotask(() => {
      addEventListener('message', (event) => {
        const { data } = event
        if (data?.__runtime_worker_init === true) {
          isReady = true

          for (const args of postMessageQueue) {
            postMessage(...args)
          }

          postMessageQueue.splice(0, postMessageQueue.length)
        } else if (data?.__runtime_worker_ipc_request) {
          const request = data.__runtime_worker_ipc_request
          if (
            typeof request?.message === 'string' &&
            request.message.startsWith('ipc://')
          ) {
            queueMicrotask(async () => {
              try {
                // eslint-disable-next-line no-use-before-define
                const transfer = []
                const message = ipc.Message.from(request.message, request.bytes)
                let promise
                if (message.name === 'buffer.map' && globalThis.window) {
                  // Android reads the mapped body using the worker's XHR header.
                  // Mapping is synchronous on the document bridge; acknowledge
                  // it here because buffer.map has no native response event.
                  try {
                    ipc.postMessage(request.message, message.bytes)
                    promise = Promise.resolve(ipc.Result.from({ data: {} }))
                  } catch (err) {
                    promise = Promise.resolve(ipc.Result.from(null, err))
                  }
                } else {
                  const options = { bytes: message.bytes }
                  const params = { ...message.rawParams }
                  // Sequence numbers are local to each worker. Reusing them on
                  // the shared render-process bridge makes concurrent workers
                  // listen for the same response event.
                  // Body keys are already unique and must survive nested relays.
                  if (message.name !== 'buffer.map') delete params.seq
                  promise = ipc.send(message.name, params, options)
                }

                if (message.get('resolve') === false) {
                  return
                }

                const result = await promise

                ipc.findMessageTransfers(transfer, result)

                // Internal replies must also reach modules that are still
                // evaluating, before the worker announces application readiness.
                postMessage(
                  {
                    __runtime_worker_ipc_result: {
                      message: message.toJSON(),
                      result: result.toJSON()
                    }
                  },
                  { transfer }
                )
              } catch (err) {
                globalThis.reportError(err)
              }
            })
          }
        } else {
          return eventTarget.dispatchEvent(new MessageEvent(event.type, event))
        }
      })
    })
  }

  get id () {
    return this.#id
  }

  get objectURL () {
    return this.#objectURL
  }

  terminate () {
    RuntimeWorker.pool.delete(this.#id)
    globalThis.removeEventListener('data', this.#onglobaldata)
    globalThis.removeEventListener(
      'broadcastchannelmessage',
      this.#onbroadcastchannelmessage
    )
    return super.terminate()
  }
}

try {
  RuntimeWorker.pool = globalThis.top?.Worker?.pool || new Map()
} catch {
  RuntimeWorker.pool = new Map()
}

// patch `globalThis.Worker`
if (globalThis.Worker === GlobalWorker) {
  globalThis.Worker = RuntimeWorker
}

// patch `globalThis.XMLHttpRequest`
if (typeof globalThis.XMLHttpRequest === 'function') {
  const { open, send, setRequestHeader } = globalThis.XMLHttpRequest.prototype
  const additionalHeaders = {}
  const headerFilters = ['user-agent']
  const isAsync = Symbol('isAsync')
  let queue = null

  if (
    typeof globalThis.__args.config.webview_fetch_headers_filter === 'string' &&
    globalThis.__args.config.webview_fetch_headers_filter.length > 0
  ) {
    const filters =
      globalThis.__args.config.webview_fetch_headers_filter.split(' ')
    for (const filter of filters) {
      headerFilters.push(
        new RegExp(filter.replace(/\*/g, '(.*)').replace(/\.\.\*/g, '.*'), 'i')
      )
    }
  }

  for (const key in globalThis.__args.config) {
    if (key.startsWith('webview_fetch_headers_')) {
      const name = key.replace('webview_fetch_headers_', '')
      additionalHeaders[name] = globalThis.__args.config[name]
    }
  }

  globalThis.XMLHttpRequest.prototype.setRequestHeader = function (
    name,
    value
  ) {
    if (testHeaderFilters(name)) {
      try {
        setRequestHeader.call(this, name, value)
      } catch {}
    }

    function testHeaderFilters (header) {
      if (header.toLowerCase().startsWith('sec-')) {
        return false
      }

      for (const filter of headerFilters) {
        if (typeof filter === 'string') {
          if (filter === name.toLowerCase()) {
            return false
          }
        } else if (filter.test(header)) {
          return false
        }
      }
      return true
    }
  }

  globalThis.XMLHttpRequest.prototype.open = function (
    method,
    url,
    isAsyncRequest,
    ...args
  ) {
    Object.defineProperty(this, isAsync, {
      configurable: false,
      enumerable: false,
      writable: false,
      value: isAsyncRequest !== false
    })

    if (typeof url === 'string') {
      try {
        url = new URL(url, location.origin)
      } catch {}
    }

    const value = open.call(
      this,
      method,
      url.toString(),
      isAsyncRequest !== false,
      ...args
    )

    if (
      method !== 'OPTIONS' &&
      (globalThis.__args?.config?.webview_fetch_allow_runtime_headers ===
        true ||
        (url.protocol && /(socket|ipc|node|npm):/.test(url.protocol)) ||
        (url.protocol && protocols.handlers.has(url.protocol.slice(0, -1))) ||
        url.hostname === globalThis.__args.config.meta_bundle_identifier)
    ) {
      for (const key in additionalHeaders) {
        this.setRequestHeader(key, additionalHeaders[key])
      }

      if (globalThis.__args?.client) {
        this.setRequestHeader('Runtime-Client-ID', globalThis.__args.client.id)
      }

      if (typeof globalThis.RUNTIME_WORKER_LOCATION === 'string') {
        this.setRequestHeader(
          'Runtime-Worker-Location',
          globalThis.RUNTIME_WORKER_LOCATION
        )
      }

      if (globalThis.top && globalThis.top !== globalThis) {
        this.setRequestHeader('Runtime-Frame-Type', 'nested')
      } else if (!globalThis.window && globalThis.self === globalThis) {
        this.setRequestHeader('Runtime-Frame-Type', 'worker')
        if (globalThis.clients && globalThis.FetchEvent) {
          this.setRequestHeader('Runtime-Worker-Type', 'serviceworker')
        } else {
          this.setRequestHeader('Runtime-Worker-Type', 'worker')
        }
      } else {
        this.setRequestHeader('Runtime-Frame-Type', 'top-level')
      }

      return value
    }
  }

  globalThis.XMLHttpRequest.prototype.send = function (...args) {
    if (!this[isAsync]) {
      return send.call(this, ...args)
    }

    return sendAsync(this, args)
  }

  async function sendAsync (request, args) {
    if (!queue) {
      // eslint-disable-next-line no-use-before-define
      queue = new ConcurrentQueue(
        // eslint-disable-next-line no-use-before-define
        parseInt(config.webview_xhr_concurrency)
      )
    }

    await queue.push(
      new Promise((resolve) => {
        request.addEventListener('error', resolve)
        request.addEventListener('readystatechange', () => {
          if (
            request.readyState === globalThis.XMLHttpRequest.DONE ||
            request.readyState === globalThis.XMLHttpRequest.UNSENT
          ) {
            resolve()
          }
        })
      })
    )

    return await send.call(request, ...args)
  }
}

import hooks, { RuntimeInitEvent } from '../hooks.js'
import { config } from '../application.js'
import globals from './globals.js'
import '../console.js'

hooks.onApplicationResume(() => {
  for (const ref of RuntimeWorker.pool.values()) {
    const worker = ref.deref()
    if (worker) {
      worker.postMessage({
        __runtime_worker_event: {
          type: 'applicationresume'
        }
      })
    }
  }
})

hooks.onApplicationPause(() => {
  for (const ref of RuntimeWorker.pool.values()) {
    const worker = ref.deref()
    if (worker) {
      worker.postMessage({
        __runtime_worker_event: {
          type: 'applicationpause'
        }
      })
    }
  }
})

hooks.onApplicationURL((e) => {
  for (const ref of RuntimeWorker.pool.values()) {
    const worker = ref.deref()
    if (worker) {
      worker.postMessage({
        __runtime_worker_event: {
          type: 'applicationurl',
          detail: { data: e.data, url: String(e.url ?? '') }
        }
      })
    }
  }
})

ipc.sendSync('platform.event', {
  value: 'load',
  'location.href': globalThis.location.href
})

class ConcurrentQueue extends EventTarget {
  concurrency = Infinity
  pending = []

  constructor (concurrency) {
    super()
    if (typeof concurrency === 'number' && concurrency > 0) {
      this.concurrency = concurrency
    }

    this.addEventListener('error', (event) => {
      if (event.defaultPrevented !== true) {
        // @ts-ignore
        const { error, type } = event
        globalThis.dispatchEvent?.(new ErrorEvent(type, { error }))
      }
    })
  }

  async wait () {
    if (this.pending.length < this.concurrency) return
    const offset = this.pending.length - this.concurrency + 1
    const pending = this.pending.slice(0, offset)
    await Promise.all(pending)
  }

  peek () {
    return this.pending[0]
  }

  timeout (request, timer) {
    let timeout = null
    const onresolve = () => {
      clearTimeout(timeout)
      const index = this.pending.indexOf(request)
      if (index > -1) {
        this.pending.splice(index, 1)
      }
    }

    timeout = setTimeout(onresolve, timer || 32)
    return onresolve
  }

  async push (request, timer) {
    await this.wait()
    this.pending.push(request)
    request.then(this.timeout(request, timer))
  }
}

class RuntimeQueuedResponses extends ConcurrentQueue {
  pendingDispatch = Promise.resolve()

  dispatch (id, seq, params, headers, options = null) {
    const workerId = options?.workerId || null
    const request = this.pendingDispatch.then(() => {
      return this.dispatchOne(id, seq, params, headers, workerId)
    })
    const pending = request.catch(() => {})
    this.pendingDispatch = pending
    return request
  }

  async dispatchOne (id, seq, params, headers, workerId) {
    if (typeof params !== 'object') {
      params = {}
    }

    // Chromium documents cannot use a binary responseType with synchronous XHR.
    // WebKit reads synchronously because custom-scheme async XHR can stall.
    const result = /android|win32/i.test(ipc.primordials.platform)
      ? await ipc.request('queuedResponse', { id }, { responseType: 'arraybuffer' })
      : ipc.sendSync('queuedResponse', { id }, { responseType: 'arraybuffer' })

    const worker = workerId
      ? RuntimeWorker.pool.get(workerId)?.deref?.()
      : null

    if (result.err) {
      if (worker) {
        worker.postMessage({
          __runtime_worker_event: {
            type: 'runtime-queued-response',
            detail: {
              id,
              seq,
              params,
              headers,
              error: {
                name: result.err.name,
                message: result.err.message,
                stack: result.err.stack
              }
            }
          }
        })
      } else if (!workerId) {
        this.dispatchEvent(new ErrorEvent('error', { error: result.err }))
      }
    } else {
      const { data } = result
      const detail = { headers, params, data, id }
      if (worker) {
        worker.postMessage({
          __runtime_worker_event: {
            type: 'runtime-queued-response',
            detail
          }
        })
      } else if (!workerId) {
        globalThis.dispatchEvent(new CustomEvent('data', { detail }))
      }
    }
  }
}

hooks.onLoad(async () => {
  const registeredServiceWorkers = new Set()
  const serviceWorkerScripts = config['webview_service-workers']
  const pending = []

  if (
    globalThis.window &&
    !globalThis.__RUNTIME_SERVICE_WORKER_CONTEXT__ &&
    globalThis.location.pathname !== '/oro/service-worker/index.html' &&
    globalThis.location.pathname !== '/socket/service-worker/index.html' &&
    String(config.permissions_allow_service_worker) !== 'false' &&
    String(config.webview_auto_register_service_workers) !== 'false'
  ) {
    const pendingServiceRegistrations = []

    if (typeof config['webview_service-workers'] === 'string') {
      for (const scriptURL of serviceWorkerScripts.trim().split(' ')) {
        pendingServiceRegistrations.push({
          scriptURL: scriptURL.trim(),
          options: {}
        })
      }
    }

    for (const key in config) {
      if (key.startsWith('webview_service-workers_')) {
        const scope = key.replace('webview_service-workers_', '')
        const scriptURL = config[key].trim()
        pendingServiceRegistrations.push({
          scriptURL,
          options: { scope }
        })
      }
    }

    for (const registration of pendingServiceRegistrations) {
      const { options } = registration
      let { scriptURL } = registration

      if (!scriptURL.startsWith('/') && scriptURL.startsWith('.')) {
        if (!URL.canParse(scriptURL, globalThis.location.href)) {
          scriptURL = `./${scriptURL}`
        }
      }

      const url = new URL(scriptURL, globalThis.location.origin)
      const scope = options.scope ?? new URL('.', url).pathname

      if (!globalThis.location.pathname.startsWith(scope)) {
        continue
      }

      if (registeredServiceWorkers.has(scriptURL)) {
        continue
      }

      const promise = globalThis.navigator.serviceWorker.register(
        scriptURL,
        options
      )
      registeredServiceWorkers.add(scriptURL)

      pending.push(promise)

      promise
        .then((registration) => {
          if (!registration) {
            console.warn(
              'ServiceWorker failed to register in preload: %s',
              scriptURL
            )
          }
        })
        .catch((err) => {
          console.error(
            'ServiceWorker registration error occurred in preload: %s:',
            scriptURL,
            err
          )
        })
    }
  }

  await Promise.all(pending)
  if (typeof globalThis.dispatchEvent === 'function') {
    globalThis.__RUNTIME_INIT_NOW__ = performance.now()
    globalThis.dispatchEvent(new RuntimeInitEvent())
  }

  if (globalThis.document) {
    const beginRuntimePreload = globalThis.document.querySelector(
      'meta[name=begin-runtime-preload]'
    )
    if (beginRuntimePreload) {
      let current = beginRuntimePreload
      while (current) {
        const next = current.nextElementSibling
        current.remove()
        if (
          current.tagName === 'META' &&
          current.name === 'end-runtime-preload'
        ) {
          current = null
        } else {
          current = next
        }
      }
    }
  }
})

// async preload modules
hooks.onReady(async () => {
  try {
    // precache 'fs.constants' and 'os.constants'
    await ipc.request('fs.constants', {}, { cache: true })
    await ipc.request('os.constants', {}, { cache: true })
    await import('../diagnostics.js')
    await import('../process/signal.js')
    await import('../fs/fds.js')
    await import('../constants.js')
    const errors = await import('../errors.js')
    // lazily install this
    const errno = await import('../errno.js')
    errors.ErrnoError.errno = errno
    // Install Web Bluetooth scaffold (navigator.bluetooth)
    await import('./bluetooth-web.js')
    // Install WebUSB scaffold (navigator.usb)
    await import('./usb-web.js')
    // Install WebHID scaffold (navigator.hid)
    await import('./hid-web.js')
    // Install Web Share fallback (navigator.share)
    await import('./web-share.js')
  } catch (err) {
    console.error(err.stack || err)
  }

  if (
    globalThis.RUNTIME_TEST_FILENAME &&
    globalThis.window === globalThis.top
  ) {
    try {
      const { GLOBAL_TEST_RUNNER } = await import('../test/index.js')
      await import(globalThis.RUNTIME_TEST_FILENAME)

      if (
        GLOBAL_TEST_RUNNER.length === 0 &&
        GLOBAL_TEST_RUNNER._filteredCount === 0
      ) {
        throw new Error(
          `No tests were registered by ${globalThis.RUNTIME_TEST_FILENAME}`
        )
      }
    } catch (err) {
      if (typeof globalThis.reportError === 'function') {
        globalThis.reportError(err)
      } else {
        console.error(err.stack || err)
      }
      return
    }
  }

  if (globalThis.window && globalThis.window !== globalThis.top) {
    globalThis.addEventListener('message', (e) => {
      if (e.data?.__runtime_frame_data) {
        globalThis.dispatchEvent(
          new CustomEvent('data', {
            detail: e.data?.__runtime_frame_data
          })
        )
      } else if (e.data?.__runtime_frame_broadcast_channel_message) {
        globalThis.dispatchEvent(
          new CustomEvent('broadcastchannelmessage', {
            detail: e.data?.__runtime_frame_broadcast_channel_message
          })
        )
      }
    })
  }

  if (
    globalThis.window &&
    globalThis.__RUNTIME_SHOULD_NOT_PROXY_DATA_TO_FRAMES__ !== true
  ) {
    globalThis.addEventListener('data', (e) => {
      if (globalThis.frames.length) {
        for (let i = 0; i < globalThis.frames.length; ++i) {
          try {
            globalThis.frames[i].postMessage(
              {
                __runtime_frame_data: e.detail
              },
              '*'
            )
          } catch (err) {
            console.error(err)
          }
        }
      }
    })
  }

  if (
    globalThis.window &&
    globalThis.__RUNTIME_SHOULD_NOT_PROXY_BROADCAST_CHANNEL_MESSAGES_TO_FRAMES__ !==
      true
  ) {
    globalThis.addEventListener('broadcastchannelmessage', (e) => {
      if (globalThis.frames.length) {
        for (let i = 0; i < globalThis.frames.length; ++i) {
          try {
            globalThis.frames[i].postMessage(
              {
                __runtime_frame_broadcast_channel_message: e.detail
              },
              '*'
            )
          } catch (err) {
            console.error(err)
          }
        }
      }
    })
  }
})

// symbolic globals
// Queued response bodies form a runtime-wide ordered transport. Fetch one body
// at a time because the native bridge exposes a single response queue shared
// by the top-level scope and all workers.
globals.register('RuntimeQueuedResponses', new RuntimeQueuedResponses())
globals.register(
  'RuntimeExecution',
  new asyncHooks.CoreAsyncResource('RuntimeExecution')
)

// prevent further construction if this class is indirectly referenced
RuntimeQueuedResponses.prototype.constructor = IllegalConstructor
Object.defineProperty(globalThis, '__globals', {
  enumerable: false,
  configurable: false,
  value: globals
})

ipc.send('platform.event', 'runtimeinit').then(() => {
  globals.get('RuntimeReadyPromiseResolvers')?.resolve()
}, reportError)

export default {
  location
}
