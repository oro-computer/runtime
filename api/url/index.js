import '../internal/shared-array-buffer.js'

import { URLPattern } from './urlpattern/urlpattern.js'
import url from './url/url.js'
import qs from '../querystring.js'
import { RUNTIME_SCHEMES } from '../internal/runtime-schemes.js'

const { URL, URLSearchParams, parseURL, serializeURLOrigin, serializeHost } =
  url

for (const key in globalThis.URL) {
  if (!URL[key]) {
    URL[key] = globalThis.URL[key].bind(globalThis.URL)
  }
}

URL.resolve = resolve
URL.parse = parse
URL.format = format

if (typeof URL.canParse !== 'function') {
  URL.canParse = function canParse (...args) {
    try {
      // eslint-disable-next-line
      void new URL(...args)
      return true
    } catch {
      return false
    }
  }
}

URL.prototype[Symbol.for('oro.runtime.util.inspect.custom')] = function () {
  return [
    'URL {',
    `  protocol: ${this.protocol || null},`,
    `  username: ${this.username || null},`,
    `  password: ${this.password || null},`,
    `  hostname: ${this.hostname || null},`,
    `  pathname: ${this.pathname || null},`,
    `  search: ${this.search || null},`,
    `  hash: ${this.hash || null}`,
    '}'
  ].join('\n')
}

/**
 * @type {Set & { handlers: Set<string> }}
 */
export const protocols = new Set([
  ...RUNTIME_SCHEMES.map((scheme) => `${scheme}:`),
  'node:',
  'npm:',
  'ipc:',

  // android
  'android.resource:',
  'content:',

  // web standard & reserved
  'bitcoin:',
  'file:',
  'ftp:',
  'ftps:',
  'geo:',
  'git:',
  'http:',
  'https:',
  'im:',
  'ipfs:',
  'irc:',
  'ircs:',
  'magnet:',
  'mailto:',
  'matrix:',
  'mms:',
  'news:',
  'nntp:',
  'openpgp4fpr:',
  'sftp:',
  'sip:',
  'sms:',
  'smsto:',
  'ssh:',
  'tel:',
  'urn:',
  'webcal:',
  'wtai:',
  'xmpp:'
])

protocols.handlers = new Set()
if (globalThis.__args?.config && typeof globalThis.__args.config === 'object') {
  const protocolHandlers = String(
    globalThis.__args.config['webview_protocol-handlers'] || ''
  )
    .split(' ')
    .filter(Boolean)

  const webviewURLProtocols = String(
    globalThis.__args.config.webview_url_protocols || ''
  )
    .split(' ')
    .filter(Boolean)

  for (const value of webviewURLProtocols) {
    const scheme = value.replace(':', '')
    if (scheme) {
      protocols.add(scheme + ':')
      protocols.handlers.add(scheme)
    }
  }

  for (const value of protocolHandlers) {
    const scheme = value.replace(':', '')
    if (scheme) {
      protocols.add(scheme + ':')
      protocols.handlers.add(scheme)
    }
  }

  for (const key in globalThis.__args.config) {
    if (key.startsWith('webview_protocol-handlers_')) {
      const scheme = key
        .replace('webview_protocol-handlers_', '')
        .replace(':', '')
      if (scheme) {
        protocols.add(scheme + ':')
        protocols.handlers.add(scheme)
      }
    }
  }
}

/**
 * @typedef {{ strict?: boolean }} URLParseOptions
 */

/**
 * @typedef {object} ParsedURL
 * @property {string|null} protocol
 * @property {string|null} host
 * @property {string|null} hostname
 * @property {string|null} origin
 * @property {string|null} auth
 * @property {string|null} username
 * @property {string|null} password
 * @property {string|null} port
 * @property {string|null} pathname
 * @property {string|null} path
 * @property {string|null} search
 * @property {string|null} hash
 * @property {string} href
 * @property {URLSearchParams} searchParams
 * @property {string|Record<string, any>} [query]
 */

/**
 * @typedef {Partial<ParsedURL>} URLFormatOptions
 */

/**
 * Parse a URL-like input into a structured object.
 * @param {string} input
 * @param {boolean|URLParseOptions|null} [options]
 * @returns {ParsedURL|null}
 * - When `options === true`, includes a Node-compatible `query` object.
 * - When `options?.strict === true`, returns `null` if input cannot be parsed.
 *
 * Example:
 * ```js
 * parse('https://user:pass@example.com:8080/a/b?x=1#h')
 * // => {
 * //   protocol: 'https:', hostname: 'example.com', origin: 'https://example.com:8080',
 * //   username: 'user', password: 'pass', port: '8080', pathname: '/a/b',
 * //   search: '?x=1', hash: '#h', path: '/a/b', href: 'https://user:pass@example.com:8080/a/b?x=1#h',
 * //   auth: 'user:pass', searchParams: URLSearchParams, query: 'x=1'
 * // }
 * ```
 */
