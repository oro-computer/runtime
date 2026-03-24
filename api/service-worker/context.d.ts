/**
 * A context given to `ExtendableEvent` interfaces and provided to
 * simplified service worker modules
 */
export class Context {
    /**
     * `Context` class constructor.
     * @param {import('./events.js').ExtendableEvent} event
     */
    constructor(event: import("./events.js").ExtendableEvent);
    /**
     * Context data. This may be a custom protocol handler scheme data
     * by default, if available.
     * @type {any?}
     */
    data: any | null;
    /**
     * The `ExtendableEvent` for this `Context` instance.
     * @type {ExtendableEvent}
     */
    get event(): ExtendableEvent;
    /**
     * An environment context object.
     * @type {object?}
     */
    get env(): object | null;
    /**
     * Resets the current environment context.
     * @return {Promise<boolean>}
     */
    resetEnvironment(): Promise<boolean>;
    /**
     * Unused, but exists for cloudflare compat.
     * @ignore
     */
    passThroughOnException(): void;
    /**
     * Tells the event dispatcher that work is ongoing.
     * It can also be used to detect whether that work was successful.
     * @param {Promise} promise
     */
    waitUntil(promise: Promise<any>): Promise<any>;
    /**
     * TODO
     */
    handled(): Promise<any>;
    /**
     * Gets the client for this event context.
     * @return {Promise<import('./clients.js').Client>}
     */
    client(): Promise<import("./clients.js").Client>;
    #private;
}
declare namespace _default {
    export { Context };
}
export default _default;
