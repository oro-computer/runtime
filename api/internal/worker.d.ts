export function onWorkerMessage(event: any): Promise<any>;
export function addEventListener(eventName: any, callback: any, ...args: any[]): any;
export function removeEventListener(eventName: any, callback: any, ...args: any[]): any;
export function dispatchEvent(event: any): any;
export function postMessage(message: any, ...args: any[]): any;
export function close(): any;
export function importScripts(...scripts: any[]): void;
export const WorkerGlobalScopePrototype: any;
/**
 * The absolute `URL` of the internal worker initialization entry.
 * @ignore
 * @type {URL}
 */
export const url: URL;
/**
 * The worker entry source.
 * @ignore
 * @type {string}
 */
export const source: string;
/**
 * A unique identifier for this worker made available on the global scope
 * @ignore
 * @type {string}
 */
export const RUNTIME_WORKER_ID: string;
/**
 * Internally scoped event interface for a worker context.
 * @ignore
 * @type {object}
 */
export const worker: object;
/**
 * A reference to the global worker scope.
 * @type {WorkerGlobalScope}
 */
export const self: WorkerGlobalScope;
declare namespace _default {
    export { RUNTIME_WORKER_ID };
    export { removeEventListener };
    export { addEventListener };
    export { importScripts };
    export { dispatchEvent };
    export { postMessage };
    export { source };
    export { close };
    export { url };
}
export default _default;
