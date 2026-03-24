/**
 * Opens an environment for a particular scope.
 * @param {EnvironmentOptions} options
 * @return {Promise<Environment>}
 */
export function open(options: EnvironmentOptions): Promise<Environment>;
/**
 * Closes an active `Environment` instance, dropping the global
 * instance reference.
 * @return {Promise<boolean>}
 */
export function close(): Promise<boolean>;
/**
 * Resets an active `Environment` instance
 * @return {Promise<boolean>}
 */
export function reset(): Promise<boolean>;
/**
 * @typedef {{
 *   scope: string
 * }} EnvironmentOptions
 */
/**
 * An event dispatched when an environment value is updated (set, delete)
 */
export class EnvironmentEvent extends Event {
    /**
     * `EnvironmentEvent` class constructor.
     * @param {'set'|'delete'} type
     * @param {object=} [entry]
     */
    constructor(type: "set" | "delete", entry?: object | undefined);
    entry: any;
}
/**
 * An environment context object with persistence and durability
 * for service worker environments.
 */
export class Environment extends EventTarget {
    /**
     * Maximum entries that will be restored from storage into the environment
     * context object.
     * @type {number}
     */
    static MAX_CONTEXT_ENTRIES: number;
    /**
     * Opens an environment for a particular scope.
     * @param {EnvironmentOptions} options
     * @return {Environment}
     */
    static open(options: EnvironmentOptions): Environment;
    /**
     * The current `Environment` instance
     * @type {Environment?}
     */
    static instance: Environment | null;
    /**
     * `Environment` class constructor
     * @ignore
     * @param {EnvironmentOptions} options
     */
    constructor(options: EnvironmentOptions);
    /**
     * A reference to the currently opened environment database.
     * @type {import('../internal/database.js').Database}
     */
    get database(): import("../internal/database.js").Database;
    /**
     * A proxied object for reading and writing environment state.
     * Values written to this object must be cloneable with respect to the
     * structured clone algorithm.
     * @see {https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Structured_clone_algorithm}
     * @type {Proxy<object>}
     */
    get context(): ProxyConstructor;
    /**
     * The environment type
     * @type {string}
     */
    get type(): string;
    /**
     * The current environment name. This value is also used as the
     * internal database name.
     * @type {string}
     */
    get name(): string;
    /**
     * Resets the current environment to an empty state.
     */
    reset(): Promise<void>;
    /**
     * Opens the environment.
     * @ignore
     */
    open(): Promise<void>;
    /**
     * Closes the environment database, purging existing state.
     * @ignore
     */
    close(): Promise<void>;
    #private;
}
declare namespace _default {
    export { Environment };
    export { close };
    export { reset };
    export { open };
}
export default _default;
export type EnvironmentOptions = {
    scope: string;
};
