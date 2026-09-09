/**
 * @module tar
 *
 * High-level helpers for reading and writing tar archives.
 *
 * This module is designed for very large archives and supports:
 * - Streaming decode via range reads on individual entries
 * - Streaming encode via chunked entry writes
 * - Optional `mmap`-backed indexing on supported native platforms
 * - Random access to entries by path
 *
 * Example:
 * ```js
 * import * as tar from 'oro:tar'
 *
 * const archive = await tar.open('./assets.tar')
 * const entries = await archive.entries()
 * const buf = await archive.read('images/logo.png')
 * ```
 */

import ipc from './ipc.js'
import { Buffer } from './buffer.js'
import { rand64 } from './crypto.js'
import * as fs from './fs.js'
import * as sysPath from './path.js'

/**
 * @typedef {'file'|'directory'|'symlink'|'hardlink'|'block-device'|'char-device'|'fifo'|'other'} TarEntryKind
 */

/**
 * @typedef {object} TarSparseRegion
 * @property {number} offset
 * @property {number} length
 */

/**
 * @typedef {object} TarEntryHeader
 * @property {string} path
 * @property {number} [size] Total number of bytes for the entry body. Required when `body` is an AsyncIterable.
 * @property {TarSparseRegion[]} [sparse] Sparse data regions for sparse file entries.
 * @property {number} [sparseSize] Logical size of the sparse file entry (defaults to the end of the last region).
 * @property {number} [mode]
 * @property {number} [mtime]
 * @property {number} [uid]
 * @property {number} [gid]
 * @property {string} [uname]
 * @property {string} [gname]
 * @property {string} [linkpath] Target path for link entries (symlink/hardlink)
 * @property {number} [devmajor] Device major number for char/block device entries
 * @property {number} [devminor] Device minor number for char/block device entries
 * @property {TarEntryKind} [kind]
 */

/**
 * @typedef {object} TarEntryStat
 * @property {string} path
 * @property {number} size
 * @property {number} mode
 * @property {number} mtime
 * @property {number} uid
 * @property {number} gid
 * @property {string} [uname]
 * @property {string} [gname]
 * @property {TarEntryKind} kind
 * @property {boolean} isFile
 * @property {boolean} isDirectory
 * @property {string} [linkpath]
 * @property {number} [devmajor]
 * @property {number} [devminor]
 * @property {TarSparseRegion[]} [sparse] Sparse data regions (present for sparse file entries)
 */

/**
 * @typedef {object} TarOpenOptions
 * @property {boolean} [writable=false]
 * @property {boolean} [mmap=false]
 * @property {number} [uid]
 * @property {number} [gid]
 * @property {string} [uname]
 * @property {string} [gname]
 * @property {number} [mtime]
 */

/**
 * @typedef {object} TarReadOptions
 * @property {number} [offset=0]
 * @property {number} [length] If omitted, reads until end of entry
 * @property {AbortSignal} [signal]
 * @property {number} [timeout]
 */

/**
 * @typedef {object} TarReadStreamOptions
 * @property {number} [highWaterMark=65536]
 * @property {number} [start=0]
 * @property {number} [end] Inclusive end offset (defaults to entry size - 1)
 * @property {AbortSignal} [signal]
 * @property {number} [timeout]
 */

/**
 * @typedef {object} TarWriteEntryOptions
 * @property {number} [mode=0o644]
 * @property {number} [mtime] Defaults to the archive global mtime (if set), otherwise current time.
 * @property {TarEntryKind} [kind='file']
 */

/**
 * @typedef {object} TarWriteOptions
 * @property {AbortSignal} [signal]
 * @property {number} [timeout]
 */

const ROUTES = Object.freeze({
  open: 'tar.open',
  openBuffer: 'tar.openBuffer',
  close: 'tar.close',
  list: 'tar.list',
  stat: 'tar.stat',
  read: 'tar.read',
  writeBegin: 'tar.write.begin',
  writeData: 'tar.write.data',
  finalize: 'tar.finalize'
})

function normalizeUint32 (value, label) {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    throw new TypeError(`${label} must be a finite number`)
  }

  if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff) {
    throw new RangeError(`${label} must be a 32-bit unsigned integer`)
  }

  return value
}

function normalizeMtime (value, label) {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    throw new TypeError(`${label} must be a finite number`)
  }

  if (value < 0) {
    throw new RangeError(`${label} must be a non-negative number`)
  }

  const seconds = Math.floor(value)
  if (!Number.isSafeInteger(seconds) || seconds < 0) {
    throw new RangeError(`${label} must be a non-negative safe integer`)
  }

  return seconds
}

function normalizeKind (typeChar) {
  switch (typeChar) {
    case '0':
    case '7':
    case '\0':
    case 'S':
      return 'file'
    case '5':
      return 'directory'
    case '2':
      return 'symlink'
    case '1':
      return 'hardlink'
    case '3':
      return 'char-device'
    case '4':
      return 'block-device'
    case '6':
      return 'fifo'
    default:
      return 'other'
  }
}

function normalizePath (path) {
  if (path instanceof URL) {
    return path.pathname.startsWith('/')
      ? path.pathname.slice(1)
      : path.pathname
  }

  if (typeof path !== 'string') {
    throw new TypeError('path must be a string or URL')
  }

  if (path.startsWith('./')) {
    return path.slice(2)
  }

  if (path.startsWith('/')) {
    return path.slice(1)
  }

  return path
}

function normalizeArchivePath (path) {
  if (path instanceof URL) {
    return path.pathname
  }

  if (typeof path === 'string') return path

  throw new TypeError('path must be a string or URL')
}

function ensureResult (result, source) {
  if (result?.err) {
    throw result.err
  }

  if (result?.source === source || result?.data !== undefined) {
    return result.data
  }

  return result
}

