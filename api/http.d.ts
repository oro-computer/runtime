/**
 * Makes a HTTP or `oro:` GET request. A simplified alias to `request()`.
 * @param {string|object} optionsOrURL
 * @param {(object|function)=} [options]
 * @param {function=} [callback]
 * @return {ClientRequest}
 */
export function get(optionsOrURL: string | object, options?: (object | Function) | undefined, callback?: Function | undefined): ClientRequest;
/**
 * Creates a HTTP server that can listen for incoming requests.
 * Requests that are dispatched to this server depend on the context
 * in which it is created, such as a service worker which will use a
 * "fetch event" adapter.
 * @param {object|function=} [options]
 * @param {function=} [callback]
 * @return {Server}
 */
export function createServer(options?: (object | Function) | undefined, callback?: Function | undefined): Server;
/**
 * All known possible HTTP methods.
 * @type {string[]}
 */
export const METHODS: string[];
/**
 * A mapping of status codes to status texts
 * @type {Record<number, string>}
 */
export const STATUS_CODES: Record<number, string>;
export const CONTINUE: 100;
export const SWITCHING_PROTOCOLS: 101;
export const PROCESSING: 102;
export const EARLY_HINTS: 103;
export const OK: 200;
export const CREATED: 201;
export const ACCEPTED: 202;
export const NONAUTHORITATIVE_INFORMATION: 203;
export const NO_CONTENT: 204;
export const RESET_CONTENT: 205;
export const PARTIAL_CONTENT: 206;
export const MULTISTATUS: 207;
export const ALREADY_REPORTED: 208;
export const IM_USED: 226;
export const MULTIPLE_CHOICES: 300;
export const MOVED_PERMANENTLY: 301;
export const FOUND: 302;
export const SEE_OTHER: 303;
export const NOT_MODIFIED: 304;
export const USE_PROXY: 305;
export const TEMPORARY_REDIRECT: 307;
export const PERMANENT_REDIRECT: 308;
export const BAD_REQUEST: 400;
export const UNAUTHORIZED: 401;
export const PAYMENT_REQUIRED: 402;
export const FORBIDDEN: 403;
export const NOT_FOUND: 404;
export const METHOD_NOT_ALLOWED: 405;
export const NOT_ACCEPTABLE: 406;
export const PROXY_AUTHENTICATION_REQUIRED: 407;
export const REQUEST_TIMEOUT: 408;
export const CONFLICT: 409;
export const GONE: 410;
export const LENGTH_REQUIRED: 411;
export const PRECONDITION_FAILED: 412;
export const PAYLOAD_TOO_LARGE: 413;
export const URI_TOO_LONG: 414;
export const UNSUPPORTED_MEDIA_TYPE: 415;
export const RANGE_NOT_SATISFIABLE: 416;
export const EXPECTATION_FAILED: 417;
export const IM_A_TEAPOT: 418;
export const MISDIRECTED_REQUEST: 421;
export const UNPROCESSABLE_ENTITY: 422;
export const LOCKED: 423;
export const FAILED_DEPENDENCY: 424;
export const TOO_EARLY: 425;
export const UPGRADE_REQUIRED: 426;
export const PRECONDITION_REQUIRED: 428;
export const TOO_MANY_REQUESTS: 429;
export const REQUEST_HEADER_FIELDS_TOO_LARGE: 431;
export const UNAVAILABLE_FOR_LEGAL_REASONS: 451;
export const INTERNAL_SERVER_ERROR: 500;
export const NOT_IMPLEMENTED: 501;
export const BAD_GATEWAY: 502;
export const SERVICE_UNAVAILABLE: 503;
export const GATEWAY_TIMEOUT: 504;
export const HTTP_VERSION_NOT_SUPPORTED: 505;
export const VARIANT_ALSO_NEGOTIATES: 506;
export const INSUFFICIENT_STORAGE: 507;
export const LOOP_DETECTED: 508;
export const BANDWIDTH_LIMIT_EXCEEDED: 509;
export const NOT_EXTENDED: 510;
export const NETWORK_AUTHENTICATION_REQUIRED: 511;
/**
 * The parent class of `ClientRequest` and `ServerResponse`.
 * It is an abstract outgoing message from the perspective of the
 * participants of an HTTP transaction.
 * @see {@link https://nodejs.org/api/http.html#class-httpoutgoingmessage}
 */
