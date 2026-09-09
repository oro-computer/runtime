/**
 * Predicate to determine if service workers are allowed
 * @return {boolean}
 */
export function isServiceWorkerAllowed(): boolean;
/**
 * A `ServiceWorkerContainer` implementation that is attached to the global
 * `globalThis.navigator.serviceWorker` object.
 */
export class ServiceWorkerContainer extends EventTarget {
    get ready(): any;
    get controller(): any;
    /**
     * A special initialization function for augmenting the global
     * `globalThis.navigator.serviceWorker` platform `ServiceWorkerContainer`
     * instance.
     *
     * All functions MUST be sure to what a lexically bound `this` becomes as the
     * target could change with respect to the `internal` `Map` instance which
     * contains private implementation properties relevant to the runtime
     * `ServiceWorkerContainer` internal state implementations.
     * @ignore
     */
    init(): Promise<any>;
    /**
     * Registers a service worker and returns its registration.
     * @param {string|URL} scriptURL
     * @param {RegistrationOptions} [options]
     * @returns {Promise<ServiceWorkerRegistration>}
     */
    register(scriptURL: string | URL, options?: RegistrationOptions): Promise<ServiceWorkerRegistration>;
    getRegistration(clientURL: any): Promise<globalThis.ServiceWorkerRegistration | ServiceWorkerRegistration>;
    getRegistrations(options: any): Promise<readonly globalThis.ServiceWorkerRegistration[] | ServiceWorkerRegistration[]>;
    startMessages(): void;
}
export default ServiceWorkerContainer;
import { ServiceWorkerRegistration } from './registration.js';
