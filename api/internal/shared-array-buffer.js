/**
 * Internal `SharedArrayBuffer` polyfill.
 *
 * Provides a minimal, non-shared implementation so modules that expect
 * `globalThis.SharedArrayBuffer` to exist (for example, the URL parser)
 * can safely introspect it without throwing at import time.
 *
 * This does **not** implement shared memory semantics.
 * @ignore
 */
let sharedArrayBufferPolyfillWarningEmitted = false

function warnSharedArrayBufferPolyfillUsage () {
  if (sharedArrayBufferPolyfillWarningEmitted) {
    return
  }

  sharedArrayBufferPolyfillWarningEmitted = true

  const message = [
    '[oro]',
    'SharedArrayBuffer is not available in this runtime.',
    'Using a polyfill that does not provide shared-memory semantics.'
  ].join(' ')

  if (globalThis.console && typeof globalThis.console.warn === 'function') {
    globalThis.console.warn(message)
    return
  }

  if (typeof globalThis.reportError === 'function') {
    globalThis.reportError(new Error(message))
  }
}

let SharedArrayBufferPolyfill = globalThis.SharedArrayBuffer

if (typeof SharedArrayBufferPolyfill !== 'function') {
  const byteLengthDescriptor = Object.getOwnPropertyDescriptor(
    ArrayBuffer.prototype,
    'byteLength'
  )

  class PolyfilledSharedArrayBuffer extends ArrayBuffer {
    constructor (...args) {
      super(...args)
      warnSharedArrayBufferPolyfillUsage()
    }

    get growable () {
      return false
    }
  }

  if (byteLengthDescriptor) {
    Object.defineProperty(
      PolyfilledSharedArrayBuffer.prototype,
      'byteLength',
      byteLengthDescriptor
    )
  }

  SharedArrayBufferPolyfill = PolyfilledSharedArrayBuffer

  try {
    Object.defineProperty(globalThis, 'SharedArrayBuffer', {
      configurable: true,
      enumerable: false,
      writable: true,
      value: SharedArrayBufferPolyfill
    })
  } catch {
    // istanbul ignore next
    globalThis.SharedArrayBuffer = SharedArrayBufferPolyfill
  }
}

/**
 * @ignore
 */
export default SharedArrayBufferPolyfill
