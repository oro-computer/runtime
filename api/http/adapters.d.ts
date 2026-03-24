/**
 * @typedef {{
 *   Connection: typeof import('../http.js').Connection,
 *   globalAgent: import('../http.js').Agent,
 *   IncomingMessage: typeof import('../http.js').IncomingMessage,
 *   ServerResponse: typeof import('../http.js').ServerResponse,
 *   STATUS_CODES: object,
 *   METHODS: string[]
 * }} HTTPModuleInterface
 */
/**
 * An abstract base class for an HTTP server adapter.
 */
export class ServerAdapter extends EventTarget {
    /**
     * `ServerAdapter` class constructor.
     * @ignore
     * @param {import('../http.js').Server} server
     * @param {HTTPModuleInterface} httpInterface
     */
    constructor(server: import("../http.js").Server, httpInterface: HTTPModuleInterface);
    /**
     * A readonly reference to the underlying HTTP(S) server
     * for this adapter.
     * @type {import('../http.js').Server}
     */
    get server(): import("../http.js").Server;
    /**
     * A readonly reference to the underlying HTTP(S) module interface
     * for creating various HTTP module class objects.
     * @type {HTTPModuleInterface}
     */
    get httpInterface(): HTTPModuleInterface;
    /**
     * A readonly reference to the `AsyncContext.Variable` associated with this
     * `ServerAdapter` instance.
     */
    get context(): import("../async/context.js").Variable<any>;
    /**
     * Called when the adapter should destroy itself.
     * @abstract
     */
    destroy(): Promise<void>;
    #private;
}
/**
 * An HTTP adapter for running an HTTP server in a Service Worker that uses the
 * "fetch" event for the request and response lifecycle.
 *
 * @event ServiceWorkerServerAdapter#install
 * @type {Event}
 * Emitted when the Service Worker 'install' event fires.
 *
 * @event ServiceWorkerServerAdapter#activate
 * @type {Event}
 * Emitted when the Service Worker 'activate' event fires.
 */
export class ServiceWorkerServerAdapter extends ServerAdapter {
    /**
     * Handles the 'install' service worker event.
     * @ignore
     * @param {import('../service-worker/events.js').ExtendableEvent} event
     */
    onInstall(event: import("../service-worker/events.js").ExtendableEvent): Promise<void>;
    /**
     * Handles the 'activate' service worker event.
     * @ignore
     * @param {import('../service-worker/events.js').ExtendableEvent} event
     */
    onActivate(event: import("../service-worker/events.js").ExtendableEvent): Promise<void>;
    /**
     * Handles the 'fetch' service worker event.
     * @ignore
     * @param {import('../service-worker/events.js').FetchEvent}
     */
    onFetch(event: any): Promise<void>;
}
declare namespace _default {
    export { ServerAdapter };
    export { ServiceWorkerServerAdapter };
}
export default _default;
export type HTTPModuleInterface = {
    Connection: typeof import("../http.js").Connection;
    globalAgent: import("../http.js").Agent;
    IncomingMessage: typeof import("../http.js").IncomingMessage;
    ServerResponse: typeof import("../http.js").ServerResponse;
    STATUS_CODES: object;
    METHODS: string[];
};
