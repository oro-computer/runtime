/**
 * @typedef {object} ServiceWorkerOptions
 * @property {string|null} [id]
 * @property {string|null} [scriptURL]
 * @property {boolean} [subscribe]
 */
/**
 * Creates an observable handle to a worker registration.
 * @param {string|null} [currentState]
 * @param {ServiceWorkerOptions} [options]
 * @returns {globalThis.ServiceWorker}
 */
export function createServiceWorker(currentState?: string | null, options?: ServiceWorkerOptions): globalThis.ServiceWorker;
export const channel: BroadcastChannel;
export const ServiceWorker: {
    new (): ServiceWorker;
    prototype: ServiceWorker;
} | {
    new (): {
        get onmessage(): any;
        set onmessage(_: any);
        get onerror(): any;
        set onerror(_: any);
        get onstatechange(): any;
        set onstatechange(_: any);
        get state(): any;
        get scriptURL(): any;
        postMessage(): void;
        addEventListener(type: string, callback: EventListenerOrEventListenerObject | null, options?: AddEventListenerOptions | boolean): void;
        dispatchEvent(event: Event): boolean;
        removeEventListener(type: string, callback: EventListenerOrEventListenerObject | null, options?: EventListenerOptions | boolean): void;
    };
};
export default createServiceWorker;
export type ServiceWorkerOptions = {
    id?: string | null;
    scriptURL?: string | null;
    subscribe?: boolean;
};
