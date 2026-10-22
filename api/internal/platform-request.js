import ipc from '../ipc.js'
import { randomBytes } from '../crypto.js'
import { AbortError } from '../errors.js'

/**
 * Runs a bounded document-owned Android operation and cancels its native work
 * when the caller aborts. Native deadlines also release callers that disappear.
 * @param {string} feature
 * @param {string} action
 * @param {object} options
 * @param {AbortSignal} [signal]
 * @returns {Promise<any>}
 */
export async function platformRequest (feature, action, options, signal) {
  if (signal?.aborted) throw new AbortError()
  const id = randomBytes(16).toString('hex')
  const documentToken = Reflect.get(globalThis, '__oroNativeDocumentToken') ?? ''
  const abort = () => {
    ipc.request('runtime.platform', {
      feature: 'cancel', action: 'cancel', id: randomBytes(16).toString('hex'), documentToken, options: JSON.stringify({ id })
    }).catch(() => {})
  }
  signal?.addEventListener('abort', abort, { once: true })
  try {
    const { data, err } = await ipc.request('runtime.platform', {
      feature, action, id, documentToken, options: JSON.stringify(options)
    }, { signal, timeout: 95000 })
    if (signal?.aborted) throw new AbortError()
    if (err) throw err
    return data
  } finally {
    signal?.removeEventListener('abort', abort)
  }
}

export default platformRequest
