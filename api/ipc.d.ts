/**
 * Converts a structured IPC error payload into an `Error` instance.
 *
 * Native responses sometimes serialize errors as plain objects. This helper
 * rehydrates them into the closest matching runtime error type while
 * preserving extra fields like `code`, `url`, and backend metadata.
 * @param {Error|{ type?: string, code?: string|number, message?: string, [key: string]: any }|null|undefined} error
 * @param {Function=} [caller]
 * @returns {Error|null}
 */
export function maybeMakeError(error: Error | {
    type?: string;
    code?: string | number;
    message?: string;
    [key: string]: any;
} | null | undefined, caller?: Function | undefined): Error | null;
/**
 * Parses `seq` as integer value
 * @param {string|number} seq
 * @param {object=} [options]
 * @param {boolean} [options.bigint = false]
 * @ignore
 */
export function parseSeq(seq: string | number, options?: object | undefined): number | bigint;
/**
 * If `debug.enabled === true`, then debug output will be printed to console.
 * @param {boolean} [enable]
 * @return {boolean}
 * @ignore
 */
export function debug(enable?: boolean): boolean;
export namespace debug {
    let enabled: boolean;
    function log(...args: any[]): any;
}
/**
 * Find transfers for an in-worker global `postMessage` that is proxied to the main thread.
 * @param {any[]} transfers
 * @param {any} object
 * @return {any[]}
 * @ignore
 */
export function findMessageTransfers(transfers: any[], object: any): any[];
/**
 * Low-level postMessage used by the runtime to communicate with native bridges.
 * @param {any} message
 * @param {...any} args
 * @return {any}
 * @ignore
 */
export function postMessage(message: any, ...args: any[]): any;
/**
 * Waits for the native IPC layer to be ready and exposed on the
 * global window object.
 * @return {Promise<void>}
 * @ignore
 */
export function ready(): Promise<void>;
/**
 * Sends a synchronous IPC command over XHR returning a `Result`
 * upon success or error.
 * @param {string} command
 * @param {any?} [value]
 * @param {(object|null|Buffer|Uint8Array|ArrayBuffer|string|Array)=} [options]
 * @param {(Buffer|Uint8Array|ArrayBuffer|string|Array|object)=} [buffer]
 * @return {Result}
 *
 * Back-compat overload:
 * - When `buffer` is omitted and `options` is a buffer-like value (including a
 *   string or array), it is treated as the request body.
 * @ignore
 */
export function sendSync(command: string, value?: any | null, options?: (object | null | Buffer | Uint8Array | ArrayBuffer | string | any[]) | undefined, buffer?: (Buffer | Uint8Array | ArrayBuffer | string | any[] | object) | undefined): Result;
/**
 * Emit event to be dispatched on `window` object.
 * @param {string} name
 * @param {any} value
 * @param {EventTarget=} [target = window]
 * @param {Object=} options
 */
export function emit(name: string, value: any, target?: EventTarget | undefined, options?: any | undefined): Promise<void>;
/**
 * Resolves a request by `seq` with possible value.
 * @param {string} seq
 * @param {any} value
 * @ignore
 */
export function resolve(seq: string, value: any): Promise<void>;
/**
 * Sends an async IPC command request with parameters.
 * @param {string} command
 * @param {any=} value
 * @param {{ cache?: boolean, bytes?: (Buffer|Uint8Array|ArrayBuffer|string|Array), useExtensionIPCIfAvailable?: boolean }=} [options]
 * @param {boolean=} [options.cache=false]
 * @param {(Buffer|Uint8Array|ArrayBuffer|string|Array)=} [options.bytes]
 * @return {Promise<Result>}
 */