async function send (route, params = {}) {
  const result = await ipc.send(route, params)
  return ensureResult(result, route)
}

async function requestBinary (route, params = {}, options = null) {
  const result = await ipc.request(route, params, {
    responseType: 'arraybuffer',
    timeout: options?.timeout,
    signal: options?.signal
  })

  if (result.err) {
    throw result.err
  }

  const contentType = result.headers?.get('content-type')
  if (contentType && contentType !== 'application/octet-stream') {
    throw new TypeError(
      `Invalid response content type from '${route}'. Received: ${contentType}`
    )
  }

  const data = result.data
  if (!data) {
    return Buffer.alloc(0)
  }

  if (ArrayBuffer.isView(data) || data instanceof ArrayBuffer) {
    return Buffer.from(data)
  }

  throw new TypeError(
    `Invalid binary response from '${route}'. Received: ${typeof data}`
  )
}

async function applyEntryMetadata (path, entry, options = null) {
  const followSymlinks = options?.followSymlinks !== false
  const preserveOwner = Boolean(options?.preserveOwner)
  const preserveSpecialModes = Boolean(options?.preserveSpecialModes)

  if (preserveOwner) {
    const uid = Number(entry?.uid ?? 0)
    const gid = Number(entry?.gid ?? 0)

    try {
      if (Number.isFinite(uid) && Number.isFinite(gid)) {
        if (followSymlinks) {
          await fs.promises.chown(path, uid, gid)
        } else {
          await fs.promises.lchown(path, uid, gid)
        }
      }
    } catch {}
  }

  const modeMask = preserveSpecialModes ? 0o7777 : 0o777
  const mode = Number(entry?.mode ?? 0) & modeMask
  try {
    if (followSymlinks) {
      await fs.promises.chmod(path, mode)
    } else {
      await fs.promises.lchmod(path, mode)
    }
  } catch {}

  const mtime = Number(entry?.mtime)
  if (Number.isFinite(mtime) && mtime >= 0) {
    try {
      if (followSymlinks) {
        await fs.promises.utimes(path, mtime, mtime)
      } else {
        await fs.promises.lutimes(path, mtime, mtime)
      }
    } catch {}
  }
}

async function lstatMaybe (path) {
  try {
    return await fs.promises.lstat(path)
  } catch (err) {
    if (err?.code === 'ENOENT' || err?.name === 'ENOENT') return null
    throw err
  }
}

async function ensureExtractPath (root, relativePath) {
  const parts = String(relativePath || '')
    .split(/[\\/]+/)
    .filter((p) => p && p !== '.')

  let current = root

  for (const part of parts) {
    if (part === '..') {
      const err = new Error('Invalid path segment in archive entry')
      err.code = 'EINVAL'
      throw err
    }

    current = sysPath.join(current, part)

    const stat = await lstatMaybe(current)
    if (stat) {
      if (stat.isSymbolicLink()) {
        const err = new Error('Refusing to traverse symlink during extraction')
        err.code = 'ELOOP'
        throw err
      }
      if (!stat.isDirectory()) {
        const err = new Error(
          'Cannot create directory: path segment is not a directory'
        )
        err.code = 'ENOTDIR'
        throw err
      }
      continue
    }

    try {
      await fs.promises.mkdir(current)
    } catch (err) {
      if (err?.code !== 'EEXIST' && err?.name !== 'EEXIST') {
        throw err
      }

      const existing = await lstatMaybe(current)
      if (existing?.isSymbolicLink()) {
        const e = new Error('Refusing to traverse symlink during extraction')
        e.code = 'ELOOP'
        throw e
      }
      if (!existing?.isDirectory()) {
        const e = new Error(
          'Cannot create directory: path segment is not a directory'
        )
        e.code = 'ENOTDIR'
        throw e
      }
    }
  }
}

async function ensureExtractFilesystemPath (destPath) {
  const absolutePath = sysPath.resolve(String(destPath || ''))
  const root = sysPath.parse(absolutePath).root || absolutePath
  const relFromRoot = sysPath.relative(root, absolutePath)

  if (!relFromRoot || relFromRoot === '.') {
    return
  }

  await ensureExtractPath(root, relFromRoot)
}

async function resolveHardlinkTarget (root, linkpath) {
  const target = sysPath.resolve(sysPath.join(root, normalizePath(linkpath)))
  const relFromRoot = sysPath.relative(root, target)

  if (relFromRoot.startsWith('..') || sysPath.isAbsolute(relFromRoot)) {
    const err = new Error('Hardlink target escapes destination root')
    err.code = 'EINVAL'
    throw err
  }

  const parts = String(relFromRoot || '')
    .split(/[\\/]+/)
    .filter((p) => p && p !== '.')

  let current = root
  let finalStat = null

  for (let i = 0; i < parts.length; i++) {
    const part = parts[i]
    if (part === '..') {
      const err = new Error('Invalid path segment in archive entry')
      err.code = 'EINVAL'
      throw err
    }

    current = sysPath.join(current, part)

    const stat = await lstatMaybe(current)
    if (!stat) {
      const err = new Error('Hardlink target does not exist')
      err.code = 'ENOENT'
      throw err
    }

    if (stat.isSymbolicLink()) {
      const err = new Error('Refusing to traverse symlink during extraction')
      err.code = 'ELOOP'
      throw err
    }

    if (i < parts.length - 1 && !stat.isDirectory()) {
      const err = new Error(
        'Hardlink target path contains a non-directory segment'
      )
      err.code = 'ENOTDIR'
      throw err
    }

    finalStat = stat
  }

  if (!finalStat) {
    const err = new Error('Missing hardlink target path in archive entry')
    err.code = 'EINVAL'
    throw err
  }

  if (finalStat.isDirectory()) {
    const err = new Error('Hardlink target is a directory')
    err.code = 'EISDIR'
    throw err
  }

  return target
}

