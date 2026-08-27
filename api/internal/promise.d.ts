export const NativePromise: PromiseConstructor;
export namespace NativePromisePrototype {
    export let then: <TResult1 = any, TResult2 = never>(onfulfilled?: (value: any) => TResult1 | PromiseLike<TResult1>, onrejected?: (reason: any) => TResult2 | PromiseLike<TResult2>) => globalThis.Promise<TResult1 | TResult2>;
    let _catch: <TResult = never>(onrejected?: (reason: any) => TResult | PromiseLike<TResult>) => globalThis.Promise<any>;
    export { _catch as catch };
    let _finally: (onfinally?: () => void) => globalThis.Promise<any>;
    export { _finally as finally };
}
export const NativePromiseAll: any;
export const NativePromiseAny: any;
/**
 * @typedef {function(any): void} ResolveFunction
 */
/**
 * @typedef {function(Error|string|null): void} RejectFunction
 */
/**
 * @typedef {function(ResolveFunction, RejectFunction): void} ResolverFunction
 */
/**
 * @typedef {{
 *   promise: Promise,
 *   resolve: ResolveFunction,
 *   reject: RejectFunction
 * }} PromiseResolvers
 */
export class Promise extends globalThis.Promise<any> {
    /**
     * Creates a new `Promise` with resolver functions.
     * @see {https://github.com/tc39/proposal-promise-with-resolvers}
     * @return {PromiseResolvers}
     */
    static withResolvers(): PromiseResolvers;
    /**
     * `Promise` class constructor.
     * @ignore
     * @param {ResolverFunction} resolver
     */
    constructor(resolver: ResolverFunction);
    [resourceSymbol]: asyncHooks.CoreAsyncResource;
}
export namespace Promise {
    function all(iterable: any): any;
    function any(iterable: any): any;
}
export default Promise;
export type ResolveFunction = (arg0: any) => void;
export type RejectFunction = (arg0: Error | string | null) => void;
export type ResolverFunction = (arg0: ResolveFunction, arg1: RejectFunction) => void;
export type PromiseResolvers = {
    promise: Promise;
    resolve: ResolveFunction;
    reject: RejectFunction;
};
declare const resourceSymbol: unique symbol;
import * as asyncHooks from './async/hooks.js';
