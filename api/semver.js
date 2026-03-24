/**
 * @module semver
 *
 * A SemVer 2.0.0 implementation backed by the native runtime. This module
 * provides helpers for parsing, comparing, incrementing, and checking ranges.
 *
 * Example:
 * ```js
 * import * as semver from 'oro:semver'
 *
 * const v = semver.parse('1.2.3-alpha.1')
 * if (!v) throw new Error('invalid')
 * console.log(v.major, v.minor, v.patch, v.prerelease)
 *
 * if (semver.satisfies('1.2.3', '>=1.0.0 <2.0.0')) {
 *   console.log('compatible')
 * }
 * ```
 */

import ipc from './ipc.js'

const WILDCARD = /^(x|\*)$/i

function isWildcard (id) {
  return id === '' || WILDCARD.test(id)
}

function splitVersionLike (input) {
  const s = String(input ?? '')
    .trim()
    .replace(/^v/i, '')
  if (!s) return []
  return s.split('.')
}

function parseNumeric (value) {
  const n = Number(value)
  return Number.isFinite(n) ? n : NaN
}

function expandXRanges (id) {
  const parts = splitVersionLike(id)
  if (parts.length === 0) return null

  const [rawMajor, rawMinor = 'x', rawPatch = 'x'] = parts

  const majorX = isWildcard(rawMajor)
  const minorX = isWildcard(rawMinor)
  const patchX = isWildcard(rawPatch)

  if (majorX) {
    // * or x or *.x etc → all versions
    return ['>=0.0.0']
  }

  const major = parseNumeric(rawMajor)
  if (!Number.isInteger(major) || major < 0) return null

  if (minorX) {
    // 1.x, 1.* → >=1.0.0 <2.0.0
    const nextMajor = major + 1
    return [`>=${major}.0.0`, `<${nextMajor}.0.0`]
  }

  const minor = parseNumeric(rawMinor)
  if (!Number.isInteger(minor) || minor < 0) return null

  if (patchX) {
    // 1.2.x → >=1.2.0 <1.3.0
    const nextMinor = minor + 1
    return [`>=${major}.${minor}.0`, `<${major}.${nextMinor}.0`]
  }

  // No wildcard; let native range handling deal with it
  return [`=${major}.${minor}.${rawPatch}`]
}

function expandTilde (id) {
  const ver = String(id).trim().replace(/^~/, '')
  const parts = splitVersionLike(ver)
  if (parts.length === 0) return null

  const [rawMajor, rawMinor = '0', rawPatch = '0'] = parts
  const major = parseNumeric(rawMajor)
  const minor = parseNumeric(rawMinor)
  const patch = parseNumeric(rawPatch)
  if (!Number.isInteger(major) || major < 0) return null
  if (!Number.isInteger(minor) || minor < 0) return null
  if (!Number.isInteger(patch) || patch < 0) return null

  // ~M        → >=M.0.0 <(M+1).0.0
  // ~M.m      → >=M.m.0 <M.(m+1).0
  // ~M.m.p    → >=M.m.p <M.(m+1).0
  const hasMinor = parts.length >= 2
  const hasPatch = parts.length >= 3

  if (!hasMinor) {
    const nextMajor = major + 1
    return [`>=${major}.0.0`, `<${nextMajor}.0.0`]
  }

  const nextMinor = minor + 1
  if (!hasPatch) {
    return [`>=${major}.${minor}.0`, `<${major}.${nextMinor}.0`]
  }

  return [`>=${major}.${minor}.${patch}`, `<${major}.${nextMinor}.0`]
}

function expandCaret (id) {
  const ver = String(id).trim().replace(/^\^/, '')
  const parts = splitVersionLike(ver)
  if (parts.length === 0) return null

  const [rawMajor, rawMinor = '0', rawPatch = '0'] = parts
  const major = parseNumeric(rawMajor)
  const minor = parseNumeric(rawMinor)
  const patch = parseNumeric(rawPatch)
  if (!Number.isInteger(major) || major < 0) return null
  if (!Number.isInteger(minor) || minor < 0) return null
  if (!Number.isInteger(patch) || patch < 0) return null

  // ^M.m.p expansion mirrors npm's semver behavior approximately:
  // - M > 0: >=M.m.p <(M+1).0.0
  // - M = 0, m > 0: >=0.m.p <0.(m+1).0
  // - M = 0, m = 0: >=0.0.p <0.0.(p+1)
  if (major > 0) {
    const nextMajor = major + 1
    return [`>=${major}.${minor}.${patch}`, `<${nextMajor}.0.0`]
  }

  if (minor > 0) {
    const nextMinor = minor + 1
    return [`>=0.${minor}.${patch}`, `<0.${nextMinor}.0`]
  }

  const nextPatch = patch + 1
  return [`>=0.0.${patch}`, `<0.0.${nextPatch}`]
}

