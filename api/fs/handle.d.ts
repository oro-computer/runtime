export const kOpening: unique symbol;
export const kClosing: unique symbol;
export const kClosed: unique symbol;
/**
 * A container for a descriptor tracked in `fds` and opened in the native layer.
 * This class implements the Node.js `FileHandle` interface
 * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#class-filehandle}
 */
export class FileHandle extends EventEmitter {
    [x: number]: (options: any) => import("../gc.js").Finalizer;
    /**
     * Emitted when the file handle has been opened.
     * @event FileHandle#open
     * @type {(fd: number) => void}
     */
    /**
     * Emitted when the file handle has been closed.
     * @event FileHandle#close
     * @type {() => void}
     */
    static get DEFAULT_ACCESS_MODE(): () => void;
    static get DEFAULT_OPEN_FLAGS(): string;
    static get DEFAULT_OPEN_MODE(): number;
    /**
     * Creates a `FileHandle` from a given `id` or `fd`
     * @param {string|number|FileHandle|object|FileSystemFileHandle} id
     * @return {FileHandle}
     */
    static from(id: string | number | FileHandle | object | FileSystemFileHandle): FileHandle;
    /**
     * Determines if access to `path` for `mode` is possible.
     * @param {string} path
     * @param {number} [mode = 0o666]
     * @param {object=} [options]
     * @return {Promise<boolean>}
     */
    static access(path: string, mode?: number, options?: object | undefined): Promise<boolean>;
    /**
     * Asynchronously open a file.
     * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#fspromisesopenpath-flags-mode}
     * @param {string | Buffer | URL} path
     * @param {string=} [flags = 'r']
     * @param {string|number=} [mode = 0o666]
     * @param {object=} [options]
     * @return {Promise<FileHandle>}
     */
    static open(path: string | Buffer | URL, flags?: string | undefined, mode?: (string | number) | undefined, options?: object | undefined): Promise<FileHandle>;
    /**
     * `FileHandle` class constructor
     * @ignore
     * @param {object} options
     */
    constructor(options: object);
    flags: number;
    path: any;
    mode: any;
    id: string;
    fd: any;
    /**
     * `true` if the `FileHandle` instance has been opened.
     * @type {boolean}
     */
    get opened(): boolean;
    /**
     * `true` if the `FileHandle` is opening.
     * @type {boolean}
     */
    get opening(): boolean;
    /**
     * `true` if the `FileHandle` is closing.
     * @type {boolean}
     */
    get closing(): boolean;
    /**
     * `true` if the `FileHandle` is closed.
     */
    get closed(): boolean;
    /**
     * Appends to a file, if handle was opened with `O_APPEND`, otherwise this
     * method is just an alias to `FileHandle#writeFile()`.
     * @param {string|Buffer|TypedArray|Array} data
     * @param {object=} [options]
     * @param {string=} [options.encoding = 'utf8']
     * @param {object=} [options.signal]
     */
    appendFile(data: string | Buffer | TypedArray | any[], options?: object | undefined): Promise<TypeError | {
        buffer: any;
        bytesWritten: number;
    }>;
    /**
     * Change permissions of file handle.
     * @param {number} mode
     * @param {object=} [options]
     */
    chmod(mode: number, options?: object | undefined): Promise<TypeError>;
    /**
     * Change ownership of file handle.
     * @param {number} uid
     * @param {number} gid
     * @param {object=} [options]
     */
    chown(uid: number, gid: number, options?: object | undefined): Promise<TypeError>;
    /**
     * Close underlying file handle
     * @param {object=} [options]
     */
    close(options?: object | undefined): Promise<any>;
    /**
     * Creates a `ReadStream` for the underlying file.
     * @param {object=} [options] - An options object
     */
    createReadStream(options?: object | undefined): ReadStream;
    /**
     * Creates a `WriteStream` for the underlying file.
     * @param {object=} [options] - An options object
     */
    createWriteStream(options?: object | undefined): WriteStream;
    /**
     * @param {object=} [options]
     */
    datasync(): Promise<TypeError>;
    /**
     * Opens the underlying descriptor for the file handle.
     * @param {object=} [options]
     */
    open(options?: object | undefined): Promise<any>;
    /**
     * Reads `length` bytes starting from `position` into `buffer` at
     * `offset`.
     * @param {Buffer|object} buffer
     * @param {number=} [offset]
     * @param {number=} [length]
     * @param {number=} [position]
     * @param {object=} [options]
     */
    read(buffer: Buffer | object, offset?: number | undefined, length?: number | undefined, position?: number | undefined, options?: object | undefined): Promise<{
        bytesRead: number;
        buffer: any;
    }>;
    /**
     * Read into multiple buffers sequentially (vector read)
     * @param {Array<Buffer|TypedArray>} buffers
     * @param {number|null=} [position]
     * @returns {Promise<{ bytesRead: number, buffers: any[] }>}
     */
    readv(buffers: Array<Buffer | TypedArray>, position?: (number | null) | undefined): Promise<{
        bytesRead: number;
        buffers: any[];
    }>;
    /**
     * Reads the entire contents of a file and returns it as a buffer or a string
     * specified of a given encoding specified at `options.encoding`.
     * @param {object=} [options]
     * @param {string=} [options.encoding = 'utf8']
     * @param {object=} [options.signal]
     */
    readFile(options?: object | undefined): Promise<string | Uint8Array<any>>;
    /**
     * Returns the stats of the underlying file.
     * @param {object=} [options]
     * @return {Promise<Stats>}
     */
    stat(options?: object | undefined): Promise<Stats>;
    /**
     * Returns the stats of the underlying symbolic link.
     * @param {object=} [options]
     * @return {Promise<Stats>}
     */
    lstat(options?: object | undefined): Promise<Stats>;
    /**
     * Synchronize a file's in-core state with storage device
     * @return {Promise}
     */
    sync(): Promise<any>;
    /**
     * @param {number} [offset = 0]
     * @return {Promise}
     */
    truncate(offset?: number): Promise<any>;
    /**
     * Writes `length` bytes at `offset` in `buffer` to the underlying file
     * at `position`.
     * @param {Buffer|object} buffer
     * @param {number} offset
     * @param {number} length
     * @param {number} position
     * @param {object=} [options]
     */
    write(buffer: Buffer | object, offset: number, length: number, position: number, options?: object | undefined): Promise<TypeError | {
        buffer: any;
        bytesWritten: number;
    }>;
    /**
     * Write multiple buffers sequentially (vector write)
     * @param {Array<Buffer|TypedArray>} buffers
     * @param {number|null=} [position]
     * @returns {Promise<number>} bytesWritten
     */
    writev(buffers: Array<Buffer | TypedArray>, position?: (number | null) | undefined): Promise<number>;
    /**
     * Writes `data` to file.
     * @param {string|Buffer|TypedArray|Array} data
     * @param {object=} [options]
     * @param {string=} [options.encoding = 'utf8']
     * @param {object=} [options.signal]
     */
    writeFile(data: string | Buffer | TypedArray | any[], options?: object | undefined): Promise<TypeError>;
    [exports.kOpening]: any;
    [exports.kClosing]: any;
    [exports.kClosed]: boolean;
    #private;
}
/**
 * A container for a directory handle tracked in `fds` and opened in the
 * native layer.
 */
