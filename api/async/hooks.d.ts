/**
 * Factory for creating a `AsyncHook` instance.
 * @param {AsyncHookCallbacks=} [callbacks]
 * @return {AsyncHook}
 */
export function createHook(callbacks?: AsyncHookCallbacks | undefined): AsyncHook;
/**
 * A container for `AsyncHooks` callbacks.
 * @ignore
 */
export class AsyncHookCallbacks {
    /**
     * `AsyncHookCallbacks` class constructor.
     * @ignore
     * @param {AsyncHookCallbacks} [options]
     */
    constructor(options?: AsyncHookCallbacks);
    init(_asyncId: any, _type: any, _triggerAsyncId: any, _resource: any): void;
    before(_asyncId: any): void;
    after(_asyncId: any): void;
    destroy(_asyncId: any): void;
    promiseResolve(_asyncId: any): void;
}
/**
 * A container for registering various callbacks for async resource hooks.
 */
export class AsyncHook {
    /**
     * @param {AsyncHookCallbacks=} [options]
     */
    constructor(callbacks?: any);
    /**
     * @type {boolean}
     */
    get enabled(): boolean;
    /**
     * Enable the async hook.
     * @return {AsyncHook}
     */
    enable(): AsyncHook;
    /**
     * Disables the async hook
     * @return {AsyncHook}
     */
    disable(): AsyncHook;
    #private;
}
export default createHook;
import { executionAsyncResource } from '../internal/async/hooks.js';
import { executionAsyncId } from '../internal/async/hooks.js';
import { triggerAsyncId } from '../internal/async/hooks.js';
export { executionAsyncResource, executionAsyncId, triggerAsyncId };