function expandHyphen (range) {
  const match = String(range)
    .trim()
    .match(/^(.+)\s+-\s+(.+)$/)
  if (!match) return null
  const [, fromRaw, toRaw] = match

  const from = valid(fromRaw)
  const to = valid(toRaw)
  if (!from || !to) return null

  return [`>=${from}`, `<=${to}`]
}

function normalizeRange (range) {
  const input = String(range ?? '').trim()
  if (!input) return null

  const orParts = input
    .split('||')
    .map((part) => part.trim())
    .filter(Boolean)
  if (orParts.length === 0) return null

  const normalizedSets = []

  for (const part of orParts) {
    const hyphenExpanded = expandHyphen(part)
    if (hyphenExpanded) {
      normalizedSets.push(hyphenExpanded.join(' '))
      continue
    }

    const tokens = part.split(/\s+/).filter(Boolean)
    const outTokens = []

    for (const token of tokens) {
      const trimmed = token.trim()
      if (!trimmed) continue

      const opMatch = trimmed.match(/^(<=|>=|<|>|=)/)
      const op = opMatch ? opMatch[1] : ''
      const body = op ? trimmed.slice(op.length) : trimmed

      if (!op && /^[~]/.test(body)) {
        const expanded = expandTilde(body)
        if (!expanded) return null
        outTokens.push(...expanded)
      } else if (!op && body.startsWith('^')) {
        const expanded = expandCaret(body)
        if (!expanded) return null
        outTokens.push(...expanded)
      } else if (!op && /[xX*]/.test(body)) {
        const expanded = expandXRanges(body)
        if (!expanded) return null
        outTokens.push(...expanded)
      } else {
        outTokens.push(trimmed)
      }
    }

    if (outTokens.length === 0) {
      return null
    }

    normalizedSets.push(outTokens.join(' '))
  }

  return normalizedSets.join(' || ')
}

function ensureResult (result, source) {
  if (result?.err) {
    throw result.err
  }

  if (!result || typeof result !== 'object') {
    throw new TypeError(`Invalid response from '${source}'`)
  }

  return result.data ?? result
}

/**
 * @typedef {object} SemVer
 * @property {number} major
 * @property {number} minor
 * @property {number} patch
 * @property {string[]} prerelease
 * @property {string[]} build
 * @property {string} version Canonical string form
 */

/**
 * Parse a semantic version string into its structured representation.
 * Returns `null` when the input is not a valid SemVer 2.0.0 version.
 *
 * @param {string} version
 * @returns {SemVer | null}
 */
export function parse (version) {
  if (typeof version !== 'string') {
    version = String(version ?? '')
  }

  const result = ipc.sendSync('semver.parse', { version })
  if (result?.err) {
    return null
  }

  const data = ensureResult(result, 'semver.parse')
  return {
    major: data.major,
    minor: data.minor,
    patch: data.patch,
    prerelease: Array.isArray(data.prerelease) ? data.prerelease.slice() : [],
    build: Array.isArray(data.build) ? data.build.slice() : [],
    version: String(data.version)
  }
}

/**
 * Returns the canonical version string when `version` is valid, otherwise `null`.
 *
 * @param {string} version
 * @returns {string | null}
 */
export function valid (version) {
  const v = parse(version)
  return v ? v.version : null
}

/**
 * Compare two semantic versions.
 *
 * @param {string} a
 * @param {string} b
 * @returns {-1|0|1}
 */
export function compare (a, b) {
  const result = ipc.sendSync('semver.compare', {
    a: String(a ?? ''),
    b: String(b ?? '')
  })
  const data = ensureResult(result, 'semver.compare')
  const value = data.result
  if (value === -1 || value === 0 || value === 1) {
    return value
  }
  throw new TypeError('Invalid comparison result from semver.compare')
}

