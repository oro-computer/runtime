export function dispatch(hook: any, asyncId: any, type: any, triggerAsyncId: any, resource: any): void;
export function getNextAsyncResourceId(): number;
export function executionAsyncResource(): any;
export function executionAsyncId(): any;
export function triggerAsyncId(): any;
export function getDefaultExecutionAsyncId(): any;
export function wrap(callback: any, type: any, asyncId?: number, triggerAsyncId?: any, resource?: any): (...args: any[]) => any;
export function getTopLevelAsyncResourceName(): any;
/**
 * The default top level async resource ID
 * @type {number}
 */
export const TOP_LEVEL_ASYNC_RESOURCE_ID: number;
export namespace state {
    let defaultExecutionAsyncId: number;
}
export namespace hooks {
    let init: any[];
    let before: any[];
    let after: any[];
    let destroy: any[];
    let promiseResolve: any[];
}
/**
 * A base class for the `AsyncResource` class or other higher level async
 * resource classes.
 */
export class CoreAsyncResource {
    [x: number]: () => import("../../gc.js").Finalizer;
    /**
     * `CoreAsyncResource` class constructor.
     * @param {string} type
     * @param {object|number=} [options]
     */
    constructor(type: string, options?: (object | number) | undefined);
    /**
     * The `CoreAsyncResource` type.
     * @type {string}
     */
    get type(): string;
    /**
     * `true` if the `CoreAsyncResource` was destroyed, otherwise `false`. This
     * value is only set to `true` if `emitDestroy()` was called, likely from
     * destroying the resource manually.
     * @type {boolean}
     */
    get destroyed(): boolean;
    /**
     * The unique async resource ID.
     * @return {number}
     */
    asyncId(): number;
    /**
     * The trigger async resource ID.
     * @return {number}
     */
    triggerAsyncId(): number;
    /**
     * Manually emits destroy hook for the resource.
     * @return {CoreAsyncResource}
     */
    emitDestroy(): CoreAsyncResource;
    /**
     * Binds function `fn` with an optional this `thisArg` binding to run
     * in the execution context of this `CoreAsyncResource`.
     * @param {function} fn
     * @param {object=} [thisArg]
     * @return {function}
     */
    bind(fn: Function, thisArg?: object | undefined): Function;
    /**
     * Runs function `fn` in the execution context of this `CoreAsyncResource`.
     * @param {function} fn
     * @param {object=} [thisArg]
     * @param {...any} [args]
     * @return {any}
     */
    runInAsyncScope(fn: Function, thisArg?: object | undefined, ...args?: any[]): any;
    #private;
}
export class TopLevelAsyncResource extends CoreAsyncResource {
}
export const asyncContextVariable: Variable<any>;
export const topLevelAsyncResource: TopLevelAsyncResource;
export default hooks;
import { Variable } from '../../async/context.js';
