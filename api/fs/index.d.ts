/**
 * Polls for file changes and invokes listener with (curr, prev) Stats.
 * This is a compatibility helper; prefer fs.watch for evented changes.
 * @param {string} path
 * @param {object|function} [options]
 * @param {number} [options.interval=5007]
 * @param {boolean} [options.bigint=false]
 * @param {function(Stats, Stats)} [listener]
 */
export function watchFile(path: string, options?: object | Function, listener?: (arg0: Stats, arg1: Stats) => any): void;
/**
 * Removes a watchFile listener or stops watching entirely for a path.
 * @param {string} path
 * @param {function=} listener
 */
export function unwatchFile(path: string, listener?: Function | undefined): void;
/**
 * Asynchronously check access to a file for a given mode calling `callback`
 * upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsopenpath-flags-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {number|function(Error|null):any} [mode = F_OK(0)]
 * @param {function(Error|null):any} [callback]
 */
export function access(path: string | Buffer | URL, mode?: number | ((arg0: Error | null) => any), callback?: (arg0: Error | null) => any): void;
/**
 * Synchronously check access to a file for a given mode calling `callback`
 * upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsopenpath-flags-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {number} [mode = F_OK(0)]
 */
export function accessSync(path: string | Buffer | URL, mode?: number): boolean;
/**
 * Checks if a path exists
 * @param {string | Buffer | URL} path
 * @param {function(Boolean)?} [callback]
 */
export function exists(path: string | Buffer | URL, callback?: ((arg0: boolean) => any) | null): void;
/**
 * Checks if a path exists
 * @param {string | Buffer | URL} path
 * @param {function(Boolean)?} [callback]
 */
export function existsSync(path: string | Buffer | URL): boolean;
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
export function chmod(path: string | Buffer | URL, mode: number, callback: (arg0: Error | null) => any): TypeError;
/**
 * Synchronously changes the permissions of a file.
 *
 * @see {@link https://nodejs.org/api/fs.html#fschmodpath-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {number} mode
 */
export function chmodSync(path: string | Buffer | URL, mode: number): void;
/**
 * Changes ownership of file or directory at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 * @param {function} callback
 */
export function chown(path: string, uid: number, gid: number, callback: Function): TypeError;
/**
 * Changes ownership of file or directory at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 */
export function chownSync(path: string, uid: number, gid: number): void;
/**
 * Asynchronously close a file descriptor calling `callback` upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsclosefd-callback}
 * @param {number} fd
 * @param {function(Error?)?} [callback]
 */
export function close(fd: number, callback?: ((arg0: Error | null) => any) | null): void;
/**
 * Synchronously close a file descriptor.
 * @param {number} fd  - fd
 */
export function closeSync(fd: number): void;
/**
 * Asynchronously copies `src` to `dest` calling `callback` upon success or error.
 * @param {string} src - The source file path.
 * @param {string} dest - The destination file path.
 * @param {number} flags - Modifiers for copy operation.
 * @param {function(Error=)=} [callback] - The function to call after completion.
 * @see {@link https://nodejs.org/api/fs.html#fscopyfilesrc-dest-mode-callback}
 */
export function copyFile(src: string, dest: string, flags?: number, callback?: ((arg0: Error | undefined) => any) | undefined): void;
/**
 * Synchronously copies `src` to `dest` calling `callback` upon success or error.
 * @param {string} src - The source file path.
 * @param {string} dest - The destination file path.
 * @param {number} flags - Modifiers for copy operation.
 * @see {@link https://nodejs.org/api/fs.html#fscopyfilesrc-dest-mode-callback}
 */
export function copyFileSync(src: string, dest: string, flags?: number): void;
/**
 * @see {@link https://nodejs.org/api/fs.html#fscreatewritestreampath-options}
 * @param {string | Buffer | URL} path
 * @param {object?} [options]
 * @returns {ReadStream}
 */