/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function eq (a, b) {
  return compare(a, b) === 0
}

/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function neq (a, b) {
  return compare(a, b) !== 0
}

/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function lt (a, b) {
  return compare(a, b) < 0
}

/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function lte (a, b) {
  return compare(a, b) <= 0
}

/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function gt (a, b) {
  return compare(a, b) > 0
}

/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function gte (a, b) {
  return compare(a, b) >= 0
}

/**
 * Test whether a version satisfies a range expression.
 *
 * Supported range grammar includes:
 * - Simple comparators (`<`, `<=`, `>`, `>=`, `=`, or bare versions)
 * - Hyphen ranges: `1.2.3 - 2.3.4`
 * - Wildcard ranges: `1.x`, `1.2.x`, `1`, `1.2`, `*`
 * - Tilde ranges: `~1.2.3`, `~1.2`, `~1`
 * - Caret ranges: `^1.2.3`, `^0.2.3`, `^0.0.3`
 * - `||` for OR between sets of comparators
 *
 * @param {string} version
 * @param {string} range
 * @returns {boolean}
 */
export function satisfies (version, range) {
  const normalizedRange = normalizeRange(range)
  if (!normalizedRange) {
    return false
  }

  const result = ipc.sendSync('semver.satisfies', {
    version: String(version ?? ''),
    range: normalizedRange
  })

  if (result?.err) {
    return false
  }

  const data = ensureResult(result, 'semver.satisfies')
  return Boolean(data.result)
}

/**
 * Increment a version according to the given release type.
 *
 * Release types:
 * - 'major', 'minor', 'patch'
 * - 'premajor', 'preminor', 'prepatch', 'prerelease'
 *
 * When a pre* release type is used, `preid` (when provided) becomes the
 * pre-release identifier (e.g., `beta` -> `1.2.3-beta.0`). If omitted,
 * `rc` is used by default.
 *
 * Returns `null` when the input version is invalid.
 *
 * @param {string} version
 * @param {'major'|'minor'|'patch'|'premajor'|'preminor'|'prepatch'|'prerelease'} release
 * @param {string} [preid]
 * @returns {string | null}
 */
export function inc (version, release, preid) {
  const result = ipc.sendSync('semver.inc', {
    version: String(version ?? ''),
    release: String(release ?? ''),
    preid: preid == null ? '' : String(preid)
  })

  if (result?.err) {
    return null
  }

  const data = ensureResult(result, 'semver.inc')
  return typeof data.version === 'string' ? data.version : null
}

/**
 * Clean a version by returning its canonical form or `null` when invalid.
 *
 * @param {string} version
 * @returns {string | null}
 */
export function clean (version) {
  return valid(version)
}

/**
 * Extract the major component of a version or `NaN` when invalid.
 * @param {string} version
 * @returns {number}
 */
export function major (version) {
  const v = parse(version)
  return v ? v.major : NaN
}

/**
 * Extract the minor component of a version or `NaN` when invalid.
 * @param {string} version
 * @returns {number}
 */
export function minor (version) {
  const v = parse(version)
  return v ? v.minor : NaN
}

/**
 * Extract the patch component of a version or `NaN` when invalid.
 * @param {string} version
 * @returns {number}
 */
export function patch (version) {
  const v = parse(version)
  return v ? v.patch : NaN
}

/**
 * Returns the prerelease components of a version or `null` when invalid
 * or when the version has no prerelease identifiers.
 *
 * @param {string} version
 * @returns {string[] | null}
 */
export function prerelease (version) {
  const v = parse(version)
  if (!v) return null
  return v.prerelease.length > 0 ? v.prerelease.slice() : null
}

/**
 * Validate and normalize a range expression. Returns the normalized
 * comparator-based range or `null` when invalid.
 *
 * @param {string} range
 * @returns {string | null}
 */
export function validRange (range) {
  const normalized = normalizeRange(range)
  return normalized || null
}

const api = {
  parse,
  valid,
  clean,
  compare,
  eq,
  neq,
  lt,
  lte,
  gt,
  gte,
  satisfies,
  validRange,
  major,
  minor,
  patch,
  prerelease,
  inc
}

export default api
