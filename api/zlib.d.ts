/**
 * Reports whether the zlib primitive is available in the current runtime
 * (based on native build configuration).
 * @return {Promise<boolean>}
 */
export function isAvailable(): Promise<boolean>;
/**
 * Compresses a buffer using zlib/deflate.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export function deflate(input: Buffer | Uint8Array | ArrayBuffer | string, options?: ZlibOptions): Promise<Buffer>;
/**
 * Decompresses a buffer using zlib/inflate.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export function inflate(input: Buffer | Uint8Array | ArrayBuffer | string, options?: ZlibOptions): Promise<Buffer>;
/**
 * Compresses a buffer using gzip framing.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export function gzip(input: Buffer | Uint8Array | ArrayBuffer | string, options?: ZlibOptions): Promise<Buffer>;
/**
 * Decompresses a gzip buffer.
 * @param {Buffer|Uint8Array|ArrayBuffer|string} input
 * @param {ZlibOptions} [options]
 * @return {Promise<Buffer>}
 */
export function gunzip(input: Buffer | Uint8Array | ArrayBuffer | string, options?: ZlibOptions): Promise<Buffer>;
/**
 * Convenience helper for creating a deflate stream.
 * @param {ZlibStreamOptions} [options]
 * @return {Promise<ZlibStream>}
 */
export function createDeflateStream(options?: ZlibStreamOptions): Promise<ZlibStream>;
/**
 * Convenience helper for creating an inflate stream.
 * @param {ZlibStreamOptions} [options]
 * @return {Promise<ZlibStream>}
 */
export function createInflateStream(options?: ZlibStreamOptions): Promise<ZlibStream>;
/**
 * Represents a stateful zlib stream backed by the native runtime.
 */
export class ZlibStream {
    /**
     * Opens a new zlib stream.
     * @param {ZlibMode} [mode='deflate']
     * @param {ZlibStreamOptions} [options]
     * @return {Promise<ZlibStream>}
     */
    static create(mode?: ZlibMode, options?: ZlibStreamOptions): Promise<ZlibStream>;
    /**
     * @ignore
     * @param {{ id: string|number|bigint, mode: ZlibMode, format: ZlibFormat }} state
     */
    constructor(state: {
        id: string | number | bigint;
        mode: ZlibMode;
        format: ZlibFormat;
    });
    id: string;
    mode: string;
    format: string;
    closed: boolean;
    /**
     * Writes a chunk into the stream and returns the processed output chunk.
     * @param {Buffer|Uint8Array|ArrayBuffer|string} chunk
     * @param {ZlibChunkOptions} [options]
     * @return {Promise<Buffer>}
     */
    write(chunk: Buffer | Uint8Array | ArrayBuffer | string, options?: ZlibChunkOptions): Promise<Buffer>;
    /**
     * Signals the end of the stream. An optional final chunk can be provided.
     * @param {Buffer|Uint8Array|ArrayBuffer|string} [chunk]
     * @param {ZlibChunkOptions} [options]
     * @return {Promise<Buffer>}
     */
    end(chunk?: Buffer | Uint8Array | ArrayBuffer | string, options?: ZlibChunkOptions): Promise<Buffer>;
    /**
     * Closes the stream without sending additional data.
     * Subsequent writes will throw.
     */
    close(): void;
}
export default api;
export type ZlibFormat = "zlib" | "gzip" | "raw";
export type ZlibMode = "deflate" | "inflate";
export type ZlibOptions = {
    format?: ZlibFormat;
    /**
     * Compression level (0-9, zlib default when omitted)
     */
    level?: number;
    signal?: AbortSignal;
    timeout?: number;
};
export type ZlibStreamOptions = ZlibOptions & {
    mode?: ZlibMode;
};
export type ZlibChunkOptions = {
    finish?: boolean;
    signal?: AbortSignal;
    timeout?: number;
};
import { Buffer } from './buffer.js';
declare const api: Readonly<{
    isAvailable: typeof isAvailable;
    deflate: typeof deflate;
    inflate: typeof inflate;
    gzip: typeof gzip;
    gunzip: typeof gunzip;
    ZlibStream: typeof ZlibStream;
    createDeflateStream: typeof createDeflateStream;
    createInflateStream: typeof createInflateStream;
}>;
