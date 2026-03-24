/**
 * Wait for a hook event to occur.
 * @template T extends Event
 * @param {string|function} nameOrFunction
 * @return {Promise<T>}
 */
export function wait<T>(nameOrFunction: string | Function): Promise<T>;
/**
 * Wait for the global Window, Document, and Runtime to be ready.
 * The callback function is called exactly once.
 * @param {function} callback
 * @return {function}
 */
export function onReady(callback: Function): Function;
/**
 * Wait for the global Window and Document to be ready. The callback
 * function is called exactly once.
 * @param {function} callback
 * @return {function}
 */
export function onLoad(callback: Function): Function;
/**
 * Wait for the runtime to be ready. The callback
 * function is called exactly once.
 * @param {function} callback
 * @return {function}
 */
export function onInit(callback: Function): Function;
/**
 * Calls callback when a global exception occurs.
 * 'error', 'messageerror', and 'unhandledrejection' events are handled here.
 * @param {function} callback
 * @return {function}
 */
export function onError(callback: Function): Function;
/**
 * Subscribes to the global data pipe calling callback when
 * new data is emitted on the global Window.
 * @param {function} callback
 * @return {function}
 */
export function onData(callback: Function): Function;
/**
 * Subscribes to global messages likely from an external `postMessage`
 * invocation.
 * @param {function} callback
 * @return {function}
 */
export function onMessage(callback: Function): Function;
/**
 * Calls callback when runtime is working online.
 * @param {function} callback
 * @return {function}
 */
export function onOnline(callback: Function): Function;
/**
 * Calls callback when runtime is not working online.
 * @param {function} callback
 * @return {function}
 */
export function onOffline(callback: Function): Function;
/**
 * Calls callback when runtime user preferred language has changed.
 * @param {function} callback
 * @return {function}
 */
export function onLanguageChange(callback: Function): Function;
/**
 * Calls callback when an application permission has changed.
 * @param {function} callback
 * @return {function}
 */
export function onPermissionChange(callback: Function): Function;
/**
 * Calls callback in response to a presented `Notification`.
 * @param {function} callback
 * @return {function}
 */
export function onNotificationResponse(callback: Function): Function;
/**
 * Calls callback when a `Notification` is presented.
 * @param {function} callback
 * @return {function}
 */
export function onNotificationPresented(callback: Function): Function;
/**
 * Calls callback when a `ApplicationURL` is opened.
 * @param {function(ApplicationURLEvent)} callback
 * @return {function}
 */
export function onApplicationURL(callback: (arg0: ApplicationURLEvent) => any): Function;
/**
 * Calls callback when a `ApplicationPause` is dispatched.
 * @param {function} callback
 * @return {function}
 */
export function onApplicationPause(callback: Function): Function;
/**
 * Calls callback when a `ApplicationResume` is dispatched.
 * @param {function} callback
 * @return {function}
 */
export function onApplicationResume(callback: Function): Function;
export const RUNTIME_INIT_EVENT_NAME: "__runtime_init__";
export const GLOBAL_EVENTS: string[];
/**
 * An event dispatched when the runtime has been initialized.
 */
export class InitEvent {
    constructor();
}
/**
 * An event dispatched when the runtime global has been loaded.
 */
export class LoadEvent {
    constructor();
}
/**
 * An event dispatched when the runtime is considered ready.
 */
export class ReadyEvent {
    constructor();
}
/**
 * An event dispatched when the runtime has been initialized.
 */
export class RuntimeInitEvent {
    constructor();
}
/**
 * An interface for registering callbacks for various hooks in
 * the runtime.
 */