export function createReadStream(path: string | Buffer | URL, options?: object | null): ReadStream;
/**
 * @see {@link https://nodejs.org/api/fs.html#fscreatewritestreampath-options}
 * @param {string | Buffer | URL} path
 * @param {object?} [options]
 * @returns {WriteStream}
 */
export function createWriteStream(path: string | Buffer | URL, options?: object | null): WriteStream;
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
export function fstat(fd: number, options: any, callback: Function | null): void;
/**
 * Request that all data for the open file descriptor is flushed
 * to the storage device.
 * @param {number} fd - A file descriptor.
 * @param {function} callback - The function to call after completion.
 */
export function fsync(fd: number, callback: Function): void;
/**
 * Truncates the file up to `offset` bytes.
 * @param {number} fd - A file descriptor.
 * @param {number=|function} [offset = 0]
 * @param {function?} callback - The function to call after completion.
 */
export function ftruncate(fd: number, offset: any, callback: Function | null): void;
/**
 * Changes ownership of a symbolic link at `path` with `uid` and `gid`.
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 * @param {function} callback
 */
export function lchown(path: string, uid: number, gid: number, callback: Function): TypeError;
/**
 * Changes permissions of link at `path` with `mode` (POSIX). No-op where unsupported.
 * @param {string|Buffer|URL} path
 * @param {number} mode
 * @param {function(Error|null):any} callback
 */
export function lchmod(path: string | Buffer | URL, mode: number, callback: (arg0: Error | null) => any): void;
/**
 * Synchronously changes permissions of a symbolic link at `path`.
 *
 * On platforms that do not implement `lchmod`, the native backend may treat
 * this as a no-op or return a platform-specific error.
 * @param {string|Buffer|URL} path
 * @param {number} mode
 */
export function lchmodSync(path: string | Buffer | URL, mode: number): void;
/**
 * Creates a link to `dest` from `src`.
 * @param {string} src
 * @param {string} dest
 * @param {function}
 */
export function link(src: string, dest: string, callback: any): void;
/**
 * Creates a hard link synchronously
 * @param {string} src
 * @param {string} dest
 */
export function linkSync(src: string, dest: string): void;
/**
 * @ignore
 */
export function mkdir(path: any, options: any, callback: any): void;
/**
 * @ignore
 * @param {string|URL} path
 * @param {object=} [options]
 */
export function mkdirSync(path: string | URL, options?: object | undefined): void;
/**
 * Create a unique temporary directory. The `prefix` is appended with a
 * platform-specific unique suffix.
 * @param {string} prefix
 * @param {object|string|function} [options]
 * @param {string} [options.encoding='utf8']
 * @param {function(Error|null, string|Buffer):any} [callback]
 */
export function mkdtemp(prefix: string, options?: object | string | Function, callback?: (arg0: Error | null, arg1: string | Buffer) => any): void;
/** Create a unique temporary directory synchronously */
export function mkdtempSync(prefix: any, options?: any): any;
/**
 * Asynchronously open a file calling `callback` upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsopenpath-flags-mode-callback}
 * @param {string | Buffer | URL} path
 * @param {string=} [flags = 'r']
 * @param {number=} [mode = 0o666]
 * @param {(object|function(Error|null, number|undefined):any)=} [options]
 * @param {(function(Error|null, number|undefined):any)|null} [callback]
 */
export function open(path: string | Buffer | URL, flags?: string | undefined, mode?: number | undefined, options?: (object | ((arg0: Error | null, arg1: number | undefined) => any)) | undefined, callback?: ((arg0: Error | null, arg1: number | undefined) => any) | null): void;
/**
 * Synchronously open a file.
 * @param {string|Buffer|URL} path
 * @param {string=} [flags = 'r']
 * @param {string=} [mode = 0o666]
 * @param {object=} [options]
 */
export function openSync(path: string | Buffer | URL, flags?: string | undefined, mode?: string | undefined, options?: object | undefined): any;
/**
 * Asynchronously open a directory calling `callback` upon success or error.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string | Buffer | URL} path
 * @param {(object|function(Error|null, Dir|undefined):any)=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @param {function(Error|null, Dir|undefined):any} [callback]
 */
