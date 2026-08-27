/**
 * @module async.context
 *
 * Async Context for JavaScript based on the TC39 proposal.
 *
 * Example usage:
 * ```js
 * // `AsyncContext` is also globally available as `globalThis.AsyncContext`
 * import AsyncContext from 'oro:async/context'
 *
 * const var = new AsyncContext.Variable()
 * var.run('top', () => {
 *   console.log(var.get()) // 'top'
 *   queueMicrotask(() => {
 *     var.run('nested', () => {
 *       console.log(var.get()) // 'nested'
 *     })
 *   })
 * })
 * ```
 *
 * @see {@link https://tc39.es/proposal-async-context}
 * @see {@link https://github.com/tc39/proposal-async-context}
 */
/**
 * @template T
 * @typedef {{
 *   name?: string,
 *   defaultValue?: T
 * }} VariableOptions
 */
/**
 * @callback AnyFunc
 * @template T
 * @this T
 * @param {...any} args
 * @returns {any}
 */
/**
 * `FrozenRevert` holds a frozen Mapping that will be simply restored
 * when the revert is run.
 * @see {@link https://github.com/tc39/proposal-async-context/blob/master/src/fork.ts}
 */
export class FrozenRevert {
    /**
     * `FrozenRevert` class constructor.
     * @param {Mapping} mapping
     */
    constructor(mapping: Mapping);
    /**
     * Restores (unchaged) mapping from this `FrozenRevert`. This function is
     * called by `AsyncContext.Storage` when it reverts a current mapping to the
     * previous state before a "fork".
     * @param {Mapping=} [unused]
     * @return {Mapping}
     */
    restore(unused?: Mapping | undefined): Mapping;
    #private;
}
/**
 * Revert holds the state on how to revert a change to the
 * `AsyncContext.Storage` current `Mapping`
 * @see {@link https://github.com/tc39/proposal-async-context/blob/master/src/fork.ts}
 * @template T
 */
export class Revert<T> {
    /**
     * `Revert` class constructor.
     * @param {Mapping} mapping
     * @param {Variable<T>} key
     */
    constructor(mapping: Mapping, key: Variable<T>);
    /**
     * @type {T|undefined}
     */
    get previousVariable(): T | undefined;
    /**
     * Restores a mapping from this `Revert`. This function is called by
     * `AsyncContext.Storage` when it reverts a current mapping to the
     * previous state before a "fork".
     * @param {Mapping} current
     * @return {Mapping}
     */
    restore(current: Mapping): Mapping;
    #private;
}
/**
 * A container for all `AsyncContext.Variable` instances and snapshot state.
 * @see {@link https://github.com/tc39/proposal-async-context/blob/master/src/mapping.ts}
 */
export class Mapping {
    /**
     * `Mapping` class constructor.
     * @param {Map<Variable<any>, any>} data
     */
    constructor(data: Map<Variable<any>, any>);
    /**
     * Freezes the `Mapping` preventing `AsyncContext.Variable` modifications with
     * `set()` and `delete()`.
     */
    freeze(): void;
    /**
     * Returns `true` if the `Mapping` is frozen, otherwise `false`.
     * @return {boolean}
     */
    isFrozen(): boolean;
    /**
     * Optionally returns a new `Mapping` if the current one is "frozen",
     * otherwise it just returns the current instance.
     * @return {Mapping}
     */
    fork(): Mapping;
    /**
     * Returns `true` if the `Mapping` has a `AsyncContext.Variable` at `key`,
     * otherwise `false.
     * @template T
     * @param {Variable<T>} key
     * @return {boolean}
     */
    has<T>(key: Variable<T>): boolean;
    /**
     * Gets an `AsyncContext.Variable` value at `key`. If not set, this function
     * returns `undefined`.
     * @template T
     * @param {Variable<T>} key
     * @return {boolean}
     */
    get<T>(key: Variable<T>): boolean;
    /**
     * Sets an `AsyncContext.Variable` value at `key`. If the `Mapping` is frozen,
     * then a "forked" (new) instance with the value set on it is returned,
     * otherwise the current instance.
     * @template T
     * @param {Variable<T>} key
     * @param {T} value
     * @return {Mapping}
     */
    set<T>(key: Variable<T>, value: T): Mapping;
    /**
     * Delete  an `AsyncContext.Variable` value at `key`.
     * If the `Mapping` is frozen, then a "forked" (new) instance is returned,
     * otherwise the current instance.
     * @template T
     * @param {Variable<T>} key
     * @param {T} value
     * @return {Mapping}
     */
    delete<T>(key: Variable<T>): Mapping;
    #private;
}
/**
 * A container of all `AsyncContext.Variable` data.
 * @ignore
 * @see {@link https://github.com/tc39/proposal-async-context/blob/master/src/storage.ts}
 */
