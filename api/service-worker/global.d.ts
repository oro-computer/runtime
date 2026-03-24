export class ServiceWorkerGlobalScope {
    get isServiceWorkerScope(): boolean;
    get ExtendableEvent(): typeof ExtendableEvent;
    get FetchEvent(): typeof FetchEvent;
    get serviceWorker(): any;
    set registration(value: any);
    get registration(): any;
    get clients(): import("./clients.js").Clients;
    set onactivate(listener: any);
    get onactivate(): any;
    set onmessage(listener: any);
    get onmessage(): any;
    set oninstall(listener: any);
    get oninstall(): any;
    set onfetch(listener: any);
    get onfetch(): any;
    skipWaiting(): Promise<void>;
}
declare const _default: ServiceWorkerGlobalScope;
export default _default;
import { ExtendableEvent } from './events.js';
import { FetchEvent } from './events.js';