export class DirectoryHandle extends EventEmitter {
    [x: number]: (options: any) => import("../gc.js").Finalizer;
    /**
     * The max number of entries that can be bufferd with the `bufferSize`
     * option.
     */
    static get MAX_BUFFER_SIZE(): number;
    static get MAX_ENTRIES(): number;
    /**
     * The default number of entries `Dirent` that are buffered
     * for each read request.
     */
    static get DEFAULT_BUFFER_SIZE(): number;
    /**
     * Creates a `DirectoryHandle` from a given `id` or `fd`
     * @param {string|number|DirectoryHandle|object|FileSystemDirectoryHandle} id
     * @param {object} options
     * @return {DirectoryHandle}
     */
    static from(id: string | number | DirectoryHandle | object | FileSystemDirectoryHandle, options: object): DirectoryHandle;
    /**
     * Asynchronously open a directory.
     * @param {string | Buffer | URL} path
     * @param {object=} [options]
     * @return {Promise<DirectoryHandle>}
     */
    static open(path: string | Buffer | URL, options?: object | undefined): Promise<DirectoryHandle>;
    /**
     * `DirectoryHandle` class constructor
     * @private
     * @param {object} options
     */
    private constructor();
    id: string;
    path: any;
    bufferSize: number;
    /**
     * DirectoryHandle file descriptor id
     */
    get fd(): string;
    /**
     * `true` if the `DirectoryHandle` instance has been opened.
     * @type {boolean}
     */
    get opened(): boolean;
    /**
     * `true` if the `DirectoryHandle` is opening.
     * @type {boolean}
     */
    get opening(): boolean;
    /**
     * `true` if the `DirectoryHandle` is closing.
     * @type {boolean}
     */
    get closing(): boolean;
    /**
     * `true` if `DirectoryHandle` is closed.
     */
    get closed(): boolean;
    /**
     * Opens the underlying handle for a directory.
     * @param {object=} options
     * @return {Promise<boolean>}
     */
    open(options?: object | undefined): Promise<boolean>;
    /**
     * Close underlying directory handle
     * @param {object=} [options]
     */
    close(options?: object | undefined): Promise<any>;
    /**
     * Reads directory entries
     * @param {object=} [options]
     * @param {number=} [options.entries = DirectoryHandle.MAX_ENTRIES]
     */
    read(options?: object | undefined): Promise<any>;
    [exports.kOpening]: any;
    [exports.kClosing]: any;
    [exports.kClosed]: boolean;
    #private;
}
export default exports;
export type TypedArray = Uint8Array | Int8Array;
import { EventEmitter } from '../events.js';
import { Buffer } from '../buffer.js';
import { ReadStream } from './stream.js';
import { WriteStream } from './stream.js';
import { Stats } from './stats.js';
import * as exports from './handle.js';