export class Hooks extends EventTarget {
    /**
     * @ignore
     */
    static GLOBAL_EVENTS: string[];
    /**
     * @ignore
     */
    static InitEvent: typeof InitEvent;
    /**
     * @ignore
     */
    static LoadEvent: typeof LoadEvent;
    /**
     * @ignore
     */
    static ReadyEvent: typeof ReadyEvent;
    /**
     * @ignore
     */
    static RuntimeInitEvent: typeof RuntimeInitEvent;
    /**
     * An array of all global events listened to in various hooks
     */
    get globalEvents(): string[];
    /**
     * Reference to global object
     * @type {object}
     */
    get global(): object;
    /**
     * Returns `document` in global.
     * @type {Document}
     */
    get document(): Document;
    /**
     * Returns `document` in global.
     * @type {Window}
     */
    get window(): Window;
    /**
     * Predicate for determining if the global document is ready.
     * @type {boolean}
     */
    get isDocumentReady(): boolean;
    /**
     * Predicate for determining if the global object is ready.
     * @type {boolean}
     */
    get isGlobalReady(): boolean;
    /**
     * Predicate for determining if the runtime is ready.
     * @type {boolean}
     */
    get isRuntimeReady(): boolean;
    /**
     * Predicate for determining if everything is ready.
     * @type {boolean}
     */
    get isReady(): boolean;
    /**
     * Predicate for determining if the runtime is working online.
     * @type {boolean}
     */
    get isOnline(): boolean;
    /**
     * Predicate for determining if the runtime is in a Worker context.
     * @type {boolean}
     */
    get isWorkerContext(): boolean;
    /**
     * Predicate for determining if the runtime is in a Window context.
     * @type {boolean}
     */
    get isWindowContext(): boolean;
    /**
     * Wait for a hook event to occur.
     * @template T extends Event
     * @param {string|function} nameOrFunction
     * @param {WaitOptions=} [options]
     * @return {Promise<T>}
     */
    wait<T>(nameOrFunction: string | Function, options?: WaitOptions | undefined): Promise<T>;
    /**
     * Wait for the global Window, Document, and Runtime to be ready.
     * The callback function is called exactly once.
     * @param {function} callback
     * @return {function}
     */
    onReady(callback: Function): Function;
    /**
     * Wait for the global Window and Document to be ready. The callback
     * function is called exactly once.
     * @param {function} callback
     * @return {function}
     */
    onLoad(callback: Function): Function;
    /**
     * Wait for the runtime to be ready. The callback
     * function is called exactly once.
     * @param {function} callback
     * @return {function}
     */
    onInit(callback: Function): Function;
    /**
     * Calls callback when a global exception occurs.
     * 'error', 'messageerror', and 'unhandledrejection' events are handled here.
     * @param {function} callback
     * @return {function}
     */
    onError(callback: Function): Function;
    /**
     * Subscribes to the global data pipe calling callback when
     * new data is emitted on the global Window.
     * @param {function} callback
     * @return {function}
     */
    onData(callback: Function): Function;
    /**
     * Subscribes to global messages likely from an external `postMessage`
     * invocation.
     * @param {function} callback
     * @return {function}
     */
    onMessage(callback: Function): Function;
    /**
     * Calls callback when runtime is working online.
     * @param {function} callback
     * @return {function}
     */
    onOnline(callback: Function): Function;
    /**
     * Calls callback when runtime is not working online.
     * @param {function} callback
     * @return {function}
     */
    onOffline(callback: Function): Function;
    /**
     * Calls callback when runtime user preferred language has changed.
     * @param {function} callback
     * @return {function}
     */
    onLanguageChange(callback: Function): Function;
    /**
     * Calls callback when an application permission has changed.
     * @param {function} callback
     * @return {function}
     */
    onPermissionChange(callback: Function): Function;
    /**
     * Calls callback in response to a displayed `Notification`.
     * @param {function} callback
     * @return {function}
     */
    onNotificationResponse(callback: Function): Function;
    /**
     * Calls callback when a `Notification` is presented.
     * @param {function} callback
     * @return {function}
     */
    onNotificationPresented(callback: Function): Function;
    /**
     * Calls callback when a `ApplicationURL` is opened.
     * @param {function} callback
     * @return {function}
     */
    onApplicationURL(callback: Function): Function;
    /**
     * Calls callback when an `ApplicationPause` is dispatched.
     * @param {function} callback
     * @return {function}
     */
    onApplicationPause(callback: Function): Function;
    /**
     * Calls callback when an `ApplicationResume` is dispatched.
     * @param {function} callback
     * @return {function}
     */
    onApplicationResume(callback: Function): Function;
    #private;
}
export default hooks;
export type WaitOptions = {
    signal?: AbortSignal;
};
export type ApplicationURLEvent = import("./internal/events.js").ApplicationURLEvent;
/**
 * `Hooks` single instance.
 * @ignore
 */
declare const hooks: Hooks;
