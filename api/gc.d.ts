/**
 * Track `object` ref to call `Symbol.for('oro.runtime.gc.finalize')` method when
 * environment garbage collects object.
 * @param {object} object
 * @return {boolean}
 */
export function ref(object: object, ...args: any[]): boolean;
/**
 * Stop tracking `object` ref to call `Symbol.for('oro.runtime.gc.finalize')` method when
 * environment garbage collects object.
 * @param {object} object
 * @return {boolean}
 */
export function unref(object: object): boolean;
/**
 * An alias for `unref()`
 * @param {object} object}
 * @return {boolean}
 */
export function retain(object: object): boolean;
/**
 * Call finalize on `object` for `gc.finalizer` implementation.
 * @param {object} object]
 * @return {Promise<boolean>}
 */
export function finalize(object: object, ...args: any[]): Promise<boolean>;
/**
 * Calls all pending finalization handlers forcefully. This function
 * may have unintended consequences as objects be considered finalized
 * and still strongly held (retained) somewhere.
 */
export function release(): Promise<void>;
export const finalizers: WeakMap<object, any>;
export const kFinalizer: unique symbol;
export const finalizer: symbol;
/**
 * @type {Set<WeakRef>}
 */
export const pool: Set<WeakRef<any>>;
/**
 * Static registry for objects to clean up underlying resources when they
 * are gc'd by the environment. There is no guarantee that the `finalizer()`
 * is called at any time.
 */
export const registry: FinalizationRegistry<Finalizer>;
/**
 * Default exports which also acts a retained value to persist bound
 * `Finalizer#handle()` functions from being gc'd before the
 * `FinalizationRegistry` callback is called because `heldValue` must be
 * strongly held (retained) in order for the callback to be called.
 */
export const gc: any;
export default gc;
/**
 * A container for strongly (retain) referenced finalizer function
 * with arguments weakly referenced to an object that will be
 * garbage collected.
 */
export class Finalizer {
    /**
     * Creates a `Finalizer` from input.
     */
    static from(handler: any): Finalizer;
    /**
     * `Finalizer` class constructor.
     * @private
     * @param {array} args
     * @param {function} handle
     */
    private constructor();
    args: any[];
    handle: any;
}
