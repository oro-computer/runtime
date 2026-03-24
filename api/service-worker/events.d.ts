export const textEncoder: TextEncoderStream;
export const FETCH_EVENT_TIMEOUT: number;
export const FETCH_EVENT_MAX_RESPONSE_REDIRECTS: number;
/**
 * The `ExtendableEvent` interface extends the lifetime of the "install" and
 * "activate" events dispatched on the global scope as part of the service
 * worker lifecycle.
 */
export class ExtendableEvent extends Event {
    /**
     * `ExtendableEvent` class constructor.
     * @ignore
     */
    constructor(...args: any[]);
    /**
     * A context for this `ExtendableEvent` instance.
     * @type {import('./context.js').Context}
     */
    get context(): import("./context.js").Context;
    /**
     * A promise that can be awaited which waits for this `ExtendableEvent`
     * instance no longer has pending promises.
     * @type {Promise}
     */
    get awaiting(): Promise<any>;
    /**
     * The number of pending promises
     * @type {number}
     */
    get pendingPromises(): number;
    /**
     * `true` if the `ExtendableEvent` instance is considered "active",
     * otherwise `false`.
     * @type {boolean}
     */
    get isActive(): boolean;
    /**
     * Tells the event dispatcher that work is ongoing.
     * It can also be used to detect whether that work was successful.
     * @param {Promise} promise
     */
    waitUntil(promise: Promise<any>): void;
    /**
     * Returns a promise that this `ExtendableEvent` instance is waiting for.
     * @return {Promise}
     */
    waitsFor(): Promise<any>;
    #private;
}
/**
 * This is the event type for "fetch" events dispatched on the service worker
 * global scope. It contains information about the fetch, including the
 * request and how the receiver will treat the response.
 */
export class FetchEvent extends ExtendableEvent {
    static defaultHeaders: Headers;
    /**
     * `FetchEvent` class constructor.
     * @ignore
     * @param {string=} [type = 'fetch']
     * @param {object=} [options]
     */
    constructor(type?: string | undefined, options?: object | undefined);
    /**
     * The handled property of the `FetchEvent` interface returns a promise
     * indicating if the event has been handled by the fetch algorithm or not.
     * This property allows executing code after the browser has consumed a
     * response, and is usually used together with the `waitUntil()` method.
     * @type {Promise}
     */
    get handled(): Promise<any>;
    /**
     * The request read-only property of the `FetchEvent` interface returns the
     * `Request` that triggered the event handler.
     * @type {Request}
     */
    get request(): Request;
    /**
     * The `clientId` read-only property of the `FetchEvent` interface returns
     * the id of the Client that the current service worker is controlling.
     * @type {string}
     */
    get clientId(): string;
    /**
     * @ignore
     * @type {string}
     */
    get resultingClientId(): string;
    /**
     * @ignore
     * @type {string}
     */
    get replacesClientId(): string;
    /**
     * @ignore
     * @type {boolean}
     */
    get isReload(): boolean;
    /**
     * @ignore
     * @type {Promise}
     */
    get preloadResponse(): Promise<any>;
    /**
     * The `respondWith()` method of `FetchEvent` prevents the webview's
     * default fetch handling, and allows you to provide a promise for a
     * `Response` yourself.
     * @param {Response|Promise<Response>} response
     */
    respondWith(response: Response | Promise<Response>): void;
    #private;
}
export class ExtendableMessageEvent extends ExtendableEvent {
    /**
     * `ExtendableMessageEvent` class constructor.
     * @param {string=} [type = 'message']
     * @param {object=} [options]
     */
    constructor(type?: string | undefined, options?: object | undefined);
    /**
     * @type {any}
     */
    get data(): any;
    /**
     * @type {MessagePort[]}
     */
    get ports(): MessagePort[];
    /**
     * @type {import('./clients.js').Client?}
     */
    get source(): import("./clients.js").Client | null;
    /**
     * @type {string?}
     */
    get origin(): string | null;
    /**
     * @type {string}
     */
    get lastEventId(): string;
    #private;
}
export class NotificationEvent extends ExtendableEvent {
    constructor(type: any, options: any);
    get action(): string;
    get notification(): any;
    #private;
}
declare namespace _default {
    export { ExtendableMessageEvent };
    export { ExtendableEvent };
    export { FetchEvent };
}
export default _default;
