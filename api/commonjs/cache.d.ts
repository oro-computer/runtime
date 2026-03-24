/**
 * @typedef {{
 *   types?: object,
 *   loader?: import('./loader.js').Loader
 * }} CacheOptions
 */
export const CACHE_CHANNEL_MESSAGE_ID: "id";
export const CACHE_CHANNEL_MESSAGE_REPLICATE: "replicate";
/**
 * @typedef {{
 *   name: string
 * }} StorageOptions
 */
/**
 * An storage context object with persistence and durability
 * for service worker storages.
 */
export class Storage extends EventTarget {
    /**
     * Maximum entries that will be restored from storage into the context object.
     * @type {number}
     */
    static MAX_CONTEXT_ENTRIES: number;
    /**
     * A mapping of known `Storage` instances.
     * @type {Map<string, Storage>}
     */
    static instances: Map<string, Storage>;
    /**
     * Opens an storage for a particular name.
     * @param {StorageOptions} options
     * @return {Promise<Storage>}
     */
    static open(options: StorageOptions): Promise<Storage>;
    /**
     * `Storage` class constructor
     * @ignore
     * @param {StorageOptions} options
     */
    constructor(options: StorageOptions);
    /**
     * A reference to the currently opened storage database.
     * @type {import('../internal/database.js').Database}
     */
    get database(): import("../internal/database.js").Database;
    /**
     * `true` if the storage is opened, otherwise `false`.
     * @type {boolean}
     */
    get opened(): boolean;
    /**
     * `true` if the storage is opening, otherwise `false`.
     * @type {boolean}
     */
    get opening(): boolean;
    /**
     * A proxied object for reading and writing storage state.
     * Values written to this object must be cloneable with respect to the
     * structured clone algorithm.
     * @see {https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Structured_clone_algorithm}
     * @type {Proxy<object>}
     */
    get context(): ProxyConstructor;
    /**
     * The current storage name. This value is also used as the
     * internal database name.
     * @type {string}
     */
    get name(): string;
    /**
     * A promise that resolves when the storage is opened.
     * @type {Promise?}
     */
    get ready(): Promise<any> | null;
    /**
     * @ignore
     * @param {Promise} promise
     */
    forwardRequest(promise: Promise<any>): Promise<any>;
    /**
     * Resets the current storage to an empty state.
     */
    reset(): Promise<void>;
    /**
     * Synchronizes database entries into the storage context.
     */
    sync(options?: any): Promise<void>;
    /**
     * Opens the storage.
     * @ignore
     */
    open(options?: any): Promise<any>;
    /**
     * Closes the storage database, purging existing state.
     * @ignore
     */
    close(): Promise<void>;
    #private;
}
/**
 * A container for `Snapshot` data storage.
 */
export class SnapshotData {
    /**
     * `SnapshotData` class constructor.
     * @param {object=} [data]
     */
    constructor(data?: object | undefined);
    toJSON: () => this;
    [Symbol.toStringTag]: string;
}
/**
 * A container for storing a snapshot of the cache data.
 */
export class Snapshot {
    /**
     * @type {typeof SnapshotData}
     */
    static Data: typeof SnapshotData;
    /**
     * A reference to the snapshot data.
     * @type {Snapshot.Data}
     */
    get data(): typeof SnapshotData;
    /**
     * @ignore
     * @return {object}
     */
    toJSON(): object;
    #private;
}
/**
 * An interface for managing and performing operations on a collection
 * of `Cache` objects.
 */
export class CacheCollection {
    /**
     * `CacheCollection` class constructor.
     * @ignore
     * @param {Cache[]|Record<string, Cache>=} [collection]
     */
    constructor(collection?: (Cache[] | Record<string, Cache>) | undefined);
    /**
     * Adds a `Cache` instance to the collection.
     * @param {string|Cache} name
     * @param {Cache=} [cache]
     * @param {boolean}
     */
    add(name: string | Cache, cache?: Cache | undefined): any;
    /**
     * Calls a method on each `Cache` object in the collection.
     * @param {string} method
     * @param {...any} args
     * @return {Promise<Record<string,any>>}
     */
    call(method: string, ...args: any[]): Promise<Record<string, any>>;
    restore(): Promise<Record<string, any>>;
    reset(): Promise<Record<string, any>>;
    snapshot(): Promise<Record<string, any>>;
    get(key: any): Promise<Record<string, any>>;
    delete(key: any): Promise<Record<string, any>>;
    keys(key: any): Promise<Record<string, any>>;
    values(key: any): Promise<Record<string, any>>;
    clear(key: any): Promise<Record<string, any>>;
}
/**
 * A container for a shared cache that lives for the life time of
 * application execution. Updates to this storage are replicated to other
 * instances in the application context, including windows and workers.
 */
