import { Deferred, AsyncContext } from '../async.js'
import { Buffer } from '../buffer.js'
import process from '../process.js'
import assert from '../assert.js'

/**
 * @typedef {{
 *   Connection: typeof import('../http.js').Connection,
 *   globalAgent: import('../http.js').Agent,
 *   IncomingMessage: typeof import('../http.js').IncomingMessage,
 *   ServerResponse: typeof import('../http.js').ServerResponse,
 *   STATUS_CODES: object,
 *   METHODS: string[]
 * }} HTTPModuleInterface
 */

/**
 * An abstract base class for an HTTP server adapter.
 */
export class ServerAdapter extends EventTarget {
  #server = null
  #context = new AsyncContext.Variable()
  #httpInterface = null

  /**
   * `ServerAdapter` class constructor.
   * @ignore
   * @param {import('../http.js').Server} server
   * @param {HTTPModuleInterface} httpInterface
   */
  constructor (server, httpInterface) {
    super()

    this.#server = server
    this.#httpInterface = httpInterface
  }

  /**
   * A readonly reference to the underlying HTTP(S) server
   * for this adapter.
   * @type {import('../http.js').Server}
   */
  get server () {
    return this.#server
  }

  /**
   * A readonly reference to the underlying HTTP(S) module interface
   * for creating various HTTP module class objects.
   * @type {HTTPModuleInterface}
   */
  get httpInterface () {
    return this.#httpInterface
  }

  /**
   * A readonly reference to the `AsyncContext.Variable` associated with this
   * `ServerAdapter` instance.
   */
  get context () {
    return this.#context
  }

  /**
   * Called when the adapter should destroy itself.
   * @abstract
   */
  async destroy () {}
}

/**
 * An HTTP adapter for running an HTTP server in a Service Worker that uses the
 * "fetch" event for the request and response lifecycle.
 *
 * @event ServiceWorkerServerAdapter#install
 * @type {Event}
 * Emitted when the Service Worker 'install' event fires.
 *
 * @event ServiceWorkerServerAdapter#activate
 * @type {Event}
 * Emitted when the Service Worker 'activate' event fires.
 */
export class ServiceWorkerServerAdapter extends ServerAdapter {
  /**
   * `ServiceWorkerServerAdapter` class constructor.
   * @ignore
   * @param {import('../http.js').Server} server
   * @param {HTTPModuleInterface} httpInterface
   */
  constructor (server, httpInterface) {
    assert(
      globalThis.isServiceWorkerScope,
      'The ServiceWorkerServerAdapter can only be used in a ServiceWorker scope'
    )

    super(server, httpInterface)

    this.onInstall = this.onInstall.bind(this)
    this.onActivate = this.onActivate.bind(this)
    this.onFetch = this.onFetch.bind(this)

    globalThis.addEventListener('install', this.onInstall, { once: true })
    globalThis.addEventListener('activate', this.onActivate, { once: true })
    globalThis.addEventListener('fetch', this.onFetch)
  }

  /**
   * Called when the adapter should destroy itself.
   */
  async destroy () {
    globalThis.removeEventListener('install', this.onInstall, { once: true })
    globalThis.removeEventListener('activate', this.onActivate, { once: true })
    globalThis.removeEventListener('fetch', this.onFetch)
  }

  /**
   * Handles the 'install' service worker event.
   * @ignore
   * @param {import('../service-worker/events.js').ExtendableEvent} event
   */
  async onInstall (event) {
    // eslint-disable-next-line
    void event
    globalThis.skipWaiting()
    this.dispatchEvent(new Event('install'))
  }

  /**
   * Handles the 'activate' service worker event.
   * @ignore
   * @param {import('../service-worker/events.js').ExtendableEvent} event
   */
  async onActivate (event) {
    // eslint-disable-next-line
    void event
    globalThis.clients.claim()
    this.dispatchEvent(new Event('activate'))
  }

  /**
   * Handles the 'fetch' service worker event.
   * @ignore
   * @param {import('../service-worker/events.js').FetchEvent}
   */
  async onFetch (event) {
    if (this.server.closed) {
      return
    }

    const url = new URL(event.request.url)

    // allow port to be ignored in request
    // this could be dangerous as it would lead to a race in request responses
    // if there are multiple HTTP server instances in a single service worker
    const ignorePortCheck =
      process.env.ORO_HTTP_ADAPTER_SERVICE_WORKER_IGNORE_PORT_CHECK ||
      process.env.ORO_RUNTIME_HTTP_ADAPTER_SERVICE_WORKER_IGNORE_PORT_CHECK

    if (!ignorePortCheck) {
      if (this.server.port !== 0 && url.port !== this.server.port) {
        return
      }
    }

    if (this.server.host !== '0.0.0.0' && this.server.host !== '*') {
      // the host MUST be checked and validated if not configured for
      // ALL interfaces or uses a special wildcard token ('*')
      if (this.server.host && url.hostname !== this.server.host) {
        return
      }
    }

    if (this.server.connections.length >= this.server.maxConnections) {
      event.respondWith(new Response())
      return
    }

    const deferred = new Deferred()
    event.respondWith(deferred.promise)

    const incomingMessage = new this.httpInterface.IncomingMessage({
      complete: !/post|put/i.test(event.request.method),
      headers: event.request.headers,
      method: event.request.method,
      server: this.server,
      url: event.request.url
    })

    incomingMessage.event = event

    const serverResponse = new this.httpInterface.ServerResponse({
      request: incomingMessage,
      server: this.server
    })

    const connection = new this.httpInterface.Connection(
      this.server,
      incomingMessage,
      serverResponse
    )

    this.server.resource.runInAsyncScope(() => {
      this.context.run(
        { connection, incomingMessage, serverResponse, event },
        () => {
          incomingMessage.context.run({ event }, () => {
            this.server.emit('connection', connection)
            this.server.emit('request', incomingMessage, serverResponse, event)
          })
        }
      )
    })

    if (/post|put/i.test(event.request.method)) {
      const { highWaterMark } = incomingMessage._readableState
      const requestArrayBuffer = await event.request.arrayBuffer()
      const buffer = Buffer.from(requestArrayBuffer)
      for (let i = 0; i < buffer.byteLength; i += highWaterMark) {
        incomingMessage.push(buffer.slice(i, i + highWaterMark))
      }
    }

    // Stream response body as chunks are written
    let stream = null
    let controller = null
    let responseResolved = false
    let lastIndex = 0

    function resolveResponseStream () {
      if (responseResolved) return
      responseResolved = true
      const init = {
        status: serverResponse.statusCode,
        statusText: serverResponse.statusText,
        headers: serverResponse.headers
      }
      if (!stream) {
        stream = new ReadableStream({
          start (c) {
            controller = c
          }
        })
      }
      deferred.resolve(new Response(stream, init))
    }

    serverResponse.on('writebuffer', () => {
      if (!stream) {
        stream = new ReadableStream({
          start (c) {
            controller = c
          }
        })
      }
      if (!responseResolved) resolveResponseStream()
      const buffers = serverResponse.buffers
      while (lastIndex < buffers.length) {
        controller.enqueue(buffers[lastIndex++])
      }
    })

    serverResponse.on('finish', () => {
      if (!responseResolved) {
        // No body written; respond with empty body
        deferred.resolve(
          new Response(new Uint8Array(0), {
            status: serverResponse.statusCode,
            statusText: serverResponse.statusText,
            headers: serverResponse.headers
          })
        )
        return
      }
      if (controller) controller.close()
    })
  }
}

export default {
  ServerAdapter,
  ServiceWorkerServerAdapter
}
