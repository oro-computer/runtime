/**
 * A typed container for optional options given to the `Database`
 * class constructor.
 *
 * @typedef {{
 *   version?: string | undefined
 * }} DatabaseOptions
 */
/**
 * A typed container for various optional options made to a `get()` function
 * on a `Database` instance.
 *
 * @typedef {{
 *   store?: string | undefined,
 *   stores?: string[] | undefined,
 *   count?: number | undefined
 * }} DatabaseGetOptions
 */
/**
 * A typed container for various optional options made to a `put()` function
 * on a `Database` instance.
 *
 * @typedef {{
 *   store?: string | undefined,
 *   stores?: string[] | undefined,
 *   durability?: 'strict' | 'relaxed' | undefined
 * }} DatabasePutOptions
 */
/**
 * A typed container for various optional options made to a `delete()` function
 * on a `Database` instance.
 *
 * @typedef {{
 *   store?: string | undefined,
 *   stores?: string[] | undefined
 * }} DatabaseDeleteOptions
 */
/**
 * A typed container for optional options given to the `Database`
 * class constructor.
 *
 * @typedef {{
 *   offset?: number | undefined,
 *   backlog?: number | undefined
 * }} DatabaseRequestQueueWaitOptions
 */
/**
 * A typed container for various optional options made to a `entries()` function
 * on a `Database` instance.
 *
 * @typedef {{
 *   store?: string | undefined,
 *   stores?: string[] | undefined
 * }} DatabaseEntriesOptions
 */
/**
 * A `DatabaseRequestQueueRequestConflict` callback function type.
 * @typedef {function(Event, DatabaseRequestQueueRequestConflict): any} DatabaseRequestQueueConflictResolutionCallback
 */
/**
 * Waits for an event of `eventType` to be dispatched on a given `EventTarget`.
 * @param {EventTarget} target
 * @param {string} eventType
 * @return {Promise<Event>}
 */
export function waitFor(target: EventTarget, eventType: string): Promise<Event>;
/**
 * Creates an opens a named `Database` instance.
 * @param {string} name
 * @param {?DatabaseOptions | undefined} [options]
 * @return {Promise<Database>}
 */
export function open(name: string, options?: (DatabaseOptions | undefined) | null): Promise<Database>;
/**
 * Complete deletes a named `Database` instance.
 * @param {string} name
 * @param {?DatabaseOptions|undefined} [options]
 */
export function drop(name: string, options?: (DatabaseOptions | undefined) | null): Promise<void>;
/**
 * A mapping of named `Database` instances that are currently opened
 * @type {Map<string, WeakRef<Database>>}
 */
export const opened: Map<string, WeakRef<Database>>;
/**
 * A container for conflict resolution for a `DatabaseRequestQueue` instance
 * `IDBRequest` instance.
 */
export class DatabaseRequestQueueRequestConflict {
    /**
     * `DatabaseRequestQueueRequestConflict` class constructor
     * @param {function(any): void)} resolve
     * @param {function(Error): void)} reject
     * @param {function(): void)} cleanup
     */
    constructor(resolve: any, reject: any, cleanup: any);
    /**
     * Called when a conflict is resolved.
     * @param {any} argument
     */
    resolve(argument?: any): void;
    /**
     * Called when a conflict is rejected
     * @param {Error} error
     */
    reject(error: Error): void;
    #private;
}
/**
 * An event dispatched on a `DatabaseRequestQueue`
 */
export class DatabaseRequestQueueEvent extends Event {
    /**
     * `DatabaseRequestQueueEvent` class constructor.
     * @param {string} type
     * @param {IDBRequest|IDBTransaction} request
     */
    constructor(type: string, request: IDBRequest | IDBTransaction);
    /**
     * A reference to the underlying request for this event.
     * @type {IDBRequest|IDBTransaction}
     */
    get request(): IDBRequest | IDBTransaction;
    #private;
}
/**
 * An event dispatched on a `Database`
 */
