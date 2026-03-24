/**
 * A container for a file system path watcher.
 *
 * @event Watcher#change
 * @type {(eventType: string, filename: (string|Buffer)) => void}
 * Emitted when a file change is detected.
 *
 * @event Watcher#error
 * @type {(err: Error) => void}
 * Emitted when an error occurs during watching.
 *
 * @event Watcher#close
 * @type {() => void}
 * Emitted when the watcher is closed.
 */
export class Watcher extends EventEmitter {
    [x: number]: (options: any) => import("../gc.js").Finalizer;
    /**
     * `Watcher` class constructor.
     * @ignore
     * @param {string} path
     * @param {object=} [options]
     * @param {AbortSignal=} [options.signal]
     * @param {string|number|bigint=} [options.id]
     * @param {string=} [options.encoding = 'utf8']
     */
    constructor(path: string, options?: object | undefined);
    /**
     * The underlying `fs.Watcher` resource id.
     * @ignore
     * @type {string}
     */
    id: string;
    /**
     * The path the `fs.Watcher` is watching
     * @type {string}
     */
    path: string;
    /**
     * `true` if closed, otherwise `false`.
     * @type {boolean}
     */
    closed: boolean;
    /**
     * `true` if aborted, otherwise `false`.
     * @type {boolean}
     */
    aborted: boolean;
    /**
     * The encoding of the `filename`
     * @type {'utf8'|'buffer'}
     */
    encoding: "utf8" | "buffer";
    /**
     * An `AbortController` `AbortSignal` for async aborts.
     * @type {AbortSignal?}
     */
    signal: AbortSignal | null;
    /**
     * Internal abort event handler reference for cleanup.
     * @ignore
     * @type {Function|null}
     */
    abortHandler: Function | null;
    /**
     * Internal event listener cancellation.
     * @ignore
     * @type {function?}
     */
    stopListening: Function | null;
    /**
     * Internal starter for watcher.
     * @ignore
     */
    start(): Promise<void>;
    /**
     * Closes watcher and stops listening for changes.
     * @return {Promise}
     */
    close(): Promise<any>;
    /**
     * Implements the `AsyncIterator` (`Symbol.asyncIterator`) interface.
     * @ignore
     * @return {AsyncIterator<{ eventType: string, filename: string }>}
     */
    [Symbol.asyncIterator](): AsyncIterator<{
        eventType: string;
        filename: string;
    }>;
    #private;
}
export default Watcher;
import { EventEmitter } from '../events.js';