export class OutgoingMessage extends Writable {
    /**
     * `OutgoingMessage` class constructor.
     * @ignore
     */
    constructor();
    /**
     * `true` if the headers were sent
     * @type {boolean}
     */
    headersSent: boolean;
    /**
     * Internal buffers
     * @ignore
     * @type {Buffer[]}
     */
    get buffers(): Buffer[];
    /**
     * An object of the outgoing message headers.
     * This is equivalent to `getHeaders()`
     * @type {object}
     */
    get headers(): object;
    /**
     * @ignore
     */
    get socket(): this;
    /**
     * `true` if the write state is "ended"
     * @type {boolean}
     */
    get writableEnded(): boolean;
    /**
     * `true` if the write state is "finished"
     * @type {boolean}
     */
    get writableFinished(): boolean;
    /**
     * The number of buffered bytes.
     * @type {number}
     */
    get writableLength(): number;
    /**
     * @ignore
     * @type {boolean}
     */
    get writableObjectMode(): boolean;
    /**
     * @ignore
     */
    get writableCorked(): number;
    /**
     * The `highWaterMark` of the writable stream.
     * @type {number}
     */
    get writableHighWaterMark(): number;
    /**
     * @ignore
     * @return {OutgoingMessage}
     */
    addTrailers(_headers: any): OutgoingMessage;
    /**
     * @ignore
     * @return {OutgoingMessage}
     */
    cork(): OutgoingMessage;
    /**
     * @ignore
     * @return {OutgoingMessage}
     */
    uncork(): OutgoingMessage;
    /**
     * Destroys the message.
     * Once a socket is associated with the message and is connected,
     * that socket will be destroyed as well.
     * @param {Error?} [err]
     * @return {OutgoingMessage}
     */
    destroy(err?: Error | null): OutgoingMessage;
    /**
     * Finishes the outgoing message.
     * @param {(Buffer|Uint8Array|string|function)=} [chunk]
     * @param {(string|function)=} [encoding]
     * @param {function=} [callback]
     * @return {OutgoingMessage}
     */
    end(chunk?: (Buffer | Uint8Array | string | Function) | undefined, encoding?: (string | Function) | undefined, callback?: Function | undefined): OutgoingMessage;
    /**
     * Append a single header value for the header object.
     * @param {string} name
     * @param {string|string[]} value
     * @return {OutgoingMessage}
     */
    appendHeader(name: string, value: string | string[]): OutgoingMessage;
    /**
     * Set a single header value for the header object, replacing any existing value.
     * @param {string} name
     * @param {string} value
     * @return {OutgoingMessage}
     */
    setHeader(name: string, value: string): OutgoingMessage;
    /**
     * Flushes the message headers.
     */
    flushHeaders(): void;
    /**
     * Gets the value of the HTTP header with the given name.
     * If that header is not set, the returned value will be `undefined`.
     * @param {string} name
     * @return {string|undefined}
     */
    getHeader(name: string): string | undefined;
    /**
     * Returns an array containing the unique names of the current outgoing
     * headers. All names are lowercase.
     * @return {string[]}
     */
    getHeaderNames(): string[];
    /**
     * @ignore
     */
    getRawHeaderNames(): string[];
    /**
     * Returns a copy of the HTTP headers as an object.
     * @return {object}
     */
    getHeaders(): object;
    /**
     * Returns true if the header identified by name is currently set in the
     * outgoing headers. The header name is case-insensitive.
     * @param {string} name
     * @return {boolean}
     */
    hasHeader(name: string): boolean;
    /**
     * Removes a header that is queued for implicit sending.
     * @param {string} name
     */
    removeHeader(name: string): void;
    /**
     * Sets the outgoing message timeout with an optional callback.
     * @param {number} timeout
     * @param {function=} [callback]
     * @return {OutgoingMessage}
     */
    setTimeout(timeout: number, callback?: Function | undefined): OutgoingMessage;
    /**
     * @ignore
     */
    _implicitHeader(): void;
    #private;
}
/**
 * An `IncomingMessage` object is created by `Server` or `ClientRequest` and
 * passed as the first argument to the 'request' and 'response' event
 * respectively.
 * It may be used to access response status, headers, and data.
 * @see {@link https://nodejs.org/api/http.html#class-httpincomingmessage}
 */
