export class ByteLengthQueuingStrategy {
    constructor(e: any);
    _byteLengthQueuingStrategyHighWaterMark: any;
    get highWaterMark(): any;
    get size(): (e: any) => any;
}
export class CountQueuingStrategy {
    constructor(e: any);
    _countQueuingStrategyHighWaterMark: any;
    get highWaterMark(): any;
    get size(): () => number;
}
export class ReadableByteStreamController {
    get byobRequest(): any;
    get desiredSize(): number;
    close(): void;
    enqueue(e: any): void;
    error(e?: any): void;
    _pendingPullIntos: v;
    [T](e: any): any;
    [C](e: any): any;
    [P](): void;
}
export class ReadableStream {
    [x: number]: (e: any) => any;
    static from(e: any): any;
    constructor(e?: {}, t?: {});
    get locked(): boolean;
    cancel(e?: any): any;
    getReader(e?: any): ReadableStreamBYOBReader | ReadableStreamDefaultReader;
    pipeThrough(e: any, t?: {}): any;
    pipeTo(e: any, t?: {}): any;
    tee(): any;
    values(e?: any): any;
}
export class ReadableStreamBYOBReader {
    constructor(e: any);
    _readIntoRequests: v;
    get closed(): any;
    cancel(e?: any): any;
    read(e: any, t?: {}): any;
    releaseLock(): void;
}
export class ReadableStreamBYOBRequest {
    get view(): any;
    respond(e: any): void;
    respondWithNewView(e: any): void;
}
export class ReadableStreamDefaultController {
    get desiredSize(): number;
    close(): void;
    enqueue(e?: any): void;
    error(e?: any): void;
    [T](e: any): any;
    [C](e: any): void;
    [P](): void;
}
export class ReadableStreamDefaultReader {
    constructor(e: any);
    _readRequests: v;
    get closed(): any;
    cancel(e?: any): any;
    read(): any;
    releaseLock(): void;
}
export class TransformStream {
    constructor(e?: {}, t?: {}, r?: {});
    get readable(): any;
    get writable(): any;
}
export class TransformStreamDefaultController {
    get desiredSize(): number;
    enqueue(e?: any): void;
    error(e?: any): void;
    terminate(): void;
}
export class WritableStream {
    constructor(e?: {}, t?: {});
    get locked(): boolean;
    abort(e?: any): any;
    close(): any;
    getWriter(): WritableStreamDefaultWriter;
}
export class WritableStreamDefaultController {
    get abortReason(): any;
    get signal(): any;
    error(e?: any): void;
    [w](e: any): any;
    [R](): void;
}
export class WritableStreamDefaultWriter {
    constructor(e: any);
    _ownerWritableStream: any;
    get closed(): any;
    get desiredSize(): number;
    get ready(): any;
    abort(e?: any): any;
    close(): any;
    releaseLock(): void;
    write(e?: any): any;
}
declare class v {
    _cursor: number;
    _size: number;
    _front: {
        _elements: any[];
        _next: any;
    };
    _back: {
        _elements: any[];
        _next: any;
    };
    get length(): number;
    push(e: any): void;
    shift(): any;
    forEach(e: any): void;
    peek(): any;
}
declare const T: unique symbol;
declare const C: unique symbol;
declare const P: unique symbol;
declare const w: unique symbol;
declare const R: unique symbol;
export {};