export function send(command: string, value?: any | undefined, options?: {
    cache?: boolean;
    bytes?: (Buffer | Uint8Array | ArrayBuffer | string | any[]);
    useExtensionIPCIfAvailable?: boolean;
} | undefined): Promise<Result>;
/**
 * Sends an async IPC command request with parameters and buffered bytes.
 * @param {string} command
 * @param {any=} value
 * @param {(Buffer|Uint8Array|ArrayBuffer|string|Array)=} buffer
 * @param {{ timeout?: number, responseType?: string, signal?: AbortSignal, useExtensionIPCIfAvailable?: boolean }=} [options]
 * @return {Promise<Result>}
 */
export function write(command: string, value?: any | undefined, buffer?: (Buffer | Uint8Array | ArrayBuffer | string | any[]) | undefined, options?: {
    timeout?: number;
    responseType?: string;
    signal?: AbortSignal;
    useExtensionIPCIfAvailable?: boolean;
} | undefined): Promise<Result>;
/**
 * Sends an async IPC command request with parameters requesting a response
 * with buffered bytes.
 * @param {string} command
 * @param {any=} value
 * @param {{ timeout?: number, responseType?: string, signal?: AbortSignal, cache?: boolean, useExtensionIPCIfAvailable?: boolean }=} [options]
 * @return {Promise<Result>}
 */
export function request(command: string, value?: any | undefined, options?: {
    timeout?: number;
    responseType?: string;
    signal?: AbortSignal;
    cache?: boolean;
    useExtensionIPCIfAvailable?: boolean;
} | undefined): Promise<Result>;
/**
 * Factory for creating a proxy-based IPC API.
 *
 * Usage:
 *   const api = createBinding('fs')
 *   await api.stat('/foo')
 *   // calls send('fs.stat', '/foo')
 *
 * You can also pass a context object or function to adjust dispatch behavior.
 * If a property path (chain) resolves to an object with a `method` field,
 * that method name is used in the dispatcher (e.g., 'send', 'request', 'write').
 * Otherwise, `ctx.default` or 'send' is used.
 *
 * @param {string} [domain] - Optional root domain for the binding (e.g., 'fs').
 * @param {(function|object)=} [ctx] - Optional context. If a function, used as target of the proxy; otherwise merged into a function.
 * @param {string=} [ctx.default] - Default dispatcher method when none is specified in chain.
 * @return {Proxy}
 * @ignore
 */
export function createBinding(domain?: string, ctx?: (Function | object) | undefined): ProxyConstructor;
/**
 * Rehydrates structured-clone friendly IPC payloads back into richer runtime
 * objects.
 *
 * This helper recursively reconstructs encoded `Buffer` instances,
 * `IPCMessagePort` handles, and custom tagged values supplied in `types`.
 * @param {any} object
 * @param {Map<string, Function>=} [types]
 * @returns {any}
 */
export function inflateIPCMessageTransfers(object: any, types?: Map<string, Function> | undefined): any;
/**
 * @param {Set<any>} transfers
 * @param {any} object
 * @return {any}
 */
export function findIPCMessageTransfers(transfers: Set<any>, object: any): any;
/**
 * Represents an OK IPC status.
 * @ignore
 */
export const OK: 0;
/**
 * Represents an ERROR IPC status.
 * @ignore
 */
export const ERROR: 1;
/**
 * Timeout in milliseconds for IPC requests.
 * @ignore
 */
export const TIMEOUT: number;
/**
 * Symbol for the `ipc.debug.enabled` property
 * @ignore
 */
export const kDebugEnabled: unique symbol;
/**
 * @ignore
 */
export class Headers extends globalThis.Headers {
    /**
     * Create a Headers from various inputs (Headers, entries, response object, raw string).
     * @param {Headers|Array|Object|string} input
     * @return {Headers}
     * @ignore
     */
    static from(input: Headers | any[] | any | string): Headers;
    /**
     * @ignore
     */
    get length(): number;
    /**
     * @ignore
     */
    toJSON(): {
        [k: string]: string;
    };
}
declare const Message_base: any;
/**
 * A container for a IPC message based on a `ipc://` URI scheme.
 * @ignore
 */
