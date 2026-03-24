/**
 * @typedef {{
 *   extensions?: string[] | Set<string>
 *   origin?: URL | string,
 *   statuses?: Cache
 *   cache?: { response?: Cache, status?: Cache },
 *   headers?: Headers | Map | object | string[][]
 * }} LoaderOptions
 */
/**
 * @typedef {{
 *   loader?: Loader,
 *   origin?: URL | string
 * }} RequestOptions
 */
/**
 * @typedef {{
 *   headers?: Headers | object | array[],
 *   status?: number
 * }} RequestStatusOptions
 */
/**
 * @typedef {{
 *   headers?: Headers | object
 * }} RequestLoadOptions
 */
/**
 * @typedef {{
 *   request?: Request,
 *   headers?: Headers,
 *   status?: number,
 *   buffer?: ArrayBuffer,
 *   text?: string
 * }} ResponseOptions
 */
/**
 * A container for the status of a CommonJS resource. A `RequestStatus` object
 * represents meta data for a `Request` that comes from a preflight
 * HTTP HEAD request.
 */
export class RequestStatus {
    [x: number]: () => {
        __type__: "RequestStatus";
        id: string;
        origin: string | null;
        status: number;
        headers: Array<string[]>;
        request: object | null;
    };
    /**
     * Creates a `RequestStatus` from JSON input.
     * @param {object} json
     * @return {RequestStatus}
     */
    static from(json: object, options: any): RequestStatus;
    /**
     * `RequestStatus` class constructor.
     * @param {Request} request
     * @param {RequestStatusOptions} [options]
     */
    constructor(request: Request, options?: RequestStatusOptions);
    set request(request: Request);
    /**
     * The `Request` object associated with this `RequestStatus` object.
     * @type {Request}
     */
    get request(): Request;
    /**
     * The unique ID of this `RequestStatus`, which is the absolute URL as a string.
     * @type {string}
     */
    get id(): string;
    /**
     * The origin for this `RequestStatus` object.
     * @type {string}
     */
    get origin(): string;
    /**
     * A HTTP status code for this `RequestStatus` object.
     * @type {number|undefined}
     */
    get status(): number | undefined;
    /**
     * An alias for `status`.
     * @type {number|undefined}
     */
    get value(): number | undefined;
    /**
     * @ignore
     */
    get valueOf(): number;
    /**
     * The HTTP headers for this `RequestStatus` object.
     * @type {Headers}
     */
    get headers(): Headers;
    /**
     * The resource location for this `RequestStatus` object. This value is
     * determined from the 'Content-Location' header, if available, otherwise
     * it is derived from the request URL pathname (including the query string).
     * @type {string}
     */
    get location(): string;
    /**
     * `true` if the response status is considered OK, otherwise `false`.
     * @type {boolean}
     */
    get ok(): boolean;
    /**
     * Loads the internal state for this `RequestStatus` object.
     * @param {RequestLoadOptions|boolean} [options]
     * @return {RequestStatus}
     */
    load(options?: RequestLoadOptions | boolean): RequestStatus;
    /**
     * Converts this `RequestStatus` to JSON.
     * @ignore
     * @return {{
     *   id: string,
     *   origin: string | null,
     *   status: number,
     *   headers: Array<string[]>
     *   request: object | null | undefined
     * }}
     */
    toJSON(includeRequest?: boolean): {
        id: string;
        origin: string | null;
        status: number;
        headers: Array<string[]>;
        request: object | null | undefined;
    };
    #private;
}
/**
 * A container for a synchronous CommonJS request to local resource or
 * over the network.
 */
export class Request {
    [x: number]: () => {
        __type__: "Request";
        url: string;
        status: object | undefined;
    };
    /**
     * Creates a `Request` instance from JSON input
     * @param {object} json
     * @param {RequestOptions=} [options]
     * @return {Request}
     */
    static from(json: object, options?: RequestOptions | undefined): Request;
    /**
     * `Request` class constructor.
     * @param {URL|string} url
     * @param {URL|string=} [origin]
     * @param {RequestOptions=} [options]
     */
    constructor(url: URL | string, origin?: (URL | string) | undefined, options?: RequestOptions | undefined);
    /**
     * The unique ID of this `Request`, which is the absolute URL as a string.
     * @type {string}
     */
    get id(): string;
    /**
     * The absolute `URL` of this `Request` object.
     * @type {URL}
     */
    get url(): URL;
    /**
     * The origin for this `Request`.
     * @type {string}
     */
    get origin(): string;
    /**
     * The `Loader` for this `Request` object.
     * @type {Loader?}
     */
    get loader(): Loader | null;
    /**
     * The `RequestStatus` for this `Request`
     * @type {RequestStatus}
     */
    get status(): RequestStatus;
    /**
     * Loads the CommonJS source file, optionally checking the `Loader` cache
     * first, unless ignored when `options.cache` is `false`.
     * @param {RequestLoadOptions=} [options]
     * @return {Response}
     */
    load(options?: RequestLoadOptions | undefined): Response;
    /**
     * Converts this `Request` to JSON.
     * @ignore
     * @return {{
     *   url: string,
     *   status: object | undefined
     * }}
     */
    toJSON(includeStatus?: boolean): {
        url: string;
        status: object | undefined;
    };
    #private;
}
/**
 * A container for a synchronous CommonJS request response for a local resource
 * or over the network.
 */
