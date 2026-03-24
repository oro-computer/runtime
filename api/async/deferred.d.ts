/**
 * Dispatched when a `Deferred` internal promise is resolved.
 */
export class DeferredResolveEvent extends Event {
    /**
     * `DeferredResolveEvent` class constructor
     * @ignore
     * @param {string=} [type]
     * @param {any=} [result]
     */
    constructor(type?: string | undefined, result?: any | undefined);
    /**
     * The `Deferred` promise result value.
     * @type {any?}
     */
    result: any | null;
}
/**
 * Dispatched when a `Deferred` internal promise is rejected.
 */
export class DeferredRejectEvent {
    /**
     * `DeferredRejectEvent` class constructor
     * @ignore
     * @param {string=} [type]
     * @param {Error=} [error]
     */
    constructor(type?: string | undefined, error?: Error | undefined);
}
/**
 * A utility class for creating deferred promises.
 */
export class Deferred extends EventTarget {
    /**
     * `Deferred` class constructor.
     * @param {Deferred|Promise?} [promise]
     */
    constructor(promise?: Deferred | (Promise<any> | null));
    /**
     * Function to resolve the associated promise.
     * @type {function}
     */
    resolve: Function;
    /**
     * Function to reject the associated promise.
     * @type {function}
     */
    reject: Function;
    /**
     * Attaches a fulfillment callback and a rejection callback to the promise,
     * and returns a new promise resolving to the return value of the called
     * callback.
     * @param {function(any)=} [resolve]
     * @param {function(Error)=} [reject]
     */
    then(resolve?: ((arg0: any) => any) | undefined, reject?: ((arg0: Error) => any) | undefined): Promise<any>;
    /**
     * Attaches a rejection callback to the promise, and returns a new promise
     * resolving to the return value of the callback if it is called, or to its
     * original fulfillment value if the promise is instead fulfilled.
     * @param {function(Error)=} [callback]
     */
    catch(callback?: ((arg0: Error) => any) | undefined): Promise<any>;
    /**
     * Attaches a callback for when the promise is settled (fulfilled or rejected).
     * @param {function(any?)} [callback]
     */
    finally(callback?: (arg0: any | null) => any): Promise<any>;
    /**
     * The promise associated with this Deferred instance.
     * @type {Promise<any>}
     */
    get promise(): Promise<any>;
    /**
     * A string representation of this Deferred instance.
     * @type {string}
     * @ignore
     */
    get [Symbol.toStringTag](): string;
    #private;
}
export default Deferred;
