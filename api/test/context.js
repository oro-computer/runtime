import console from '../console.js'
import process from '../process.js'

import '../application.js'

let finishing = false
let isInitialized = false

function shouldSuppressAutoExit () {
  return process.env.ORO_TEST_NO_AUTO_EXIT === '1'
}

export default function (GLOBAL_TEST_RUNNER) {
  if (isInitialized) return

  if (typeof globalThis?.addEventListener === 'function') {
    globalThis.addEventListener('error', onerror)
    globalThis.addEventListener('messageerror', onerror)
    globalThis.addEventListener('unhandledrejection', onerror)
  }

  GLOBAL_TEST_RUNNER.onFinish(({ fail }) => {
    if (!finishing) {
      finishing = true
      if (!shouldSuppressAutoExit()) {
        setTimeout(() => {
          process.exit(fail > 0 ? 1 : 0)
        }, 1024) // give app time to print TAP output
      }
    }
  })

  isInitialized = true

  function onerror (e) {
    const err = e.error || e.stack || e.reason || e.message || e
    if (err.ignore || err[Symbol.for('oro.runtime.test.error.ignore')]) return
    if (globalThis.RUNTIME_TEST_FILENAME || GLOBAL_TEST_RUNNER.length > 0) {
      console.error(err)
    }

    if (finishing || shouldSuppressAutoExit()) {
      return
    }

    if (globalThis.RUNTIME_TEST_FILENAME || GLOBAL_TEST_RUNNER.length > 0) {
      process.nextTick(() => {
        process.exit(1)
      })
    }
  }
}