export function opendir(path: string | Buffer | URL, options?: (object | ((arg0: Error | null, arg1: Dir | undefined) => any)) | undefined, callback?: (arg0: Error | null, arg1: Dir | undefined) => any): void;
/**
 * Synchronously open a directory.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string|Buffer|URL} path
 * @param {object} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @return {Dir}
 */
export function opendirSync(path: string | Buffer | URL, options?: {
    encoding?: string | undefined;
    withFileTypes?: boolean | undefined;
}): Dir;
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
export function read(fd: number, buffer: object | Buffer | Uint8Array, offset: number, length: number, position: number | bigint | null, options: any, callback: (arg0: Error | null, arg1: number | undefined, arg2: Buffer | undefined) => any): void;
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
export function write(fd: number, buffer: object | Buffer | Uint8Array, offset: number, length: number, position: number | bigint | null, options: any, callback: (arg0: Error | null, arg1: number | undefined, arg2: Buffer | undefined) => any): void;
/**
 * Vector write convenience: writes multiple buffers sequentially to fd
 * @param {number} fd
 * @param {Array<Buffer|TypedArray>} buffers
 * @param {number|null|function} [position]
 * @param {function(Error|null, number=):any} [callback]
 */
export function writev(fd: number, buffers: Array<Buffer | TypedArray>, position?: number | null | Function, callback?: (arg0: Error | null, arg1: number | undefined) => any): void;
/**
 * Vector read convenience: reads into multiple buffers sequentially from fd
 * @param {number} fd
 * @param {Array<Buffer|TypedArray>} buffers
 * @param {number|null|function} [position]
 * @param {function(Error|null, number, any[]):any} [callback]
 */
export function readv(fd: number, buffers: Array<Buffer | TypedArray>, position?: number | null | Function, callback?: (arg0: Error | null, arg1: number, arg2: any[]) => any): void;
/**
 * Asynchronously read all entries in a directory.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string|Buffer|URL} path
 * @param {object|function(Error|null, (Dirent|string)[]|undefined):any} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @param {function(Error|null, (Dirent|string)[]):any} [callback]
 */
export function readdir(path: string | Buffer | URL, options?: object | ((arg0: Error | null, arg1: (Dirent | string)[] | undefined) => any), callback?: (arg0: Error | null, arg1: (Dirent | string)[]) => any): void;
/**
 * Synchronously read all entries in a directory.
 * @see {@link https://nodejs.org/api/fs.html#fsreaddirpath-options-callback}
 * @param {string|Buffer | URL } path
 * @param {object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {boolean=} [options.withFileTypes = false]
 * @return {(Dirent|string)[]}
 */
export function readdirSync(path: string | Buffer | URL, options?: object | undefined): (Dirent | string)[];
/**
 * @param {string|Buffer|URL|number} path
 * @param {object|function(Error|null, Buffer|string|undefined):any} options
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 * @param {AbortSignal|undefined} [options.signal]
 * @param {function(Error|null, Buffer|string|undefined):any} callback
 */
export function readFile(path: string | Buffer | URL | number, options: object | ((arg0: Error | null, arg1: Buffer | string | undefined) => any), callback: (arg0: Error | null, arg1: Buffer | string | undefined) => any): void;
/**
 * @param {string|Buffer|URL|number} path
 * @param {{ encoding?: string, flags?: string }} [options]
 * @param {object|function(Error|null, Buffer|undefined):any} [options]
 * @param {AbortSignal|undefined} [options.signal]
 * @return {string|Buffer}
 */
export function readFileSync(path: string | Buffer | URL | number, options?: {
    encoding?: string;
    flags?: string;
}): string | Buffer;
/**
 * Reads link at `path`
 * @param {string} path
 * @param {function(Error|null, string|undefined):any} callback
 */
export function readlink(path: string, options: any, callback: (arg0: Error | null, arg1: string | undefined) => any): void;
/**
 * Reads link target at `path` synchronously
 * @param {string} path
 * @return {string}
 */