export class Cache {
    [x: number]: () => gc.Finalizer;
    /**
     * A globally shared type mapping for the cache to use when
     * derserializing a value.
     * @type {Map<string, function>}
     */
    static types: Map<string, Function>;
    /**
     * A globally shared cache store keyed by cache name. This is useful so
     * when multiple instances of a `Cache` are created, they can share the
     * same data store, reducing duplications.
     * @type {Record<string, Map<string, object>}
     */
    static shared: Record<string, Map<string, object>>;
    /**
     * A mapping of opened `Storage` instances.
     * @type {Map<string, Storage>}
     */
    static storages: Map<string, Storage>;
    /**
     * The `Cache.Snapshot` class.
     * @type {typeof Snapshot}
     */
    static Snapshot: typeof Snapshot;
    /**
     * The `Cache.Storage` class
     * @type {typeof Storage}
     */
    static Storage: typeof Storage;
    /**
     * Creates a snapshot of the current cache which can be serialized and
     * stored in persistent storage.
     * @return {Snapshot}
     */
    static snapshot(): Snapshot;
    /**
     * Restore caches from persistent storage.
     * @param {string[]} names
     * @return {Promise}
     */
    static restore(names: string[]): Promise<any>;
    /**
     * `Cache` class constructor.
     * @param {string} name
     * @param {CacheOptions=} [options]
     */
    constructor(name: string, options?: CacheOptions | undefined);
    /**
     * The unique ID for this cache.
     * @type {string}
     */
    get id(): string;
    /**
     * The loader associated with this cache.
     * @type {import('./loader.js').Loader}
     */
    get loader(): import("./loader.js").Loader;
    /**
     * A reference to the persisted storage.
     * @type {Storage}
     */
    get storage(): Storage;
    /**
     * The cache name
     * @type {string}
     */
    get name(): string;
    /**
     * The underlying cache data map.
     * @type {Map}
     */
    get data(): Map<any, any>;
    /**
     * The broadcast channel associated with this cach.
     * @type {BroadcastChannel}
     */
    get channel(): BroadcastChannel;
    /**
     * The size of the cache.
     * @type {number}
     */
    get size(): number;
    /**
     * @type {Map}
     */
    get types(): Map<any, any>;
    /**
     * Resets the cache map and persisted storage.
     */
    reset(): Promise<void>;
    /**
     * Restores cache data from storage.
     */
    restore(): Promise<void>;
    /**
     * Creates a snapshot of the current cache which can be serialized and
     * stored in persistent storage.
     * @return {Snapshot.Data}
     */
    snapshot(): typeof SnapshotData;
    /**
     * Get a value at `key`.
     * @param {string} key
     * @return {object|undefined}
     */
    get(key: string): object | undefined;
    /**
     * Set `value` at `key`.
     * @param {string} key
     * @param {object} value
     * @return {Cache}
     */
    set(key: string, value: object): Cache;
    /**
     * Returns `true` if `key` is in cache, otherwise `false`.
     * @param {string}
     * @return {boolean}
     */
    has(key: any): boolean;
    /**
     * Delete a value at `key`.
     * This does not replicate to shared caches.
     * @param {string} key
     * @return {boolean}
     */
    delete(key: string): boolean;
    /**
     * Returns an iterator for all cache keys.
     * @return {object}
     */
    keys(): object;
    /**
     * Returns an iterator for all cache values.
     * @return {object}
     */
    values(): object;
    /**
     * Returns an iterator for all cache entries.
     * @return {object}
     */
    entries(): object;
    /**
     * Clears all entries in the cache.
     * This does not replicate to shared caches.
     * @return {undefined}
     */
    clear(): undefined;
    /**
     * Enumerates entries in map calling `callback(value, key
     * @param {function(object, string, Cache): any} callback
     */
    forEach(callback: (arg0: object, arg1: string, arg2: Cache) => any): void;
    /**
     * Broadcasts a replication to other shared caches.
     */
    replicate(): this;
    /**
     * Destroys the cache. This function stops the broadcast channel and removes
     * and listeners
     */
    destroy(): void;
    /**
     * @ignore
     */
    [Symbol.iterator](): any;
    #private;
}
export default Cache;
export type CacheOptions = {
    types?: object;
    loader?: import("./loader.js").Loader;
};
export type StorageOptions = {
    name: string;
};
