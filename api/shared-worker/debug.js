import globals from '../internal/globals.js'
import util from '../util.js'

export function debug (...args) {
  const state = globals.get('SharedWorker.state')

  const sharedWorkerDebug =
    process.env.ORO_SHARED_WORKER_DEBUG ||
    process.env.ORO_RUNTIME_SHARED_WORKER_DEBUG

  if (sharedWorkerDebug) {
    console.debug(...args)
  }

  if (args[0] instanceof Error) {
    globalThis.postMessage({
      __shared_worker_debug: [
        `[${state.sharedWorker.scriptURL}]: ${util.format(...args)}`
      ]
    })

    if (typeof state?.reportError === 'function') {
      state.reportError(args[0])
    } else if (typeof globalThis.reportError === 'function') {
      globalThis.reportError(args[0])
    }
  } else {
    globalThis.postMessage({ __shared_worker_debug: [util.format(...args)] })
  }
}

export default debug
