/* global XMLHttpRequest, ErrorEvent */
import application from '../application.js'
import serialize from '../internal/serialize.js'
import location from '../location.js'
import process from '../process.js'
import crypto from '../crypto.js'
import client from '../application/client.js'
import ipc from '../ipc.js'

let contextWindow = null
let contextWindowRequest = null

export const SHARED_WORKER_WINDOW_TITLE = 'oro:shared-worker'
export const SHARED_WORKER_WINDOW_PATH = '/oro/shared-worker/index.html'

export const channel = new BroadcastChannel('oro.runtime.sharedWorker')
export const workers = new Map()
const workerReferences = new Map()

channel.addEventListener('message', (event) => {
  if (event.data?.error?.id !== null && event.data?.error?.id !== undefined) {
    const refs = workerReferences.get(event.data.error.id)
    if (refs) {
      const error = new Error(event.data.error.message)

      for (const ref of refs) {
        const worker = ref.deref()
        if (!worker) {
          refs.delete(ref)
        } else {
          worker.dispatchEvent(
            new ErrorEvent('error', {
              error,
              message: error.message
            })
          )
        }
      }

      if (refs.size === 0) {
        workerReferences.delete(event.data.error.id)
        workers.delete(event.data.error.id)
      } else {
        updateWorkerReference(event.data.error.id, refs)
      }
    }
  }
})

// Register before the first await so early module failures reach every
// SharedWorker object using the same URL and name.
function registerWorker (sharedWorker) {
  let refs = workerReferences.get(sharedWorker.id)
  if (!refs) {
    refs = new Set()
    workerReferences.set(sharedWorker.id, refs)
  }

  const ref = new WeakRef(sharedWorker)
  refs.add(ref)
  workers.set(sharedWorker.id, ref)
  return { ref, refs }
}

function unregisterWorker (sharedWorker, registration) {
  const refs = workerReferences.get(sharedWorker.id)
  if (refs === registration.refs) {
    refs.delete(registration.ref)

    if (refs.size === 0) {
      workerReferences.delete(sharedWorker.id)
      workers.delete(sharedWorker.id)
    } else {
      updateWorkerReference(sharedWorker.id, refs)
    }
  }
}

function updateWorkerReference (id, refs) {
  const current = workers.get(id)
  if (current && refs.has(current) && current.deref()) {
    return
  }

  for (const ref of refs) {
    if (ref.deref()) {
      workers.set(id, ref)
      return
    }
  }

  workerReferences.delete(id)
  workers.delete(id)
}

export async function init (sharedWorker, options) {
  const registration = registerWorker(sharedWorker)

  try {
    const currentWindow = await application.getCurrentWindow()
    const window = await getContextWindow()
    const port = serialize(
      ipc.findIPCMessageTransfers(new Set(), sharedWorker.channel.port2)
    )

    // Serialize before marking the sending endpoint as transferred, then
    // activate the local relay before the receiving worker can reply.
    ipc.IPCMessagePort.transfer(sharedWorker.channel.port2)

    await currentWindow.send({
      event: 'connect',
      window: window.index,
      value: {
        connect: {
          scriptURL: options.scriptURL,
          client: client.toJSON(),
          name: options.name,
          port,
          id: sharedWorker.id
        }
      }
    })
  } catch (err) {
    unregisterWorker(sharedWorker, registration)
    try {
      sharedWorker.channel.port1.close()
      sharedWorker.channel.port2.close()
    } catch {}
    throw err
  }
}

export class SharedWorkerMessagePort extends ipc.IPCMessagePort {
  [Symbol.for('oro.runtime.serialize')] () {
    return {
      ...super[Symbol.for('oro.runtime.serialize')](),
      __type__: 'SharedWorkerMessagePort'
    }
  }
}

export class SharedWorker extends EventTarget {
  #id = null
  #ready = null
  #onerror = null
  #channel = new ipc.IPCMessageChannel()

