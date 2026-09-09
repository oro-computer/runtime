export class ServiceWorkerRegistration extends EventTarget {
    constructor(info: any, serviceWorker: any);
    get scope(): any;
    get updateViaCache(): string;
    get installing(): any;
    get waiting(): any;
    get active(): any;
    set onupdatefound(onupdatefound: any);
    get onupdatefound(): any;
    get navigationPreload(): any;
    getNotifications(): Promise<any>;
    showNotification(title: any, options: any): Promise<void>;
    /**
     * Removes this registration from the native service worker container.
     * @returns {Promise<boolean>}
     */
    unregister(): Promise<boolean>;
    update(): Promise<void>;
    #private;
}
export default ServiceWorkerRegistration;
