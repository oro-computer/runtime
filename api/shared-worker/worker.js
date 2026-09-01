/* eslint-disable import/first */
globalThis.isSharedWorkerScope = true

import { SharedWorkerMessagePort, channel } from './index.js'
import { SharedWorkerGlobalScope } from './global.js'
import { Module, createRequire } from '../module.js'
import { Environment } from '../service-worker/env.js'
import { Buffer } from '../buffer.js'
import { Cache } from '../commonjs/cache.js'
import globals from '../internal/globals.js'
import process from '../process.js'
import debug from './debug.js'
import hooks from '../hooks.js'
import state from './state.js'
import path from '../path.js'
import ipc from '../ipc.js'

import '../console.js'

export default null

Object.defineProperties(
  globalThis,
  Object.getOwnPropertyDescriptors(SharedWorkerGlobalScope.prototype)
)

export const SHARED_WORKER_READY_TOKEN = { __shared_worker_ready: true }

// state
export const module = { exports: {} }
export const connections = new Set()
let installation = null
let hasCustomReportError = false

// event listeners
hooks.onReady(onReady)
globalThis.addEventListener('message', onMessage)

// shared worker globals
globals.register('SharedWorker.state', state)
globals.register('SharedWorker.module', module)

export function onReady () {
  globalThis.postMessage(SHARED_WORKER_READY_TOKEN)
}

export async function onMessage (event) {
  const { data } = event

  if (data?.install) {
    event.stopImmediatePropagation()

    if (!installation) {
      installation = install(data.install)
    }

    try {
      await installation
    } catch {}
    return
  }

  if (data?.connect) {
    event.stopImmediatePropagation()

    if (!installation) {
      reportWorkerError(
        data.connect.id,
        new Error('SharedWorker received a connection before installation')
      )
      return
    }

    try {
      await installation
    } catch {
      return
    }

    try {
      dispatchConnection(data.connect)
    } catch (err) {
      reportWorkerError(data.connect.id, err)
    }
  }
}

async function install (info) {
  try {
    await installWorker(info)
  } catch (err) {
    state.sharedWorker.state = 'error'
    throw reportWorkerError(info?.id, err)
  }
}

async function installWorker (info) {
  const { id, scriptURL } = info
  const url = new URL(scriptURL)

  if (!url.pathname.startsWith('/oro/')) {
    // preload commonjs cache for user space server workers
    Cache.restore(['loader.status', 'loader.response'])
  }

  state.id = id
  state.sharedWorker.id = id
  state.sharedWorker.scriptURL = scriptURL
  state.sharedWorker.state = 'installing'

  Module.main.addEventListener('error', (event) => {
    if (event.error) {
      debug(event.error.stack ?? event.error.message)
    }
  })

  Object.defineProperties(globalThis, {
    require: {
      configurable: false,
      enumerable: false,
      writable: false,
      value: createRequire(scriptURL)
    },

    origin: {
      configurable: false,
      enumerable: true,
      writable: false,
      value: url.origin
    },

    __dirname: {
      configurable: false,
      enumerable: false,
      writable: false,
      value: path.dirname(url.pathname)
    },

    __filename: {
      configurable: false,
      enumerable: false,
      writable: false,
      value: url.pathname
    },

    module: {
      configurable: false,
      enumerable: false,
      writable: false,
      value: module
    },

    exports: {
      configurable: false,
      enumerable: false,
      get: () => module.exports
    },

    process: {
      configurable: false,
      enumerable: false,
      get: () => process
    },

    Buffer: {
      configurable: false,
      enumerable: false,
      get: () => Buffer
    },

    global: {
      configurable: false,
      enumerable: false,
      get: () => globalThis
    }
  })

  // define the actual location of the worker, not `blob:...`
  globalThis.RUNTIME_WORKER_LOCATION = scriptURL
  state.env = await Environment.open({
    type: 'sharedWorker',
    scope: String(id)
  })
  // import module, which could be ESM, CommonJS,
  // or a simple SharedWorker
  const result = await import(scriptURL)

  if (typeof module.exports === 'function') {
    module.exports = {
      default: module.exports
    }
  } else {
    Object.assign(module.exports, result)
  }

  if (module.exports.default && typeof module.exports.default === 'object') {
    if (typeof module.exports.default.connect === 'function') {
      state.connect = module.exports.default.connect.bind(module.exports.default)
    }
  } else if (typeof module.exports.connect === 'function') {
    state.connect = module.exports.connect.bind(module.exports)
  }

  if (module.exports.default && typeof module.exports.default === 'object') {
    if (typeof module.exports.default.reportError === 'function') {
      state.reportError = module.exports.default.reportError.bind(
        module.exports.default
      )
      hasCustomReportError = true
    }
  } else if (typeof module.exports.reportError === 'function') {
    state.reportError = module.exports.reportError.bind(module.exports)
    hasCustomReportError = true
  }

  if (typeof state.connect === 'function') {
    globalThis.addEventListener('connect', async (event) => {
      try {
        const promise = state.connect(state.env, event.data, event.ports[0])
        await promise
      } catch (err) {
        reportWorkerError(state.id, err)
      }
    })
  }

  state.sharedWorker.state = 'installed'

  debug(
    '[%s]: SharedWorker (%s) installed',
    new URL(scriptURL).pathname.replace(/^\/oro\//, 'oro:'),
    state.id
  )

  channel.postMessage({ installed: { id: state.id } })
}

function reportWorkerError (id, value) {
  const error = value instanceof Error ? value : new Error(String(value))

  try {
    channel.postMessage({
      error: { id, message: error.message }
    })
  } catch {}

  try {
    debug(error.stack ?? error.message)
  } catch {}

  if (hasCustomReportError) {
    try {
      state.reportError(error)
    } catch {}
  }

  return error
}

function dispatchConnection (data) {
  const connection = ipc.inflateIPCMessageTransfers(
    data,
    new Map(
      Object.entries({
        SharedWorkerMessagePort
      })
    )
  )

  for (const entry of connections) {
    if (entry.id === connection.port.id) {
      entry.close(false)
      connections.delete(entry)
      break
    }
  }

  connections.add(connection.port)
  const connectEvent = new MessageEvent('connect', { data: connection })
  Object.defineProperty(connectEvent, 'ports', {
    configurable: false,
    writable: false,
    value: Object.seal(Object.freeze([connection.port]))
  })

  debug(
    '[%s]: SharedWorker (%s) connection from client (%s/%s) at %s',
    new URL(state.sharedWorker.scriptURL).pathname.replace(/^\/oro\//, 'oro:'),
    state.id,
    connection.client.id,
    [connection.client.frameType.replace('none', ''), connection.client.type]
      .filter(Boolean)
      .join('-'),
    connection.client.location
  )
  globalThis.dispatchEvent(connectEvent)
}