  /**
   * `SharedWorker` class constructor.
   * @param {string|URL|Blob} aURL
   * @param {string|object=} [nameOrOptions]
   */
  constructor (aURL, nameOrOptions = null) {
    if (typeof aURL === 'string' && !URL.canParse(aURL, location.href)) {
      const blob = new Blob([aURL], { type: 'text/javascript' })
      aURL = URL.createObjectURL(blob).toString()
    } else if (String(aURL).startsWith('blob:')) {
      const request = new XMLHttpRequest()
      request.open('GET', String(aURL), false)
      request.send()

      const blob = new Blob([request.responseText || request.response], {
        type: 'application/javascript'
      })

      aURL = URL.createObjectURL(blob)
    }

    const url = new URL(aURL, location.origin)
    const name =
      typeof nameOrOptions === 'string'
        ? nameOrOptions
        : typeof nameOrOptions?.name === 'string'
          ? nameOrOptions.name
          : null
    const id = crypto.murmur3(`${url.toString()}\0${name ?? ''}`)

    // @ts-ignore
    super(url.toString(), nameOrOptions)

    this.#id = id
    this.#ready = init(this, {
      scriptURL: url.toString(),
      name
    })
  }

  get onerror () {
    return this.#onerror
  }

  set onerror (onerror) {
    if (typeof this.#onerror === 'function') {
      this.removeEventListener('error', this.#onerror)
    }

    this.#onerror = null

    if (typeof onerror === 'function') {
      this.#onerror = onerror
      this.addEventListener('error', onerror)
    }
  }

  get ready () {
    return this.#ready
  }

  get channel () {
    return this.#channel
  }

  get port () {
    return this.#channel.port1
  }

  get id () {
    return this.#id
  }
}

/**
 * Gets the SharedWorker context window.
 * This function will create it if it does not already exist.
 * @return {Promise<import('./window.js').ApplicationWindow}
 */
export async function getContextWindow () {
  if (contextWindow) {
    await contextWindow.ready
    return contextWindow
  }

  if (contextWindowRequest) {
    return await contextWindowRequest
  }

  const request = initializeContextWindow()
  contextWindowRequest = request

  try {
    return await request
  } finally {
    if (contextWindowRequest === request) {
      contextWindowRequest = null
    }
  }
}

async function initializeContextWindow () {
  const windows = await application.getWindows([], { max: false })
  const url = new URL(SHARED_WORKER_WINDOW_PATH, globalThis.location.origin)
  for (const window of windows) {
    if (window.location.href === url.href) {
      contextWindow = window
      break
    }
  }

  const sharedWorkerDebug =
    process.env.ORO_SHARED_WORKER_DEBUG ||
    process.env.ORO_RUNTIME_SHARED_WORKER_DEBUG

  if (!contextWindow) {
    contextWindow = await application.createWindow({
      // @ts-ignore
      canExit: false,
      headless: !sharedWorkerDebug,
      // @ts-ignore
      debug: Boolean(sharedWorkerDebug),
      reserved: true,
      // @ts-ignore
      unique: true,
      token: url.href,
      index: -1,
      title: SHARED_WORKER_WINDOW_TITLE,
      path: SHARED_WORKER_WINDOW_PATH,
      width: '80%',
      height: '80%',
      config: {
        ...globalThis.__args.config,
        webview_watch_reload: false
      }
    })
  }

  if (!contextWindow.ready) {
    const index = contextWindow.index
    contextWindow.ready = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        channel.removeEventListener('message', onMessage)
        reject(new Error(`SharedWorker context window ${index} did not become ready`))
      }, 10_000)

      function onMessage (event) {
        if (event.data?.ready === index) {
          clearTimeout(timeout)
          channel.removeEventListener('message', onMessage)
          resolve(null)
        }
      }

      channel.addEventListener('message', onMessage)
      channel.postMessage({ probe: index })
    })
  }

  if (!sharedWorkerDebug && globalThis.__args.config.build_headless !== true) {
    await contextWindow.hide()
  }

  await contextWindow.ready
  return contextWindow
}

export default SharedWorker