export class DatabaseEvent extends Event {
    /**
     * `DatabaseEvent` class constructor.
     * @param {string} type
     * @param {Database} database
     */
    constructor(type: string, database: Database);
    /**
     * A reference to the underlying database for this event.
     * @type {Database}
     */
    get database(): Database;
    #private;
}
/**
 * An error event dispatched on a `DatabaseRequestQueue`
 */
export class DatabaseRequestQueueErrorEvent extends ErrorEvent {
    /**
     * `DatabaseRequestQueueErrorEvent` class constructor.
     * @param {string} type
     * @param {IDBRequest|IDBTransaction} request
     * @param {{ error: Error, cause?: Error }} options
     */
    constructor(type: string, request: IDBRequest | IDBTransaction, options: {
        error: Error;
        cause?: Error;
    });
    /**
     * A reference to the underlying request for this error event.
     * @type {IDBRequest|IDBTransaction}
     */
    get request(): IDBRequest | IDBTransaction;
    #private;
}
/**
 * A container for various `IDBRequest` and `IDBTransaction` instances
 * occurring during the life cycles of a `Database` instance.
 */
export class DatabaseRequestQueue extends EventTarget {
    /**
     * Computed queue length
     * @type {number}
     */
    get length(): number;
    /**
     * Pushes an `IDBRequest` or `IDBTransaction onto the queue and returns a
     * `Promise` that resolves upon a 'success' or 'complete' event and rejects
     * upon an error' event.
     * @param {IDBRequest|IDBTransaction}
     * @param {?DatabaseRequestQueueConflictResolutionCallback} [conflictResolutionCallback]
     * @return {Promise}
     */
    push(request: any, conflictResolutionCallback?: DatabaseRequestQueueConflictResolutionCallback | null): Promise<any>;
    /**
     * Waits for all pending requests to complete. This function will throw when
     * an `IDBRequest` or `IDBTransaction` instance emits an 'error' event.
     * Callers of this function can optionally specify a maximum backlog to wait
     * for instead of waiting for all requests to finish.
     * @param {?DatabaseRequestQueueWaitOptions | undefined} [options]
     */
    wait(options?: (DatabaseRequestQueueWaitOptions | undefined) | null): Promise<any[]>;
    #private;
}
/**
 * An interface for reading from named databases backed by IndexedDB.
 */
