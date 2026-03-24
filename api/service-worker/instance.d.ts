export function createServiceWorker(currentState?: any, options?: any): any;
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