export class Storage {
    /**
     * The current `Mapping` for this `AsyncContext`.
     * @type {Mapping}
     */
    static "__#private@#current": Mapping;
    /**
     * Returns `true` if the current `Mapping` has a
     * `AsyncContext.Variable` at `key`,
     * otherwise `false.
     * @template T
     * @param {Variable<T>} key
     * @return {boolean}
     */
    static has<T>(key: Variable<T>): boolean;
    /**
     * Gets an `AsyncContext.Variable` value at `key` for the current `Mapping`.
     * If not set, this function returns `undefined`.
     * @template T
     * @param {Variable<T>} key
     * @return {T|undefined}
     */
    static get<T>(key: Variable<T>): T | undefined;
    /**
     * Set updates the `AsyncContext.Variable` with a new value and returns a
     * revert action that allows the modification to be reversed in the future.
     * @template T
     * @param {Variable<T>} key
     * @param {T} value
     * @return {Revert<T>|FrozenRevert}
     */
    static set<T>(key: Variable<T>, value: T): Revert<T> | FrozenRevert;
    /**
     * "Freezes" the current storage `Mapping`, and returns a new `FrozenRevert`
     * or `Revert` which can restore the storage state to the state at
     * the time of the snapshot.
     * @return {FrozenRevert}
     */
    static snapshot(): FrozenRevert;
    /**
     * Restores the storage `Mapping` state to state at the time the
     * "revert" (`FrozenRevert` or `Revert`) was created.
     * @template T
     * @param {Revert<T>|FrozenRevert} revert
     */
    static restore<T>(revert: Revert<T> | FrozenRevert): void;
    /**
     * Switches storage `Mapping` state to the state at the time of a
     * "snapshot".
     * @param {FrozenRevert} snapshot
     * @return {FrozenRevert}
     */
    static switch(snapshot: FrozenRevert): FrozenRevert;
}
/**
 * `AsyncContext.Variable` is a container for a value that is associated with
 * the current execution flow. The value is propagated through async execution
 * flows, and can be snapshot and restored with Snapshot.
 * @template T
 * @see {@link https://github.com/tc39/proposal-async-context/blob/master/README.md#asynccontextvariable}
 */
export class Variable<T> {
    /**
     * `Variable` class constructor.
     * @param {VariableOptions<T>=} [options]
     */
    constructor(options?: VariableOptions<T> | undefined);
    set defaultValue(defaultValue: T);
    /**
     * @ignore
     */
    get defaultValue(): T;
    /**
     * @ignore
     */
    get revert(): FrozenRevert | Revert<T>;
    /**
     * The name of this async context variable.
     * @type {string}
     */
    get name(): string;
    /**
     * Executes a function `fn` with specified arguments,
     * setting a new value to the current context before the call,
     * and ensuring the environment is reverted back afterwards.
     * The function allows for the modification of a specific context's
     * state in a controlled manner, ensuring that any changes can be undone.
     * @template T
     * @template {AnyFunc} F
     * @param {T} value
     * @param {F} fn
     * @param {...any} args
     * @returns {ReturnType<F>}
     */
    run<T_1, F extends AnyFunc>(value: T_1, fn: F, ...args: any[]): ReturnType<F>;
    /**
     * Get the `AsyncContext.Variable` value.
     * @template T
     * @return {T|undefined}
     */
    get<T_1>(): T_1 | undefined;
    #private;
}
/**
 * `AsyncContext.Snapshot` allows you to opaquely capture the current values of
 * all `AsyncContext.Variable` instances and execute a function at a later time
 * as if those values were still the current values (a snapshot and restore).
 * @see {@link https://github.com/tc39/proposal-async-context/blob/master/README.md#asynccontextsnapshot}
 */
export class Snapshot {
    /**
     * Wraps a given function `fn` with additional logic to take a snapshot of
     * `Storage` before invoking `fn`. Returns a new function with the same
     * signature as `fn` that when called, will invoke `fn` with the current
     * `this` context and provided arguments, after restoring the `Storage`
     * snapshot.
     *
     * `AsyncContext.Snapshot.wrap` is a helper which captures the current values
     * of all Variables and returns a wrapped function. When invoked, this
     * wrapped function restores the state of all Variables and executes the
     * inner function.
     *
     * @see {@link https://github.com/tc39/proposal-async-context/blob/master/README.md#asynccontextsnapshotwrap}
     *
     * @template {AnyFunc} F
     * @param {F} fn
     * @returns {F}
     */
    static wrap<F extends AnyFunc>(fn: F): F;
    /**
     * Runs the given function `fn` with arguments `args`, using a `null`
     * context and the current snapshot.
     *
     * @template {AnyFunc} F
     * @param {F} fn
     * @param {...any} args
     * @returns {ReturnType<F>}
     */
    run<F extends AnyFunc>(fn: F, ...args: any[]): ReturnType<F>;
    #private;
}
/**
 * `AsyncContext` container.
 */
export class AsyncContext {
    /**
     * `AsyncContext.Variable` is a container for a value that is associated with
     * the current execution flow. The value is propagated through async execution
     * flows, and can be snapshot and restored with Snapshot.
     * @see {@link https://github.com/tc39/proposal-async-context/blob/master/README.md#asynccontextvariable}
     * @type {typeof Variable}
     */
    static Variable: typeof Variable;
    /**
     * `AsyncContext.Snapshot` allows you to opaquely capture the current values of
     * all `AsyncContext.Variable` instances and execute a function at a later time
     * as if those values were still the current values (a snapshot and restore).
     * @see {@link https://github.com/tc39/proposal-async-context/blob/master/README.md#asynccontextsnapshot}
     * @type {typeof Snapshot}
     */
    static Snapshot: typeof Snapshot;
}
export default AsyncContext;
export type VariableOptions<T> = {
    name?: string;
    defaultValue?: T;
};
export type AnyFunc = () => any;