async function extractSparseFile (archive, entry, destPath, options) {
  const normalizedEntryPath = normalizePath(entry.path)
  const regions = Array.isArray(entry.sparse) ? entry.sparse : []
  const totalSize = Number(entry.size || 0)

  const handle = await fs.promises.open(destPath, 'w')
  let completed = false

  try {
    await handle.truncate(totalSize)

    for (const region of regions) {
      let offset = Number(region.offset || 0)
      let remaining = Number(region.length || 0)

      if (!Number.isFinite(offset) || offset < 0) {
        continue
      }

      if (!Number.isFinite(remaining) || remaining <= 0) {
        continue
      }

      while (remaining > 0) {
        const size = Math.min(65536, remaining)
        const chunk = await requestBinary(
          ROUTES.read,
          {
            id: archive.id,
            path: normalizedEntryPath,
            offset,
            size
          },
          { signal: options?.signal, timeout: options?.timeout }
        )

        if (!chunk.length) {
          const err = new Error('Unexpected end of sparse entry data')
          err.code = 'EIO'
          throw err
        }

        const result = await handle.write(chunk, 0, chunk.length, offset)
        if (result.bytesWritten !== chunk.length) {
          const err = new Error('Failed to write sparse entry payload to disk')
          err.code = 'EIO'
          throw err
        }

        offset += chunk.length
        remaining -= chunk.length
      }
    }

    completed = true
  } finally {
    await handle.close()
  }

  if (completed) {
    await applyEntryMetadata(destPath, entry, options)
  }
}

/**
 * Represents an open tar archive on disk.
 */
export class TarArchive {
  /**
   * Opens an existing tar archive.
   * @param {string|URL} path
   * @param {TarOpenOptions} [options]
   * @return {Promise<TarArchive>}
   */
  static async open (path, options = null) {
    const writable = Boolean(options?.writable)
    const mmap = Boolean(options?.mmap)
    const archivePath = normalizeArchivePath(path)
    const params = {
      path: archivePath,
      writable,
      mmap
    }

    if (options?.uid != null) {
      params.uid = normalizeUint32(options.uid, 'options.uid')
    }
    if (options?.gid != null) {
      params.gid = normalizeUint32(options.gid, 'options.gid')
    }
    if (options?.mtime != null) {
      params.mtime = normalizeMtime(options.mtime, 'options.mtime')
    }
    if (options?.uname != null) {
      if (typeof options.uname !== 'string') {
        throw new TypeError('options.uname must be a string')
      }
      params.uname = options.uname
    }
    if (options?.gname != null) {
      if (typeof options.gname !== 'string') {
        throw new TypeError('options.gname must be a string')
      }
      params.gname = options.gname
    }

    const data = await send(ROUTES.open, params)

    const id = data?.id
    if (!id) {
      throw new Error('tar.open did not return an archive id')
    }

    return new TarArchive({
      id,
      path: data.path ?? archivePath,
      writable,
      mmap,
      size: data.size ?? 0,
      entryCount: data.entryCount ?? 0
    })
  }

  /**
   * Creates a new tar archive for writing.
   * If the archive already exists it will be truncated.
   * @param {string|URL} path
   * @param {TarOpenOptions} [options]
   * @return {Promise<TarArchive>}
   */
  static async create (path, options = null) {
    const openOptions = { ...options, writable: true }
    return TarArchive.open(path, openOptions)
  }

  /**
   * Opens a tar archive from an in-memory buffer (read-only).
   * @param {Buffer|Uint8Array|ArrayBuffer} buffer
   * @return {Promise<TarArchive>}
   */
  static async fromBuffer (buffer) {
    let bytes = buffer
    if (!Buffer.isBuffer(bytes)) {
      if (ArrayBuffer.isView(bytes)) {
        bytes = Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength)
      } else if (bytes instanceof ArrayBuffer) {
        bytes = Buffer.from(bytes)
      } else {
        throw new TypeError(
          'buffer must be a Buffer, Uint8Array, or ArrayBuffer'
        )
      }
    }

    const result = await ipc.write(ROUTES.openBuffer, {}, bytes)

    const data = ensureResult(result, ROUTES.openBuffer)
    const id = data?.id
    if (!id) {
      throw new Error('tar.openBuffer did not return an archive id')
    }

