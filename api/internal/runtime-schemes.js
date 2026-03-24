export const PRIMARY_SCHEME = 'oro'
export const RUNTIME_SCHEMES = Object.freeze([PRIMARY_SCHEME])

const RUNTIME_SPECIFIER_PREFIX = `${PRIMARY_SCHEME}:`
const RUNTIME_URL_PREFIX = `${PRIMARY_SCHEME}://`

/**
 * @param {string} value
 * @returns {boolean}
 */
export function isRuntimeSpecifier (value) {
  if (typeof value !== 'string') {
    return false
  }

  return value.startsWith(RUNTIME_SPECIFIER_PREFIX)
}

/**
 * @param {string} value
 * @returns {boolean}
 */
export function isRuntimeURL (value) {
  if (typeof value !== 'string') {
    return false
  }

  return value.startsWith(RUNTIME_URL_PREFIX)
}

/**
 * Rewrites the provided specifier or URL so it uses the preferred runtime scheme.
 * When `options.url === true`, the `://` delimiter is assumed. Otherwise `:` is used.
 * @param {string} value
 * @param {{ url?: boolean, scheme?: string }} [options]
 * @returns {string}
 */
export function withPreferredRuntimeScheme (
  value,
  { url = false, scheme = PRIMARY_SCHEME } = {}
) {
  if (typeof value !== 'string' || value.length === 0) {
    return value
  }

  if (scheme !== PRIMARY_SCHEME) {
    return value
  }

  const delimiter = url ? '://' : ':'

  const prefix = `${PRIMARY_SCHEME}${delimiter}`
  if (value.startsWith(prefix)) {
    return value
  }

  return value
}

/**
 * Builds a runtime origin string (e.g., `oro://com.example.app`).
 * @param {string} bundleIdentifier
 * @param {{ scheme?: string }} [options]
 * @returns {string}
 */
export function runtimeOrigin (
  bundleIdentifier,
  { scheme = PRIMARY_SCHEME } = {}
) {
  if (typeof bundleIdentifier !== 'string' || bundleIdentifier.length === 0) {
    return ''
  }

  return `${scheme === PRIMARY_SCHEME ? scheme : PRIMARY_SCHEME}://${bundleIdentifier}`
}

/**
 * Normalizes a scheme string (with/without the trailing colon) to the runtime value.
 * Returns an empty string for unrecognised schemes.
 * @param {string} scheme
 * @returns {string}
 */
export function normalizeRuntimeScheme (scheme) {
  if (typeof scheme !== 'string' || scheme.length === 0) {
    return ''
  }

  const normalized = scheme.endsWith(':') ? scheme.slice(0, -1) : scheme

  return normalized === PRIMARY_SCHEME ? normalized : ''
}
