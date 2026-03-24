export function init(sharedWorker: any, options: any): Promise<void>;
/**
 * Gets the SharedWorker context window.
 * This function will create it if it does not already exist.
 * @return {Promise<import('./window.js').ApplicationWindow}
 */
export function getContextWindow(): Promise<any>;
export const SHARED_WORKER_WINDOW_TITLE: "oro:shared-worker";
export const SHARED_WORKER_WINDOW_PATH: "/oro/shared-worker/index.html";
export const channel: BroadcastChannel;
export const workers: Map<any, any>;
export class SharedWorkerMessagePort extends ipc.IPCMessagePort {
}
export class SharedWorker extends EventTarget {
    /**
     * `SharedWorker` class constructor.
     * @param {string|URL|Blob} aURL
     * @param {string|object=} [nameOrOptions]
     */
    constructor(aURL: string | URL | Blob, nameOrOptions?: (string | object) | undefined);
    set onerror(onerror: any);
    get onerror(): any;
    get ready(): any;
    get channel(): ipc.IPCMessageChannel;
    get port(): any;
    get id(): any;
    #private;
}
export default SharedWorker;
import ipc from '../ipc.js';
