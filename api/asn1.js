import ipc from './ipc.js'

/**
 * @module asn1
 */

/**
 * Describes options when parsing ASN.1 inputs.
 * @typedef {Object} Asn1ParseOptions
 * @property {boolean} [lexerDebug=false] Emits lexer debug information inside the native parser.
 * @property {boolean} [includeSourceText=false] When true, the original input is echoed back in the response.
 * @property {number} [maxDepth=64] Controls how deep nested type/value trees are traversed when building the JSON representation.
 */

/**
 * Represents a parsed ASN.1 definition module.
 * @typedef {Object} Asn1Module
 * @property {string} [name]
 * @property {string} [oid]
 * @property {Record<string, boolean>} [flags]
 * @property {Asn1Import[]} [imports]
 * @property {Asn1Export[]} [exports]
 * @property {Asn1Expression[]} [members]
 * @property {string} [sourceFile]
 */

/**
 * @typedef {Object} Asn1Import
 * @property {string} [module]
 * @property {Array<{ identifier?: string, metaType: string, exprType: string }>} [symbols]
 * @property {string} [kind]
 */

/**
 * @typedef {Object} Asn1Export
 * @property {string} [identifier]
 * @property {string} metaType
 * @property {string} exprType
 */

/**
 * @typedef {Object} Asn1Expression
 * @property {string} metaType
 * @property {string} exprType
 * @property {string} [identifier]
 * @property {number} [line]
 * @property {boolean} [unique]
 * @property {string} [reference]
 * @property {Asn1Value} [value]
 * @property {Asn1Constraint|Asn1Constraint[]} [constraints]
 * @property {Asn1Expression[]} [members]
 * @property {string[]} [markers]
 * @property {{ description?: string, class?: string, mode?: string, value?: string }} [tag]
 * @property {boolean} [truncated]
 */

/**
 * @typedef {Object} Asn1Constraint
 * @property {string} type
 * @property {string} presence
 * @property {number} [line]
 * @property {Asn1Value} [value]
 * @property {{ start?: Asn1Value, stop?: Asn1Value }} [range]
 * @property {Asn1Constraint[]} [elements]
 * @property {boolean} [truncated]
 */

/**
 * @typedef {Object} Asn1Value
 * @property {string} type
 * @property {string} [repr]
 * @property {string} [integer]
 * @property {number} [real]
 * @property {string} [string]
 * @property {string} [reference]
 * @property {number[]} [bytes]
 * @property {number} [sizeInBits]
 * @property {Asn1Constraint|Asn1Constraint[]} [valueSet]
 */

/**
 * Validates and normalises parse options.
 * @param {Asn1ParseOptions} [options]
 * @returns {Partial<Asn1ParseOptions>}
 */
function normalizeOptions (options = {}) {
  if (options == null) return {}
  if (typeof options !== 'object') {
    throw new TypeError('options must be an object')
  }

  const normalized = {}

  if (typeof options.lexerDebug !== 'undefined') {
    normalized.lexerDebug = Boolean(options.lexerDebug)
  }

  if (typeof options.includeSourceText !== 'undefined') {
    normalized.includeSourceText = Boolean(options.includeSourceText)
  }

  if (typeof options.maxDepth !== 'undefined') {
    const depth = Number(options.maxDepth)
    if (!Number.isFinite(depth) || depth <= 0) {
      throw new TypeError('maxDepth must be a positive number')
    }
    normalized.maxDepth = Math.min(Math.floor(depth), 1024)
  }

  return normalized
}

/**
 * Ensures an IPC result matches the expected format.
 * @param {object} result
 * @param {string} source
 * @returns {any}
 */
function ensureResult (result, source) {
  if (result?.err) {
    throw result.err
  }

  if (result?.data !== undefined) {
    return result.data
  }

  if (result?.source === source) {
    return result
  }

  return result
}

/**
 * Parses an ASN.1 document provided as a string.
 * @param {string} source
 * @param {Asn1ParseOptions} [options]
 * @returns {Promise<{ modules: Asn1Module[], modulesCount: number, lexerDebug: boolean, maxDepth: number, sourceText?: string }>}
 */
export async function parse (source, options = {}) {
  if (typeof source !== 'string' || source.trim().length === 0) {
    throw new TypeError('source must be a non-empty string')
  }

  const params = normalizeOptions(options)
  params.source = source

  const result = await ipc.request('asn1.parse', params)
  return ensureResult(result, 'asn1.parse')
}

/**
 * Parses an ASN.1 document from a file path on disk.
 * @param {string} path
 * @param {Asn1ParseOptions} [options]
 * @returns {Promise<{ modules: Asn1Module[], modulesCount: number, lexerDebug: boolean, maxDepth: number, sourceText?: string }>}
 */
export async function parseFile (path, options = {}) {
  if (typeof path !== 'string' || path.trim().length === 0) {
    throw new TypeError('path must be a non-empty string')
  }

  const params = normalizeOptions(options)
  params.path = path

  const result = await ipc.request('asn1.parseFile', params)
  return ensureResult(result, 'asn1.parseFile')
}

export default Object.freeze({
  parse,
  parseFile
})
