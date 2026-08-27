/**
 * @module fs
 *
 * This module enables interacting with the file system in a way modeled on
 * standard POSIX functions.
 *
 * The Application Sandbox restricts access to the file system.
 *
 * iOS Application Sandboxing has a set of rules that limits access to the file
 * system. Apps can only access files in their own sandboxed home directory.
 *
 * | Directory | Description |
 * | --- | --- |
 * | `Documents` | The app’s sandboxed documents directory. The contents of this directory are backed up by iTunes and may be set as accessible to the user via iTunes when `UIFileSharingEnabled` is set to `true` in the application's `info.plist`. |
 * | `Library` | The app’s sandboxed library directory. The contents of this directory are synchronized via iTunes (except the `Library/Caches` subdirectory, see below), but never exposed to the user. |
 * | `Library/Caches` | The app’s sandboxed caches directory. The contents of this directory are not synchronized via iTunes and may be deleted by the system at any time. It's a good place to store data which provides a good offline-first experience for the user. |
 * | `Library/Preferences` | The app’s sandboxed preferences directory. The contents of this directory are synchronized via iTunes. Its purpose is to be used by the Settings app. Avoid creating your own files in this directory. |
 * | `tmp` | The app’s sandboxed temporary directory. The contents of this directory are not synchronized via iTunes and may be deleted by the system at any time. Although, it's recommended that you delete data that is not necessary anymore manually to minimize the space your app takes up on the file system. Use this directory to store data that is only useful during the app runtime. |
 *
 * Example usage:
 * ```js
 * import * as fs from 'oro:fs';
 * ```
 */

import { isBufferLike, isFunction, noop } from '../util.js'
import { rand64 } from '../crypto.js'
import { Buffer } from '../buffer.js'
import ipc from '../ipc.js'
import gc from '../gc.js'

import { Dir, Dirent, sortDirectoryEntries } from './dir.js'
import { DirectoryHandle, FileHandle } from './handle.js'
import { ReadStream, WriteStream } from './stream.js'
import { normalizeFlags } from './flags.js'
import * as constants from './constants.js'
import * as promises from './promises.js'
import { Watcher } from './watcher.js'
import { Stats } from './stats.js'
import bookmarks from './bookmarks.js'
import fds from './fds.js'

import * as exports from './index.js'

const kFileDescriptor = Symbol.for('oro.runtime.fs.web.FileDescriptor')
const kFileFullName = Symbol.for('oro.runtime.fs.web.FileFullName')
const kFileSystemHandleFullName = Symbol.for(
  'oro.runtime.fs.web.FileSystemHandleFullName'
)
const kWatchFileRegistry = Symbol.for('oro.runtime.fs.watchFileRegistry')

/**
 * @typedef {Uint8Array|Int8Array} TypedArray
 * @ignore
 */
function defaultCallback (err) {
  if (err) throw err
}

function normalizePath (path) {
  if (path instanceof URL) {
    if (path.origin === globalThis.location.origin) {
      return normalizePath(path.href)
    }

    return null
  }

  if (typeof path === 'string') {
    try {
      if (URL.canParse(path)) {
        const url = new URL(path)
        if (url.origin === globalThis.location.origin) {
          path = `./${url.pathname.slice(1)}`
        }
      }
    } catch {
      // Relative and platform-native paths are not required to parse as URLs.
    }
  }

  return path
}

function getDescriptorId (fd) {
  const candidate = fd?.id ?? fd
  const id = fds.id(candidate) || (fds.get(candidate) ? candidate : null)
  if (!id) {
    throw new Error('Invalid file descriptor.')
  }
  return id
}

function toSeconds (value) {
  if (value instanceof Date) return value.getTime() / 1000
  if (typeof value === 'string') {
    const n = Number(value)
    if (!Number.isFinite(n)) {
      throw new TypeError('time must be a number, Date, or numeric string')
    }
    return n
  }
  if (typeof value === 'number') return value
  throw new TypeError('time must be a number, Date, or numeric string')
}

function joinPath (base, name) {
  const sep = base.includes('\\') ? '\\' : '/'
  return base.endsWith(sep) ? base + name : base + sep + name
}

// Simple polling-based watchFile registry
globalThis[kWatchFileRegistry] = globalThis[kWatchFileRegistry] || new Map()
function getWatchFileEntry (path) {
  const reg = globalThis[kWatchFileRegistry]
  let entry = reg.get(path)
  if (!entry) {
    entry = {
      timer: null,
      listeners: new Set(),
      prev: null,
      interval: 5007,
      options: {}
    }
    reg.set(path, entry)
  }
  return entry
}

/**
 * Polls for file changes and invokes listener with (curr, prev) Stats.
 * This is a compatibility helper; prefer fs.watch for evented changes.
 * @param {string} path
 * @param {object|function} [options]
 * @param {number} [options.interval=5007]
 * @param {boolean} [options.bigint=false]
 * @param {function(Stats, Stats)} [listener]
 */
export function watchFile (path, options, listener) {
  if (typeof options === 'function') {
    listener = options
    options = {}
  }
  if (typeof listener !== 'function') {
    throw new TypeError('listener must be a function')
  }
  path = normalizePath(path)
  const entry = getWatchFileEntry(path)
  entry.listeners.add(listener)
  entry.interval = Number.isFinite(options?.interval)
    ? options.interval
    : entry.interval
  entry.options = options || {}

  const poll = async () => {
    try {
      const curr = await new Promise((resolve, reject) =>
        stat(path, entry.options, (err, stats) =>
          err ? reject(err) : resolve(stats)
        )
      )
      if (!entry.prev) {
        entry.prev = curr
      } else {
        const prev = entry.prev
        // compare selected fields
        if (
          curr.mtimeMs !== prev.mtimeMs ||
          curr.ctimeMs !== prev.ctimeMs ||
          curr.size !== prev.size ||
          curr.mode !== prev.mode
        ) {
          for (const fn of entry.listeners) {
            try {
              fn(curr, prev)
            } catch {}
          }
          entry.prev = curr
        }
      }
    } catch {
      // On error, emit once and keep polling
      // Consumers expect (curr, prev); we cannot create valid Stats on error
    }
  }

  if (!entry.timer) {
    // prime prev
    try {
      entry.prev = statSync(path, entry.options)
    } catch {}
    entry.timer = setInterval(poll, entry.interval)
  }
}

/**
 * Removes a watchFile listener or stops watching entirely for a path.
 * @param {string} path
 * @param {function=} listener
 */
export function unwatchFile (path, listener = null) {
  path = normalizePath(path)
  const reg = globalThis[kWatchFileRegistry]
  const entry = reg.get(path)
  if (!entry) return
  if (typeof listener === 'function') {
    entry.listeners.delete(listener)
  } else {
    entry.listeners.clear()
  }
  if (entry.listeners.size === 0 && entry.timer) {
    clearInterval(entry.timer)
    reg.delete(path)
  }
}

async function visit (path, options = null, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  path = normalizePath(path)

  const { flags, flag, mode } = options || {}

  let handle = null
  try {
    handle = await FileHandle.open(path, flags || flag, mode, options)
  } catch (err) {
    return callback(err)
  }

  if (handle) {
    await callback(null, handle)

    try {
      await handle.close(options)
    } catch (err) {
      console.warn(err.message || err)
    }
  }
}

/**
 * Asynchronously check access to a file for a given mode calling `callback`
 * upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsopenpath-flags-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {number|function(Error|null):any} [mode = F_OK(0)]
 * @param {function(Error|null):any} [callback]
 */
