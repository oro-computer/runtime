export function pipelinePromise(...streams: any[]): Promise<any>;
export function pipeline(stream: any, ...streams: any[]): any;
export function isStream(stream: any): boolean;
export function isStreamx(stream: any): boolean;
export function getStreamError(stream: any): any;
export function isReadStreamx(stream: any): any;
export { web };
export class FixedFIFO {
    constructor(hwm: any);
    buffer: any[];
    mask: number;
    top: number;
    btm: number;
    next: any;
    clear(): void;
    push(data: any): boolean;
    shift(): any;
    peek(): any;
    isEmpty(): boolean;
}
export class FIFO {
    constructor(hwm: any);
    hwm: any;
    head: FixedFIFO;
    tail: FixedFIFO;
    length: number;
    clear(): void;
    push(val: any): void;
    shift(): any;
    peek(): any;
    isEmpty(): boolean;
}
export class WritableState {
    constructor(stream: any, { highWaterMark, map, mapWritable, byteLength, byteLengthWritable }?: {
        highWaterMark?: number;
        map?: any;
    });
    stream: any;
    queue: FIFO;
    highWaterMark: number;
    buffered: number;
    error: any;
    pipeline: any;
    drains: any;
    byteLength: any;
    map: any;
    afterWrite: any;
    afterUpdateNextTick: any;
    get ended(): boolean;
    push(data: any): boolean;
    shift(): any;
    end(data: any): void;
    autoBatch(data: any, cb: any): any;
    update(): void;
    updateNonPrimary(): void;
    continueUpdate(): boolean;
    updateCallback(): void;
    updateNextTick(): void;
}
export class ReadableState {
    constructor(stream: any, { highWaterMark, map, mapReadable, byteLength, byteLengthReadable }?: {
        highWaterMark?: number;
        map?: any;
    });
    stream: any;
    queue: FIFO;
    highWaterMark: number;
    buffered: number;
    readAhead: boolean;
    error: any;
    pipeline: Pipeline;
    byteLength: any;
    map: any;
    pipeTo: any;
    afterRead: any;
    afterUpdateNextTick: any;
    get ended(): boolean;
    pipe(pipeTo: any, cb: any): void;
    push(data: any): boolean;
    shift(): any;
    unshift(data: any): void;
    read(): any;
    drain(): void;
    update(): void;
    updateNonPrimary(): void;
    continueUpdate(): boolean;
    updateCallback(): void;
    updateNextTick(): void;
}
export class TransformState {
    constructor(stream: any);
    data: any;
    afterTransform: any;
    afterFinal: any;
}
export class Pipeline {
    constructor(src: any, dst: any, cb: any);
    from: any;
    to: any;
    afterPipe: any;
    error: any;
    pipeToFinished: boolean;
    finished(): void;
    done(stream: any, err: any): void;
}
export class Stream extends EventEmitter {
    constructor(opts: any);
    _duplexState: number;
    _readableState: any;
    _writableState: any;
    _open(cb: any): void;
    _destroy(cb: any): void;
    _predestroy(): void;
    _signal: any;
    _abortHandler: any;
    get readable(): boolean;
    get writable(): boolean;
    get destroyed(): boolean;
    get destroying(): boolean;
    destroy(err: any): void;
}
/**
 * Emitted when there is data available to read.
 * @event Readable#readable
 * @type {() => void}
 */
/**
 * Emitted when a chunk of data is available.
 * @event Readable#data
 * @type {(chunk: Buffer) => void}
 */
/**
 * Emitted when no more data will be provided.
 * @event Readable#end
 * @type {() => void}
 */
/**
 * Emitted when the stream and any of its underlying resources have been closed.
 * @event Readable#close
 * @type {() => void}
 */
/**
 * Emitted if an error occurs.
 * @event Readable#error
 * @type {(err: Error) => void}
 */
export class Readable extends Stream {
    [x: symbol]: () => {
        [asyncIterator]: () => /*elided*/ any;
        next(): Promise<any>;
        return(): Promise<any>;
        throw(err: any): Promise<any>;
    };
    static _fromAsyncIterator(ite: any, opts: any): Readable;
    static from(data: any, opts: any): any;
    static isBackpressured(rs: any): boolean;
    static isPaused(rs: any): boolean;
    _readableState: ReadableState;
    _read(cb: any): void;
    pipe(dest: any, cb: any): any;
    read(): any;
    push(data: any): boolean;
    unshift(data: any): void;
    resume(): this;
    pause(): this;
}
/**
 * Emitted when it is safe to write more data.
 * @event Writable#drain
 * @type {() => void}
 */
/**
 * Emitted when all data has been flushed to the underlying system.
 * @event Writable#finish
 * @type {() => void}
 */
/**
 * Emitted when the stream and any of its underlying resources have been closed.
 * @event Writable#close
 * @type {() => void}
 */
/**
 * Emitted if an error occurs.
 * @event Writable#error
 * @type {(err: Error) => void}
 */
export class Writable extends Stream {
    static isBackpressured(ws: any): boolean;
    static drained(ws: any): Promise<any>;
    _writableState: WritableState;
    _writev(batch: any, cb: any): void;
    _write(data: any, cb: any): void;
    _final(cb: any): void;
    write(data: any): boolean;
    end(data: any): this;
}
/**
 * Duplex streams are both readable and writable.
 * @event Duplex#readable
 * @event Duplex#data
 * @event Duplex#end
 * @event Duplex#drain
 * @event Duplex#finish
 * @event Duplex#close
 * @event Duplex#error
 */
export class Duplex extends Readable {
    _writableState: WritableState;
    _writev(batch: any, cb: any): void;
    _write(data: any, cb: any): void;
    _final(cb: any): void;
    write(data: any): boolean;
    end(data: any): this;
}
export class Transform extends Duplex {
    _transformState: TransformState;
    _transform(data: any, cb: any): void;
    _flush(cb: any): void;
}
export class PassThrough extends Transform {
}
declare const _default: typeof Stream & {
    web: typeof web;
    Readable: typeof Readable;
    Writable: typeof Writable;
    Duplex: typeof Duplex;
    Transform: typeof Transform;
    PassThrough: typeof PassThrough;
    pipeline: typeof pipeline & {
        [x: symbol]: typeof pipelinePromise;
    };
};
export default _default;
import web from './stream/web.js';
import { EventEmitter } from './events.js';
declare const asyncIterator: symbol;
