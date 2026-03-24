/**
 * Makes a HTTPS request, optionally a `oro://` for relative paths when
 * `oro:` is the origin protocol.
 * @param {string|object} optionsOrURL
 * @param {(object|function)=} [options]
 * @param {function=} [callback]
 * @return {ClientRequest}
 */
export function request(optionsOrURL: string | object, options?: (object | Function) | undefined, callback?: Function | undefined): ClientRequest;
/**
 * Makes a HTTPS or `oro:` GET request. A simplified alias to `request()`.
 * @param {string|object} optionsOrURL
 * @param {(object|function)=} [options]
 * @param {function=} [callback]
 * @return {ClientRequest}
 */
export function get(optionsOrURL: string | object, options?: (object | Function) | undefined, callback?: Function | undefined): ClientRequest;
/**
 * Creates a HTTPS server that can listen for incoming requests.
 * Requests that are dispatched to this server depend on the context
 * in which it is created, such as a service worker which will use a
 * "fetch event" adapter.
 * @param {object|function=} [options]
 * @param {function=} [callback]
 * @return {Server}
 */
export function createServer(...args: any[]): Server;
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
 * All known possible HTTP methods.
 * @type {string[]}
 */
export const METHODS: string[];
/**
 * A mapping of status codes to status texts
 * @type {Record<number, string>}
 */
export const STATUS_CODES: Record<number, string>;
/**
 * An options object container for an `Agent` instance.
 */
export class AgentOptions extends http.AgentOptions {
}
/**
 * An Agent is responsible for managing connection persistence
 * and reuse for HTTPS clients.
 * @see {@link https://nodejs.org/api/https.html#class-httpsagent}
 */
export class Agent extends http.Agent {
}
/**
 * An object that is created internally and returned from `request()`.
 * @see {@link https://nodejs.org/api/http.html#class-httpclientrequest}
 */
export class ClientRequest extends http.ClientRequest {
}
/**
 * The parent class of `ClientRequest` and `ServerResponse`.
 * It is an abstract outgoing message from the perspective of the
 * participants of an HTTP transaction.
 * @see {@link https://nodejs.org/api/http.html#class-httpoutgoingmessage}
 */
export class OutgoingMessage extends http.OutgoingMessage {
}
/**
 * An `IncomingMessage` object is created by `Server` or `ClientRequest` and
 * passed as the first argument to the 'request' and 'response' event
 * respectively.
 * It may be used to access response status, headers, and data.
 * @see {@link https://nodejs.org/api/http.html#class-httpincomingmessage}
 */
export class IncomingMessage extends http.IncomingMessage {
}
/**
 * An object that is created internally by a `Server` instance, not by the user.
 * It is passed as the second parameter to the 'request' event.
 * @see {@link https://nodejs.org/api/http.html#class-httpserverresponse}
 */
export class ServerResponse extends http.ServerResponse {
}
/**
 * A duplex stream between a HTTP request `IncomingMessage` and the
 * response `ServerResponse`
 */
export class Connection extends http.Connection {
}
/**
 * A nodejs compat HTTP server typically intended for running in a "worker"
 * environment.
 * @see {@link https://nodejs.org/api/http.html#class-httpserver}
 */
export class Server extends http.Server {
}
/**
 * The global and default HTTPS agent.
 * @type {Agent}
 */
export const globalAgent: Agent;
export default exports;
import http from './http.js';
import * as exports from './http.js';
