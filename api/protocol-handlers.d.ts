/**
 * @typedef {{ scheme: string }} GetServiceWorkerOptions

/**
 * @param {GetServiceWorkerOptions} options
 * @return {Promise<ServiceWorker|null>
 */
export function getServiceWorker(options: GetServiceWorkerOptions): Promise<ServiceWorker | null>;
declare namespace _default {
    export { getServiceWorker };
}
export default _default;
/**
 * /**
 */
export type GetServiceWorkerOptions = {
    scheme: string;
};