    return new TarArchive({
      id,
      path: '',
      writable: false,
      mmap: false,
      size: data.size ?? bytes.length,
      entryCount: data.entryCount ?? 0
    })
  }

  /**
   * Creates a new in-memory tar archive for writing.
   * @param {TarOpenOptions} [options]
   * @return {Promise<TarArchive>}
   */
  static async createInMemory (options = null) {
    return createInMemory(options)
  }

  /**
   * @ignore
   * @param {object} state
   */
  constructor (state) {
    this.id = String(state.id || rand64())
    this.path = String(state.path || '')
    this.writable = Boolean(state.writable)
    this.mmap = Boolean(state.mmap)
    this.size = Number(state.size || 0)
    this.entryCount = Number(state.entryCount || 0)
    this.closed = false
    this.finalized = false
  }

  /**
   * Closes the underlying archive descriptor.
   * Further operations on this instance will throw.
   * @return {Promise<void>}
   */
  async close () {
    if (this.closed) return
    await send(ROUTES.close, { id: this.id })
    this.closed = true
  }

  #ensureOpen () {
    if (this.closed) {
      throw new Error('TarArchive is closed')
    }
  }

  /**
   * Lists all entries in the archive.
   * @return {Promise<TarEntryStat[]>}
   */
  async entries () {
    this.#ensureOpen()
    const data = await send(ROUTES.list, { id: this.id })
    if (data?.size != null) {
      this.size = Number(data.size || 0)
    }
    if (data?.entryCount != null) {
      this.entryCount = Number(data.entryCount || 0)
    }
    const entries = Array.isArray(data?.entries) ? data.entries : []
    return entries.map((entry) => ({
      path: String(entry.path),
      linkpath: entry.linkpath ? String(entry.linkpath) : undefined,
      devmajor: entry.devmajor != null ? Number(entry.devmajor) : undefined,
      devminor: entry.devminor != null ? Number(entry.devminor) : undefined,
      size: Number(entry.size || 0),
      mode: Number(entry.mode || 0),
      mtime: Number(entry.mtime || 0),
      uid: Number(entry.uid ?? 0),
      gid: Number(entry.gid ?? 0),
      uname: entry.uname ? String(entry.uname) : undefined,
      gname: entry.gname ? String(entry.gname) : undefined,
      sparse: Array.isArray(entry.sparse)
        ? entry.sparse.map((region) => ({
          offset: Number(region.offset || 0),
          length: Number(region.length || 0)
        }))
        : undefined,
      kind: normalizeKind(entry.type || '0'),
      isFile: Boolean(entry.isFile),
      isDirectory: Boolean(entry.isDirectory)
    }))
  }

  /**
   * Extracts all file and directory entries in the archive into a destination
   * directory using streaming. Attempts to preserve mode/mtime metadata.
   * @param {string|URL} destDir
   * @param {{ signal?: AbortSignal, timeout?: number, filter?: (entry: TarEntryStat) => boolean, preserveLinks?: boolean, preserveOwner?: boolean, preserveSpecialModes?: boolean }} [options]
   * @return {Promise<void>}
   */
  async extractAll (destDir, options = null) {
    this.#ensureOpen()
    const destBase = destDir instanceof URL ? destDir.pathname : String(destDir)
    const root = sysPath.resolve(destBase)
    await fs.promises.mkdir(root, { recursive: true })

    const rootStat = await lstatMaybe(root)
    if (rootStat?.isSymbolicLink()) {
      const err = new Error('Refusing to extract into a symlinked destination')
      err.code = 'ELOOP'
      throw err
    }

    const entries = await this.entries()
    const filter = options?.filter
    const preserveLinks = Boolean(options?.preserveLinks)
    const preserveOwner = Boolean(options?.preserveOwner)
    const preserveSpecialModes = Boolean(options?.preserveSpecialModes)
    const metadataOptions = { preserveOwner, preserveSpecialModes }
    const hardlinks = []
    const symlinks = []
    const directories = []

    for (const entry of entries) {
      const isLink = entry.kind === 'symlink' || entry.kind === 'hardlink'
      if (!entry.isFile && !entry.isDirectory && !(preserveLinks && isLink)) {
        continue
      }
      if (typeof filter === 'function' && !filter(entry)) continue

      const rawRelativePath = entry.path.replace(/^[/\\]+/, '')
      const parts = String(rawRelativePath || '')
        .split(/[\\/]+/)
        .filter((p) => p && p !== '.')

      if (parts.includes('..')) {
        const err = new Error('Invalid path segment in archive entry')
        err.code = 'EINVAL'
        throw err
      }

      if (!parts.length) continue

      const relativePath = parts.join('/')
      const candidate = sysPath.resolve(sysPath.join(root, relativePath))
      const relFromRoot = sysPath.relative(root, candidate)

      // Skip entries that would escape the destination root.
      if (relFromRoot.startsWith('..') || sysPath.isAbsolute(relFromRoot)) {
        continue
      }

      if (entry.isDirectory) {
        await ensureExtractPath(root, relFromRoot)
        directories.push({ entry, destPath: candidate, relFromRoot })
        continue
      }

      if (entry.isFile) {
        const parentRel = sysPath.dirname(relFromRoot)
        if (parentRel && parentRel !== '.') {
          await ensureExtractPath(root, parentRel)
        }

        const existing = await lstatMaybe(candidate)
        if (existing?.isSymbolicLink()) {
          const err = new Error(
            'Refusing to overwrite symlink during extraction'
          )
          err.code = 'ELOOP'
          throw err
        }

        if (Array.isArray(entry.sparse)) {
          await extractSparseFile(this, entry, candidate, {
            ...metadataOptions,
            signal: options?.signal,
            timeout: options?.timeout
          })
          continue
        }

        const stream = fs.createWriteStream(candidate, {
          signal: options?.signal,
          timeout: options?.timeout
        })

        let completed = false
        try {
          for await (const chunk of this.readStream(entry.path, {
            signal: options?.signal,
            timeout: options?.timeout
          })) {
            if (!stream.write(chunk)) {
              await new Promise((resolve, reject) => {
                const onError = (err) => {
                  stream.off('error', onError)
                  stream.off('drain', onDrain)
                  reject(err)
                }
                const onDrain = () => {
                  stream.off('error', onError)
                  stream.off('drain', onDrain)
                  resolve()
                }
                stream.once('error', onError)
                stream.once('drain', onDrain)
              })
            }
          }
          completed = true
        } finally {
          await new Promise((resolve) => {
            stream.end(() => resolve())
          })
        }

        if (completed) {
          await applyEntryMetadata(candidate, entry, metadataOptions)
        }
        continue
      }

      if (!preserveLinks) continue

      if (entry.kind === 'hardlink') {
        hardlinks.push({ entry, destPath: candidate, relFromRoot })
      } else if (entry.kind === 'symlink') {
        symlinks.push({ entry, destPath: candidate, relFromRoot })
      }
    }

    if (hardlinks.length) {
      let pending = hardlinks.slice()

      for (let pass = 0; pass < 1024 && pending.length; pass++) {
        const next = []
        let progressed = false

        for (const item of pending) {
          const parentRel = sysPath.dirname(item.relFromRoot)
          if (parentRel && parentRel !== '.') {
            await ensureExtractPath(root, parentRel)
          }

          const existing = await lstatMaybe(item.destPath)
          if (existing?.isSymbolicLink()) {
            const err = new Error(
              'Refusing to overwrite symlink during extraction'
            )
            err.code = 'ELOOP'
            throw err
          }

          if (!item.entry.linkpath) {
            const err = new Error(
              'Missing hardlink target path in archive entry'
            )
            err.code = 'EINVAL'
            throw err
          }

          try {
            const target = await resolveHardlinkTarget(
              root,
              item.entry.linkpath
            )
            await fs.promises.link(target, item.destPath)
            await applyEntryMetadata(item.destPath, item.entry, metadataOptions)
            progressed = true
          } catch (err) {
            if (err?.code === 'ENOENT') {
              next.push(item)
              continue
            }
            throw err
          }
        }

        if (!next.length) {
          pending = next
          break
        }

        if (!progressed) {
          const err = new Error(
            'Unable to resolve hardlink targets during extraction'
          )
          err.code = 'ELOOP'
          throw err
        }

        pending = next
      }

      if (pending.length) {
        const err = new Error(
          'Hardlink target resolution exceeded maximum depth'
        )
        err.code = 'ELOOP'
        throw err
      }
    }

    for (const item of symlinks) {
      const parentRel = sysPath.dirname(item.relFromRoot)
      if (parentRel && parentRel !== '.') {
        await ensureExtractPath(root, parentRel)
      }

      if (!item.entry.linkpath) {
        const err = new Error('Missing symlink target path in archive entry')
        err.code = 'EINVAL'
        throw err
      }

      await fs.promises.symlink(item.entry.linkpath, item.destPath)
      await applyEntryMetadata(item.destPath, item.entry, {
        ...metadataOptions,
        followSymlinks: false
      })
    }

    if (directories.length) {
      directories.sort((a, b) => b.relFromRoot.length - a.relFromRoot.length)

      for (const item of directories) {
        await applyEntryMetadata(item.destPath, item.entry, metadataOptions)
      }
    }
  }

  /**
   * Returns metadata for a single entry path.
   * @param {string} entryPath
   * @return {Promise<TarEntryStat>}
   */
  async stat (path) {
    this.#ensureOpen()
    const data = await send(ROUTES.stat, {
      id: this.id,
      path: normalizePath(path)
    })

    return {
      path: String(data.path),
      linkpath: data.linkpath ? String(data.linkpath) : undefined,
      devmajor: data.devmajor != null ? Number(data.devmajor) : undefined,
      devminor: data.devminor != null ? Number(data.devminor) : undefined,
      size: Number(data.size || 0),
      mode: Number(data.mode || 0),
      mtime: Number(data.mtime || 0),
      uid: Number(data.uid ?? 0),
      gid: Number(data.gid ?? 0),
      uname: data.uname ? String(data.uname) : undefined,
      gname: data.gname ? String(data.gname) : undefined,
      sparse: Array.isArray(data.sparse)
        ? data.sparse.map((region) => ({
          offset: Number(region.offset || 0),
          length: Number(region.length || 0)
        }))
        : undefined,
      kind: normalizeKind(data.type || '0'),
      isFile: Boolean(data.isFile),
      isDirectory: Boolean(data.isDirectory)
    }
  }

  /**
   * Reads a slice of an entry as a Buffer.
   * @param {string} path
   * @param {TarReadOptions} [options]
   * @return {Promise<Buffer>}
   */
  async read (path, options = null) {
    this.#ensureOpen()
    const normalizedPath = normalizePath(path)
    const stat = await this.stat(normalizedPath)

    if (stat.isDirectory) {
      const err = new Error('Cannot read body of directory entry')
      err.code = 'EISDIR'
      throw err
    }

    const offset = Number(options?.offset ?? 0)
    if (!Number.isFinite(offset) || offset < 0) {
      throw new RangeError('offset must be a non-negative finite number')
    }

    let length
    if (options?.length == null) {
      length = stat.size - offset
    } else {
      length = Number(options.length)
    }

    if (!Number.isFinite(length) || length < 0) {
      throw new RangeError('length must be a non-negative finite number')
    }

    if (offset >= stat.size || length === 0) {
      return Buffer.alloc(0)
    }

    const size = Math.min(length, stat.size - offset)
    if (size > 0xffffffff) {
      throw new RangeError('read() size exceeds maximum supported request size')
    }

    return await requestBinary(
      ROUTES.read,
      {
        id: this.id,
        path: normalizedPath,
        offset,
        size
      },
      options
    )
  }

  /**
   * Creates an async iterator that yields Buffer chunks for a given entry.
   * This provides a streaming decode interface without requiring Node.js streams.
   * @param {string} path
   * @param {TarReadStreamOptions} [options]
   * @return {AsyncIterableIterator<Buffer>}
   */
  readStream (path, options = null) {
    this.#ensureOpen()
    const archive = this
    const normalizedPath = normalizePath(path)

    const highWaterMark =
      typeof options?.highWaterMark === 'number'
        ? Math.floor(options.highWaterMark)
        : 64 * 1024

    if (!Number.isFinite(highWaterMark) || highWaterMark <= 0) {
      throw new RangeError('highWaterMark must be a positive finite number')
    }

    if (highWaterMark > 0xffffffff) {
      throw new RangeError(
        'highWaterMark exceeds maximum supported request size'
      )
    }

    const start =
      typeof options?.start === 'number' && options.start >= 0
        ? options.start
        : 0

    const signal = options?.signal
    const timeout = options?.timeout

    async function * iterator () {
      const stat = await archive.stat(normalizedPath)

      if (stat.isDirectory) {
        const err = new Error('Cannot read body of directory entry')
        err.code = 'EISDIR'
        throw err
      }

      if (stat.size === 0 || start >= stat.size) {
        return
      }

      const end =
        typeof options?.end === 'number' && options.end >= 0
          ? Math.min(options.end, stat.size - 1)
          : stat.size - 1

      let offset = start

      while (offset <= end) {
        const remaining = end - offset + 1
        const size = Math.min(highWaterMark, remaining)
        const chunk = await requestBinary(
          ROUTES.read,
          {
            id: archive.id,
            path: normalizedPath,
            offset,
            size
          },
          { signal, timeout }
        )

        if (!chunk.length) {
          break
        }

        offset += chunk.length
        yield chunk
      }
    }

    return iterator()
  }

  /**
   * Appends a single entry to the archive.
   * The entry body can be a Buffer, ArrayBuffer, Uint8Array, or an async iterable of Buffers.
   * @param {TarEntryHeader} header
   * @param {Buffer|Uint8Array|ArrayBuffer|AsyncIterable<Buffer|Uint8Array>} body
   * @param {TarWriteOptions} [options]
   * @return {Promise<void>}
   */
  async append (header, body, options = null) {
    this.#ensureOpen()

    if (!this.writable) {
      throw new Error('TarArchive is not opened in writable mode')
    }

    if (this.finalized) {
      throw new Error('TarArchive has been finalized')
    }

    if (!header || typeof header.path !== 'string' || !header.path.length) {
      throw new TypeError('header.path must be a non-empty string')
    }

    const path = normalizePath(header.path)
    if (!path.length) {
      throw new TypeError('header.path must be a non-empty string')
    }
    const kind = header.kind || 'file'
    let type = '0'

    switch (kind) {
      case 'directory':
        type = '5'
        break
      case 'symlink':
        type = '2'
        break
      case 'hardlink':
        type = '1'
        break
      case 'block-device':
        type = '4'
        break
      case 'char-device':
        type = '3'
        break
      case 'fifo':
        type = '6'
        break
      default:
        type = '0'
        break
    }

    const isAsyncIterable =
      body && typeof body[Symbol.asyncIterator] === 'function'

    const isLink = kind === 'symlink' || kind === 'hardlink'
    const linkpath = isLink ? header.linkpath : null
    const isDevice = kind === 'block-device' || kind === 'char-device'
    const isBodyless =
      kind === 'directory' ||
      kind === 'block-device' ||
      kind === 'char-device' ||
      kind === 'fifo'

    if (isLink) {
      if (typeof linkpath !== 'string' || !linkpath.length) {
        throw new TypeError(
          'header.linkpath must be a non-empty string for link entries'
        )
      }

      if (isAsyncIterable) {
        throw new TypeError('link entries do not support AsyncIterable bodies')
      }
    }

    let devmajor = 0
    let devminor = 0

    if (isDevice) {
      if (header.devmajor == null || header.devminor == null) {
        throw new TypeError(
          'header.devmajor and header.devminor are required for device entries'
        )
      }

      devmajor = normalizeUint32(header.devmajor, 'header.devmajor')
      devminor = normalizeUint32(header.devminor, 'header.devminor')
    }

    if (isBodyless && isAsyncIterable) {
      throw new TypeError(`${kind} entries do not support AsyncIterable bodies`)
    }

    if (typeof header.size === 'number') {
      if (!Number.isFinite(header.size) || header.size < 0) {
        throw new RangeError('header.size must be a non-negative finite number')
      }

      if (!Number.isSafeInteger(header.size)) {
        throw new RangeError('header.size must be a non-negative safe integer')
      }
    }

    if (isAsyncIterable) {
      if (
        typeof header.size !== 'number' ||
        !Number.isFinite(header.size) ||
        header.size < 0
      ) {
        throw new TypeError(
          'header.size must be a non-negative finite number when body is an AsyncIterable'
        )
      }
    }

    let size = 0
    if (typeof header.size === 'number') {
      size = header.size
    }

    if (isLink || isBodyless) {
      if (size !== 0) {
        const label = isLink ? 'link' : kind
        throw new RangeError(`header.size must be 0 for ${label} entries`)
      }

      if (body != null) {
        const buf = Buffer.isBuffer(body) ? body : Buffer.from(body)
        if (buf.length !== 0) {
          const label = isLink ? 'link' : kind
          throw new RangeError(`${label} entries must not include a body`)
        }
      }

      body = null
      size = 0
    } else if (!isAsyncIterable && body != null) {
      const buf = Buffer.isBuffer(body) ? body : Buffer.from(body)

      if (
        typeof header.size === 'number' &&
        header.size >= 0 &&
        buf.length !== size
      ) {
        throw new RangeError(
          'header.size does not match body length when provided'
        )
      }

      if (size === 0) {
        size = buf.length
      }

      body = buf
    }

    if (body == null && size > 0) {
      throw new TypeError('body must be provided when header.size is non-zero')
    }

    const hasSparse = Array.isArray(header.sparse)
    const hasSparseSize = typeof header.sparseSize === 'number'

    if (hasSparseSize && !hasSparse) {
      throw new TypeError('header.sparseSize requires header.sparse')
    }

    let sparse = null
    let sparseSize = 0

    if (hasSparse) {
      if (kind !== 'file') {
        throw new TypeError('header.sparse is only supported for file entries')
      }

      if (typeof header.linkpath === 'string' && header.linkpath.length) {
        throw new TypeError(
          'header.linkpath must not be set for sparse entries'
        )
      }

      if (header.sparse.length > 16384) {
        throw new RangeError(
          'header.sparse exceeds maximum supported region count'
        )
      }

      sparse = []

      let storedSize = 0
      let lastEnd = 0

      for (const region of header.sparse) {
        if (!region || typeof region !== 'object') {
          throw new TypeError('header.sparse entries must be objects')
        }

        const offset = Number(region.offset)
        const length = Number(region.length)

        if (!Number.isSafeInteger(offset) || offset < 0) {
          throw new RangeError(
            'sparse region offset must be a non-negative integer'
          )
        }

        if (!Number.isSafeInteger(length) || length <= 0) {
          throw new RangeError(
            'sparse region length must be a positive integer'
          )
        }

        if (offset < lastEnd) {
          throw new RangeError(
            'sparse regions must be sorted and non-overlapping'
          )
        }

        const end = offset + length
        if (!Number.isSafeInteger(end) || end < offset) {
          throw new RangeError('sparse region offset+length overflow')
        }

        storedSize += length
        if (!Number.isSafeInteger(storedSize)) {
          throw new RangeError('sparse region lengths overflow stored size')
        }

        sparse.push({ offset, length })
        lastEnd = end
      }

      if (typeof header.sparseSize === 'number') {
        sparseSize = Number(header.sparseSize)

        if (!Number.isSafeInteger(sparseSize) || sparseSize < 0) {
          throw new RangeError(
            'header.sparseSize must be a non-negative integer'
          )
        }
      } else {
        sparseSize = lastEnd
      }

      if (sparseSize < lastEnd) {
        throw new RangeError(
          'header.sparseSize must be at least the end of the last sparse region'
        )
      }

      if (storedSize !== size) {
        throw new RangeError(
          'header.size/body length must equal the sum of sparse region lengths'
        )
      }
    }

    const defaultMode = kind === 'directory' ? 0o755 : 0o644
    const mode =
      header.mode == null
        ? defaultMode
        : normalizeUint32(header.mode, 'header.mode')

    const beginParams = {
      id: this.id,
      path,
      size,
      mode,
      type
    }

    if (header.mtime != null) {
      beginParams.mtime = normalizeMtime(header.mtime, 'header.mtime')
    }

    if (header.uid != null) {
      beginParams.uid = normalizeUint32(header.uid, 'header.uid')
    }

    if (header.gid != null) {
      beginParams.gid = normalizeUint32(header.gid, 'header.gid')
    }

    if (header.uname != null) {
      if (typeof header.uname !== 'string') {
        throw new TypeError('header.uname must be a string')
      }
      beginParams.uname = header.uname
    }

    if (header.gname != null) {
      if (typeof header.gname !== 'string') {
        throw new TypeError('header.gname must be a string')
      }
      beginParams.gname = header.gname
    }

    if (isLink) {
      beginParams.linkpath = linkpath
    }

    if (isDevice) {
      beginParams.devmajor = devmajor
      beginParams.devminor = devminor
    }

    if (sparse) {
      beginParams.sparseSize = sparseSize
      beginParams.sparse = JSON.stringify(sparse)
    }

    await send(ROUTES.writeBegin, beginParams)

    if (isAsyncIterable) {
      let remaining = size
      let wroteAny = false

      for await (const chunk of body) {
        const buf = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk)
        if (!buf.length) continue

        if (buf.length > remaining) {
          throw new RangeError('body yielded more data than header.size allows')
        }

        wroteAny = true
        const result = await ipc.write(
          ROUTES.writeData,
          {
            id: this.id
          },
          buf,
          {
            timeout: options?.timeout,
            signal: options?.signal
          }
        )

        if (result.err) {
          throw result.err
        }

        remaining -= buf.length
      }

      if (remaining !== 0) {
        throw new RangeError('body ended before header.size bytes were yielded')
      }

      if (!wroteAny) {
        const result = await ipc.write(
          ROUTES.writeData,
          { id: this.id },
          Buffer.alloc(0),
          {
            timeout: options?.timeout,
            signal: options?.signal
          }
        )

        if (result.err) {
          throw result.err
        }
      }
    } else if (body != null) {
      const buf = Buffer.isBuffer(body) ? body : Buffer.from(body)
      const result = await ipc.write(ROUTES.writeData, { id: this.id }, buf, {
        timeout: options?.timeout,
        signal: options?.signal
      })

      if (result.err) {
        throw result.err
      }
    } else {
      const result = await ipc.write(
        ROUTES.writeData,
        { id: this.id },
        Buffer.alloc(0),
        {
          timeout: options?.timeout,
          signal: options?.signal
        }
      )

      if (result.err) {
        throw result.err
      }
    }
  }

  /**
   * Finalizes the archive, writing terminating blocks and flushing the sink.
   * After calling this, the archive is still considered open but no further
   * writes should be performed.
   * @return {Promise<void>}
   */
  async finalize () {
    this.#ensureOpen()
    if (!this.writable) {
      throw new Error('TarArchive is not opened in writable mode')
    }

    await send(ROUTES.finalize, { id: this.id })
    this.finalized = true
  }

  /**
   * Finalizes the archive if necessary and returns the underlying tar
   * archive bytes as a Buffer. For in-memory writable archives this
   * contains the composed archive; for read-only archives created via
   * fromBuffer it returns the original buffer.
   * @param {{ signal?: AbortSignal, timeout?: number }} [options]
   * @return {Promise<Buffer>}
   */
  async toBuffer (options = null) {
    this.#ensureOpen()
    if (this.writable && !this.finalized) {
      await this.finalize()
    }

    if (this.path) {
      return await fs.promises.readFile(this.path, { signal: options?.signal })
    }

    return await requestBinary('tar.buffer', { id: this.id }, options)
  }

  /**
   * Extracts a single entry to a destination path on disk.
   * This helper uses streaming reads for large entries.
   * @param {string} path
   * @param {string|URL} destPath
   * @param {{ signal?: AbortSignal, timeout?: number, preserveOwner?: boolean, preserveSpecialModes?: boolean }} [options]
   * @return {Promise<void>}
   */
  async extract (entryPath, destPath, options = null) {
    this.#ensureOpen()
    const normalizedPath = normalizePath(entryPath)
    const entry = await this.stat(normalizedPath)
    const metadataOptions = {
      preserveOwner: Boolean(options?.preserveOwner),
      preserveSpecialModes: Boolean(options?.preserveSpecialModes)
    }

    const dest = destPath instanceof URL ? destPath.pathname : String(destPath)
    const destRoot = sysPath.resolve(dest)

    if (entry.isDirectory) {
      await ensureExtractFilesystemPath(destRoot)
      await applyEntryMetadata(destRoot, entry, metadataOptions)
      return
    }

    const destDir = sysPath.dirname(destRoot)

    await ensureExtractFilesystemPath(destDir)

    const existing = await lstatMaybe(destRoot)
    if (existing?.isSymbolicLink()) {
      const err = new Error('Refusing to overwrite symlink during extraction')
      err.code = 'ELOOP'
      throw err
    }

    if (entry.kind === 'symlink') {
      if (!entry.linkpath) {
        const err = new Error('Missing symlink target path in archive entry')
        err.code = 'EINVAL'
        throw err
      }

      await fs.promises.symlink(entry.linkpath, destRoot)
      await applyEntryMetadata(destRoot, entry, {
        ...metadataOptions,
        followSymlinks: false
      })
      return
    }

    if (entry.kind === 'hardlink') {
      if (!entry.linkpath) {
        const err = new Error('Missing hardlink target path in archive entry')
        err.code = 'EINVAL'
        throw err
      }

      const target = await resolveHardlinkTarget(destDir, entry.linkpath)
      await fs.promises.link(target, destRoot)
      await applyEntryMetadata(destRoot, entry, metadataOptions)
      return
    }

    if (!entry.isFile) {
      const err = new Error('Unsupported tar entry type')
      err.code = 'EINVAL'
      throw err
    }

    if (Array.isArray(entry.sparse)) {
      await extractSparseFile(this, entry, destRoot, {
        ...metadataOptions,
        signal: options?.signal,
        timeout: options?.timeout
      })
      return
    }

    const stream = fs.createWriteStream(destRoot, {
      signal: options?.signal,
      timeout: options?.timeout
    })

    let completed = false
    try {
      for await (const chunk of this.readStream(normalizedPath, {
        signal: options?.signal,
        timeout: options?.timeout
      })) {
        if (!stream.write(chunk)) {
          await new Promise((resolve, reject) => {
            const onError = (err) => {
              stream.off('error', onError)
              stream.off('drain', onDrain)
              reject(err)
            }
            const onDrain = () => {
              stream.off('error', onError)
              stream.off('drain', onDrain)
              resolve()
            }
            stream.once('error', onError)
            stream.once('drain', onDrain)
          })
        }
      }
      completed = true
    } finally {
      await new Promise((resolve) => {
        stream.end(() => resolve())
      })
    }

    if (completed) {
      await applyEntryMetadata(destRoot, entry, metadataOptions)
    }
  }
}