export class Message extends Message_base {
    [x: string]: any;
    /**
     * The expected protocol for an IPC message.
     * @ignore
     */
    static get PROTOCOL(): string;
    /**
     * Creates a `Message` instance from a variety of input.
     * @param {string|URL|Message|Buffer|object} input
     * @param {(object|string|URLSearchParams)=} [params]
     * @param {(ArrayBuffer|Uint8Array|string)?} [bytes]
     * @return {Message}
     * @ignore
     */
    static from(input: string | URL | Message | Buffer | object, params?: (object | string | URLSearchParams) | undefined, bytes?: (ArrayBuffer | Uint8Array | string) | null): Message;
    /**
     * Predicate to determine if `input` is valid for constructing
     * a new `Message` instance.
     * @param {string|URL|Message|Buffer|object} input
     * @return {boolean}
     * @ignore
     */
    static isValidInput(input: string | URL | Message | Buffer | object): boolean;
    /**
     * `Message` class constructor.
     * @protected
     * @param {string|URL} input
     * @param {(object|Uint8Array)?} [bytes]
     * @ignore
     */
    protected constructor();
    /**
     *  @type {Uint8Array?}
     *  @ignore
     */
    bytes: Uint8Array | null;
    /**
     * Computed IPC message name.
     * @type {string}
     * @ignore
     */
    get command(): string;
    /**
     * Computed IPC message name.
     * @type {string}
     * @ignore
     */
    get name(): string;
    /**
     * Computed `id` value for the command.
     * @type {string}
     * @ignore
     */
    get id(): string;
    /**
     * Computed `seq` (sequence) value for the command.
     * @type {string}
     * @ignore
     */
    get seq(): string;
    /**
     * Computed message value potentially given in message parameters.
     * This value is automatically decoded, but not treated as JSON.
     * @type {string}
     * @ignore
     */
    get value(): string;
    /**
     * Computed `index` value for the command potentially referring to
     * the window index the command is scoped to or originating from. If not
     * specified in the message parameters, then this value defaults to `-1`.
     * @type {number}
     * @ignore
     */
    get index(): number;
    /**
     * Computed value parsed as JSON. This value is `null` if the value is not present
     * or it is invalid JSON.
     * @type {object?}
     * @ignore
     */
    get json(): object | null;
    /**
     * Computed readonly object of message parameters.
     * @type {object}
     * @ignore
     */
    get params(): object;
    /**
     * Gets unparsed message parameters.
     * @type {Array<Array<string>>}
     * @ignore
     */
    get rawParams(): Array<Array<string>>;
    /**
     * Returns computed parameters as entries
     * @return {Array<Array<any>>}
     * @ignore
     */
    entries(): Array<Array<any>>;
    /**
     * Set a parameter `value` by `key`.
     * @param {string} key
     * @param {any} value
     * @ignore
     */
    set(key: string, value: any): any;
    /**
     * Get a parameter value by `key`.
     * @param {string} key
     * @param {any=} [defaultValue]
     * @return {any}
     * @ignore
     */
    get(key: string, defaultValue?: any | undefined): any;
    /**
     * Delete a parameter by `key`.
     * @param {string} key
     * @return {boolean}
     * @ignore
     */
    delete(key: string): boolean;
    /**
     * Computed parameter keys.
     * @return {Array<string>}
     * @ignore
     */
    keys(): Array<string>;
    /**
     * Computed parameter values.
     * @return {Array<any>}
     * @ignore
     */
    values(): Array<any>;
    /**
     * Predicate to determine if parameter `key` is present in parameters.
     * @param {string} key
     * @return {boolean}
     * @ignore
     */
    has(key: string): boolean;
}
/**
 * A result type used internally for handling
 * IPC result values from the native layer that are in the form
 * of `{ err?, data? }`. The `data` and `err` properties on this
 * type of object are in tuple form and be accessed at `[data?,err?]`
 * @ignore
 */