export function readlinkSync(path: string, options?: any): string;
/**
 * Computes real path for `path`
 * @param {string} path
 * @param {function(Error|null, string|undefined):any} callback
 */
export function realpath(path: string, callback: (arg0: Error | null, arg1: string | undefined) => any): void;
/**
 * Computes real path for `path`
 * @param {string} path
 * @return {string}
 */
export function realpathSync(path: string): string;
/**
 * Renames file or directory at `src` to `dest`.
 * @param {string} src
 * @param {string} dest
 * @param {function(Error|null):any} callback
 */
export function rename(src: string, dest: string, callback: (arg0: Error | null) => any): void;
/**
 * Renames file or directory at `src` to `dest`, synchronously.
 * @param {string} src
 * @param {string} dest
 */
export function renameSync(src: string, dest: string): void;
/**
 * Removes directory at `path`.
 * @param {string} path
 * @param {function(Error|null):any} callback
 */
export function rmdir(path: string, callback: (arg0: Error | null) => any): void;
/**
 * Removes directory at `path`, synchronously.
 * @param {string} path
 */
export function rmdirSync(path: string): void;
/**
 * Synchronously get the stats of a file
 * @param {string} path - filename or file descriptor
 * @param {object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 */
export function statSync(path: string, options?: object | undefined): promises.Stats;
/**
 * Synchronously get the stats of an open file descriptor.
 * @param {number|FileHandle} fd
 * @param {object=} [options]
 */
export function fstatSync(fd: number | FileHandle, options?: object | undefined): promises.Stats;
/**
 * Get the stats of a file
 * @param {string|Buffer|URL|number} path - filename or file descriptor
 * @param {(object|function(Error|null, Stats|undefined):any)=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 * @param {AbortSignal|undefined} [options.signal]
 * @param {function(Error|null, Stats|undefined):any} [callback]
 */
export function stat(path: string | Buffer | URL | number, options?: (object | ((arg0: Error | null, arg1: Stats | undefined) => any)) | undefined, callback?: (arg0: Error | null, arg1: Stats | undefined) => any): void;
/**
 * Get the stats of a symbolic link
 * @param {string|Buffer|URL|number} path - filename or file descriptor
 * @param {(object|function(Error|null, Stats|undefined):any)=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {string=} [options.flag = 'r']
 * @param {AbortSignal|undefined} [options.signal]
 * @param {function(Error|null, Stats|undefined):any} [callback]
 */
export function lstat(path: string | Buffer | URL | number, options?: (object | ((arg0: Error | null, arg1: Stats | undefined) => any)) | undefined, callback?: (arg0: Error | null, arg1: Stats | undefined) => any): void;
/**
 * Synchronously get stats of a symbolic link
 * @param {string|Buffer|URL} path
 * @param {object=} [options]
 */
export function lstatSync(path: string | Buffer | URL, options?: object | undefined): promises.Stats;
/**
 * Creates a symlink of `src` at `dest`.
 * @param {string} src
 * @param {string} dest
 * @param {function(Error|null):any} [callback]
 */
export function symlink(src: string, dest: string, type?: any, callback?: (arg0: Error | null) => any): void;
/**
 * Synchronously create a symlink
 * @param {string} src
 * @param {string} dest
 * @param {string=} [type]
 */
export function symlinkSync(src: string, dest: string, type?: string | undefined): void;
/**
 * Unlinks (removes) file at `path`.
 * @param {string} path
 * @param {function(Error|null):any} callback
 */
export function unlink(path: string, callback: (arg0: Error | null) => any): void;
/**
 * Unlinks (removes) file at `path`, synchronously.
 * @param {string} path
 */
export function unlinkSync(path: string): void;
/**
 * Changes ownership of link at `path` synchronously
 * @param {string} path
 * @param {number} uid
 * @param {number} gid
 */
