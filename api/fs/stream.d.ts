export const DEFAULT_STREAM_HIGH_WATER_MARK: number;
/**
 * @typedef {import('./handle.js').FileHandle} FileHandle
 */
/**
 * A `Readable` stream for a `FileHandle`.
 */
/**
 * Emitted when the underlying file descriptor is opened.
 * @event ReadStream#open
 * @type {(fd: number) => void}
 */
/**
 * Emitted when the stream is ready to be used.
 * @event ReadStream#ready
 * @type {() => void}
 */
/**
 * Emitted when the stream is closed.
 * @event ReadStream#close
 * @type {() => void}
 */
export class ReadStream extends Readable {
    end: any;
    start: any;
    handle: any;
    buffer: ArrayBuffer;
    signal: any;
    timeout: any;
    bytesRead: number;
    shouldEmitClose: boolean;
    /**
     * Sets file handle for the ReadStream.
     * @param {FileHandle} handle
     */
    setHandle(handle: FileHandle): void;
    /**
     * The max buffer size for the ReadStream.
     */
    get highWaterMark(): number;
    /**
     * Relative or absolute path of the underlying `FileHandle`.
     */
    get path(): any;
    /**
     * `true` if the stream is in a pending state.
     */
    get pending(): boolean;
    _open(callback: any): Promise<any>;
    _read(callback: any): Promise<any>;
}
export namespace ReadStream {
    export { DEFAULT_STREAM_HIGH_WATER_MARK as highWaterMark };
}
/**
 * A `Writable` stream for a `FileHandle`.
 */
/**
 * Emitted when the underlying file descriptor is opened.
 * @event WriteStream#open
 * @type {(fd: number) => void}
 */
/**
 * Emitted when the stream is ready to be used.
 * @event WriteStream#ready
 * @type {() => void}
 */
/**
 * Emitted when the stream is closed.
 * @event WriteStream#close
 * @type {() => void}
 */
export class WriteStream extends Writable {
    start: any;
    handle: any;
    signal: any;
    timeout: any;
    bytesWritten: number;
    shouldEmitClose: boolean;
    /**
     * Sets file handle for the WriteStream.
     * @param {FileHandle} handle
     */
    setHandle(handle: FileHandle): void;
    /**
     * The max buffer size for the Writetream.
     */
    get highWaterMark(): number;
    /**
     * Relative or absolute path of the underlying `FileHandle`.
     */
    get path(): any;
    /**
     * `true` if the stream is in a pending state.
     */
    get pending(): boolean;
    _open(callback: any): Promise<any>;
    _write(buffer: any, callback: any): any;
}
export namespace WriteStream {
    export { DEFAULT_STREAM_HIGH_WATER_MARK as highWaterMark };
}
export const FileReadStream: typeof exports.ReadStream;
export const FileWriteStream: typeof exports.WriteStream;
export default exports;
export type FileHandle = import("./handle.js").FileHandle;
import { Readable } from '../stream.js';
import { Writable } from '../stream.js';
import * as exports from './stream.js';
