export default exports;
export const ABORT_ERR: any;
export const ENCODING_ERR: any;
export const INVALID_ACCESS_ERR: any;
export const INDEX_SIZE_ERR: any;
export const NETWORK_ERR: any;
export const NOT_ALLOWED_ERR: any;
export const NOT_FOUND_ERR: any;
export const NOT_SUPPORTED_ERR: any;
export const OPERATION_ERR: any;
export const SECURITY_ERR: any;
export const TIMEOUT_ERR: any;
/**
 * An `AbortError` is an error type thrown in an `onabort()` level 0
 * event handler on an `AbortSignal` instance.
 */
export class AbortError extends Error {
    /**
     * The code given to an `ABORT_ERR` `DOMException`
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/DOMException}
     */
    static get code(): any;
    /**
     * `AbortError` class constructor.
     * @param {AbortSignal|string} reasonOrSignal
     * @param {AbortSignal=} [signal]
     */
    constructor(reason: any, signal?: AbortSignal | undefined, ...args: any[]);
    signal: AbortSignal;
    get name(): string;
    get code(): string;
}
/**
 * An `BadRequestError` is an error type thrown in an `onabort()` level 0
 * event handler on an `BadRequestSignal` instance.
 */
export class BadRequestError extends Error {
    /**
     * The default code given to a `BadRequestError`
     */
    static get code(): number;
    /**
     * `BadRequestError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `EncodingError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class EncodingError extends Error {
    /**
     * The code given to an `ENCODING_ERR` `DOMException`.
     */
    static get code(): any;
    /**
     * `EncodingError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An error type derived from an `errno` code.
 */
export class ErrnoError extends Error {
    static get code(): string;
    static errno: any;
    /**
     * `ErrnoError` class constructor.
     * @param {import('./errno').errno|string} code
     */
    constructor(code: import("./errno").errno | string, message?: any, ...args: any[]);
    get name(): string;
    get code(): number;
    #private;
}
/**
 * An `FinalizationRegistryCallbackError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class FinalizationRegistryCallbackError extends Error {
    /**
     * The default code given to an `FinalizationRegistryCallbackError`
     */
    static get code(): number;
    /**
     * `FinalizationRegistryCallbackError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `IllegalConstructorError` is an error type thrown when a constructor is
 * called for a class constructor when it shouldn't be.
 */
export class IllegalConstructorError extends TypeError {
    /**
     * The default code given to an `IllegalConstructorError`
     */
    static get code(): number;
    /**
     * `IllegalConstructorError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `IndexSizeError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class IndexSizeError extends Error {
    /**
     * The code given to an `INDEX_SIZE_ERR` `DOMException`
     */
    static get code(): any;
    /**
     * `IndexSizeError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
export const kInternalErrorCode: unique symbol;
/**
 * An `InternalError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class InternalError extends Error {
    /**
     * The default code given to an `InternalError`
     */
    static get code(): number;
    /**
     * `InternalError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, code?: number, ...args: any[]);
    get name(): string;
    /**
     * @param {number|string}
     */
    set code(code: number | string);
    /**
     * @type {number|string}
     */
    get code(): number | string;
    [exports.kInternalErrorCode]: number;
}
/**
 * An `InvalidAccessError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class InvalidAccessError extends Error {
    /**
     * The code given to an `INVALID_ACCESS_ERR` `DOMException`
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/DOMException}
     */
    static get code(): any;
    /**
     * `InvalidAccessError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `NetworkError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class NetworkError extends Error {
    /**
     * The code given to an `NETWORK_ERR` `DOMException`
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/DOMException}
     */
    static get code(): any;
    /**
     * `NetworkError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `NotAllowedError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class NotAllowedError extends Error {
    /**
     * The code given to an `NOT_ALLOWED_ERR` `DOMException`
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/DOMException}
     */
    static get code(): any;
    /**
     * `NotAllowedError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `NotFoundError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class NotFoundError extends Error {
    /**
     * The code given to an `NOT_FOUND_ERR` `DOMException`
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/DOMException}
     */
    static get code(): any;
    /**
     * `NotFoundError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `NotSupportedError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class NotSupportedError extends Error {
    /**
     * The code given to an `NOT_SUPPORTED_ERR` `DOMException`
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/DOMException}
     */
    static get code(): any;
    /**
     * `NotSupportedError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `ModuleNotFoundError` is an error type thrown when an imported or
 * required module is not found.
 */
export class ModuleNotFoundError extends exports.NotFoundError {
    /**
     * `ModuleNotFoundError` class constructor.
     * @param {string} message
     * @param {string[]=} [requireStack]
     */
    constructor(message: string, requireStack?: string[] | undefined);
    requireStack: string[];
}
/**
 * An `OperationError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class OperationError extends Error {
    /**
     * The code given to an `OPERATION_ERR` `DOMException`
     */
    static get code(): any;
    /**
     * `OperationError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `SecurityError` is an error type thrown when an internal exception
 * has occurred, such as in the native IPC layer.
 */
export class SecurityError extends Error {
    /**
     * The code given to an `SECURITY_ERR` `DOMException`
     */
    static get code(): any;
    /**
     * `SecurityError` class constructor.
     * @param {string} message
     * @param {number} [code]
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
/**
 * An `TimeoutError` is an error type thrown when an operation timesout.
 */
export class TimeoutError extends Error {
    /**
     * The code given to an `TIMEOUT_ERR` `DOMException`
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/DOMException}
     */
    static get code(): any;
    /**
     * `TimeoutError` class constructor.
     * @param {string} message
     */
    constructor(message: string, ...args: any[]);
    get name(): string;
    get code(): string;
}
import * as exports from './errors.js';
