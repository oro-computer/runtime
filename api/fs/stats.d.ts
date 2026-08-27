/**
 * A container for various stats about a file or directory.
 */
export class Stats {
    /**
     * Creates a `Stats` instance from input, optionally with `BigInt` data types
     * @param {object|Stats} [stat]
     * @param {boolean=} [fromBigInt = false]
     * @return {Stats}
     */
    static from(stat?: object | Stats, fromBigInt?: boolean | undefined): Stats;
    /**
     * `Stats` class constructor.
     * @param {object|Stats} stat
     */
    constructor(stat: object | Stats);
    dev: any;
    ino: any;
    mode: any;
    nlink: any;
    uid: any;
    gid: any;
    rdev: any;
    size: any;
    blksize: any;
    blocks: any;
    atimeMs: any;
    mtimeMs: any;
    ctimeMs: any;
    birthtimeMs: any;
    atimeNs: any;
    mtimeNs: any;
    ctimeNs: any;
    birthtimeNs: any;
    atime: Date;
    mtime: Date;
    ctime: Date;
    birthtime: Date;
    /**
     * Returns `true` if stats represents a directory.
     * @return {Boolean}
     */
    isDirectory(): boolean;
    /**
     * Returns `true` if stats represents a file.
     * @return {Boolean}
     */
    isFile(): boolean;
    /**
     * Returns `true` if stats represents a block device.
     * @return {Boolean}
     */
    isBlockDevice(): boolean;
    /**
     * Returns `true` if stats represents a character device.
     * @return {Boolean}
     */
    isCharacterDevice(): boolean;
    /**
     * Returns `true` if stats represents a symbolic link.
     * @return {Boolean}
     */
    isSymbolicLink(): boolean;
    /**
     * Returns `true` if stats represents a FIFO.
     * @return {Boolean}
     */
    isFIFO(): boolean;
    /**
     * Returns `true` if stats represents a socket.
     * @return {Boolean}
     */
    isSocket(): boolean;
}
export default exports;
import * as exports from './stats.js';