export function access (path, mode, callback) {
  if (typeof mode === 'function') {
    callback = mode
    mode = FileHandle.DEFAULT_ACCESS_MODE
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  if (
    path instanceof globalThis.FileSystemFileHandle ||
    path instanceof globalThis.FileSystemDirectoryHandle
  ) {
    return queueMicrotask(() => callback(null, mode))
  }

  path = normalizePath(path)

  FileHandle.access(path, mode)
    .then((mode) => callback(null, mode))
    .catch((err) => callback(err))
}

/**
 * Synchronously check access to a file for a given mode calling `callback`
 * upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsopenpath-flags-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {number} [mode = F_OK(0)]
 */
export function accessSync (path, mode = constants.F_OK) {
  path = normalizePath(path)
  const result = ipc.sendSync('fs.access', { path, mode })

  if (result.err) {
    throw result.err
  }

  // F_OK means access in any way
  return mode === constants.F_OK ? true : (result.data?.mode && mode) > 0
}

/**
 * Checks if a path exists
 * @param {string | Buffer | URL} path
 * @param {function(Boolean)?} [callback]
 */
export function exists (path, callback) {
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  path = normalizePath(path)
  access(path, (err) => {
    // callback argument is true when the path exists
    // eslint-disable-next-line n/no-callback-literal
    callback(err === null)
  })
}

/**
 * Checks if a path exists
 * @param {string | Buffer | URL} path
 * @param {function(Boolean)?} [callback]
 */
export function existsSync (path) {
  path = normalizePath(path)
  try {
    accessSync(path)
    return true
  } catch {
    return false
  }
}

/**
 * Asynchronously changes the permissions of a file.
 * No arguments other than a possible exception are given to the completion callback
 *
 * @see {@link https://nodejs.org/api/fs.html#fschmodpath-mode-callback}
 *
 * @param {string | Buffer | URL} path
 * @param {number} mode
 * @param {function(Error?)} callback
 */
export function chmod (path, mode, callback) {
  if (typeof mode !== 'number') {
    throw new TypeError(
      `The argument 'mode' must be a 32-bit unsigned integer or an octal string. Received ${mode}`
    )
  }

  if (mode < 0 || !Number.isInteger(mode)) {
    throw new RangeError(
      `The value of "mode" is out of range. It must be an integer. Received ${mode}`
    )
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  if (
    path instanceof globalThis.FileSystemFileHandle ||
    path instanceof globalThis.FileSystemDirectoryHandle
  ) {
    return new TypeError('FileSystemHandle is not writable')
  }

  path = normalizePath(path)
  ipc.request('fs.chmod', { mode, path }).then((result) => {
    if (result?.err) {
      callback(result.err)
    } else {
      callback(null)
    }
  })
}

/**
 * Synchronously changes the permissions of a file.
 *
 * @see {@link https://nodejs.org/api/fs.html#fschmodpath-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {number} mode
 */
export function chmodSync (path, mode) {
  if (typeof mode !== 'number') {
    throw new TypeError(
      `The argument 'mode' must be a 32-bit unsigned integer or an octal string. Received ${mode}`
    )
  }

  if (mode < 0 || !Number.isInteger(mode)) {
    throw new RangeError(
      `The value of "mode" is out of range. It must be an integer. Received ${mode}`
    )
  }

  path = normalizePath(path)
  const result = ipc.sendSync('fs.chmod', { mode, path })

  if (result.err) {
    throw result.err
  }
}

/**
 * Changes ownership of file or directory at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 * @param {function} callback
 */
export function chown (path, uid, gid, callback) {
  path = normalizePath(path)
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (!Number.isInteger(uid)) {
    throw new TypeError("The argument 'uid' must be an integer")
  }

  if (!Number.isInteger(gid)) {
    throw new TypeError("The argument 'gid' must be an integer")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  if (
    path instanceof globalThis.FileSystemFileHandle ||
    path instanceof globalThis.FileSystemDirectoryHandle
  ) {
    return new TypeError('FileSystemHandle is not writable')
  }

  ipc
    .request('fs.chown', { path, uid, gid })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Changes ownership of file or directory at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 */
export function chownSync (path, uid, gid) {
  path = normalizePath(path)
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (!Number.isInteger(uid)) {
    throw new TypeError("The argument 'uid' must be an integer")
  }

  if (!Number.isInteger(gid)) {
    throw new TypeError("The argument 'gid' must be an integer")
  }

  const result = ipc.sendSync('fs.chown', { path, uid, gid })

  if (result.err) {
    throw result.err
  }
}

/**
 * Asynchronously close a file descriptor calling `callback` upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsclosefd-callback}
 * @param {number} fd
 * @param {function(Error?)?} [callback]
 */
export function close (fd, callback) {
  if (typeof callback !== 'function') {
    callback = defaultCallback
  }

  try {
    FileHandle.from(fd)
      .close()
      .then(() => callback(null))
      .catch((err) => callback(err))
  } catch (err) {
    callback(err)
  }
}

/**
 * Synchronously close a file descriptor.
 * @param {number} fd  - fd
 */
export function closeSync (fd) {
  const id = fds.id(fd) || fd
  const result = ipc.sendSync('fs.close', { id })
  if (result.err) {
    throw result.err
  }
  fds.release(id, false)
}

/**
 * Asynchronously copies `src` to `dest` calling `callback` upon success or error.
 * @param {string} src - The source file path.
 * @param {string} dest - The destination file path.
 * @param {number} flags - Modifiers for copy operation.
 * @param {function(Error=)=} [callback] - The function to call after completion.
 * @see {@link https://nodejs.org/api/fs.html#fscopyfilesrc-dest-mode-callback}
 */
export function copyFile (src, dest, flags = 0, callback) {
  if (typeof flags === 'function') {
    callback = flags
    flags = 0
  }

  dest = normalizePath(dest)

  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }

  if (flags && !Number.isInteger(flags)) {
    throw new TypeError("The argument 'flags' must be an integer")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  if (src instanceof globalThis.FileSystemFileHandle) {
    Promise.resolve(src.getFile())
      .then(async (file) => {
        const filename =
          file?.[kFileDescriptor]?.path ||
          file?.[kFileFullName] ||
          src[kFileSystemHandleFullName]
        if (filename) {
          copyFile(filename, dest, flags, callback)
        } else {
          writeFile(dest, await file.arrayBuffer(), { flags }, callback)
        }
      })
      .catch(callback)
    return
  }

  src = normalizePath(src)
  if (typeof src !== 'string') {
    throw new TypeError("The argument 'src' must be a string")
  }

  ipc
    .request('fs.copyFile', { src, dest, flags })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Synchronously copies `src` to `dest` calling `callback` upon success or error.
 * @param {string} src - The source file path.
 * @param {string} dest - The destination file path.
 * @param {number} flags - Modifiers for copy operation.
 * @see {@link https://nodejs.org/api/fs.html#fscopyfilesrc-dest-mode-callback}
 */
export function copyFileSync (src, dest, flags = 0) {
  src = normalizePath(src)
  dest = normalizePath(dest)

  if (typeof src !== 'string') {
    throw new TypeError("The argument 'src' must be a string")
  }

  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }

  if (!Number.isInteger(flags)) {
    throw new TypeError("The argument 'flags' must be an integer")
  }

  const result = ipc.sendSync('fs.copyFile', { src, dest, flags })

  if (result.err) {
    throw result.err
  }
}

/**
 * @see {@link https://nodejs.org/api/fs.html#fscreatewritestreampath-options}
 * @param {string | Buffer | URL} path
 * @param {object?} [options]
 * @returns {ReadStream}
 */
export function createReadStream (path, options) {
  if (path?.fd) {
    options = path
    path = options?.path || null
  }

  path = normalizePath(path)

  let handle = null
  const stream = new ReadStream({
    autoClose: typeof options?.fd !== 'number',
    ...options
  })

  if (options?.fd) {
    handle = FileHandle.from(options.fd)
  } else {
    // @ts-ignore
    handle = new FileHandle({ flags: 'r', path, ...options })
    // @ts-ignore
    handle.open(options).catch((err) => stream.emit('error', err))
  }

  stream.once('end', async () => {
    if (options?.autoClose !== false) {
      try {
        await handle.close(options)
      } catch (err) {
        // @ts-ignore
        stream.emit('error', err)
      }
    }
  })

  stream.setHandle(handle)

  return stream
}

/**
 * @see {@link https://nodejs.org/api/fs.html#fscreatewritestreampath-options}
 * @param {string | Buffer | URL} path
 * @param {object?} [options]
 * @returns {WriteStream}
 */
export function createWriteStream (path, options) {
  if (path?.fd) {
    options = path
    path = options?.path || null
  }

  if (path instanceof globalThis.FileSystemHandle) {
    return new TypeError('FileSystemHandle is not writable')
  }

  path = normalizePath(path)

  let handle = null
  const stream = new WriteStream({
    autoClose: typeof options?.fd !== 'number',
    ...options
  })

  if (typeof options?.fd === 'number') {
    handle = FileHandle.from(options.fd)
  } else {
    handle = new FileHandle({ flags: 'w', path, ...options })
    // @ts-ignore
    handle.open(options).catch((err) => stream.emit('error', err))
  }

  stream.once('finish', async () => {
    if (options?.autoClose !== false) {
      try {
        await handle.close(options)
      } catch (err) {
        // @ts-ignore
        stream.emit('error', err)
      }
    }
  })

  stream.setHandle(handle)

  return stream
}

/**
 * Invokes the callback with the <fs.Stats> for the file descriptor. See
 * the POSIX fstat(2) documentation for more detail.
 *
 * @see {@link https://nodejs.org/api/fs.html#fsfstatfd-options-callback}
 *
 * @param {number} fd - A file descriptor.
 * @param {object?|function?} [options] - An options object.
 * @param {function?} callback - The function to call after completion.
 */
export function fstat (fd, options, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  try {
    FileHandle.from(fd)
      .stat(options)
      .then((stats) => callback(null, stats))
      .catch((err) => callback(err))
  } catch (err) {
    callback(err)
  }
}

/**
 * Request that all data for the open file descriptor is flushed
 * to the storage device.
 * @param {number} fd - A file descriptor.
 * @param {function} callback - The function to call after completion.
 */
export function fsync (fd, callback) {
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  try {
    FileHandle.from(fd)
      .sync()
      .then(() => callback(null))
      .catch((err) => callback(err))
  } catch (err) {
    callback(err)
  }
}

/**
 * Truncates the file up to `offset` bytes.
 * @param {number} fd - A file descriptor.
 * @param {number=|function} [offset = 0]
 * @param {function?} callback - The function to call after completion.
 */
export function ftruncate (fd, offset, callback) {
  if (typeof offset === 'function') {
    callback = offset
    offset = {}
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  try {
    FileHandle.from(fd)
      .truncate(offset)
      .then(() => callback(null))
      .catch((err) => callback(err))
  } catch (err) {
    callback(err)
  }
}

/**
 * Changes ownership of a symbolic link at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 * @param {function} callback
 */
export function lchown (path, uid, gid, callback) {
  if (
    path instanceof globalThis.FileSystemFileHandle ||
    path instanceof globalThis.FileSystemDirectoryHandle
  ) {
    return new TypeError('FileSystemHandle is not writable')
  }

  path = normalizePath(path)

  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (!Number.isInteger(uid)) {
    throw new TypeError("The argument 'uid' must be an integer")
  }

  if (!Number.isInteger(gid)) {
    throw new TypeError("The argument 'gid' must be an integer")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  ipc
    .request('fs.lchown', { path, uid, gid })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Changes permissions of link at `path` with `mode` (POSIX). No-op where unsupported.
 * @param {string|Buffer|URL} path
 * @param {number} mode
 * @param {function(Error|null):any} callback
 */
export function lchmod (path, mode, callback) {
  path = normalizePath(path)
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }
  if (typeof mode !== 'number') {
    throw new TypeError("The argument 'mode' must be a number")
  }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  ipc
    .request('fs.lchmod', { path, mode })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Synchronously changes permissions of a symbolic link at `path`.
 *
 * On platforms that do not implement `lchmod`, the native backend may treat
 * this as a no-op or return a platform-specific error.
 * @param {string|Buffer|URL} path
 * @param {number} mode
 */
export function lchmodSync (path, mode) {
  path = normalizePath(path)
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }
  if (typeof mode !== 'number') {
    throw new TypeError("The argument 'mode' must be a number")
  }
  const result = ipc.sendSync('fs.lchmod', { path, mode })
  if (result.err) throw result.err
}

/**
 * Creates a link to `dest` from `src`.
 * @param {string} src
 * @param {string} dest
 * @param {function}
 */
export function link (src, dest, callback) {
  src = normalizePath(src)
  dest = normalizePath(dest)

  if (typeof src !== 'string') {
    throw new TypeError("The argument 'src' must be a string")
  }

  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  ipc
    .request('fs.link', { src, dest })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Creates a hard link synchronously
 * @param {string} src
 * @param {string} dest
 */
export function linkSync (src, dest) {
  src = normalizePath(src)
  dest = normalizePath(dest)
  if (typeof src !== 'string') {
    throw new TypeError("The argument 'src' must be a string")
  }
  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }
  const result = ipc.sendSync('fs.link', { src, dest })
  if (result.err) throw result.err
}

/**
 * @ignore
 */
export function mkdir (path, options, callback) {
  path = normalizePath(path)

  if (typeof options === 'function') {
    callback = options
    options = null
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  const mode = options.mode || 0o777
  const recursive = Boolean(options.recursive) // default to false

  if (typeof mode !== 'number') {
    throw new TypeError('mode must be a number.')
  }

  if (mode < 0 || !Number.isInteger(mode)) {
    throw new RangeError('mode must be a positive finite number.')
  }

  ipc
    .request('fs.mkdir', { mode, path, recursive })
    .then((result) => (result?.err ? callback(result.err) : callback(null)))
    .catch((err) => callback(err))
}

/**
 * @ignore
 * @param {string|URL} path
 * @param {object=} [options]
 */
export function mkdirSync (path, options = null) {
  path = normalizePath(path)

  const mode = options?.mode || 0o777
  const recursive = Boolean(options?.recursive) // default to false

  if (typeof mode !== 'number') {
    throw new TypeError('mode must be a number.')
  }

  if (mode < 0 || !Number.isInteger(mode)) {
    throw new RangeError('mode must be a positive finite number.')
  }

  const result = ipc.sendSync('fs.mkdir', { mode, path, recursive })

  if (result.err) {
    throw result.err
  }
}

/**
 * Create a unique temporary directory. The `prefix` is appended with a
 * platform-specific unique suffix.
 * @param {string} prefix
 * @param {object|string|function} [options]
 * @param {string} [options.encoding='utf8']
 * @param {function(Error|null, string|Buffer):any} [callback]
 */
export function mkdtemp (prefix, options, callback) {
  if (typeof options === 'function') {
    callback = options
    options = null
  }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  if (typeof prefix !== 'string') throw new TypeError('prefix must be a string')
  ipc
    .request('fs.mkdtemp', { prefix })
    .then((result) => {
      if (result?.err) return callback(result.err)
      const path = result.data.path
      const enc = typeof options === 'string' ? options : options?.encoding
      if (enc === 'buffer') return callback(null, Buffer.from(path))
      callback(null, path)
    })
    .catch(callback)
}

/** Create a unique temporary directory synchronously */
export function mkdtempSync (prefix, options = null) {
  if (typeof prefix !== 'string') throw new TypeError('prefix must be a string')
  const result = ipc.sendSync('fs.mkdtemp', { prefix })
  if (result.err) throw result.err
  const path = result.data.path
  const enc = typeof options === 'string' ? options : options?.encoding
  if (enc === 'buffer') return Buffer.from(path)
  return path
}

/**
 * Asynchronously open a file calling `callback` upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsopenpath-flags-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {string=} [flags = 'r']
 * @param {number=} [mode = 0o666]
 * @param {(object|function(Error|null, number|undefined):any)=} [options]
 * @param {(function(Error|null, number|undefined):any)|null} [callback]
 */
export function open (
  path,
  flags = 'r',
  mode = 0o666,
  options = null,
  callback = null
) {
  if (typeof flags === 'object') {
    callback = mode
    options = flags
    flags = FileHandle.DEFAULT_OPEN_FLAGS
    mode = FileHandle.DEFAULT_OPEN_MODE
  }

  if (typeof mode === 'object') {
    callback = options
    options = mode
    flags = FileHandle.DEFAULT_OPEN_FLAGS
    mode = FileHandle.DEFAULT_OPEN_MODE
  }

  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof flags === 'function') {
    callback = flags
    flags = FileHandle.DEFAULT_OPEN_FLAGS
    mode = FileHandle.DEFAULT_OPEN_MODE
  }

  if (typeof mode === 'function') {
    callback = mode
    mode = FileHandle.DEFAULT_OPEN_MODE
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  path = normalizePath(path)

  FileHandle.open(path, flags, mode, options)
    .then((handle) => {
      gc.unref(handle)
      callback(null, handle.fd)
    })
    .catch((err) => callback(err))
}

/**
 * Synchronously open a file.
 * @param {string|Buffer|URL} path
 * @param {string=} [flags = 'r']
 * @param {string=} [mode = 0o666]
 * @param {object=} [options]
 */
export function openSync (path, flags = 'r', mode = 0o666, options = null) {
  if (typeof flags === 'object') {
    options = flags
    flags = FileHandle.DEFAULT_OPEN_FLAGS
    mode = FileHandle.DEFAULT_OPEN_MODE
  }

  if (typeof mode === 'object') {
    options = mode
    flags = FileHandle.DEFAULT_OPEN_FLAGS
    mode = FileHandle.DEFAULT_OPEN_MODE
  }

  path = normalizePath(path)
  flags = normalizeFlags(flags)

  const id = String(options?.id || rand64())
  const result = ipc.sendSync(
    'fs.open',
    {
      id,
      mode,
      path,
      flags
    },
    {
      ...options
    }
  )

  if (result.err) {
    throw result.err
  }

  if (result.data?.fd) {
    fds.set(id, result.data.fd, 'file')
  } else {
    fds.set(id, id, 'file')
  }

  return result.data?.fd || id
}

/**
 * Asynchronously open a directory calling `callback` upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string | Buffer | URL} path
 * @param {(object|function(Error|null, Dir|undefined):any)=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @param {function(Error|null, Dir|undefined):any} [callback]
 */
export function opendir (path, options = {}, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  path = normalizePath(path)

  DirectoryHandle.open(path, options)
    .then((handle) => callback(null, new Dir(handle, options)))
    .catch((err) => callback(err))
}

/**
 * Synchronously open a directory.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string|Buffer|URL} path
 * @param {object} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @return {Dir}
 */
export function opendirSync (path, options = {}) {
  path = normalizePath(path)
  // @ts-ignore
  const id = String(options?.id || rand64())
  const result = ipc.sendSync('fs.opendir', { id, path }, options)

  if (result.err) {
    throw result.err
  }

  fds.set(id, id, 'directory')

  // @ts-ignore
  const handle = new DirectoryHandle({ id, path })
  return new Dir(handle, options)
}

/**
 * Asynchronously read from an open file descriptor.
 * @see {@link https://nodejs.org/api/fs.html#fsreadfd-buffer-offset-length-position-callback}
 * @param {number} fd
 * @param {object|Buffer|Uint8Array} buffer - The buffer that the data will be written to.
 * @param {number} offset - The position in buffer to write the data to.
 * @param {number} length - The number of bytes to read.
 * @param {number|BigInt|null} position - Specifies where to begin reading from in the file. If position is null or -1 , data will be read from the current file position, and the file position will be updated. If position is an integer, the file position will be unchanged.
 * @param {function(Error|null, number|undefined, Buffer|undefined):any} callback
 */
export function read (fd, buffer, offset, length, position, options, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof buffer === 'object' && !isBufferLike(buffer)) {
    options = buffer
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  try {
    FileHandle.from(fd)
      .read({ ...options, buffer, offset, length, position })
      .then(({ bytesRead, buffer }) => callback(null, bytesRead, buffer))
      .catch((err) => callback(err))
  } catch (err) {
    callback(err)
  }
}

/**
 * Asynchronously write to an open file descriptor.
 * @see {@link https://nodejs.org/api/fs.html#fswritefd-buffer-offset-length-position-callback}
 * @param {number} fd
 * @param {object|Buffer|Uint8Array} buffer - The buffer that the data will be written to.
 * @param {number} offset - The position in buffer to write the data to.
 * @param {number} length - The number of bytes to read.
 * @param {number|BigInt|null} position - Specifies where to begin reading from in the file. If position is null or -1 , data will be read from the current file position, and the file position will be updated. If position is an integer, the file position will be unchanged.
 * @param {function(Error|null, number|undefined, Buffer|undefined):any} callback
 */
export function write (fd, buffer, offset, length, position, options, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof buffer === 'object' && !isBufferLike(buffer)) {
    options = buffer
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  try {
    FileHandle.from(fd)
      .write({ ...options, buffer, offset, length, position })
      .then(({ bytesWritten, buffer }) => callback(null, bytesWritten, buffer))
      .catch((err) => callback(err))
  } catch (err) {
    callback(err)
  }
}

/**
 * Vector write convenience: writes multiple buffers sequentially to fd
 * @param {number} fd
 * @param {Array<Buffer|TypedArray>} buffers
 * @param {number|null|function} [position]
 * @param {function(Error|null, number=):any} [callback]
 */
export function writev (fd, buffers, position, callback = defaultCallback) {
  if (typeof position === 'function') {
    callback = position
    position = null
  }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  try {
    FileHandle.from(fd)
      .writev(buffers, position)
      .then(
        (n) => callback(null, n),
        (err) => callback(err)
      )
  } catch (err) {
    callback(err)
  }
}

/**
 * Vector read convenience: reads into multiple buffers sequentially from fd
 * @param {number} fd
 * @param {Array<Buffer|TypedArray>} buffers
 * @param {number|null|function} [position]
 * @param {function(Error|null, number, any[]):any} [callback]
 */
export function readv (fd, buffers, position, callback = defaultCallback) {
  if (typeof position === 'function') {
    callback = position
    position = null
  }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  try {
    FileHandle.from(fd)
      .readv(buffers, position)
      .then(
        ({ bytesRead, buffers }) => callback(null, bytesRead, buffers),
        (err) => callback(err)
      )
  } catch (err) {
    callback(err)
  }
}

/**
 * Asynchronously read all entries in a directory.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string|Buffer|URL} path
 * @param {object|function(Error|null, (Dirent|string)[]|undefined):any} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @param {function(Error|null, (Dirent|string)[]):any} [callback]
 */
export function readdir (path, options = {}, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (!options || typeof options !== 'object') {
    options = {}
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  path = normalizePath(path)
  options = {
    entries: DirectoryHandle.MAX_ENTRIES,
    withFileTypes: false,
    ...options
  }

  DirectoryHandle.open(path, options)
    .then(async (handle) => {
      const entries = []
      const dir = new Dir(handle, options)

      try {
        for await (const entry of dir.entries(options)) {
          entries.push(entry)
        }
      } catch (err) {
        return callback(err)
      } finally {
        if (!dir.closing && !dir.closed) {
          dir.close().catch(noop)
        }
      }

      callback(null, entries.sort(sortDirectoryEntries))
    })
    .catch((err) => callback(err))
}

/**
 * Synchronously read all entries in a directory.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string|Buffer | URL } path
 * @param {object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @return {(Dirent|string)[]}
 */
export function readdirSync (path, options = {}) {
  options = {
    entries: DirectoryHandle.MAX_ENTRIES,
    withFileTypes: false,
    ...options
  }
  const dir = opendirSync(path, options)
  const entries = []

  for (
    let entry = dir.readSync(options);
    entry && entry.length !== 0;
    entry = dir.readSync(options)
  ) {
    entries.push(...[].concat(entry))
  }

  dir.closeSync()
  return entries
}

/**
 * @param {string|Buffer|URL|number} path
 * @param {object|function(Error|null, Buffer|string|undefined):any} options
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 * @param {AbortSignal|undefined} [options.signal]
 * @param {function(Error|null, Buffer|string|undefined):any} callback
 */
export function readFile (path, options = {}, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof options === 'string') {
    options = { encoding: options }
  }

  path = normalizePath(path)
  options = { flags: 'r', ...options }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  visit(path, options, async (err, handle) => {
    let buffer = null

    if (err) {
      callback(err)
      return
    }

    try {
      buffer = await handle.readFile(options)
    } catch (err) {
      callback(err)
      return
    }

    callback(null, buffer)
  })
}

/**
 * @param {string|Buffer|URL|number} path
 * @param {{ encoding?: string, flags?: string }} [options]
 * @param {object|function(Error|null, Buffer|undefined):any} [options]
 * @param {AbortSignal|undefined} [options.signal]
 * @return {string|Buffer}
 */
export function readFileSync (path, options = null) {
  if (typeof options === 'string') {
    options = { encoding: options }
  }

  path = normalizePath(path)
  options = {
    flags: 'r',
    encoding: options?.encoding,
    ...options
  }

  let result = null

  const stats = statSync(path)
  const flags = normalizeFlags(options.flags)
  const mode = FileHandle.DEFAULT_OPEN_MODE
  // @ts-ignore
  const id = String(options?.id || rand64())

  result = ipc.sendSync(
    'fs.open',
    {
      mode,
      flags,
      id,
      path
    },
    options
  )

  if (result.err) {
    throw result.err
  }

  result = ipc.sendSync(
    'fs.read',
    {
      id,
      size: stats.size,
      offset: 0
    },
    { responseType: 'arraybuffer' }
  )

  if (result.err) {
    throw result.err
  }

  const data = result.data

  result = ipc.sendSync('fs.close', { id }, options)

  if (result.err) {
    throw result.err
  }

  const buffer = data ? Buffer.from(data) : Buffer.alloc(0)

  if (typeof options?.encoding === 'string') {
    return buffer.toString(options.encoding)
  }

  return buffer
}

/**
 * Reads link at `path`
 * @param {string} path
 * @param {function(Error|null, string|undefined):any} callback
 */
export function readlink (path, options, callback) {
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (typeof options === 'function') {
    callback = options
    options = null
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  path = normalizePath(path)
  ipc
    .request('fs.readlink', { path })
    .then((result) => {
      if (result?.err) return callback(result.err)
      const target = result.data.path
      const enc = typeof options === 'string' ? options : options?.encoding
      if (enc === 'buffer') {
        return callback(null, Buffer.from(target))
      }
      callback(null, target)
    })
    .catch(callback)
}

/**
 * Reads link target at `path` synchronously
 * @param {string} path
 * @return {string}
 */
export function readlinkSync (path, options = null) {
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }
  const result = ipc.sendSync('fs.readlink', { path })
  if (result.err) throw result.err
  const target = result.data.path
  const enc = typeof options === 'string' ? options : options?.encoding
  if (enc === 'buffer') return Buffer.from(target)
  return target
}

/**
 * Computes real path for `path`
 * @param {string} path
 * @param {function(Error|null, string|undefined):any} callback
 */
export function realpath (path, callback) {
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  ipc
    .request('fs.realpath', { path })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null, result.data.path)
      }
    })
    .catch(callback)
}

/**
 * Computes real path for `path`
 * @param {string} path
 * @return {string}
 */
export function realpathSync (path) {
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  const result = ipc.sendSync('fs.realpath', { path })

  if (result.err) {
    throw result.err
  }

  return result.data?.path
}

/**
 * Renames file or directory at `src` to `dest`.
 * @param {string} src
 * @param {string} dest
 * @param {function(Error|null):any} callback
 */
export function rename (src, dest, callback) {
  src = normalizePath(src)
  dest = normalizePath(dest)

  if (typeof src !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  ipc.request('fs.rename', { src, dest }).then((result) => {
    if (result?.err) {
      callback(result.err)
    } else {
      callback(null)
    }
  })
}

/**
 * Renames file or directory at `src` to `dest`, synchronously.
 * @param {string} src
 * @param {string} dest
 */
export function renameSync (src, dest) {
  src = normalizePath(src)
  dest = normalizePath(dest)

  if (typeof src !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }

  const result = ipc.sendSync('fs.rename', { src, dest })

  if (result.err) {
    throw result.err
  }
}

/**
 * Removes directory at `path`.
 * @param {string} path
 * @param {function(Error|null):any} callback
 */
export function rmdir (path, callback) {
  path = normalizePath(path)

  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  ipc.request('fs.rmdir', { path }).then((result) => {
    if (result?.err) {
      callback(result.err)
    } else {
      callback(null)
    }
  })
}

/**
 * Removes directory at `path`, synchronously.
 * @param {string} path
 */
export function rmdirSync (path) {
  path = normalizePath(path)

  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  const result = ipc.sendSync('fs.rmdir', { path })

  if (result.err) {
    throw result.err
  }
}

/**
 * Synchronously get the stats of a file
 * @param {string} path - filename or file descriptor
 * @param {object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 */
export function statSync (path, options = null) {
  path = normalizePath(path)
  const result = ipc.sendSync('fs.stat', { path })

  if (result.err) {
    throw result.err
  }

  return Stats.from(result.data, Boolean(options?.bigint))
}

/**
 * Synchronously get the stats of an open file descriptor.
 * @param {number|FileHandle} fd
 * @param {object=} [options]
 */
export function fstatSync (fd, options = null) {
  const handle = FileHandle.from(fd)
  const result = ipc.sendSync('fs.fstat', { id: handle.id })
  if (result.err) throw result.err
  const stats = Stats.from(result.data, Boolean(options?.bigint))
  stats.handle = handle
  return stats
}

/**
 * Get the stats of a file
 * @param {string|Buffer|URL|number} path - filename or file descriptor
 * @param {(object|function(Error|null, Stats|undefined):any)=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 * @param {AbortSignal|undefined} [options.signal]
 * @param {function(Error|null, Stats|undefined):any} [callback]
 */
export function stat (path, options, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  if (
    path instanceof globalThis.FileSystemFileHandle ||
    path instanceof globalThis.FileSystemDirectoryHandle
  ) {
    if (path[kFileSystemHandleFullName]) {
      stat(path[kFileSystemHandleFullName], options, callback)
      return
    }

    if (path instanceof globalThis.FileSystemDirectoryHandle) {
      queueMicrotask(() => {
        const info = {
          st_mode: constants.S_IFDIR,
          st_size: 0
        }

        const stats = Stats.from(info, Boolean(options?.bigint))
        callback(null, stats)
      })
      return
    }

    Promise.resolve(path.getFile())
      .then((file) => {
        if (file?.[kFileDescriptor]) {
          return file[kFileDescriptor].stat(options)
        }

        return Stats.from(
          { st_mode: constants.S_IFREG, st_size: file?.size ?? 0 },
          Boolean(options?.bigint)
        )
      })
      .then((stats) => callback(null, stats), callback)
    return
  }

  visit(path, {}, async (err, handle) => {
    let stats = null

    if (err) {
      callback(err)
      return
    }

    try {
      stats = await handle.stat(options)
    } catch (err) {
      callback(err)
      return
    }

    callback(null, stats)
  })
}

/**
 * Get the stats of a symbolic link
 * @param {string|Buffer|URL|number} path - filename or file descriptor
 * @param {(object|function(Error|null, Stats|undefined):any)=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 * @param {AbortSignal|undefined} [options.signal]
 * @param {function(Error|null, Stats|undefined):any} [callback]
 */
export function lstat (path, options, callback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  if (
    path instanceof globalThis.FileSystemFileHandle ||
    path instanceof globalThis.FileSystemDirectoryHandle
  ) {
    return stat(path, options, callback)
  }

  visit(path, {}, async (err, handle) => {
    let stats = null

    if (err) {
      callback(err)
      return
    }

    try {
      stats = await handle.lstat(options)
    } catch (err) {
      callback(err)
      return
    }

    callback(null, stats)
  })
}

/**
 * Synchronously get stats of a symbolic link
 * @param {string|Buffer|URL} path
 * @param {object=} [options]
 */
export function lstatSync (path, options = null) {
  path = normalizePath(path)
  const result = ipc.sendSync('fs.lstat', { path })
  if (result.err) throw result.err
  return Stats.from(result.data, Boolean(options?.bigint))
}

/**
 * Creates a symlink of `src` at `dest`.
 * @param {string} src
 * @param {string} dest
 * @param {function(Error|null):any} [callback]
 */
export function symlink (src, dest, type = null, callback) {
  let flags = 0
  src = normalizePath(src)
  dest = normalizePath(dest)

  if (typeof src !== 'string') {
    throw new TypeError("The argument 'src' must be a string")
  }

  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }

  if (typeof type === 'function') {
    callback = type
  }

  if (!type) {
    type = 'file'
  }

  if (type === 'file') {
    flags = 0
  } else if (type === 'dir') {
    flags = constants.UV_FS_SYMLINK_DIR
  } else if (type === 'junction') {
    flags = constants.UV_FS_SYMLINK_JUNCTION
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  ipc
    .request('fs.symlink', { src, dest, flags })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Synchronously create a symlink
 * @param {string} src
 * @param {string} dest
 * @param {string=} [type]
 */
export function symlinkSync (src, dest, type = null) {
  let flags = 0
  src = normalizePath(src)
  dest = normalizePath(dest)
  if (typeof src !== 'string') {
    throw new TypeError("The argument 'src' must be a string")
  }
  if (typeof dest !== 'string') {
    throw new TypeError("The argument 'dest' must be a string")
  }
  if (!type) type = 'file'
  if (type === 'file') flags = 0
  else if (type === 'dir') flags = constants.UV_FS_SYMLINK_DIR
  else if (type === 'junction') flags = constants.UV_FS_SYMLINK_JUNCTION
  const result = ipc.sendSync('fs.symlink', { src, dest, flags })
  if (result.err) throw result.err
}

/**
 * Unlinks (removes) file at `path`.
 * @param {string} path
 * @param {function(Error|null):any} callback
 */
export function unlink (path, callback) {
  path = normalizePath(path)

  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  ipc
    .request('fs.unlink', { path })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Unlinks (removes) file at `path`, synchronously.
 * @param {string} path
 */
export function unlinkSync (path) {
  path = normalizePath(path)

  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }

  const result = ipc.sendSync('fs.unlink', { path })

  if (result.err) {
    throw result.err
  }
}

/**
 * Changes ownership of link at `path` synchronously
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 */
export function lchownSync (path, uid, gid) {
  path = normalizePath(path)
  if (typeof path !== 'string') {
    throw new TypeError("The argument 'path' must be a string")
  }
  if (!Number.isInteger(uid)) {
    throw new TypeError("The argument 'uid' must be an integer")
  }
  if (!Number.isInteger(gid)) {
    throw new TypeError("The argument 'gid' must be an integer")
  }
  const result = ipc.sendSync('fs.lchown', { path, uid, gid })
  if (result.err) throw result.err
}

/**
 * @see {@link https://nodejs.org/api/fs.html#fswritefilefile-data-options-callback}
 * @param {string|Buffer|URL|number} path - filename or file descriptor
 * @param {string|Buffer|TypedArray|DataView|object} data
 * @param {(object|function(Error|null):any)=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.mode = 0o666]
 * @param {string=} [options.flag = 'w']
 * @param {AbortSignal|undefined} [options.signal]
 * @param {function(Error|null):any} [callback]
 */
export function writeFile (path, data, options, callback) {
  if (typeof options === 'function') {
    callback = options
    options = { encoding: 'utf8' }
  }

  if (typeof options === 'string') {
    options = { encoding: options }
  }

  path = normalizePath(path)
  options = { mode: 0o666, flag: 'w', ...options }

  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }

  visit(path, options, async (err, handle) => {
    if (err) {
      callback(err)
      return
    }

    try {
      await handle.writeFile(data, options)
    } catch (err) {
      callback(err)
      return
    }

    callback(null)
  })
}

/**
 * Writes data to a file synchronously.
 * @param {string|Buffer|URL|number} path - filename or file descriptor
 * @param {string|Buffer|TypedArray|DataView|object} data
 * @param {object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.mode = 0o666]
 * @param {string=} [options.flag = 'w']
 * @param {AbortSignal|undefined} [options.signal]
 * @see {@link https://nodejs.org/api/fs.html#fswritefilesyncfile-data-options}
 */
export function writeFileSync (path, data, options) {
  if (typeof options === 'string') {
    options = { encoding: options }
  }

  path = normalizePath(path)
  options = options || {}
  const id = String(options?.id || rand64())
  const flags = normalizeFlags(options.flag ?? options.flags ?? 'w')

  let result = ipc.sendSync(
    'fs.open',
    {
      id,
      mode: options.mode ?? 0o666,
      path,
      flags
    },
    options
  )

  if (result.err) {
    throw result.err
  }

  result = ipc.sendSync('fs.write', { id, offset: 0 }, null, data)

  if (result.err) {
    throw result.err
  }

  result = ipc.sendSync('fs.close', { id }, options)

  if (result.err) {
    throw result.err
  }
}

/**
 * Truncate file at `path` to `len` bytes (default 0)
 * @param {string} path
 * @param {number|function} [len=0]
 * @param {function(Error|null):any} [callback]
 */
export function truncate (path, len, callback) {
  if (typeof len === 'function') {
    callback = len
    len = 0
  }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  path = normalizePath(path)
  FileHandle.open(path, 'r+').then(
    async (handle) => {
      try {
        await handle.truncate(len || 0)
        await handle.close()
      } catch (err) {
        return callback(err)
      }
      callback(null)
    },
    (err) => callback(err)
  )
}

/** Truncate file synchronously */
export function truncateSync (path, len = 0) {
  const fd = openSync(path, 'r+')
  const result = ipc.sendSync('fs.ftruncate', {
    id: fds.id(fd) || fd,
    offset: len
  })
  if (result.err) throw result.err
  closeSync(fd)
}

/**
 * Append data to a file
 * @param {string|Buffer|URL|number} path
 * @param {string|Buffer|TypedArray|DataView|object} data
 * @param {(object|function(Error|null):any)=} [options]
 * @param {string=} [options.encoding]
 * @param {number=} [options.mode]
 * @param {string=} [options.flag]
 * @param {function(Error|null):any} callback
 */
export function appendFile (path, data, options, callback = defaultCallback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }
  if (typeof options === 'string') options = { encoding: options }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  options = { flag: 'a', mode: 0o666, ...options }
  writeFile(path, data, options, callback)
}

/** Append data synchronously */
export function appendFileSync (path, data, options) {
  if (typeof options === 'string') options = { encoding: options }
  options = { flag: 'a', mode: 0o666, ...options }
  writeFileSync(path, data, options)
}

/**
 * Remove a file or directory
 * @param {string} path
 * @param {{ recursive?: boolean, force?: boolean }} [options]
 * @param {function(Error|null):any} callback
 */
export function rm (path, options, callback = defaultCallback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  path = normalizePath(path)
  const { recursive = false, force = false } = options || {}
  const done = (err) => {
    if (
      err &&
      force &&
      /exist|found|enoent/i.test(err.message || String(err))
    ) {
      return callback(null)
    }
    callback(err || null)
  }
  stat(path, (err, stats) => {
    if (err) return done(err)
    if (stats.isDirectory()) {
      if (!recursive) return done(new Error('EISDIR: recursive not set'))
      readdir(path, { withFileTypes: true }, (err, entries) => {
        if (err) return done(err)
        let i = 0
        const next = () => {
          if (i >= entries.length) {
            return rmdir(path, done)
          }
          const entry = entries[i++]
          const full = joinPath(
            path,
            typeof entry === 'string' ? entry : entry.name
          )
          rm(full, { recursive, force }, (err2) => {
            if (err2 && !force) return done(err2)
            next()
          })
        }
        next()
      })
    } else {
      unlink(path, done)
    }
  })
}

/** Remove synchronously */
export function rmSync (path, options = {}) {
  path = normalizePath(path)
  const { recursive = false, force = false } = options
  try {
    const st = statSync(path)
    if (st.isDirectory()) {
      if (!recursive) throw new Error('EISDIR: recursive not set')
      const entries = readdirSync(path, { withFileTypes: true })
      for (const entry of entries) {
        const name = typeof entry === 'string' ? entry : entry.name
        const full = joinPath(path, name)
        rmSync(full, { recursive, force })
      }
      rmdirSync(path)
    } else {
      unlinkSync(path)
    }
  } catch (err) {
    if (!(force && /exist|found|enoent/i.test(err.message || String(err)))) {
      throw err
    }
  }
}

/**
 * Copy file or directory.
 * Options:
 * - recursive: copy directories recursively
 * - dereference: follow symlinks (default true). When false, copies symlinks as symlinks
 * - force: overwrite if destination exists; when false and errorOnExist is false, leaves dest untouched
 * - errorOnExist: if true and destination exists, error (when force is false)
 * - preserveTimestamps: set atime/mtime on dest to match src (files)
 * - preserveMode: apply src mode (chmod) to dest
 * - preserveOwner: attempt to apply src uid/gid (chown) to dest (may be ignored by platform; may require privileges)
 * - filter: function (src, dest) => boolean|Promise<boolean> to include/exclude entries
 * @param {string} src
 * @param {string} dest
 * @param {{ recursive?: boolean, dereference?: boolean, force?: boolean, errorOnExist?: boolean, preserveTimestamps?: boolean, preserveMode?: boolean, preserveOwner?: boolean, filter?: function(string, string): (boolean|Promise<boolean>) }} [options]
 * @param {function(Error|null):any} callback
 */
export function cp (src, dest, options, callback = defaultCallback) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  src = normalizePath(src)
  dest = normalizePath(dest)
  const {
    recursive = false,
    dereference = true,
    force = false,
    preserveTimestamps = false,
    filter = null,
    errorOnExist = false,
    preserveMode = false,
    preserveOwner = false
  } = options || {}
  const onErr = (err) => {
    if (err && !force) callback(err)
    else callback(null)
  }
  stat(src, (err, stats) => {
    if (err) return onErr(err)
    if (stats.isDirectory()) {
      if (!recursive) return onErr(new Error('EISDIR: recursive not set'))
      mkdir(dest, { recursive: true }, (err2) => {
        if (err2) return onErr(err2)
        if (preserveMode) {
          try {
            chmodSync(dest, stats.mode & 0o777)
          } catch {}
        }
        if (preserveOwner) {
          try {
            chownSync(dest, stats.uid, stats.gid)
          } catch {}
        }
        readdir(src, { withFileTypes: true }, async (err3, entries) => {
          if (err3) return onErr(err3)
          let i = 0
          const next = async () => {
            if (i >= entries.length) return callback(null)
            const entry = entries[i++]
            const name = typeof entry === 'string' ? entry : entry.name
            const s = joinPath(src, name)
            const d = joinPath(dest, name)
            if (typeof filter === 'function') {
              try {
                if (!(await filter(s, d))) return next()
              } catch (e) {
                return onErr(e)
              }
            }
            cp(
              s,
              d,
              {
                recursive,
                dereference,
                force,
                preserveTimestamps,
                filter,
                errorOnExist,
                preserveMode,
                preserveOwner
              },
              (err4) => {
                if (err4 && !force) return callback(err4)
                next()
              }
            )
          }
          next()
        })
      })
    } else {
      // file-like: follow symlinks unless dereference=false and it's a link
      if (!dereference) {
        lstat(src, {}, (errL, stL) => {
          if (!errL && stL.isSymbolicLink()) {
            return readlink(src, (errRL, target) => {
              if (errRL) return onErr(errRL)
              // destination existence handling
              if (!force || errorOnExist) {
                if (existsSync(dest)) {
                  return onErr(
                    errorOnExist
                      ? new Error('EEXIST: file already exists')
                      : null
                  )
                }
              }
              symlink(target, dest, (errS) => onErr(errS))
            })
          }
          // destination existence check
          if (!force || errorOnExist) {
            if (existsSync(dest)) {
              return onErr(
                errorOnExist ? new Error('EEXIST: file already exists') : null
              )
            }
          }
          copyFile(src, dest, 0, async (errC) => {
            if (errC) return onErr(errC)
            if (preserveMode) {
              try {
                chmodSync(dest, stats.mode & 0o777)
              } catch {}
            }
            if (preserveOwner) {
              try {
                chownSync(dest, stats.uid, stats.gid)
              } catch {}
            }
            if (preserveTimestamps) {
              try {
                const srcStat = statSync(src)
                utimesSync(dest, srcStat.atimeMs / 1000, srcStat.mtimeMs / 1000)
              } catch {}
            }
            onErr(null)
          })
        })
      } else {
        if (!force || errorOnExist) {
          if (existsSync(dest)) {
            return onErr(
              errorOnExist ? new Error('EEXIST: file already exists') : null
            )
          }
        }
        copyFile(src, dest, 0, async (errC) => {
          if (errC) return onErr(errC)
          if (preserveMode) {
            try {
              chmodSync(dest, stats.mode & 0o777)
            } catch {}
          }
          if (preserveOwner) {
            try {
              chownSync(dest, stats.uid, stats.gid)
            } catch {}
          }
          if (preserveTimestamps) {
            try {
              const srcStat = statSync(src)
              utimesSync(dest, srcStat.atimeMs / 1000, srcStat.mtimeMs / 1000)
            } catch {}
          }
          onErr(null)
        })
      }
    }
  })
}

/** Copy synchronously */
export function cpSync (src, dest, options = {}) {
  src = normalizePath(src)
  dest = normalizePath(dest)
  const {
    recursive = false,
    dereference = true,
    force = false,
    preserveTimestamps = false,
    filter = null,
    errorOnExist = false,
    preserveMode = false,
    preserveOwner = false
  } = options
  try {
    const st = statSync(src)
    if (st.isDirectory()) {
      if (!recursive) throw new Error('EISDIR: recursive not set')
      try {
        mkdirSync(dest, { recursive: true })
      } catch {}
      if (preserveMode) {
        try {
          chmodSync(dest, st.mode & 0o777)
        } catch {}
      }
      const entries = readdirSync(src, { withFileTypes: true })
      for (const entry of entries) {
        const name = typeof entry === 'string' ? entry : entry.name
        const s = joinPath(src, name)
        const d = joinPath(dest, name)
        if (typeof filter === 'function') {
          if (!filter(s, d)) continue
        }
        cpSync(s, d, {
          recursive,
          dereference,
          force,
          preserveTimestamps,
          filter,
          errorOnExist,
          preserveMode,
          preserveOwner
        })
      }
    } else {
      if (!dereference) {
        const stl = lstatSync(src)
        if (stl.isSymbolicLink()) {
          if (!force || errorOnExist) {
            if (existsSync(dest)) {
              if (errorOnExist) throw new Error('EEXIST: file already exists')
              return
            }
          }
          const target = readlinkSync(src)
          symlinkSync(target, dest)
          return
        }
      }
      if (!force || errorOnExist) {
        if (existsSync(dest)) {
          if (errorOnExist) throw new Error('EEXIST: file already exists')
          return
        }
      }
      copyFileSync(src, dest)
      if (preserveMode) {
        try {
          chmodSync(dest, st.mode & 0o777)
        } catch {}
      }
      if (preserveOwner) {
        try {
          chownSync(dest, st.uid, st.gid)
        } catch {}
      }
      if (preserveTimestamps) {
        try {
          const srcStat = statSync(src)
          utimesSync(dest, srcStat.atimeMs / 1000, srcStat.mtimeMs / 1000)
        } catch {}
      }
    }
  } catch (err) {
    if (!(force && /exist|enoent|found/i.test(err.message || String(err)))) {
      throw err
    }
  }
}
/**
 * Update atime/mtime for a path
 * @param {string} path
 * @param {number|Date|string} atime
 * @param {number|Date|string} mtime
 * @param {function(Error=)=} [callback]
 */
export function utimes (path, atime, mtime, callback) {
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  path = normalizePath(path)
  const a = toSeconds(atime)
  const m = toSeconds(mtime)
  ipc
    .request('fs.utimes', { path, atime: a, mtime: m })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Update atime/mtime for a symlink without following it
 */
export function lutimes (path, atime, mtime, callback) {
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  path = normalizePath(path)
  const a = toSeconds(atime)
  const m = toSeconds(mtime)
  ipc
    .request('fs.lutimes', { path, atime: a, mtime: m })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/** Update atime/mtime for a symlink without following it (sync) */
export function lutimesSync (path, atime, mtime) {
  path = normalizePath(path)
  const a = toSeconds(atime)
  const m = toSeconds(mtime)
  const result = ipc.sendSync('fs.lutimes', { path, atime: a, mtime: m })
  if (result.err) throw result.err
}

/**
 * Update atime/mtime for a path (sync)
 */
export function utimesSync (path, atime, mtime) {
  path = normalizePath(path)
  const a = toSeconds(atime)
  const m = toSeconds(mtime)
  const result = ipc.sendSync('fs.utimes', { path, atime: a, mtime: m })
  if (result.err) throw result.err
}

/**
 * Update atime/mtime for an fd
 * @param {number|FileHandle} fd
 */
export function futimes (fd, atime, mtime, callback) {
  if (typeof callback !== 'function') {
    throw new TypeError('callback must be a function.')
  }
  const id = getDescriptorId(fd)
  const a = toSeconds(atime)
  const m = toSeconds(mtime)
  ipc
    .request('fs.futimes', { id, atime: a, mtime: m })
    .then((result) => {
      if (result?.err) {
        callback(result.err)
      } else {
        callback(null)
      }
    })
    .catch(callback)
}

/**
 * Update atime/mtime for an fd (sync)
 */
export function futimesSync (fd, atime, mtime) {
  const id = getDescriptorId(fd)
  const a = toSeconds(atime)
  const m = toSeconds(mtime)
  const result = ipc.sendSync('fs.futimes', {
    id,
    atime: a,
    mtime: m
  })
  if (result.err) throw result.err
}

/**
 * Watch for changes at `path` calling `callback`
 * @param {string}
 * @param {function|object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {function=} [callback]
 * @return {Watcher}
 */
export function watch (path, options, callback = null) {
  if (typeof options === 'function') {
    callback = options
  }

  path = normalizePath(path)
  const watcher = new Watcher(path, options)
  watcher.on('change', callback)
  return watcher
}

// re-exports
export {
  bookmarks,
  constants,
  Dir,
  DirectoryHandle,
  Dirent,
  fds,
  FileHandle,
  promises,
  ReadStream,
  Stats,
  Watcher,
  WriteStream
}

export default exports

for (const key in exports) {
  const value = exports[key]
  if (key in promises && isFunction(value) && isFunction(promises[key])) {
    value[Symbol.for('nodejs.util.promisify.custom')] = promises[key]
    value[Symbol.for('oro.runtime.util.promisify.custom')] = promises[key]
  }
}