export class IncomingMessage extends Readable {
    /**
     * `IncomingMessage` class constructor.
     * @ignore
     * @param {object} options
     */
    constructor(options: object);
    set url(url: string);
    /**
     * The URL for this incoming message. This value is not absolute with
     * respect to the protocol and hostname. It includes the path and search
     * query component parameters.
     * @type {string}
     */
    get url(): string;
    /**
     * @type {Server}
     */
    get server(): Server;
    /**
     * @type {AsyncContext.Variable}
     */
    get context(): typeof import("./async/context.js").Variable;
    /**
     * This property will be `true` if a complete HTTP message has been received
     * and successfully parsed.
     * @type {boolean}
     */
    get complete(): boolean;
    /**
     * An object of the incoming message headers.
     * @type {object}
     */
    get headers(): object;
    /**
     * Similar to `message.headers`, but there is no join logic and the values
     * are always arrays of strings, even for headers received just once.
     * @type {object}
     */
    get headersDistinct(): object;
    /**
     * The HTTP major version of this request.
     * @type {number}
     */
    get httpVersionMajor(): number;
    /**
     * The HTTP minor version of this request.
     * @type {number}
     */
    get httpVersionMinor(): number;
    /**
     * The HTTP version string.
     * A concatenation of `httpVersionMajor` and `httpVersionMinor`.
     * @type {string}
     */
    get httpVersion(): string;
    /**
     * The HTTP request method.
     * @type {string}
     */
    get method(): string;
    /**
     * The raw request/response headers list potentially  as they were received.
     * @type {string[]}
     */
    get rawHeaders(): string[];
    /**
     * @ignore
     */
    get rawTrailers(): any[];
    /**
     * @ignore
     */
    get socket(): this;
    /**
     * The HTTP request status code.
     * Only valid for response obtained from `ClientRequest`.
     * @type {number}
     */
    get statusCode(): number;
    /**
     * The HTTP response status message (reason phrase).
     * Such as "OK" or "Internal Server Error."
     * Only valid for response obtained from `ClientRequest`.
     * @type {string?}
     */
    get statusMessage(): string | null;
    /**
     * An alias for `statusCode`
     * @type {number}
     */
    get status(): number;
    /**
     * An alias for `statusMessage`
     * @type {string?}
     */
    get statusText(): string | null;
    /**
     * @ignore
     */
    get trailers(): {};
    /**
     * @ignore
     */
    get trailersDistinct(): {};
    /**
     * Gets the value of the HTTP header with the given name.
     * If that header is not set, the returned value will be `undefined`.
     * @param {string} name
     * @return {string|undefined}
     */
    getHeader(name: string): string | undefined;
    /**
     * Returns an array containing the unique names of the current outgoing
     * headers. All names are lowercase.
     * @return {string[]}
     */
    getHeaderNames(): string[];
    /**
     * @ignore
     */
    getRawHeaderNames(): string[];
    /**
     * Returns a copy of the HTTP headers as an object.
     * @return {object}
     */
    getHeaders(): object;
    /**
     * Returns true if the header identified by name is currently set in the
     * outgoing headers. The header name is case-insensitive.
     * @param {string} name
     * @return {boolean}
     */
    hasHeader(name: string): boolean;
    /**
     * Sets the incoming message timeout with an optional callback.
     * @param {number} timeout
     * @param {function=} [callback]
     * @return {IncomingMessage}
     */
    setTimeout(timeout: number, callback?: Function | undefined): IncomingMessage;
    #private;
}
/**
 * An object that is created internally and returned from `request()`.
 * @see {@link https://nodejs.org/api/http.html#class-httpclientrequest}
 */
export class ClientRequest extends exports.OutgoingMessage {
    /**
     * `ClientRequest` class constructor.
     * @ignore
     * @param {object} options
     */
    constructor(options: object);
    /**
     * The HTTP request method.
     * @type {string}
     */
    get method(): string;
    /**
     * The request protocol
     * @type {string?}
     */
    get protocol(): string | null;
    /**
     * The request path.
     * @type {string}
     */
    get path(): string;
    /**
     * The request host name (including port).
     * @type {string?}
     */
    get host(): string | null;
    /**
     * The URL for this outgoing message. This value is not absolute with
     * respect to the protocol and hostname. It includes the path and search
     * query component parameters.
     * @type {string}
     */
    get url(): string;
    /**
     * @ignore
     * @type {boolean}
     */
    get finished(): boolean;
    /**
     * @ignore
     * @type {boolean}
     */
    get reusedSocket(): boolean;
    /**
     * @ignore
     * @param {boolean=} [value]
     * @return {ClientRequest}
     */
    setNoDelay(_value?: boolean): ClientRequest;
    /**
     * @ignore
     * @param {boolean=} [enable]
     * @param {number=} [initialDelay]
     * @return {ClientRequest}
     */
    setSocketKeepAlive(_enable?: boolean, _initialDelay?: number): ClientRequest;
    #private;
}
/**
 * An object that is created internally by a `Server` instance, not by the user.
 * It is passed as the second parameter to the 'request' event.
 * @see {@link https://nodejs.org/api/http.html#class-httpserverresponse}
 */
