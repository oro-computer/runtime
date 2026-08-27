/**
 * Adds callback to the 'nextTick' queue.
 * @param {Function} callback
 */
export function nextTick(callback: Function, ...args: any[]): void;
/**
 * Computed high resolution time as a `BigInt`.
 * @param {Array<number>?} [time]
 * @return {bigint}
 */
export function hrtime(time?: Array<number> | null): bigint;
export namespace hrtime {
    function bigint(): any;
}
/**
 * @param {number=} [code=0] - The exit code. Default: 0.
 */
export function exit(code?: number | undefined): Promise<void>;
/**
 * Returns an object describing the memory usage of the Node.js process measured in bytes.
 * @returns {Object}
 */
export function memoryUsage(): any;
export namespace memoryUsage {
    function rss(): any;
}
/**
 * @typedef {Object} ProcessVersionsMap
 * @property {string} oro - Current Oro Runtime semantic version.
 * @property {string} [uv]
 * @property {string} [llama]
 * @property {string} [whisper]
 * @property {string} [iroh]
 * @property {string} [sqlite]
 * @property {string} [libusb]
 * @property {string} [libsodium]
 * @property {string} [mbedtls]
 * @property {string} [cpp_httplib]
 * @property {string} [nlohmann_json]
 */
/**
 * Value stored on the observable process environment proxy.
 * @typedef {unknown} ProcessEnvironmentValue
 */
/**
 * Shape of environment change notifications dispatched by `env`.
 * @typedef {{ key: string|symbol, value: ProcessEnvironmentValue }} ProcessEnvironmentMutation
 */
/**
 * Proxy object returned by `process.env`.
 * @typedef {Record<string, ProcessEnvironmentValue>} ProcessEnvironmentProxy
 */
/**
 * Exported `env` binding, combining the event target with the proxy exposed as
 * `process.env`.
 * @typedef {ProcessEnvironment & { readonly proxy: ProcessEnvironmentProxy }} ProcessEnvironmentBinding
 */
/**
 * Event emitted by `env` when an environment variable is set, deleted, or
 * otherwise changed.
 */
export class ProcessEnvironmentEvent extends Event {
    /**
     * @param {string} type
     * @param {string|symbol} key
     * @param {ProcessEnvironmentValue=} [value]
     */
    constructor(type: string, key: string | symbol, value?: ProcessEnvironmentValue | undefined);
    /** @type {string|symbol} */
    key: string | symbol;
    /** @type {ProcessEnvironmentValue} */
    value: ProcessEnvironmentValue;
}
/**
 * Observable environment store backing `process.env`.
 *
 * Listen on this object to receive `set`, `delete`, and `change` events while
 * reading environment values through `process.env` or `env.proxy`.
 */
export class ProcessEnvironment extends EventTarget {
    get [Symbol.toStringTag](): string;
}
/**
 * Emitted when an environment variable is set.
 * @event ProcessEnvironment#set
 * @type {ProcessEnvironmentMutation}
 */
/**
 * Emitted when an environment variable is deleted.
 * @event ProcessEnvironment#delete
 * @type {ProcessEnvironmentMutation}
 */
/**
 * Emitted when an environment variable is changed (set or delete).
 * @event ProcessEnvironment#change
 * @type {ProcessEnvironmentMutation}
 */
/**
 * Observable process-environment state.
 *
 * `process.env` returns `env.proxy`, which behaves like a mutable object of
 * string keys to string values. The exported `env` object itself is the
 * `EventTarget` you can subscribe to for environment change events.
 */
export const env: ProcessEnvironmentBinding;
export default process;
export type ProcessVersionsMap = {
    /**
     * - Current Oro Runtime semantic version.
     */
    oro: string;
    uv?: string;
    llama?: string;
    whisper?: string;
    iroh?: string;
    sqlite?: string;
    libusb?: string;
    libsodium?: string;
    mbedtls?: string;
    cpp_httplib?: string;
    nlohmann_json?: string;
};
/**
 * Value stored on the observable process environment proxy.
 */
export type ProcessEnvironmentValue = unknown;
/**
 * Shape of environment change notifications dispatched by `env`.
 */
export type ProcessEnvironmentMutation = {
    key: string | symbol;
    value: ProcessEnvironmentValue;
};
/**
 * Proxy object returned by `process.env`.
 */
export type ProcessEnvironmentProxy = Record<string, ProcessEnvironmentValue>;
/**
 * Exported `env` binding, combining the event target with the proxy exposed as
 * `process.env`.
 */
export type ProcessEnvironmentBinding = ProcessEnvironment & {
    readonly proxy: ProcessEnvironmentProxy;
};
declare const process: any;