export class Result {
    /**
     * Creates a `Result` instance from input that may be an object
     * like `{ err?, data? }`, an `Error` instance, or just `data`.
     * @param {(object|Error|any)?} result
     * @param {Error|object} [maybeError]
     * @param {string} [maybeSource]
     * @param {object|string|Headers} [maybeHeaders]
     * @return {Result}
     * @ignore
     */
    static from(result: (object | Error | any) | null, maybeError?: Error | object, maybeSource?: string, maybeHeaders?: object | string | Headers): Result;
    /**
     * `Result` class constructor.
     * @private
     * @param {string?} [id = null]
     * @param {Error?} [err = null]
     * @param {object?} [data = null]
     * @param {string?} [source = null]
     * @param {(object|string|Headers)?} [headers = null]
     * @ignore
     */
    private constructor();
    /**
     * The unique ID for this result.
     * @type {string}
     * @ignore
     */
    id: string;
    /**
     * An optional error in the result.
     * @type {Error?}
     * @ignore
     */
    err: Error | null;
    /**
     * Result data if given.
     * @type {(string|object|Uint8Array)?}
     * @ignore
     */
    data: (string | object | Uint8Array) | null;
    /**
     * The source of this result.
     * @type {string?}
     * @ignore
     */
    source: string | null;
    /**
     * Result headers, if given.
     * @type {Headers?}
     * @ignore
     */
    headers: Headers | null;
    /**
     * Computed result length.
     * @ignore
     */
    get length(): any;
    /**
     * @ignore
     */
    toJSON(): {
        headers: {
            [k: string]: string;
        };
        source: string;
        data: any;
        err: {
            name: string;
            message: string;
            stack?: string;
            cause?: unknown;
            type: any;
            code: any;
        };
    };
    /**
     * Generator for an `Iterable` interface over this instance.
     * @ignore
     */
    [Symbol.iterator](): Generator<any, void, unknown>;
}
/**
 * A URLSearchParams helper that injects common IPC metadata
 * such as `index`, `seq`, runtime frame/worker information,
 * and optionally a `nonce` and a `value` parameter.
 * @ignore
 */
export class IPCSearchParams extends URLSearchParams {
    /**
     * @param {object|any} params - Either a params object or a bare value which becomes `value`.
     * @param {string|number|null} [nonce=null] - Optional nonce to include.
     */
    constructor(params: object | any, nonce?: string | number | null);
}
/**
 * @ignore
 */
export const primordials: any;
/**
 * A message port abstraction implemented using BroadcastChannel under the hood.
 * This mirrors the MessagePort surface where practical and enables structured
 * clone + transfer of ArrayBuffers and nested IPCMessagePorts.
 */
/**
 * Emitted when a message is received by this port.
 * @event IPCMessagePort#message
 * @type {MessageEvent}
 */
/**
 * Emitted when an error occurs while processing a message.
 * @event IPCMessagePort#messageerror
 * @type {ErrorEvent}
 */
export class IPCMessagePort extends MessagePort {
    static ports: Map<any, any>;
    /**
     * Create or retrieve an IPCMessagePort from options.
     * @param {{ id?: string, rx?: string, tx?: string, transferred?: boolean }=} [options]
     * @return {IPCMessagePort}
     */
    static from(options?: {
        id?: string;
        rx?: string;
        tx?: string;
        transferred?: boolean;
    } | undefined): IPCMessagePort;
    /**
     * Mark a port as transferred (used when passing through postMessage).
     * @param {IPCMessagePort} port
     * @return {IPCMessagePort}
     */
    static transfer(port: IPCMessagePort): IPCMessagePort;
    /**
     * Create a new IPCMessagePort instance from options.
     * @param {{ id?: string, rx?: string, tx?: string, transferred?: boolean }=} [options]
     * @return {IPCMessagePort}
     */
    static create(options?: {
        id?: string;
        rx?: string;
        tx?: string;
        transferred?: boolean;
    } | undefined): IPCMessagePort;
    get id(): any;
    get started(): any;
    get closed(): any;
    set onmessage(onmessage: any);
    get onmessage(): any;
    set onmessageerror(onmessageerror: any);
    get onmessageerror(): any;
    /**
     * Post a message to the paired port.
     * @param {any} message
     * @param {{ transfer?: any[] }|any[]=} [optionsOrTransferList]
     * @return {void}
     */
    postMessage(message: any, optionsOrTransferList?: ({
        transfer?: any[];
    } | any[]) | undefined): void;
    addEventListener(...args: any[]): any;
    removeEventListener(...args: any[]): any;
    dispatchEvent(event: any): any;
}
/**
 * A message channel abstraction that pairs two IPCMessagePorts together.
 */