export function parse (input, options = null) {
  const zone = parseIPv6Zone(input)
  const parseInput = zone?.input || input
  let u = null
  if (URL.canParse(parseInput)) {
    u = new URL(parseInput)
  }

  if (options?.strict === true && !URL.canParse(parseInput)) {
    return null
  }

  for (const scheme of RUNTIME_SCHEMES) {
    if (URL.canParse(parseInput, `${scheme}://`)) {
      u = new URL(parseInput, globalThis.location.origin)
      break
    }
  }

  if (!u) {
    return null
  }

  const encodedUsername = u.username
  const encodedPassword = u.password
  const username = decodeAuthComponent(encodedUsername)
  const password = decodeAuthComponent(encodedPassword)
  let hostname = stripIPv6Brackets(u.hostname) || null
  let host = u.host || null
  let origin = u.origin || null

  if (zone && hostname) {
    hostname += zone.identifier
    host = `[${hostname}]${u.port ? `:${u.port}` : ''}`
    if (origin && origin !== 'null' && u.host) {
      origin = origin.replace(u.host, host)
    }
  }

  const out = {
    hash: u.hash || (u.href.endsWith('#') ? '#' : null),
    host,
    hostname,
    origin,
    auth:
      encodedUsername || encodedPassword
        ? `${username || ''}${encodedPassword ? `:${password}` : ''}`
        : null,
    password: password || null,
    pathname: u.pathname || null,
    path: u.pathname || null,
    port: u.port || null,
    protocol: u.protocol || null,
    search: u.search || (u.href.split('#')[0].endsWith('?') ? '?' : null),
    searchParams: u.searchParams,
    username: username || null,
    [Symbol.toStringTag]: 'URL (Parsed)'
  }

  if (options === true) {
    // for nodejs compat
    out.query = Object.fromEntries(out.searchParams.entries())
  } else if (out.search) {
    out.query = out.search.slice(1) ?? null
  }

  if (!input.startsWith(out.protocol)) {
    out.protocol = null
    out.hostname = null
    out.origin = null
    out.host = null
    out.href = `${out.pathname || ''}${out.search || ''}${out.hash || ''}`
  } else {
    out.href = `${out.protocol}//`

    if (encodedUsername || encodedPassword) {
      out.href += encodedUsername
      if (encodedPassword) {
        out.href += `:${encodedPassword}`
      }

      if (out.hostname) {
        out.href += '@'
      }
    }

    out.href += `${out.host || out.hostname || ''}${out.pathname || ''}${out.search || ''}${out.hash || ''}`
  }

  return out
}

function decodeAuthComponent (value) {
  if (!value) return ''

  try {
    return decodeURIComponent(value)
  } catch {
    return value
  }
}

function stripIPv6Brackets (hostname) {
  if (hostname?.startsWith('[') && hostname.endsWith(']')) {
    return hostname.slice(1, -1)
  }

  return hostname
}