export class ServerResponse extends exports.OutgoingMessage {
    /**
     * `ServerResponse` class constructor.
     * @param {object} options
     */
    constructor(options: object);
    /**
     * @type {Server}
     */
    get server(): Server;
    /**
     * A reference to the original HTTP request object.
     * @type {IncomingMessage}
     */
    get request(): IncomingMessage;
    /**
     * A reference to the original HTTP request object.
     * @type {IncomingMessage}
     */
    get req(): IncomingMessage;
    set statusCode(statusCode: number);
    /**
     * The HTTP request status code.
     * Only valid for response obtained from `ClientRequest`.
     * @type {number}
     */
    get statusCode(): number;
    set statusMessage(statusMessage: string | null);
    /**
     * The HTTP response status message (reason phrase).
     * Such as "OK" or "Internal Server Error."
     * Only valid for response obtained from `ClientRequest`.
     * @type {string?}
     */
    get statusMessage(): string | null;
    set status(status: number);
    /**
     * An alias for `statusCode`
     * @type {number}
     */
    get status(): number;
    set statusText(statusText: string | null);
    /**
     * An alias for `statusMessage`
     * @type {string?}
     */
    get statusText(): string | null;
    set sendDate(value: boolean);
    /**
     * If `true`, the "Date" header will be automatically generated and sent in
     * the response if it is not already present in the headers.
     * Defaults to `true`.
     * @type {boolean}
     */
    get sendDate(): boolean;
    /**
     * @ignore
     */
    writeContinue(): this;
    /**
     * @ignore
     */
    writeEarlyHints(): this;
    /**
     * @ignore
     */
    writeProcessing(): this;
    /**
     * Writes the response header to the request.
     * The `statusCode` is a 3-digit HTTP status code, like 200 or 404.
     * The last argument, `headers`, are the response headers.
     * Optionally one can give a human-readable `statusMessage`
     * as the second argument.
     * @param {number|string} statusCode
     * @param {string|object|string[]} [statusMessage]
     * @param {object|string[]} [headers]
     * @return {ClientRequest}
     */
    writeHead(statusCode: number | string, statusMessage?: string | object | string[], headers?: object | string[]): ClientRequest;
    #private;
}
/**
 * An options object container for an `Agent` instance.
 */
export class AgentOptions {
    /**
     * `AgentOptions` class constructor.
     * @ignore
     * @param {{
     *   keepAlive?: boolean,
     *   timeout?: number
     * }} [options]
     */
    constructor(options?: {
        keepAlive?: boolean;
        timeout?: number;
    });
    keepAlive: boolean;
    timeout: number;
}
/**
 * An Agent is responsible for managing connection persistence
 * and reuse for HTTP clients.
 * @see {@link https://nodejs.org/api/http.html#class-httpagent}
 */
export class Agent extends EventEmitter {
    /**
     * `Agent` class constructor.
     * @param {AgentOptions=} [options]
     */
    constructor(options?: AgentOptions | undefined);
    defaultProtocol: string;
    options: any;
    requests: Set<any>;
    sockets: {};
    maxFreeSockets: number;
    maxTotalSockets: number;
    maxSockets: number;
    /**
     * @ignore
     */
    get freeSockets(): {};
    /**
     * @ignore
     * @param {object} options
     */
    getName(options: object): string;
    /**
     * Produces a socket/stream to be used for HTTP requests.
     * @param {object} options
     * @param {function(Duplex)=} [callback]
     * @return {Duplex}
     */
    createConnection(options: object, callback?: ((arg0: Duplex) => any) | undefined): Duplex;
    /**
     * @ignore
     */
    keepSocketAlive(): void;
    /**
     * @ignore
     */
    reuseSocket(): void;
    /**
     * @ignore
     */
    destroy(): void;
}
/**
 * The global and default HTTP agent.
 * @type {Agent}
 */