/**
 * Opens an existing tar archive.
 * @param {string|URL} path
 * @param {TarOpenOptions} [options]
 * @return {Promise<TarArchive>}
 */
export async function open (path, options = null) {
  return TarArchive.open(path, options)
}

/**
 * Creates a new tar archive for writing.
 * If the archive already exists it will be truncated.
 * @param {string|URL} path
 * @param {TarOpenOptions} [options]
 * @return {Promise<TarArchive>}
 */
export async function create (path, options = null) {
  return TarArchive.create(path, options)
}

/**
 * Opens a tar archive from an in-memory buffer (read-only).
 * @param {Buffer|Uint8Array|ArrayBuffer} buffer
 * @return {Promise<TarArchive>}
 */
export async function fromBuffer (buffer) {
  return TarArchive.fromBuffer(buffer)
}

/**
 * Creates a new in-memory tar archive for writing.
 * @param {TarOpenOptions} [options]
 * @return {Promise<TarArchive>}
 */
export async function createInMemory (options = null) {
  const params = {}

  if (options?.uid != null) {
    params.uid = normalizeUint32(options.uid, 'options.uid')
  }
  if (options?.gid != null) {
    params.gid = normalizeUint32(options.gid, 'options.gid')
  }
  if (options?.mtime != null) {
    params.mtime = normalizeMtime(options.mtime, 'options.mtime')
  }
  if (options?.uname != null) {
    if (typeof options.uname !== 'string') {
      throw new TypeError('options.uname must be a string')
    }
    params.uname = options.uname
  }
  if (options?.gname != null) {
    if (typeof options.gname !== 'string') {
      throw new TypeError('options.gname must be a string')
    }
    params.gname = options.gname
  }

  const data = await send('tar.createBuffer', params)
  const id = data?.id
  if (!id) {
    throw new Error('tar.createBuffer did not return an archive id')
  }

  return new TarArchive({
    id,
    path: '',
    writable: true,
    mmap: false,
    size: 0,
    entryCount: 0
  })
}

const api = Object.freeze({
  TarArchive,
  open,
  create,
  fromBuffer,
  createInMemory
})

export default api