export class Database extends EventTarget {
    [x: number]: () => import("../gc.js").Finalizer;
    /**
     * `Database` class constructor.
     * @param {string} name
     * @param {?DatabaseOptions | undefined} [options]
     */
    constructor(name: string, options?: (DatabaseOptions | undefined) | null);
    /**
     * `true` if the `Database` is currently opening, otherwise `false`.
     * A `Database` instance should not attempt to be opened if this property value
     * is `true`.
     * @type {boolean}
     */
    get opening(): boolean;
    /**
     * `true` if the `Database` instance was successfully opened such that the
     * internal `IDBDatabase` storage instance was created and can be referenced
     * on the `Database` instance, otherwise `false`.
     * @type {boolean}
     */
    get opened(): boolean;
    /**
     * `true` if the `Database` instance was closed or has not been opened such
     * that the internal `IDBDatabase` storage instance was not created or cannot
     * be referenced on the `Database` instance, otherwise `false`.
     * @type {boolean}
     */
    get closed(): boolean;
    /**
     * `true` if the `Database` is currently closing, otherwise `false`.
     * A `Database` instance should not attempt to be closed if this property value
     * is `true`.
     * @type {boolean}
     */
    get closing(): boolean;
    /**
     * The name of the `IDBDatabase` database. This value cannot be `null`.
     * @type {string}
     */
    get name(): string;
    /**
     * The version of the `IDBDatabase` database. This value may be `null`.
     * @type {?string}
     */
    get version(): string | null;
    /**
     * A reference to the `IDBDatabase`, if the `Database` instance was opened.
     * This value may ba `null`.
     * @type {?IDBDatabase}
     */
    get storage(): IDBDatabase | null;
    /**
     * Opens the `IDBDatabase` database optionally at a specific "version" if
     * one was given upon construction of the `Database` instance. This function
     * is not idempotent and will throw if the underlying `IDBDatabase` instance
     * was created successfully or is in the process of opening.
     * @return {Promise}
     */
    open(): Promise<any>;
    /**
     * Closes the `IDBDatabase` database storage, if opened. This function is not
     * idempotent and will throw if the underlying `IDBDatabase` instance is
     * already closed (not opened) or currently closing.
     * @return {Promise}
     */
    close(): Promise<any>;
    /**
     * Deletes entire `Database` instance and closes after successfully
     * delete storage.
     */
    drop(): Promise<void>;
    /**
     * Gets a "readonly" value by `key` in the `Database` object storage.
     * @param {string} key
     * @param {?DatabaseGetOptions|undefined} [options]
     * @return {Promise<object|object[]|null>}
     */
    get(key: string, options?: (DatabaseGetOptions | undefined) | null): Promise<object | object[] | null>;
    /**
     * Put a `value` at `key`, updating if it already exists, otherwise
     * "inserting" it into the `Database` instance.
     * @param {string} key
     * @param {any} value
     * @param {?DatabasePutOptions|undefined} [options]
     * @return {Promise}
     */
    put(key: string, value: any, options?: (DatabasePutOptions | undefined) | null): Promise<any>;
    /**
     * Inserts a new `value` at `key`. This function throws if a value at `key`
     * already exists.
     * @param {string} key
     * @param {any} value
     * @param {?DatabasePutOptions|undefined} [options]
     * @return {Promise}
     */
    insert(key: string, value: any, options?: (DatabasePutOptions | undefined) | null): Promise<any>;
    /**
     * Update a `value` at `key`, updating if it already exists, otherwise
     * "inserting" it into the `Database` instance.
     * @param {string} key
     * @param {any} value
     * @param {?DatabasePutOptions|undefined} [options]
     * @return {Promise}
     */
    update(key: string, value: any, options?: (DatabasePutOptions | undefined) | null): Promise<any>;
    /**
     * Delete a value at `key`.
     * @param {string} key
     * @param {?DatabaseDeleteOptions|undefined} [options]
     * @return {Promise}
     */
    delete(key: string, options?: (DatabaseDeleteOptions | undefined) | null): Promise<any>;
    /**
     * Gets a "readonly" value by `key` in the `Database` object storage.
     * @param {?DatabaseEntriesOptions|undefined} [options]
     * @return {Promise<object|object[]|null>}
     */
    entries(options?: (DatabaseEntriesOptions | undefined) | null): Promise<object | object[] | null>;
    #private;
}
declare namespace _default {
    export { Database };
    export { open };
    export { drop };
}
export default _default;
/**
 * A typed container for optional options given to the `Database`
 * class constructor.
 */
export type DatabaseOptions = {
    version?: string | undefined;
};
/**
 * A typed container for various optional options made to a `get()` function
 * on a `Database` instance.
 */
export type DatabaseGetOptions = {
    store?: string | undefined;
    stores?: string[] | undefined;
    count?: number | undefined;
};
/**
 * A typed container for various optional options made to a `put()` function
 * on a `Database` instance.
 */
export type DatabasePutOptions = {
    store?: string | undefined;
    stores?: string[] | undefined;
    durability?: "strict" | "relaxed" | undefined;
};
/**
 * A typed container for various optional options made to a `delete()` function
 * on a `Database` instance.
 */
export type DatabaseDeleteOptions = {
    store?: string | undefined;
    stores?: string[] | undefined;
};
/**
 * A typed container for optional options given to the `Database`
 * class constructor.
 */
export type DatabaseRequestQueueWaitOptions = {
    offset?: number | undefined;
    backlog?: number | undefined;
};
/**
 * A typed container for various optional options made to a `entries()` function
 * on a `Database` instance.
 */
export type DatabaseEntriesOptions = {
    store?: string | undefined;
    stores?: string[] | undefined;
};
/**
 * A `DatabaseRequestQueueRequestConflict` callback function type.
 */
export type DatabaseRequestQueueConflictResolutionCallback = (arg0: Event, arg1: DatabaseRequestQueueRequestConflict) => any;