export class IPCMessageChannel extends MessageChannel {
    static "__#private@#connect"(port1: any, port2: any): {
        port1: any;
        port2: any;
    };
    /**
     * @param {{ id?: string, port1?: object, port2?: object }=} [options]
     */
    constructor(options?: {
        id?: string;
        port1?: object;
        port2?: object;
    } | undefined);
    get id(): any;
    get port1(): any;
    get port2(): any;
    #private;
}
/**
 * Emitted when a broadcast message is received.
 * @event IPCBroadcastChannel#message
 * @type {MessageEvent}
 */
/**
 * Emitted when an error occurs while posting or receiving a message.
 * @event IPCBroadcastChannel#messageerror
 * @type {ErrorEvent}
 */
export class IPCBroadcastChannel extends EventTarget {
    static subscriptions: Map<any, any>;
    /**
     * @param {string} name
     * @param {{ origin?: string }=} [options]
     */
    constructor(name: string, options?: {
        origin?: string;
    } | undefined);
    get name(): string;
    get origin(): any;
    get key(): any;
    get token(): string;
    set onmessage(onmessage: (arg0: MessageEvent) => any | null);
    /**
     * @type {function(MessageEvent):any|null}
     */
    get onmessage(): (arg0: MessageEvent) => any | null;
    set onmessageerror(onmessageerror: (arg0: ErrorEvent) => any | null);
    /**
     * @type {function(ErrorEvent):any|null}
     */
    get onmessageerror(): (arg0: ErrorEvent) => any | null;
    set onerror(onerror: (arg0: ErrorEvent) => any | null);
    /**
     * @type {function(ErrorEvent):any|null}
     */
    get onerror(): (arg0: ErrorEvent) => any | null;
    startMessages(): Promise<void>;
    /**
     * @overload
     * @param {'message'} type
     * @param {function(MessageEvent):any} callback
     * @param {{ once?: boolean }=} [options]
     *
     * @overload
     * @param {'messageerror'} type
     * @param {function(ErrorEvent):any} callback
     * @param {{ once?: boolean }=} [options]
     */
    addEventListener(type: "message", callback: (arg0: MessageEvent) => any, options?: {
        once?: boolean;
    } | undefined): any;
    /**
     * @overload
     * @param {'message'} type
     * @param {function(MessageEvent):any} callback
     * @param {{ once?: boolean }=} [options]
     *
     * @overload
     * @param {'messageerror'} type
     * @param {function(ErrorEvent):any} callback
     * @param {{ once?: boolean }=} [options]
     */
    addEventListener(type: "messageerror", callback: (arg0: ErrorEvent) => any, options?: {
        once?: boolean;
    } | undefined): any;
    /**
     * Post a message to subscribers.
     * @param {any} message
     * @param {{ origin?: string, transfer?: any[] }|any[]=} [optionsOrTransferList]
     * @return {Promise<any>}
     */
    postMessage(message: any, optionsOrTransferList?: ({
        origin?: string;
        transfer?: any[];
    } | any[]) | undefined): Promise<any>;
    #private;
}
export default exports;
import { Buffer } from './buffer.js';
import { URL } from './url.js';
import * as exports from './ipc.js';
