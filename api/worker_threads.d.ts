/**
 * Set shared worker environment data.
 * @param {string} key
 * @param {any} value
 */
export function setEnvironmentData(key: string, value: any): void;
/**
 * Get shared worker environment data.
 * @param {string} key
 * @return {any}
 */
export function getEnvironmentData(key: string): any;
/**

 * A pool of known worker threads.
 * @type {Map<string, Worker>}
 */
export const workers: Map<string, Worker>;
/**
 * `true` if this is the "main" thread, otherwise `false`
 * The "main" thread is the top level webview window.
 * @type {boolean}
 */
export const isMainThread: boolean;
/**
 * The main thread `MessagePort` which is `null` when the
 * current context is not the "main thread".
 * @type {MessagePort?}
 */
export const mainPort: MessagePort | null;
/**
 * A worker thread `BroadcastChannel` class.
 */
export class BroadcastChannel extends globalThis.BroadcastChannel {
}
/**
 * A worker thread `MessageChannel` class.
 */
export class MessageChannel extends globalThis.MessageChannel {
}
/**
 * A worker thread `MessagePort` class.
 */
export class MessagePort extends globalThis.MessagePort {
}
/**
 * The current unique thread ID.
 * @type {number}
 */
export const threadId: number;
/**
 * The parent `MessagePort` instance
 * @type {MessagePort?}
 */
export const parentPort: MessagePort | null;
/**
 * Transferred "worker data" when creating a new `Worker` instance.
 * @type {any?}
 */
export const workerData: any | null;
export class Pipe extends AsyncResource {
    /**
     * `Pipe` class constructor.
     * @param {Worker} worker
     * @ignore
     */
    constructor(worker: Worker);
    /**
     * `true` if the pipe is still reading, otherwise `false`.
     * @type {boolean}
     */
    get reading(): boolean;
    /**
     * Destroys the pipe
     */
    destroy(): void;
    #private;
}
/**
 * @typedef {{
 *   env?: object,
 *   stdin?: boolean = false,
 *   stdout?: boolean = false,
 *   stderr?: boolean = false,
 *   workerData?: any,
 *   transferList?: any[],
 *   eval?: boolean = false
 * }} WorkerOptions

/**
 * A worker thread that can communicate directly with a parent thread,
 * share environment data, and process streamed data.
 */
export class Worker extends EventEmitter {
    /**
     * `Worker` class constructor.
     * @param {string} filename
     * @param {WorkerOptions=} [options]
     */
    constructor(filename: string, options?: WorkerOptions | undefined);
    /**
     * Handles incoming worker messages.
     * @ignore
     * @param {MessageEvent} event
     */
    onWorkerMessage(event: MessageEvent): boolean;
    /**
     * Handles process environment change events
     * @ignore
     * @param {import('./process.js').ProcessEnvironmentEvent} event
     */
    onProcessEnvironmentEvent(event: import("./process.js").ProcessEnvironmentEvent): void;
    /**
     * The unique ID for this `Worker` thread instance.
     * @type {number}
     */
    get id(): number;
    get threadId(): number;
    /**
     * `true` after the worker has loaded and can receive application messages.
     * @type {boolean}
     */
    get online(): boolean;
    /**
     * A `Writable` standard input stream if `{ stdin: true }` was set when
     * creating this `Worker` instance.
     * @type {import('./stream.js').Writable?}
     */
    get stdin(): import("./stream.js").Writable | null;
    /**
     * A `Readable` standard output stream if `{ stdout: true }` was set when
     * creating this `Worker` instance.
     * @type {import('./stream.js').Readable?}
     */
    get stdout(): import("./stream.js").Readable | null;
    /**
     * A `Readable` standard error stream if `{ stderr: true }` was set when
     * creating this `Worker` instance.
     * @type {import('./stream.js').Readable?}
     */
    get stderr(): import("./stream.js").Readable | null;
    /**
     * Terminates the `Worker` instance
     */
    terminate(): void;
    postMessage(...args: any[]): void;
    #private;
}
declare namespace _default {
    export { Worker };
    export { isMainThread };
    export { parentPort };
    export { setEnvironmentData };
    export { getEnvironmentData };
    export { workerData };
    export { threadId };
    export { SHARE_ENV };
}
export default _default;
/**
 * /**
 * A worker thread that can communicate directly with a parent thread,
 * share environment data, and process streamed data.
 */
export type WorkerOptions = {
    env?: object;
    stdin?: boolean;
    stdout?: boolean;
    stderr?: boolean;
    workerData?: any;
    transferList?: any[];
    eval?: boolean;
};
import { AsyncResource } from './async/resource.js';
import { EventEmitter } from './events.js';
import { SHARE_ENV } from './worker_threads/init.js';
import init from './worker_threads/init.js';
export { SHARE_ENV, init };
