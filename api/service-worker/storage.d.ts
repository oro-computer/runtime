/**
 * A factory for creating storage interfaces.
 * @param {'memoryStorage'|'localStorage'|'sessionStorage'} type
 * @return {Promise<Storage>}
 */
export function createStorageInterface(type: "memoryStorage" | "localStorage" | "sessionStorage"): Promise<Storage>;
/**
 * @typedef {{ done: boolean, value: string | undefined }} IndexIteratorResult
 */
/**
 * An iterator interface for an `Index` instance.
 */
export class IndexIterator {
    /**
     * `IndexIterator` class constructor.
     * @ignore
     * @param {Index} index
     */
    constructor(index: Index);
    /**
     * `true` if the iterator is "done", otherwise `false`.
     * @type {boolean}
     */
    get done(): boolean;
    /**
     * Returns the next `IndexIteratorResult`.
     * @return {IndexIteratorResult}
     */
    next(): IndexIteratorResult;
    /**
     * Mark `IndexIterator` as "done"
     * @return {IndexIteratorResult}
     */
    return(): IndexIteratorResult;
    #private;
}
/**
 * A container used by the `Provider` to index keys and values
 */
export class Index {
    /**
     * A reference to the keys in this index.
     * @type {string[]}
     */
    get keys(): string[];
    /**
     * A reference to the values in this index.
     * @type {string[]}
     */
    get values(): string[];
    /**
     * The number of entries in this index.
     * @type {number}
     */
    get length(): number;
    /**
     * Returns the key at a given `index`, if it exists otherwise `null`.
     * @param {number} index}
     * @return {string?}
     */
    key(index: number): string | null;
    /**
     * Returns the value at a given `index`, if it exists otherwise `null`.
     * @param {number} index}
     * @return {string?}
     */
    value(index: number): string | null;
    /**
     * Inserts a value in the index.
     * @param {string} key
     * @param {string} value
     */
    insert(key: string, value: string): void;
    /**
     * Computes the index of a key in this index.
     * @param {string} key
     * @return {number}
     */
    indexOf(key: string): number;
    /**
     * Clears all keys and values in the index.
     */
    clear(): void;
    /**
     * Returns an entry at `index` if it exists, otherwise `null`.
     * @param {number} index
     * @return {string[]|null}
     */
    entry(index: number): string[] | null;
    /**
     * Removes entries at a given `index`.
     * @param {number} index
     * @return {boolean}
     */
    remove(index: number): boolean;
    /**
     * Returns an array of computed entries in this index.
     * @return {IndexIterator}
     */
    entries(): IndexIterator;
    /**
     * @ignore
     * @return {IndexIterator}
     */
    [Symbol.iterator](): IndexIterator;
    #private;
}
/**
 * A base class for a storage provider.
 */
export class Provider {
    /**
     * An error currently associated with the provider, likely from an
     * async operation.
     * @type {Error?}
     */
    get error(): Error | null;
    /**
     * A promise that resolves when the provider is ready.
     * @type {Promise}
     */
    get ready(): Promise<any>;
    /**
     * A reference the service worker storage ID, which is the service worker
     * registration ID.
     * @type {string}
     * @throws DOMException
     */
    get id(): string;
    /**
     * A reference to the provider `Index`
     * @type {Index}
     * @throws DOMException
     */
    get index(): Index;
    /**
     * The number of entries in the provider.
     * @type {number}
     * @throws DOMException
     */
    get length(): number;
    /**
     * Returns `true` if the provider has a value for a given `key`.
     * @param {string} key}
     * @return {boolean}
     * @throws DOMException
     */
    has(key: string): boolean;
    /**
     * Get a value by `key`.
     * @param {string} key
     * @return {string?}
     * @throws DOMException
     */
    get(key: string): string | null;
    /**
     * Sets a `value` by `key`
     * @param {string} key
     * @param {string} value
     * @throws DOMException
     */
    set(key: string, value: string): void;
    /**
     * Removes a value by `key`.
     * @param {string} key
     * @return {boolean}
     * @throws DOMException
     */
    remove(key: string): boolean;
    /**
     * Clear all keys and values.
     * @throws DOMException
     */
    clear(): void;
    /**
     * The keys in the provider index.
     * @return {string[]}
     * @throws DOMException
     */
    keys(): string[];
    /**
     * The values in the provider index.
     * @return {string[]}
     * @throws DOMException
     */
    values(): string[];
    /**
     * Returns the key at a given `index`
     * @param {number} index
     * @return {string|null}
     * @throws DOMException
     */
    key(index: number): string | null;
    /**
     * Loads the internal index with keys and values.
     * @return {Promise}
     */
    load(): Promise<any>;
    #private;
}
/**
 * An in-memory storage provider. It just used the built-in provider `Index`
 * for storing key-value entries.
 */
