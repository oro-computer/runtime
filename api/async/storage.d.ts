/**
 * A container for storing values that remain present during
 * asynchronous operations.
 */
export class AsyncLocalStorage {
    /**
     * Binds function `fn` to run in the execution context of an
     * anonymous `AsyncResource`.
     * @param {function} fn
     * @return {function}
     */
    static bind(fn: Function): Function;
    /**
     * Captures the current async context and returns a function that runs
     * a function in that execution context.
     * @return {function}
     */
    static snapshot(): Function;
    /**
     * @type {boolean}
     */
    get enabled(): boolean;
    /**
     * Disables the `AsyncLocalStorage` instance. When disabled,
     * `getStore()` will always return `undefined`.
     */
    disable(): void;
    /**
     * Enables the `AsyncLocalStorage` instance.
     */
    enable(): void;
    /**
     * Enables and sets the `AsyncLocalStorage` instance default store value.
     * @param {any} store
     */
    enterWith(store: any): void;
    /**
     * Runs function `fn` in the current asynchronous execution context with
     * a given `store` value and arguments given to `fn`.
     * @param {any} store
     * @param {function} fn
     * @param {...any} args
     * @return {any}
     */
    run(store: any, fn: Function, ...args: any[]): any;
    exit(fn: any, ...args: any[]): any;
    /**
     * If the `AsyncLocalStorage` instance is enabled, it returns the current
     * store value for this asynchronous execution context.
     * @return {any|undefined}
     */
    getStore(): any | undefined;
    #private;
}
export default AsyncLocalStorage;