export function lchownSync(path: string, uid: number, gid: number): void;
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
export function writeFile(path: string | Buffer | URL | number, data: string | Buffer | TypedArray | DataView | object, options?: (object | ((arg0: Error | null) => any)) | undefined, callback?: (arg0: Error | null) => any): void;
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
export function writeFileSync(path: string | Buffer | URL | number, data: string | Buffer | TypedArray | DataView | object, options?: object | undefined): void;
/**
 * Truncate file at `path` to `len` bytes (default 0)
 * @param {string} path
 * @param {number|function} [len=0]
 * @param {function(Error|null):any} [callback]
 */
export function truncate(path: string, len?: number | Function, callback?: (arg0: Error | null) => any): void;
/** Truncate file synchronously */
export function truncateSync(path: any, len?: number): void;
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
export function appendFile(path: string | Buffer | URL | number, data: string | Buffer | TypedArray | DataView | object, options?: (object | ((arg0: Error | null) => any)) | undefined, callback?: (arg0: Error | null) => any): void;
/** Append data synchronously */
export function appendFileSync(path: any, data: any, options: any): void;
/**
 * Remove a file or directory
 * @param {string} path
 * @param {{ recursive?: boolean, force?: boolean }} [options]
 * @param {function(Error|null):any} callback
 */
export function rm(path: string, options?: {
    recursive?: boolean;
    force?: boolean;
}, callback?: (arg0: Error | null) => any): void;
/** Remove synchronously */
export function rmSync(path: any, options?: {}): void;
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
export function cp(src: string, dest: string, options?: {
    recursive?: boolean;
    dereference?: boolean;
    force?: boolean;
    errorOnExist?: boolean;
    preserveTimestamps?: boolean;
    preserveMode?: boolean;
    preserveOwner?: boolean;
    filter?: (arg0: string, arg1: string) => (boolean | Promise<boolean>);
}, callback?: (arg0: Error | null) => any): void;
/** Copy synchronously */
export function cpSync(src: any, dest: any, options?: {}): void;
/**
 * Update atime/mtime for a path
 * @param {string} path
 * @param {number|Date|string} atime
 * @param {number|Date|string} mtime
 * @param {function(Error=)=} [callback]
 */
export function utimes(path: string, atime: number | Date | string, mtime: number | Date | string, callback?: ((arg0: Error | undefined) => any) | undefined): void;
/**
 * Update atime/mtime for a symlink without following it
 */
export function lutimes(path: any, atime: any, mtime: any, callback: any): void;
/** Update atime/mtime for a symlink without following it (sync) */
export function lutimesSync(path: any, atime: any, mtime: any): void;
/**
 * Update atime/mtime for a path (sync)
 */
export function utimesSync(path: any, atime: any, mtime: any): void;
/**
 * Update atime/mtime for an fd
 * @param {number|FileHandle} fd
 */
export function futimes(fd: number | FileHandle, atime: any, mtime: any, callback: any): void;
/**
 * Update atime/mtime for an fd (sync)
 */
export function futimesSync(fd: any, atime: any, mtime: any): void;
/**
 * Watch for changes at `path` calling `callback`
 * @param {string}
 * @param {function|object=} [options]
 * @param {string=} [options.encoding = 'utf8']
 * @param {function=} [callback]
 * @return {Watcher}
 */
export function watch(path: any, options?: (Function | object) | undefined, callback?: Function | undefined): Watcher;
export default exports;
export type TypedArray = Uint8Array | Int8Array;
import { Stats } from './stats.js';
import { Buffer } from '../buffer.js';
import { ReadStream } from './stream.js';
import { WriteStream } from './stream.js';
import { Dir } from './dir.js';
import { Dirent } from './dir.js';
import * as promises from './promises.js';
import { FileHandle } from './handle.js';
import { Watcher } from './watcher.js';
import bookmarks from './bookmarks.js';
import * as constants from './constants.js';
import { DirectoryHandle } from './handle.js';
import fds from './fds.js';
import * as exports from './index.js';
export { bookmarks, constants, Dir, DirectoryHandle, Dirent, fds, FileHandle, promises, ReadStream, Stats, Watcher, WriteStream };
