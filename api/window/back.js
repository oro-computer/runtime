/**
 * @module window/back
 * Android application Back interception for the current interactive document.
 * Native IME dismissal and fullscreen exit take priority. An unhandled request
 * follows WebView history, closes a secondary window or backgrounds the root.
 */
import request from '../internal/platform-request.js'

let updates = Promise.resolve()
let previousRemoval = null

/**
 * @typedef {object} BackRequest
 * @property {number} deadlineMs Native fallback deadline, 750 milliseconds.
 */
/**
 * @typedef {object} BackHandlerOptions
 * @property {AbortSignal} [signal] Removes the handler when aborted.
 */

/**
 * Installs one handler for this document. Return true to consume Back, or false
 * to use native navigation. Promise results are accepted within 750 ms. Throws,
 * missing replies and stalled renderers use native navigation. Navigation or
 * renderer replacement invalidates registration; install again in the new page.
 * Application code decides modal, drawer and router priority. Gesture progress
 * remains available through the existing backgesture event.
 * @param {(request: BackRequest) => boolean|Promise<boolean>} handler
 * @param {BackHandlerOptions} [options]
 * @returns {Promise<() => Promise<void>>} Idempotent handler removal.
 */
export async function setHandler (handler, options = {}) {
  if (typeof handler !== 'function') throw new TypeError('A Back handler is required')
  if (options.signal?.aborted) throw new DOMException('The operation was aborted', 'AbortError')
  const update = updates.then(() => installHandler(handler, options))
  updates = update.then(() => undefined, () => undefined)
  return update
}

async function installHandler (handler, options) {
  if (options.signal?.aborted) throw new DOMException('The operation was aborted', 'AbortError')
  if (previousRemoval) await previousRemoval().catch(() => {})
  const registration = await request('back', 'enable', {})
  let active = true
  async function onRequest (event) {
    const value = event.detail
    if (!active || value?.registration !== registration.registration) return
    let handled = false
    try { handled = (await handler({ deadlineMs: value.deadlineMs })) === true } catch {}
    if (active) {
      await request('back', 'respond', {
        registration: registration.registration, id: value.id, handled
      }).catch(() => {})
    }
  }
  async function remove () {
    if (!active) return
    active = false
    globalThis.removeEventListener('backrequest', onRequest)
    options.signal?.removeEventListener('abort', abort)
    await request('back', 'disable', { registration: registration.registration })
  }
  function abort () { remove().catch(() => {}) }
  globalThis.addEventListener('backrequest', onRequest)
  options.signal?.addEventListener('abort', abort, { once: true })
  if (options.signal?.aborted) await remove()
  else previousRemoval = remove
  return remove
}

export default { setHandler }
