/**
 * Stores a value inside secure storage for the given key.
 * @param {string} key
 * @param {string|Uint8Array|ArrayBuffer|Buffer} value
 * @param {SetItemOptions} [options]
 * @returns {Promise<void>}
 */
export function setItem(key: string, value: string | Uint8Array | ArrayBuffer | Buffer, options?: SetItemOptions): Promise<void>;
/**
 * Retrieves a previously stored value.
 * @param {string} key
 * @param {GetItemOptions} [options]
 * @returns {Promise<string|Uint8Array|null>}
 */
export function getItem(key: string, options?: GetItemOptions): Promise<string | Uint8Array | null>;
/**
 * Removes a single key from secure storage.
 * @param {string} key
 * @param {SecureStorageOptions} [options]
 * @returns {Promise<void>}
 */
export function removeItem(key: string, options?: SecureStorageOptions): Promise<void>;
/**
 * Clears all keys for the provided scope (or default scope).
 * @param {SecureStorageOptions} [options]
 * @returns {Promise<void>}
 */
export function clear(options?: SecureStorageOptions): Promise<void>;
/**
 * Lists the stored keys for the provided scope.
 * @param {SecureStorageOptions} [options]
 * @returns {Promise<string[]>}
 */
export function keys(options?: SecureStorageOptions): Promise<string[]>;
export default api;
export type SecureStorageOptions = {
    /**
     * Origin string identifying the storage namespace.
     */
    scope?: string | null;
};
export type SetItemOptions = SecureStorageOptions & {
    encoding?: "utf8" | "base64" | "hex";
};
export type GetItemOptions = SecureStorageOptions & {
    encoding?: "utf8" | "base64" | "hex" | "buffer";
};
export type SecureStorageModule = {
    setItem: typeof setItem;
    getItem: typeof getItem;
    removeItem: typeof removeItem;
    clear: typeof clear;
    keys: typeof keys;
};
import { Buffer } from './buffer.js';
/**
 * @typedef {Object} SecureStorageModule
 * @property {typeof setItem} setItem
 * @property {typeof getItem} getItem
 * @property {typeof removeItem} removeItem
 * @property {typeof clear} clear
 * @property {typeof keys} keys
 */
/** @type {SecureStorageModule} */
declare const api: SecureStorageModule;