export const globalAgent: Agent;
/**
 * A duplex stream between a HTTP request `IncomingMessage` and the
 * response `ServerResponse`
 */
export class Connection extends Duplex {
    /**
     * `Connection` class constructor.
     * @ignore
     * @param {Server} server
     * @param {IncomingMessage} incomingMessage
     * @param {ServerResponse} serverResponse
     */
    constructor(server: Server, incomingMessage: IncomingMessage, serverResponse: ServerResponse);
    server: any;
    active: boolean;
    request: any;
    response: any;
    /**
     * Closes the connection, destroying the underlying duplex, request, and
     * response streams.
     * @return {Connection}
     */
    close(): Connection;
}
/**
 * A nodejs compat HTTP server typically intended for running in a "worker"
 * environment.
 * @see {@link https://nodejs.org/api/http.html#class-httpserver}
 */
export class Server extends EventEmitter {
    [x: number]: () => import("./gc.js").Finalizer;
    requestTimeout: number;
    timeout: number;
    maxRequestsPerSocket: number;
    keepAliveTimeout: number;
    headersTimeout: number;
    /**
     * @ignore
     * @type {AsyncResource}
     */
    get resource(): AsyncResource;
    /**
     * The adapter interface for this `Server` instance.
     * @ignore
     */
    get adapterInterace(): {
        Connection: typeof exports.Connection;
        globalAgent: exports.Agent;
        IncomingMessage: typeof exports.IncomingMessage;
        METHODS: string[];
        ServerResponse: typeof exports.ServerResponse;
        STATUS_CODES: Record<number, string>;
    };
    /**
     * Back-compat alias with correct spelling.
     * @ignore
     */
    get adapterInterface(): {
        Connection: typeof exports.Connection;
        globalAgent: exports.Agent;
        IncomingMessage: typeof exports.IncomingMessage;
        METHODS: string[];
        ServerResponse: typeof exports.ServerResponse;
        STATUS_CODES: Record<number, string>;
    };
    /**
     * `true` if the server is closed, otherwise `false`.
     * @type {boolean}
     */
    get closed(): boolean;
    /**
     * The host to listen to. This value can be `null`.
     * Defaults to `location.hostname`. This value
     * is used to filter requests by hostname.
     * @type {string?}
     */
    get host(): string | null;
    /**
     * The `port` to listen on. This value can be `0`, which is the default.
     * This value is used to filter requests by port, if given. A port value
     * of `0` does not filter on any port.
     * @type {number}
     */
    get port(): number;
    /**
     * A readonly array of all active or inactive (idle) connections.
     * @type {Connection[]}
     */
    get connections(): Connection[];
    /**
     * `true` if the server is listening for requests.
     * @type {boolean}
     */
    get listening(): boolean;
    set maxConnections(value: number);
    /**
     * The number of concurrent max connections this server should handle.
     * Default: Infinity
     * @type {number}
     */
    get maxConnections(): number;
    /**
     * Gets the HTTP server address and port that it this server is
     * listening (emulated) on in the runtime with respect to the
     * adapter internal being used by the server.
     * @return {{ family: string, address: string, port: number}}
     */
    address(): {
        family: string;
        address: string;
        port: number;
    };
    /**
     * Closes the server.
     * @param {function=} [close]
     */
    close(callback?: any): void;
    /**
     * Closes all connections.
     */
    closeAllConnections(): void;
    /**
     * Closes all idle connections.
     */
    closeIdleConnections(): void;
    /**
     * @ignore
     */
    setTimeout(_timeout?: number, _callback?: any): this;
    /**
     * @param {number|object=} [port]
     * @param {string=} [host]
     * @param {function|null} [unused]
     * @param {function=} [callback
     * @return Server
     */
    listen(port?: (number | object) | undefined, host?: string | undefined, unused?: Function | null, callback?: Function | undefined): this;
    #private;
}
export default exports;
import { Writable } from './stream.js';
import { Buffer } from './buffer.js';
import { Readable } from './stream.js';
import { EventEmitter } from './events.js';
import { Duplex } from './stream.js';
import { AsyncResource } from './async/resource.js';
import * as exports from './http.js';