export class Response {
    [x: number]: () => {
        __type__: "Response";
        id: string;
        text: string;
        status: number;
        buffer: number[] | null;
        headers: Array<string[]>;
    };
    /**
     * Creates a `Response` from JSON input
     * @param {obejct} json
     * @param {ResponseOptions=} [options]
     * @return {Response}
     */
    static from(json: obejct, options?: ResponseOptions | undefined): Response;
    /**
     * `Response` class constructor.
     * @param {Request|ResponseOptions} request
     * @param {ResponseOptions=} [options]
     */
    constructor(request: Request | ResponseOptions, options?: ResponseOptions | undefined);
    /**
     * The unique ID of this `Response`, which is the absolute
     * URL of the request as a string.
     * @type {string}
     */
    get id(): string;
    /**
     * The `Request` object associated with this `Response` object.
     * @type {Request}
     */
    get request(): Request;
    /**
     * The response headers from the associated request.
     * @type {Headers}
     */
    get headers(): Headers;
    /**
     * The `Loader` associated with this `Response` object.
     * @type {Loader?}
     */
    get loader(): Loader | null;
    /**
     * The `Response` status code from the associated `Request` object.
     * @type {number}
     */
    get status(): number;
    /**
     * The `Response` string from the associated `Request`
     * @type {string}
     */
    get text(): string;
    /**
     * The `Response` array buffer from the associated `Request`
     * @type {ArrayBuffer?}
     */
    get buffer(): ArrayBuffer | null;
    /**
     * `true` if the response is considered OK, otherwise `false`.
     * @type {boolean}
     */
    get ok(): boolean;
    /**
     * Converts this `Response` to JSON.
     * @ignore
     * @return {{
     *   id: string,
     *   text: string,
     *   status: number,
     *   buffer: number[] | null,
     *   headers: Array<string[]>
     * }}
     */
    toJSON(): {
        id: string;
        text: string;
        status: number;
        buffer: number[] | null;
        headers: Array<string[]>;
    };
    #private;
}
/**
 * A container for loading CommonJS module sources
 */
export class Loader {
    /**
     * A request class used by `Loader` objects.
     * @type {typeof Request}
     */
    static Request: typeof Request;
    /**
     * A response class used by `Loader` objects.
     * @type {typeof Request}
     */
    static Response: typeof Request;
    /**
     * Resolves a given module URL to an absolute URL with an optional `origin`.
     * @param {URL|string} url
     * @param {URL|string} [origin]
     * @return {string}
     */
    static resolve(url: URL | string, origin?: URL | string): string;
    /**
     * Default extensions for a loader.
     * @type {Set<string>}
     */
    static defaultExtensions: Set<string>;
    /**
     * `Loader` class constructor.
     * @param {string|URL|LoaderOptions} origin
     * @param {LoaderOptions=} [options]
     */
    constructor(origin: string | URL | LoaderOptions, options?: LoaderOptions | undefined);
    /**
     * The internal caches for this `Loader` object.
     * @type {{ response: Cache, status: Cache }}
     */
    get cache(): {
        response: Cache;
        status: Cache;
    };
    /**
     * Headers used in too loader requests.
     * @type {Headers}
     */
    get headers(): Headers;
    /**
     * A set of supported `Loader` extensions.
     * @type {Set<string>}
     */
    get extensions(): Set<string>;
    set origin(origin: string);
    /**
     * The origin of this `Loader` object.
     * @type {string}
     */
    get origin(): string;
    /**
     * Loads a CommonJS module source file at `url` with an optional `origin`, which
     * defaults to the application origin.
     * @param {URL|string} url
     * @param {URL|string|object} [origin]
     * @param {RequestOptions=} [options]
     * @return {Response}
     */
    load(url: URL | string, origin?: URL | string | object, options?: RequestOptions | undefined): Response;
    /**
     * Queries the status of a CommonJS module source file at `url` with an
     * optional `origin`, which defaults to the application origin.
     * @param {URL|string} url
     * @param {URL|string|object} [origin]
     * @param {RequestOptions=} [options]
     * @return {RequestStatus}
     */
    status(url: URL | string, origin?: URL | string | object, options?: RequestOptions | undefined): RequestStatus;
    /**
     * Resolves a given module URL to an absolute URL based on the loader origin.
     * @param {URL|string} url
     * @param {URL|string} [origin]
     * @return {string}
     */
    resolve(url: URL | string, origin?: URL | string): string;
    #private;
}
export default Loader;
export type LoaderOptions = {
    extensions?: string[] | Set<string>;
    origin?: URL | string;
    statuses?: Cache;
    cache?: {
        response?: Cache;
        status?: Cache;
    };
    headers?: Headers | Map<any, any> | object | string[][];
};
export type RequestOptions = {
    loader?: Loader;
    origin?: URL | string;
};
export type RequestStatusOptions = {
    headers?: Headers | object | any[][];
    status?: number;
};
export type RequestLoadOptions = {
    headers?: Headers | object;
};
export type ResponseOptions = {
    request?: Request;
    headers?: Headers;
    status?: number;
    buffer?: ArrayBuffer;
    text?: string;
};
import { Headers } from '../ipc.js';
import URL from '../url.js';
import { Cache } from './cache.js';
