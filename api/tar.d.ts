/**
 * Opens an existing tar archive.
 * @param {string|URL} path
 * @param {TarOpenOptions} [options]
 * @return {Promise<TarArchive>}
 */
export function open(path: string | URL, options?: TarOpenOptions): Promise<TarArchive>;
/**
 * Creates a new tar archive for writing.
 * If the archive already exists it will be truncated.
 * @param {string|URL} path
 * @param {TarOpenOptions} [options]
 * @return {Promise<TarArchive>}
 */
export function create(path: string | URL, options?: TarOpenOptions): Promise<TarArchive>;
/**
 * Opens a tar archive from an in-memory buffer (read-only).
 * @param {Buffer|Uint8Array|ArrayBuffer} buffer
 * @return {Promise<TarArchive>}
 */
export function fromBuffer(buffer: Buffer | Uint8Array | ArrayBuffer): Promise<TarArchive>;
/**
 * Creates a new in-memory tar archive for writing.
 * @param {TarOpenOptions} [options]
 * @return {Promise<TarArchive>}
 */
export function createInMemory(options?: TarOpenOptions): Promise<TarArchive>;
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
    static open(path: string | URL, options?: TarOpenOptions): Promise<TarArchive>;
    /**
     * Creates a new tar archive for writing.
     * If the archive already exists it will be truncated.
     * @param {string|URL} path
     * @param {TarOpenOptions} [options]
     * @return {Promise<TarArchive>}
     */
    static create(path: string | URL, options?: TarOpenOptions): Promise<TarArchive>;
    /**
     * Opens a tar archive from an in-memory buffer (read-only).
     * @param {Buffer|Uint8Array|ArrayBuffer} buffer
     * @return {Promise<TarArchive>}
     */
    static fromBuffer(buffer: Buffer | Uint8Array | ArrayBuffer): Promise<TarArchive>;
    /**
     * Creates a new in-memory tar archive for writing.
     * @param {TarOpenOptions} [options]
     * @return {Promise<TarArchive>}
     */
    static createInMemory(options?: TarOpenOptions): Promise<TarArchive>;
    /**
     * @ignore
     * @param {object} state
     */
    constructor(state: object);
    id: string;
    path: string;
    writable: boolean;
    mmap: boolean;
    size: number;
    entryCount: number;
    closed: boolean;
    finalized: boolean;
    /**
     * Closes the underlying archive descriptor.
     * Further operations on this instance will throw.
     * @return {Promise<void>}
     */
    close(): Promise<void>;
    /**
     * Lists all entries in the archive.
     * @return {Promise<TarEntryStat[]>}
     */
    entries(): Promise<TarEntryStat[]>;
    /**
     * Extracts all file and directory entries in the archive into a destination
     * directory using streaming. Attempts to preserve mode/mtime metadata.
     * @param {string|URL} destDir
     * @param {{ signal?: AbortSignal, timeout?: number, filter?: (entry: TarEntryStat) => boolean, preserveLinks?: boolean, preserveOwner?: boolean, preserveSpecialModes?: boolean }} [options]
     * @return {Promise<void>}
     */
    extractAll(destDir: string | URL, options?: {
        signal?: AbortSignal;
        timeout?: number;
        filter?: (entry: TarEntryStat) => boolean;
        preserveLinks?: boolean;
        preserveOwner?: boolean;
        preserveSpecialModes?: boolean;
    }): Promise<void>;
    /**
     * Returns metadata for a single entry path.
     * @param {string} entryPath
     * @return {Promise<TarEntryStat>}
     */
    stat(path: any): Promise<TarEntryStat>;
    /**
     * Reads a slice of an entry as a Buffer.
     * @param {string} path
     * @param {TarReadOptions} [options]
     * @return {Promise<Buffer>}
     */
    read(path: string, options?: TarReadOptions): Promise<Buffer>;
    /**
     * Creates an async iterator that yields Buffer chunks for a given entry.
     * This provides a streaming decode interface without requiring Node.js streams.
     * @param {string} path
     * @param {TarReadStreamOptions} [options]
     * @return {AsyncIterableIterator<Buffer>}
     */
    readStream(path: string, options?: TarReadStreamOptions): AsyncIterableIterator<Buffer>;
    /**
     * Appends a single entry to the archive.
     * The entry body can be a Buffer, ArrayBuffer, Uint8Array, or an async iterable of Buffers.
     * @param {TarEntryHeader} header
     * @param {Buffer|Uint8Array|ArrayBuffer|AsyncIterable<Buffer|Uint8Array>} body
     * @param {TarWriteOptions} [options]
     * @return {Promise<void>}
     */
    append(header: TarEntryHeader, body: Buffer | Uint8Array | ArrayBuffer | AsyncIterable<Buffer | Uint8Array>, options?: TarWriteOptions): Promise<void>;
    /**
     * Finalizes the archive, writing terminating blocks and flushing the sink.
     * After calling this, the archive is still considered open but no further
     * writes should be performed.
     * @return {Promise<void>}
     */
    finalize(): Promise<void>;
    /**
     * Finalizes the archive if necessary and returns the underlying tar
     * archive bytes as a Buffer. For in-memory writable archives this
     * contains the composed archive; for read-only archives created via
     * fromBuffer it returns the original buffer.
     * @param {{ signal?: AbortSignal, timeout?: number }} [options]
     * @return {Promise<Buffer>}
     */
    toBuffer(options?: {
        signal?: AbortSignal;
        timeout?: number;
    }): Promise<Buffer>;
    /**
     * Extracts a single entry to a destination path on disk.
     * This helper uses streaming reads for large entries.
     * @param {string} path
     * @param {string|URL} destPath
     * @param {{ signal?: AbortSignal, timeout?: number, preserveOwner?: boolean, preserveSpecialModes?: boolean }} [options]
     * @return {Promise<void>}
     */
    extract(entryPath: any, destPath: string | URL, options?: {
        signal?: AbortSignal;
        timeout?: number;
        preserveOwner?: boolean;
        preserveSpecialModes?: boolean;
    }): Promise<void>;
    #private;
}
export default api;
export type TarEntryKind = "file" | "directory" | "symlink" | "hardlink" | "block-device" | "char-device" | "fifo" | "other";
export type TarSparseRegion = {
    offset: number;
    length: number;
};
export type TarEntryHeader = {
    path: string;
    /**
     * Total number of bytes for the entry body. Required when `body` is an AsyncIterable.
     */
    size?: number;
    /**
     * Sparse data regions for sparse file entries.
     */
    sparse?: TarSparseRegion[];
    /**
     * Logical size of the sparse file entry (defaults to the end of the last region).
     */
    sparseSize?: number;
    mode?: number;
    mtime?: number;
    uid?: number;
    gid?: number;
    uname?: string;
    gname?: string;
    /**
     * Target path for link entries (symlink/hardlink)
     */
    linkpath?: string;
    /**
     * Device major number for char/block device entries
     */
    devmajor?: number;
    /**
     * Device minor number for char/block device entries
     */
    devminor?: number;
    kind?: TarEntryKind;
};
export type TarEntryStat = {
    path: string;
    size: number;
    mode: number;
    mtime: number;
    uid: number;
    gid: number;
    uname?: string;
    gname?: string;
    kind: TarEntryKind;
    isFile: boolean;
    isDirectory: boolean;
    linkpath?: string;
    devmajor?: number;
    devminor?: number;
    /**
     * Sparse data regions (present for sparse file entries)
     */
    sparse?: TarSparseRegion[];
};
export type TarOpenOptions = {
    writable?: boolean;
    mmap?: boolean;
    uid?: number;
    gid?: number;
    uname?: string;
    gname?: string;
    mtime?: number;
};
export type TarReadOptions = {
    offset?: number;
    /**
     * If omitted, reads until end of entry
     */
    length?: number;
    signal?: AbortSignal;
    timeout?: number;
};
export type TarReadStreamOptions = {
    highWaterMark?: number;
    start?: number;
    /**
     * Inclusive end offset (defaults to entry size - 1)
     */
    end?: number;
    signal?: AbortSignal;
    timeout?: number;
};
export type TarWriteEntryOptions = {
    mode?: number;
    /**
     * Defaults to the archive global mtime (if set), otherwise current time.
     */
    mtime?: number;
    kind?: TarEntryKind;
};
export type TarWriteOptions = {
    signal?: AbortSignal;
    timeout?: number;
};
import { Buffer } from './buffer.js';
declare const api: Readonly<{
    TarArchive: typeof TarArchive;
    open: typeof open;
    create: typeof create;
    fromBuffer: typeof fromBuffer;
    createInMemory: typeof createInMemory;
}>;
