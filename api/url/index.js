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
 * Parse a URL-like input into a structured object.
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
  let u = null
  if (URL.canParse(input)) {
    u = new URL(input)
  }

  if (options?.strict === true && !URL.canParse(input)) {
    return null
  }

  for (const scheme of RUNTIME_SCHEMES) {
    if (URL.canParse(input, `${scheme}://`)) {
      u = new URL(input, globalThis.location.origin)
      break
    }
  }

  if (!u) {
    return null
  }

  const out = {
    hash: u.hash || null,
    host: u.host || null,
    hostname: u.hostname || null,
    origin: u.origin || null,
    auth: [u.username, u.password].filter(Boolean).join(':') || null,
    password: u.password || null,
    pathname: u.pathname || null,
    path: u.pathname || null,
    port: u.port || null,
    protocol: u.protocol || null,
    search: u.search || null,
    searchParams: u.searchParams,
    username: u.username || null,
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

    if (out.username) {
      out.href += [out.username, out.password].filter(Boolean).join(':')

      if (out.hostname) {
        out.href += '@'
      }
    }

    out.href += `${out.host || out.hostname || ''}${out.pathname || ''}${out.search || ''}${out.hash || ''}`
  }

  return out
}

/**
 * Resolve a target URL/path `to` against a base `from`.
 * Mirrors Node.js `url.resolve()` semantics.
 *
 * Example:
 * ```js
 * resolve('http://example.com/a/b', '../c') // => 'http://example.com/c'
 * resolve('/a/b', 'c') // => '/a/b/c'
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

  if (input.username) {
    formatted += encodeURIComponent(input.username)

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

  if (input.query && typeof input.query === 'object') {
    formatted += `?${qs.stringify(input.query)}`
  } else if (input.query && typeof input.query === 'string') {
    if (!input.query.startsWith('?')) {
      formatted += '?'
    }

    formatted += encodeURIComponent(decodeURIComponent(input.query))
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