function parseIPv6Zone (input) {
  if (typeof input !== 'string') return null

  const match = input.match(
    /^([a-z][a-z\d+.-]*:\/\/(?:[^/?#]*@)?\[)([\da-f:.]+)(%25(?:[a-z\d._~-]|%[\da-f]{2})+)(\](?::\d+)?)/i
  )
  if (!match) return null

  return {
    identifier: match[3],
    input: match[1] + match[2] + match[4] + input.slice(match[0].length)
  }
}

/**
 * Resolve a target URL/path `to` against a base `from`.
 * Mirrors Node.js `url.resolve()` semantics.
 *
 * Example:
 * ```js
 * resolve('http://example.com/a/b', '../c') // => 'http://example.com/c'
 * resolve('/a/b', 'c') // => '/a/c'
 * ```
 */
export function resolve (from, to) {
  const resolved = new URL(to, new URL(from, 'resolve://'))

  if (resolved.protocol === 'resolve:') {
    const { pathname, search, hash } = resolved
    return pathname + search + hash
  }

  return resolved.toString()
}

/**
 * Format a URL from either a string or a partial object containing URL fields.
 * Returns an empty string if the input is invalid or insufficient.
 *
 * Example (object):
 * ```js
 * format({ protocol: 'https:', hostname: 'example.com', pathname: '/a/b' })
 * // => 'https://example.com/a/b'
 * ```
 *
 * Example (string):
 * ```js
 * format('https://example.com/a/b') // => 'https://example.com/a/b'
 * ```
 *
 * Notes
 * - When specifying `hostname` with an IPv6 literal, brackets are added automatically.
 *   Alternatively, you can pass `host` directly as `[2001:db8::1]:8080`.
 * @param {string|URLFormatOptions} input
 * @returns {string}
 */
export function format (input) {
  if (!input || (typeof input !== 'string' && typeof input !== 'object')) {
    throw new TypeError(
      `The 'input' argument must be one of type object or string. Received: ${input}`
    )
  }

  if (typeof input === 'string') {
    if (!URL.canParse(input)) {
      return ''
    }

    return new URL(input).toString()
  }

  let formatted = ''

  if (input.protocol) {
    formatted += `${input.protocol}//`
  }

  if (input.username || input.password) {
    formatted += encodeURIComponent(input.username || '')

    if (input.password) {
      formatted += `:${encodeURIComponent(input.password)}`
    }

    formatted += '@'
  }
  const hostnameRaw = input.hostname || ''
  const needsBrackets =
    hostnameRaw &&
    hostnameRaw.includes(':') &&
    !(hostnameRaw.startsWith('[') && hostnameRaw.endsWith(']'))
  const hostname = needsBrackets ? `[${hostnameRaw}]` : hostnameRaw
  const host =
    input.host ||
    (hostname ? hostname + (input.port ? `:${input.port}` : '') : '')
  const allowEmptyHost = input.protocol === 'file:'
  if (!host && !allowEmptyHost) return ''
  if (!host && allowEmptyHost) {
    // file:/// for empty host
    formatted += '/'
  } else if (host) {
    formatted += host
  }

  if (input.pathname) {
    if (allowEmptyHost && !host && input.pathname.startsWith('/')) {
      formatted += input.pathname.slice(1)
    } else {
      formatted += input.pathname
    }
  }

  if (typeof input.search === 'string' && input.search.length > 0) {
    formatted += input.search.startsWith('?') ? input.search : `?${input.search}`
  } else if (input.query && typeof input.query === 'object') {
    formatted += `?${qs.stringify(input.query)}`
  } else if (input.query && typeof input.query === 'string') {
    if (!input.query.startsWith('?')) {
      formatted += '?'
    }

    formatted += input.query
  }

  if (input.hash && typeof input.hash === 'string') {
    if (!input.hash.startsWith('#')) {
      formatted += '#'
    }

    formatted += input.hash
  }

  return formatted
}

export function fileURLToPath (url) {
  if (typeof url === 'string') {
    url = new URL(url, globalThis.location.origin)
  }

  if (!(url instanceof URL)) {
    throw new TypeError(
      `Expecting 'url' to be a URL or string. Received: ${url}`
    )
  }

  if (
    url.protocol !== 'file:' &&
    !RUNTIME_SCHEMES.some((scheme) => url.protocol === `${scheme}:`)
  ) {
    throw new TypeError(
      `Expecting 'url' to have a 'file:' or runtime URL scheme. Received: ${url.protocol}`
    )
  }

  return url.pathname
}

url.serializeURLOrigin = function (input) {
  const { scheme, protocol, host } = input

  if (globalThis.__args.config.meta_application_protocol) {
    if (
      protocol &&
      globalThis.__args.config.meta_application_protocol ===
        protocol.slice(0, -1)
    ) {
      return `${protocol}//${serializeHost(host)}`
    }

    if (
      scheme &&
      globalThis.__args.config.meta_application_protocol === scheme
    ) {
      return `${scheme}://${serializeHost(host)}`
    }
  }

  if (protocols.has(protocol)) {
    return `${protocol}//${serializeHost(host)}`
  }

  if (protocols.has(`${scheme}:`)) {
    return `${scheme}://${serializeHost(host)}`
  }

  return serializeURLOrigin(input)
}

const descriptors = Object.getOwnPropertyDescriptors(URL.prototype)
Object.defineProperties(URL.prototype, {
  ...descriptors,
  origin: {
    ...descriptors.origin,
    get () {
      return url.serializeURLOrigin(this)
    }
  }
})

export default URL
export { URL, URLSearchParams, parseURL, URLPattern }
