/**
 * Asynchronously check access a file.
 * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#fspromisesaccesspath-mode}
 * @param {string|Buffer|URL} path
 * @param {number=} [mode]
 * @param {object=} [options]
 */
export function access(path: string | Buffer | URL, mode?: number | undefined, options?: object | undefined): Promise<boolean>;
/**
 * @see {@link https://nodejs.org/api/fs.html#fspromiseschmodpath-mode}
 * @param {string | Buffer | URL} path
 * @param {number} mode
 * @returns {Promise<void>}
 */
export function chmod(path: string | Buffer | URL, mode: number): Promise<void>;
/**
 * Changes ownership of file or directory at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 * @return {Promise}
 */
export function chown(path: string, uid: number, gid: number): Promise<any>;
/**
 * Asynchronously copies `src` to `dest` calling `callback` upon success or error.
 * @param {string} src - The source file path.
 * @param {string} dest - The destination file path.
 * @param {number} flags - Modifiers for copy operation.
 * @return {Promise}
 */
export function copyFile(src: string, dest: string, flags?: number): Promise<any>;
/**
 * Changes ownership of a symbolic link at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 * @return {Promise}
 */
export function lchown(path: string, uid: number, gid: number): Promise<any>;
/**
 * Changes permissions of a symbolic link at `path`.
 *
 * On platforms that do not implement `lchmod`, the native backend may treat
 * this as a no-op or reject the request with a platform-specific error.
 * @param {string|Buffer|URL} path
 * @param {number} mode
 * @returns {Promise<void>}
 */
export function lchmod(path: string | Buffer | URL, mode: number): Promise<void>;
/**
 * Creates a hard link to `dest` from `src`.
 * @param {string} src
 * @param {string} dest
 * @return {Promise}
 */
export function link(src: string, dest: string): Promise<any>;
/**
 * Asynchronously creates a directory.
 *
 * @param {string} path - The path to create
 * @param {object} [options] - The optional options argument can be an integer specifying mode (permission and sticky bits), or an object with a mode property and a recursive property indicating whether parent directories should be created. Calling fs.mkdir() when path is a directory that exists results in an error only when recursive is false.
 * @param {boolean} [options.recursive=false] - Recursively create missing path segments.
 * @param {number} [options.mode=0o777] - Set the mode of directory, or missing path segments when recursive is true.
 * @return {Promise} - Upon success, fulfills with undefined if recursive is false, or the first directory path created if recursive is true.
 */
export function mkdir(path: string, options?: {
    recursive?: boolean;
    mode?: number;
}): Promise<any>;
/** Create a unique temporary directory */
export function mkdtemp(prefix: any, options: any): Promise<any>;
/**
 * Asynchronously open a file.
 * @see {@link https://nodejs.org/api/fs.html#fspromisesopenpath-flags-mode }
 *
 * @param {string | Buffer | URL} path
 * @param {string=} flags - default: 'r'
 * @param {number=} mode - default: 0o666
 * @return {Promise<FileHandle>}
 */
export function open(path: string | Buffer | URL, flags?: string | undefined, mode?: number | undefined): Promise<FileHandle>;
/**
 * @see {@link https://nodejs.org/api/fs.html#fspromisesopendirpath-options}
 * @param {string | Buffer | URL} path
 * @param {object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {number=} [options.bufferSize = 32]
 * @return {Promise<Dir>}
 */
export function opendir(path: string | Buffer | URL, options?: object | undefined): Promise<Dir>;
/**
 * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#fspromisesreaddirpath-options}
 * @param {string|Buffer|URL} path
 * @param {object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @return {Promise<(string|Dirent)[]>}
 */
export function readdir(path: string | Buffer | URL, options?: object | undefined): Promise<(string | Dirent)[]>;
/**
 * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#fspromisesreadfilepath-options}
 * @param {string} path
 * @param {object=} [options]
 * @param {(string|null)=} [options.encoding = null]
 * @param {string=} [options.flag = 'r']
 * @param {AbortSignal|undefined} [options.signal]
 * @return {Promise<Buffer | string>}
 */
export function readFile(path: string, options?: object | undefined): Promise<Buffer | string>;
/**
 * Reads link at `path`
 * @param {string} path
 * @return {Promise<string>}
 */
export function readlink(path: string, options: any): Promise<string>;
/**
 * Computes real path for `path`
 * @param {string} path
 * @return {Promise<string>}
 */
export function realpath(path: string): Promise<string>;
/**
 * Renames file or directory at `src` to `dest`.
 * @param {string} src
 * @param {string} dest
 * @return {Promise}
 */
