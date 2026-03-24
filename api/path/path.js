/**
 * @module path
 *
 * Example usage:
 * ```js
 * import { Path } from 'oro:path'
 * ```
 *
 * Notes
 * - URL-aware joins: When the first component is a URL, subsequent components
 *   are applied to its pathname. An absolute component (starting with `/` or `\\` on Windows)
 *   resets the pathname to the origin root. Username/password/host are preserved.
 * - Windows specifics:
 *   - Drive-relative paths like `C:foo` are not absolute.
 *   - Cross-drive relative paths do not exist; `relative('C:\\a', 'D:\\b')` returns `D:\\b`.
 *   - UNC roots (e.g., `\\\\server\\share`) are preserved by join/normalize/parse.
 * - Supported PathComponent inputs include strings, URL objects, and objects
 *   with `{ url }` or `{ pathname }`.
 */
import { resolve as resolveURL, URL, URLPattern } from '../url.js'
import location from '../location.js'

const windowsDriveRegex = /^[a-z]:/i
const windowsDriveAndSlashesRegex = /^([a-z]:(\\|\/\/))/i
const windowsDriveInPathRegex = /^\/[a-z]:/i

function maybeURL (uri, baseURL = undefined) {
  let url = null

  if (typeof baseURL === 'string' && baseURL.startsWith('blob:')) {
    baseURL = new URL(baseURL).pathname
  }

  if (typeof uri === 'string' && uri.startsWith('blob:')) {
    uri = new URL(uri).pathname
  }

  try {
    baseURL = new URL(baseURL)
  } catch {}

  try {
    url = new URL(uri, baseURL)
  } catch {}

  return url
}

function toComponentString (component) {
  if (component && typeof component === 'object') {
    if (typeof component.href === 'string') return component.href
    if (typeof component.pathname === 'string') return component.pathname
    if (typeof component.url === 'string') return component.url
  }
  return String(component)
}

/**
 * The path.resolve() method resolves a sequence of paths or path segments into an absolute path.
 * @param {object} options
 * @param {...PathComponent} components
 * @returns {string}
 * @see {@link https://nodejs.org/api/path.html#path_path_resolve_paths}
 */