export class MemoryStorageProvider extends Provider {
}
/**
 * A session storage provider that persists for the runtime of the
 * application and through service worker restarts.
 */
export class SessionStorageProvider extends Provider {
    /**
     * Remove a value by `key`.
     * @param {string} key
     * @return {string?}
     * @throws DOMException
     * @throws NotFoundError
     */
    remove(key: string): string | null;
}
/**
 * A local storage provider that persists until the data is cleared.
 */
export class LocalStorageProvider extends Provider {
}
/**
 * A generic interface for storage implementations
 */
export class Storage {
    /**
     * A factory for creating a `Storage` instance that is backed
     * by a storage provider. Extending classes should define a `Provider`
     * class that is statically available on the extended `Storage` class.
     * @param {symbol} token
     * @return {Promise<Proxy<Storage>>}
     */
    static create(token: symbol): Promise<ProxyConstructor>;
    /**
     * `Storage` class constructor.
     * @ignore
     * @param {symbol} token
     * @param {Provider} provider
     */
    constructor(token: symbol, provider: Provider);
    /**
     * A readonly reference to the storage provider.
     * @type {Provider}
     */
    get provider(): Provider;
    /**
     * The number of entries in the storage.
     * @type {number}
     */
    get length(): number;
    /**
     * Returns `true` if the storage has a value for a given `key`.
     * @param {string} key
     * @return {boolean}
     * @throws TypeError
     */
    hasItem(key: string, ...args: any[]): boolean;
    /**
     * Clears the storage of all entries
     */
    clear(): void;
    /**
     * Returns the key at a given `index`
     * @param {number} index
     * @return {string|null}
     */
    key(index: number, ...args: any[]): string | null;
    /**
     * Get a storage value item for a given `key`.
     * @param {string} key
     * @return {string|null}
     */
    getItem(key: string, ...args: any[]): string | null;
    /**
     * Removes a storage value entry for a given `key`.
     * @param {string}
     * @return {boolean}
     */
    removeItem(key: any, ...args: any[]): boolean;
    /**
     * Sets a storage item `value` for a given `key`.
     * @param {string} key
     * @param {string} value
     */
    setItem(key: string, value: string, ...args: any[]): void;
    /**
     * @ignore
     */
    get [Symbol.toStringTag](): string;
    #private;
}
/**
 * An in-memory `Storage` interface.
 */
export class MemoryStorage extends Storage {
    static Provider: typeof MemoryStorageProvider;
}
/**
 * A locally persisted `Storage` interface.
 */
export class LocalStorage extends Storage {
    static Provider: typeof LocalStorageProvider;
}
/**
 * A session `Storage` interface.
 */
export class SessionStorage extends Storage {
    static Provider: typeof SessionStorageProvider;
}
declare namespace _default {
    export { Storage };
    export { LocalStorage };
    export { MemoryStorage };
    export { SessionStorage };
    export { createStorageInterface };
}
export default _default;
export type IndexIteratorResult = {
    done: boolean;
    value: string | undefined;
};