export function rename(src: string, dest: string): Promise<any>;
/**
 * Removes directory at `path`.
 * @param {string} path
 * @return {Promise}
 */
export function rmdir(path: string): Promise<any>;
/**
 * Get the stats of a file
 * @see {@link https://nodejs.org/api/fs.html#fspromisesstatpath-options}
 * @param {string | Buffer | URL} path
 * @param {object=} [options]
 * @param {boolean=} [options.bigint = false]
 * @return {Promise<Stats>}
 */
export function stat(path: string | Buffer | URL, options?: object | undefined): Promise<Stats>;
/**
 * Get the stats of an open file descriptor.
 * @see {@link https://nodejs.org/api/fs.html#fspromisesfstatfd-options}
 * @param {number|FileHandle} fd
 * @param {object=} [options]
 * @param {boolean=} [options.bigint = false]
 * @return {Promise<Stats>}
 */
export function fstat(fd: number | FileHandle, options?: object | undefined): Promise<Stats>;
/**
 * Get the stats of a symbolic link.
 * @see {@link https://nodejs.org/api/fs.html#fspromiseslstatpath-options}
 * @param {string | Buffer | URL} path
 * @param {object=} [options]
 * @param {boolean=} [options.bigint = false]
 * @return {Promise<Stats>}
 */
export function lstat(path: string | Buffer | URL, options?: object | undefined): Promise<Stats>;
/**
 * Creates a symlink of `src` at `dest`.
 * @param {string} src
 * @param {string} dest
 * @return {Promise}
 */
export function symlink(src: string, dest: string, type?: any): Promise<any>;
/**
 * Update atime/mtime for a path (promises)
 */
export function utimes(path: any, atime: any, mtime: any): Promise<void>;
/**
 * Update atime/mtime for an fd (promises)
 */
export function futimes(fd: any, atime: any, mtime: any): Promise<void>;
/** Update atime/mtime for a symlink without following it (promises) */
export function lutimes(path: any, atime: any, mtime: any): Promise<void>;
/**
 * Unlinks (removes) file at `path`.
 * @param {string} path
 * @return {Promise}
 */
export function unlink(path: string): Promise<any>;
/**
 * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#fspromiseswritefilefile-data-options}
 * @param {string|Buffer|URL|FileHandle} path - filename or FileHandle
 * @param {string|Buffer|Array|DataView|TypedArray} data
 * @param {object=} [options]
 * @param {(string|null)=} [options.encoding = 'utf8']
 * @param {number=} [options.mode = 0o666]
 * @param {string=} [options.flag = 'w']
 * @param {AbortSignal|undefined} [options.signal]
 * @return {Promise<void>}
 */
export function writeFile(path: string | Buffer | URL | FileHandle, data: string | Buffer | any[] | DataView | TypedArray, options?: object | undefined): Promise<void>;
/** Vector write: write multiple buffers to a FileHandle or fd */
export function writev(fdOrHandle: any, buffers: any, position?: any): Promise<number>;
/** Vector read: read into multiple buffers from a FileHandle or fd */
export function readv(fdOrHandle: any, buffers: any, position?: any): Promise<{
    bytesRead: number;
    buffers: any[];
}>;
/** Truncate file */
export function truncate(path: any, len?: number): Promise<void>;
/** Append data */
export function appendFile(path: any, data: any, options: any): Promise<void>;
/** Remove file or directory */
export function rm(path: any, options: any): Promise<void>;
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
 */
export function cp(src: any, dest: any, options: any): Promise<void>;
/**
 * Watch for changes at `path` calling `callback`
 * @param {string}
 * @param {function|object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {AbortSignal=} [options.signal]
 * @return {Watcher}
 */
export function watch(path: any, options?: (Function | object) | undefined): Watcher;
export type Stats = import("./stats.js").Stats;
export default exports;
export type Buffer = import("../buffer.js").Buffer;
export type TypedArray = Uint8Array | Int8Array;
import { Buffer } from '../buffer.js';
import { FileHandle } from './handle.js';
import { Dir } from './dir.js';
import { Dirent } from './dir.js';
import { Stats } from './stats.js';
import { Watcher } from './watcher.js';
import bookmarks from './bookmarks.js';
import * as constants from './constants.js';
import { DirectoryHandle } from './handle.js';
import fds from './fds.js';
import { ReadStream } from './stream.js';
import { WriteStream } from './stream.js';
import * as exports from './promises.js';
export { bookmarks, constants, Dir, DirectoryHandle, Dirent, fds, FileHandle, ReadStream, Watcher, WriteStream };
