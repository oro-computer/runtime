/**
 * @typedef {{
 *   triggerAsyncId?: number,
 *   requireManualDestroy?: boolean
 * }} AsyncResourceOptions
 */
/**
 * A container that should be extended that represents a resource with
 * an asynchronous execution context.
 */
export class AsyncResource extends CoreAsyncResource {
    /**
     * Binds function `fn` with an optional this `thisArg` binding to run
     * in the execution context of an anonymous `AsyncResource`.
     * @param {function} fn
     * @param {object|string=} [type]
     * @param {object=} [thisArg]
     * @return {function}
     */
    static bind(fn: Function, type?: (object | string) | undefined, thisArg?: object | undefined): Function;
    /**
     * `AsyncResource` class constructor.
     * @param {string} type
     * @param {AsyncResourceOptions|number=} [options]
     */
    constructor(type: string, options?: (AsyncResourceOptions | number) | undefined);
}
export default AsyncResource;
export type AsyncResourceOptions = {
    triggerAsyncId?: number;
    requireManualDestroy?: boolean;
};
import { executionAsyncResource } from '../internal/async/hooks.js';
import { executionAsyncId } from '../internal/async/hooks.js';
import { triggerAsyncId } from '../internal/async/hooks.js';
import { CoreAsyncResource } from '../internal/async/hooks.js';
export { executionAsyncResource, executionAsyncId, triggerAsyncId };
