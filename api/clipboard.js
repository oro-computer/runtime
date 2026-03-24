/**
 * @module clipboard
 *
 * Cross-environment helpers for interacting with the system clipboard.
 * In browser contexts this delegates to the standard `navigator.clipboard`
 * API, falling back to a temporary textarea when direct access is not
 * available. In non-browser runtimes the helpers reject with a descriptive
 * error so callers can present a friendly message.
 */

const hasNavigatorClipboard =
  typeof navigator !== 'undefined' &&
  navigator !== null &&
  typeof navigator.clipboard !== 'undefined'

const supportsNavigatorWrite =
  hasNavigatorClipboard && typeof navigator.clipboard.writeText === 'function'

const supportsNavigatorRead =
  hasNavigatorClipboard && typeof navigator.clipboard.readText === 'function'

/**
 * Normalise any input into a string. `null`/`undefined` collapse to an
 * empty string to match the behaviour of the native Clipboard API.
 * @param {any} value
 * @returns {string}
 */
function normaliseText (value) {
  if (value === null || value === undefined) {
    return ''
  }
  return String(value)
}

/**
 * Attempt to write text to the clipboard using a temporary textarea node.
 * This is the traditional fallback when the async Clipboard API is not
 * available but the document still allows `execCommand('copy')`.
 * @param {string} text
 * @returns {Promise<void>}
 */
async function fallbackWrite (text) {
  if (typeof document === 'undefined') {
    throw new Error('Clipboard write is not available in this environment')
  }

  const textarea = document.createElement('textarea')
  textarea.value = text
  textarea.setAttribute('readonly', '')
  textarea.style.position = 'absolute'
  textarea.style.left = '-9999px'

  document.body.appendChild(textarea)
  const selection = document.getSelection()
  const range =
    selection && selection.rangeCount > 0 ? selection.getRangeAt(0) : null

  textarea.select()

  let succeeded = false
  try {
    succeeded = document.execCommand('copy')
  } catch {
    succeeded = false
  }

  textarea.remove()
  if (range && selection) {
    selection.removeAllRanges()
    selection.addRange(range)
  }

  if (!succeeded) {
    throw new Error('Failed to copy text to clipboard')
  }
}

/**
 * Write a string to the system clipboard.
 * @param {string} text
 * @returns {Promise<void>}
 */
export async function writeText (text) {
  const normalised = normaliseText(text)

  if (supportsNavigatorWrite) {
    await navigator.clipboard.writeText(normalised)
    return
  }

  await fallbackWrite(normalised)
}

/**
 * Read the current text contents from the system clipboard.
 * @returns {Promise<string>}
 */
export async function readText () {
  if (supportsNavigatorRead) {
    return navigator.clipboard.readText()
  }
  throw new Error('Clipboard read is not available in this environment')
}

/**
 * @returns {boolean} True when clipboard write operations are supported.
 */
export function canWriteText () {
  return supportsNavigatorWrite || typeof document !== 'undefined'
}

/**
 * @returns {boolean} True when clipboard read operations are supported.
 */
export function canReadText () {
  return supportsNavigatorRead
}

export default {
  writeText,
  readText,
  canWriteText,
  canReadText
}
