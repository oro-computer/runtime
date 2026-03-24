import { ALL, ADDRCONFIG, V4MAPPED } from './constants.js'

const FAMILY_ALIAS = {
  ipv4: 4,
  ipv6: 6
}

/**
 * Normalizes options for dns.lookup style APIs.
 * @param {(number|string|object)=} input
 * @returns {{ family: 0|4|6, hints: number, all: boolean, verbatim: boolean }}
 * @ignore
 */
export function normalizeLookupOptions (input) {
  if (typeof input === 'number' || typeof input === 'string') {
    input = { family: input }
  } else if (input == null) {
    input = {}
  } else if (typeof input !== 'object') {
    throw new TypeError(
      'The "options" argument must be of type object or number'
    )
  }

  const options = {
    family: 0,
    hints: 0,
    all: false,
    verbatim: false
  }

  if ('family' in input) {
    options.family = normalizeFamily(input.family)
  }

  if ('hints' in input && input.hints != null) {
    options.hints = normalizeHints(input.hints)
  }

  if ('all' in input && input.all != null) {
    options.all = normalizeBooleanOption('all', input.all)
  }

  if ('verbatim' in input && input.verbatim != null) {
    options.verbatim = normalizeBooleanOption('verbatim', input.verbatim)
  }

  return options
}

/**
 * Creates a Node.js compatible getaddrinfo error.
 * @param {string} hostname
 * @param {Error|object|null} cause
 * @returns {Error}
 * @ignore
 */
export function createLookupError (hostname, cause) {
  const code = getErrorCode(cause)
  const err = new Error(`getaddrinfo ${code} ${hostname}`)
  err.code = code
  err.errno = code
  err.syscall = 'getaddrinfo'
  err.hostname = hostname

  if (cause && typeof cause === 'object') {
    if (cause !== err) {
      err.cause = cause
    }

    for (const key in cause) {
      if (key === 'message' || key === 'stack' || key === 'cause') continue
      try {
        if (typeof err[key] === 'undefined') {
          err[key] = cause[key]
        }
      } catch {}
    }
  }

  return err
}

function normalizeFamily (family) {
  if (family == null) return 0

  if (typeof family === 'string') {
    const numeric = Number(family)
    if (Number.isInteger(numeric)) {
      family = numeric
    } else {
      const alias = FAMILY_ALIAS[family.toLowerCase()]
      if (alias) {
        family = alias
      } else {
        throw new TypeError(
          'The "options.family" property must be 4, 6, 0, "IPv4", or "IPv6"'
        )
      }
    }
  }

  if (!Number.isInteger(family)) {
    throw new TypeError('The "options.family" property must be an integer')
  }

  if (family === 0 || family === 4 || family === 6) {
    return family
  }

  throw new RangeError('The "options.family" property must be 0, 4, or 6')
}

function normalizeHints (hints) {
  if (typeof hints === 'string' && hints.trim().length > 0) {
    const numeric = Number(hints.trim())
    if (Number.isInteger(numeric)) {
      hints = numeric
    }
  }

  if (!Number.isInteger(hints)) {
    throw new TypeError('The "options.hints" property must be of type integer')
  }

  const supported = ADDRCONFIG | V4MAPPED | ALL
  if ((hints & ~supported) !== 0) {
    throw new RangeError(
      'The "options.hints" property contains unsupported flags'
    )
  }

  return hints
}

function normalizeBooleanOption (name, value) {
  if (typeof value !== 'boolean') {
    throw new TypeError(
      `The "options.${name}" property must be of type boolean`
    )
  }

  return value
}

function getErrorCode (cause) {
  if (cause && typeof cause === 'object') {
    if (typeof cause.code === 'string' && cause.code.length > 0) {
      return cause.code
    }

    if (typeof cause.errno === 'string' && cause.errno.length > 0) {
      return cause.errno
    }
  }

  return 'ENOTFOUND'
}
