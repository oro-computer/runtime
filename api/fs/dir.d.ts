/**
 * Sorts directory entries
 * @param {string|Dirent} a
 * @param {string|Dirent} b
 * @return {number}
 */
export function sortDirectoryEntries(a: string | Dirent, b: string | Dirent): number;
export const kType: unique symbol;
/**
 * A containerr for a directory and its entries. This class supports scanning
 * a directory entry by entry with a `read()` method. The `Symbol.asyncIterator`
 * interface is exposed along with an AsyncGenerator `entries()` method.
 * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#class-fsdir}
 */
export class Dir {
    static from(fdOrHandle: any, options: any): exports.Dir;
    /**
     * `Dir` class constructor.
     * @param {DirectoryHandle} handle
     * @param {object=} options
     */
    constructor(handle: DirectoryHandle, options?: object | undefined);
    path: any;
    handle: DirectoryHandle;
    encoding: any;
    withFileTypes: boolean;
    /**
     * `true` if closed, otherwise `false`.
     * @ignore
     * @type {boolean}
     */
    get closed(): boolean;
    /**
     * `true` if closing, otherwise `false`.
     * @ignore
     * @type {boolean}
     */
    get closing(): boolean;
    /**
     * Closes container and underlying handle.
     * @param {object|function} options
     * @param {function=} callback
     */
    close(options?: object | Function, callback?: Function | undefined): Promise<any>;
    /**
     * Closes container and underlying handle
     * synchronously.
     * @param {object=} [options]
     */
    closeSync(options?: object | undefined): void;
    /**
     * Reads and returns directory entry.
     * @param {object|function} options
     * @param {function=} callback
     * @return {Promise<Dirent[]|string[]>}
     */
    read(options: object | Function, callback?: Function | undefined): Promise<Dirent[] | string[]>;
    /**
     * Reads and returns directory entry synchronously.
     * @param {object|function} options
     * @return {Dirent[]|string[]}
     */
    readSync(options?: object | Function): Dirent[] | string[];
    /**
     * AsyncGenerator which yields directory entries.
     * @param {object=} options
     */
    entries(options?: object | undefined): AsyncGenerator<string | exports.Dirent, void, unknown>;
    /**
     * `for await (...)` AsyncGenerator support.
     */
    get [Symbol.asyncIterator](): (options?: object | undefined) => AsyncGenerator<string | exports.Dirent, void, unknown>;
}
/**
 * A container for a directory entry.
 * @see {@link https://nodejs.org/dist/latest-v20.x/docs/api/fs.html#class-fsdirent}
 */
export class Dirent {
    static get UNKNOWN(): number;
    static get FILE(): number;
    static get DIR(): number;
    static get LINK(): number;
    static get FIFO(): number;
    static get SOCKET(): number;
    static get CHAR(): number;
    static get BLOCK(): number;
    /**
     * Creates `Dirent` instance from input.
     * @param {object|string} name
     * @param {(string|number)=} type
     */
    static from(name: object | string, type?: (string | number) | undefined): exports.Dirent;
    /**
     * `Dirent` class constructor.
     * @param {string} name
     * @param {string|number} type
     */
    constructor(name: string, type: string | number);
    name: string;
    /**
     * Read only type.
     */
    get type(): number;
    /**
     * `true` if `Dirent` instance is a directory.
     */
    isDirectory(): boolean;
    /**
     * `true` if `Dirent` instance is a file.
     */
    isFile(): boolean;
    /**
     * `true` if `Dirent` instance is a block device.
     */
    isBlockDevice(): boolean;
    /**
     * `true` if `Dirent` instance is a character device.
     */
    isCharacterDevice(): boolean;
    /**
     * `true` if `Dirent` instance is a symbolic link.
     */
    isSymbolicLink(): boolean;
    /**
     * `true` if `Dirent` instance is a FIFO.
     */
    isFIFO(): boolean;
    /**
     * `true` if `Dirent` instance is a socket.
     */
    isSocket(): boolean;
    [exports.kType]: number;
}
export default exports;
import { DirectoryHandle } from './handle.js';
import * as exports from './dir.js';