export function resolve (options, ...components) {
  const { sep } = options
  let resolved = ''
  while (components.length) {
    let component = toComponentString(components.shift()).replace(/\\/g, '/')

    if (component.length > 1) {
      component = component.replace(/\/$/g, '')
    }

    if (component.startsWith('blob:')) {
      component = maybeURL(component)
    }

    resolved = resolveURL(resolved + '/', component)

    if (resolved.length > 1) {
      resolved = resolved.replace(/\/$/g, '')
    }
  }

  if (sep === '\\') {
    resolved = resolved.replace(/\//g, sep)
  }

  if (resolved.length > 1) {
    resolved = resolved.replace(new RegExp(`\\${sep}$`, 'g'), '')
  }

  return resolved
}

/**
 * Computes current working directory for a path
 * @param {object=} [opts]
 * @param {boolean=} [opts.posix] Set to `true` to force POSIX style path
 * @return {string}
 */
export function cwd (opts) {
  let cwd = '.'

  cwd = location.pathname

  if (cwd && !cwd.endsWith('/')) {
    cwd = maybeURL('..', location)?.pathname
  }

  if (!cwd) {
    cwd = '.'
  }

  if (cwd && opts?.posix === true) {
    cwd = cwd.replace(/\\/g, '/')
  }

  return cwd
}

/**
 * Computed location origin. Defaults to `oro:///` if not available.
 * @return {string}
 */
export function origin () {
  return location.origin
}

/**
 * Computes the relative path from `from` to `to`.
 * @param {object} options
 * @param {PathComponent} from
 * @param {PathComponent} to
 * @return {string}
 */
export function relative (options, from, to) {
  const { sep } = options
  if (from === to) return ''
  from = resolve(options, from)
  to = resolve(options, to)

  // On Windows, if roots differ (e.g., different drive letters), no relative path exists.
  // In that case, return the target path `to` unchanged.
  if (sep === '\\') {
    const fromDrive =
      (from.match(windowsDriveRegex) || [])[0]?.toLowerCase() || null
    const toDrive =
      (to.match(windowsDriveRegex) || [])[0]?.toLowerCase() || null
    if (fromDrive && toDrive && fromDrive !== toDrive) {
      return to
    }
  }

  const components = {
    output: [],
    from: from.split(sep).filter(Boolean),
    to: to.split(sep).filter(Boolean)
  }

  for (let i = components.from.length - 1; i > -1; --i) {
    const a = components.from[i]
    const b = components.to[i]
    if (a !== b) {
      components.output.push('..')
    }
  }

  for (let i = 0; i < components.output.length; ++i) {
    components.from.pop()
  }

  components.output.push(
    ...components.to
      .join(sep)
      .replace(components.from.join(sep), '')
      .split(sep)
      .filter(Boolean)
  )

  const relative = components.output.join(sep)

  if (windowsDriveRegex.test(from) || windowsDriveRegex.test(to)) {
    return relative.replace(windowsDriveRegex, '')
  }

  return relative
}

/**
 * Joins path components. This function may not return an absolute path.
 * @param {object} options
 * @param {...PathComponent} components
 * @return {string}
 */
export function join (options, ...components) {
  const { sep } = options
  const resolved = []
  const isWindows = sep === '\\'
  let absolute = String(toComponentString(components[0] || ''))
    .trim()
    .startsWith(sep)
  let drive = null
  let uncRoot = false
  let url = null

  while (components.length) {
    const raw = components.shift()
    const rawStr = toComponentString(raw)
    let s = rawStr

    // Normalize separators on Windows
    if (isWindows) s = s.replace(/\//g, sep)

    if (!url && URL.canParse(s)) {
      // Capture base URL and seed with its pathname components
      url = new URL(s)
      const baseParts = url.pathname.split('/').filter(Boolean)
      for (const part of baseParts) {
        if (part && part !== '.') resolved.push(part)
      }
      continue
    }

    if (!s) continue

    // Track drive and detect absolute drive paths on Windows
    if (isWindows) {
      const sDrive = (s.match(windowsDriveRegex) || [])[0] || null
      if (sDrive) {
        drive = sDrive
        if (s.startsWith(sDrive + sep) || s.startsWith(sDrive + '/')) {
          // Absolute with drive: reset accumulation
          resolved.length = 0
          absolute = true
        }
      }

      // UNC root detection (e.g., \\server\share)
      const normalizedRaw = rawStr.replace(/\//g, sep)
      if (!url && normalizedRaw.startsWith('\\\\')) {
        uncRoot = true
        absolute = true
        resolved.length = 0
      }
    }

    if (s.startsWith(sep)) {
      // Reset to root on absolute segment
      resolved.length = 0
      absolute = true
    }

    const parts = s.split(sep)
    for (const part of parts) {
      if (!part) continue
      if (part === '.') continue
      if (part === '..') {
        if (resolved.length > 1 && resolved[0] !== '..') {
          resolved.pop()
          continue
        }
      }
      resolved.push(part)
    }
  }

  const joined = resolved.join(sep)

  if (url) {
    const rel = (absolute ? sep : '') + joined
    const auth = url.username
      ? encodeURIComponent(url.username) +
        (url.password ? ':' + encodeURIComponent(url.password) : '') +
        '@'
      : ''
    const host = url.host || ''
    const base =
      url.protocol === 'file:'
        ? `file://${host}`
        : `${url.protocol}//${auth}${host}`
    return new URL(rel, base).href
  }

  if (absolute) {
    if (isWindows) {
      // If joined already contains a drive, return as-is; else prefix any known drive
      if (windowsDriveRegex.test(joined)) return joined
      if (drive) return `${drive}${sep}${joined}`
      if (uncRoot) return `${sep}${sep}${joined}`
    }
    return sep + joined
  }

  return joined
}

/**
 * Computes directory name of path.
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function dirname (options, path) {
  if (typeof path !== 'string') {
    throw Object.assign(
      new Error(
        `The "path" argument must be of type string. Received: ${path}`
      ),
      {
        code: 'ERR_INVALID_ARG_TYPE'
      }
    )
  }

  if (windowsDriveInPathRegex.test(path)) {
    path = path.slice(1)
  }

  const { sep } = options
  const [drive] = path.match(windowsDriveRegex) || []

  const pathWithoutDrive = path.replace(windowsDriveAndSlashesRegex, '')
  let resolved = resolve(options, pathWithoutDrive, '..')

  if (!pathWithoutDrive.startsWith(sep)) {
    if (resolved.startsWith(sep)) {
      if (resolved === sep && !drive) {
        resolved = '.'
      } else if (pathWithoutDrive.startsWith('.')) {
        resolved = '.' + resolved
      } else {
        resolved = resolved.slice(1)
      }
    }
  }

  if (drive) {
    return `${drive}${sep}${resolved}`
  }

  return resolved
}

/**
 * Computes base name of path.
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function basename (options, path) {
  const { sep } = options
  const components = path.endsWith(sep)
    ? path.slice(0, -1).split(sep)
    : path.split(sep)
  return components.pop()
}

/**
 * Computes extension name of path.
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function extname (options, path) {
  // Avoid passing sep as cwd; extname is based solely on the path.
  return Path.from(path).ext
}

/**
 * Computes normalized path
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function normalize (options, path) {
  const { sep } = options
  path = String(path)
  const [drive] = path.match(windowsDriveRegex) || []
  const isWindows = sep === '\\'
  const hasDrive = Boolean(drive)
  const hasDriveWithSlash = windowsDriveAndSlashesRegex.test(path)
  const isDriveRelative = isWindows && hasDrive && !hasDriveWithSlash
  path = path
    .replace(windowsDriveAndSlashesRegex, '')
    .replace(new RegExp(`(${sep + sep})+`, 'g'), sep)

  const url = isDriveRelative ? null : maybeURL(path)
  const pathWithoutDrive = isDriveRelative
    ? path.replace(windowsDriveRegex, '')
    : url?.pathname || path
  let resolved = resolve(options, pathWithoutDrive)

  if (!isDriveRelative && url?.protocol) {
    if (!pathWithoutDrive.startsWith(sep) && resolved.startsWith(sep)) {
      resolved = resolved.slice(1)
      resolved = `${url?.protocol}${url?.hostname || ''}${resolved}`
    } else {
      resolved = `${url?.protocol}/${url?.hostname || ''}${resolved}`
    }
  }

  if (!pathWithoutDrive.startsWith(sep)) {
    if (resolved.startsWith(sep)) {
      if (resolved === sep && !drive) {
        resolved = '.'
      } else if (pathWithoutDrive.startsWith('.')) {
        resolved = '.' + resolved
      } else {
        resolved = resolved.slice(1)
      }
    }
  }

  let normalized =
    resolved.replace(/(\/)+/g, '/').replace(/\//g, sep) +
    (path.endsWith(sep) ? sep : '')

  // Preserve UNC double-leading backslashes for Windows UNC inputs
  if (!drive && sep === '\\' && String(path).startsWith('\\\\')) {
    if (!normalized.startsWith('\\\\')) {
      if (normalized.startsWith('\\')) normalized = '\\' + normalized
      else normalized = '\\\\' + normalized
    }
  }

  if (drive) {
    if (isDriveRelative) {
      return `${drive}${normalized}`
    }
    return `${drive}${sep}${normalized}`
  }

  return normalized
}

/**
 * Formats `Path` object into a string.
 * @param {object} options
 * @param {object|Path} path
 * @return {string}
 */
export function format (options, path) {
  const { root, dir, base, ext, name } = path
  const { sep } = options
  const output = []
  if (dir) output.push(dir, sep)
  else if (root) output.push(root)
  if (base) output.push(base)
  else if (name) output.push(name + (ext || ''))
  return output.join('').replace(/\//g, sep)
}

/**
 * Parses input `path` into a `Path` instance.
 * @param {PathComponent} path
 * @return {object}
 */
export function parse (path) {
  const sep = /\\/.test(path) ? '\\' : '/'
  const [drive] = path.match(windowsDriveRegex) || []
  const parsed = { root: '', dir: '', base: '', ext: '', name: '' }
  const parts = path.replace(windowsDriveRegex, '').split(sep)

  // Handle Windows UNC paths: \\server\share\...
  if (sep === '\\' && path.startsWith('\\\\')) {
    const normalized = String(path).replace(/\//g, '\\')
    const withoutPrefix = normalized.slice(2)
    const uncParts = withoutPrefix.split('\\')
    const server = uncParts[0] || ''
    const share = uncParts[1] || ''
    const rest = uncParts.slice(2).join('\\')

    parsed.root = `\\\\${server}${share ? '\\' + share : ''}\\`

    if (!rest) {
      parsed.dir = parsed.root.replace(/\\$/, '')
      parsed.base = ''
      parsed.ext = ''
      parsed.name = ''
      return parsed
    }

    const last = rest.lastIndexOf('\\')
    if (last === -1) {
      parsed.dir = parsed.root.replace(/\\$/, '')
      parsed.base = rest
    } else {
      parsed.dir = parsed.root + rest.slice(0, last)
      parsed.base = rest.slice(last + 1)
    }
    parsed.ext = extname({ sep }, parsed.base)
    parsed.name = parsed.base.replace(parsed.ext, '')
    return parsed
  }

  parsed.root = !parts[0] ? parts[0] || sep : ''
  parsed.dir =
    !parts[0] || parts[0].startsWith('.')
      ? parts.slice(0, -1).join(sep)
      : [parsed.root].concat(parts.slice(1).slice(0, -1)).join(sep)
  parsed.ext = extname({ sep }, path)
  parsed.base = basename({ sep }, path)
  parsed.name = parsed.base.replace(parsed.ext, '')

  if (!parsed.dir) parsed.dir = parsed.root
  if (drive && parsed.root) parsed.root = drive + parsed.root
  if (drive && parsed.dir) parsed.dir = drive + parsed.dir

  return parsed
}

/**
 * @typedef {(string|Path|URL|{ pathname: string }|{ url: string)} PathComponent
 */

/**
 * A container for a parsed Path.
 */
export class Path {
  /**
   * Creates a `Path` instance from `input` and optional `cwd`.
   * @param {PathComponent} input
   * @param {string} [cwd]
   */
  static from (input, cwd) {
    if (typeof input?.href === 'string') {
      return this.from(input.href, cwd)
    } else if (typeof input?.pathname === 'string') {
      return this.from(input.pathname, cwd)
    } else if (typeof input?.url === 'string') {
      return this.from(input.url, cwd)
    }

    return new this(String(input || '.'), cwd ? String(cwd) : null)
  }

  #backSlashesDetected = false
  #leadingDot = false
  #isRelative = false
  #hasProtocol = false
  #source = null
  #drive = null

  /**
   * `Path` class constructor.
   * @protected
   * @param {string} pathname
   * @param {string} [cwd = Path.cwd()]
   */
  constructor (pathname, cwd) {
    const isRelative = !/^[/|\\]/.test(pathname.replace(windowsDriveRegex, ''))
    pathname = String(pathname || '.').trim()

    if (cwd) {
      cwd = cwd.replace(/\\/g, '/')
      cwd = maybeURL(`file://${cwd.replace('file://', '')}`)
    } else if (pathname.startsWith('..')) {
      pathname = pathname.slice(2)
      cwd = 'file:///..'
    } else if (isRelative) {
      cwd = maybeURL('file:///.')
    } else {
      cwd = maybeURL(`file://${Path.cwd()}`)
    }

    if (cwd === 'oro:/') {
      cwd = Path.origin()
    }

    if (pathname.startsWith('.')) {
      this.#leadingDot = true
    }

    try {
      this.pattern = new URLPattern(pathname)
      this.#hasProtocol = Boolean(this.pattern.protocol)
    } catch {}

    this.url = maybeURL(pathname, cwd)

    const [drive] =
      pathname.match(windowsDriveRegex) ||
      this.pathname.match(windowsDriveRegex) ||
      []

    this.#backSlashesDetected = /\\/.test(pathname)
    this.#isRelative = isRelative
    this.#source = pathname
    this.#drive = drive ? drive.replace(/(\\|\/)/g, '') : null
  }

  get pathname () {
    if (!this.url) {
      return null
    }

    let { pathname } = this.url

    if (this.#leadingDot || this.isRelative) {
      pathname = pathname.replace(/^\//g, '')
    }

    if (this.#backSlashesDetected) {
      pathname = pathname.replace(/\//g, '\\')
    }

    return pathname
  }

  get protocol () {
    return this.url?.protocol
  }

  get href () {
    return this.url?.href
  }

  /**
   * `true` if the path is relative, otherwise `false.
   * @type {boolean}
   */
  get isRelative () {
    return this.#isRelative
  }

  /**
   * The working value of this path.
   */
  get value () {
    const { protocol, drive } = this
    const regex = new RegExp(`^${protocol}(//)?(.[\\|/])?`, 'i')
    let { href } = this

    if (!this.#hasProtocol) {
      href = href.replace(regex, '')
      if (this.isRelative) {
        // eslint-disable-next-line
        href = href.replace(/^[\/|\\]/g, '')
      }
    }

    if (this.#backSlashesDetected) {
      const prefix = this.#hasProtocol ? href.slice(0, protocol.length) : ''
      href = href.slice(prefix.length)
      if (prefix.length) {
        href = prefix + '//' + href.slice(2).replace(/\//g, '\\')
      } else {
        href = href.replace(/\//g, '\\')
      }
    }

    if (!drive) {
      href = href.replace(windowsDriveInPathRegex, '')
    }

    return href
  }

  /**
   * The original source, unresolved.
   * @type {string}
   */
  get source () {
    return this.#source
  }

  /**
   * Computed parent path.
   * @type {string}
   */
  get parent () {
    let pathname = this.value
    let i = pathname.lastIndexOf('/')

    if (i === -1) i = pathname.lastIndexOf('\\')
    if (i >= 0) pathname = pathname.slice(0, i + 1)
    else pathname = ''

    if (this.drive) {
      return this.drive + pathname.replace(windowsDriveInPathRegex, '')
    }

    return pathname
  }

  /**
   * Computed root in path.
   * @type {string}
   */
  get root () {
    const { dir } = this
    const drive = (dir.match(windowsDriveRegex) || [])[0] || this.drive

    if (!this.isRelative) {
      if (this.value.includes('\\') || this.#backSlashesDetected) {
        return `${drive || ''}\\`
      } else if (this.value.startsWith('/')) {
        return '/'
      }
    } else if (drive) {
      return drive
    }

    return ''
  }

  /**
   * Computed directory name in path.
   * @type {string}
   */
  get dir () {
    const { isRelative } = this
    let { parent } = this

    if (this.#backSlashesDetected) {
      parent = parent.replace(/\//g, '\\')
    } else {
      if (parent.endsWith('/') && parent.length > 1) {
        parent = parent.slice(0, -1)
      }
    }

    if (isRelative && (parent.startsWith('/') || parent.startsWith('\\'))) {
      parent = parent.slice(1)
    }

    if (this.drive) {
      return this.drive + parent.replace(windowsDriveRegex, '')
    }

    if (this.#source.startsWith('./') || this.#source.startsWith('.\\')) {
      return `.${parent || ''}`
    }

    if (parent === '.') {
      return ''
    }

    return parent
  }

  /**
   * Computed base name in path.
   * @type {string}
   */
  get base () {
    let i = this.pathname.lastIndexOf('/')
    if (i === -1) {
      i = this.pathname.lastIndexOf('\\')
    }
    return this.pathname.slice(i >= 0 ? i + 1 : undefined)
  }

  /**
   * Computed base name in path without path extension.
   * @type {string}
   */
  get name () {
    return this.base.replace(this.ext, '')
  }

  /**
   * Computed extension name in path.
   * @type {string}
   */
  get ext () {
    let i = this.pathname.lastIndexOf('/')

    if (i === -1) {
      i = this.pathname.lastIndexOf('\\')
    }

    const pathname = i > -1 ? this.pathname.slice(i) : this.pathname

    i = pathname.lastIndexOf('.')
    if (i === -1) return ''
    return pathname.slice(i >= 0 ? i : undefined)
  }

  /**
   * The computed drive, if given in the path.
   * @type {string?}
   */
  get drive () {
    return this.#drive
  }

  /**
   * @return {URL}
   */
  toURL () {
    return maybeURL(this.href.replace(/\\/g, '/'))
  }

  /**
   * Converts this `Path` instance to a string.
   * @return {string}
   */
  toString () {
    return decodeURIComponent(this.value)
  }

  /**
   * @ignore
   */
  inspect () {
    const p = this
    // eslint-disable-next-line new-parens
    return new (class Path {
      root = p.root
      dir = p.dir
      base = p.base
      ext = p.ext
      name = p.name
    })()
  }

  /**
   * @ignore
   */
  [Symbol.toStringTag] () {
    return 'Path'
  }
}

Object.assign(Path, {
  basename,
  cwd,
  dirname,
  extname,
  format,
  join,
  normalize,
  origin,
  parse,
  relative,
  resolve
})

export default Path
