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
/**
 * @typedef {object} SecureStorageStatus
 * @property {boolean} available Whether the Android Keystore metadata query succeeded.
 * @property {boolean} keyPresent Whether an existing runtime key was found.
 * @property {'unknown'|'not-created'|'key-missing'|'software'|'hardware'|'tee'|'strongbox'} protection
 * @property {boolean} requiresRecovery Encrypted values exist without their key.
 * @property {boolean|null} userAuthenticationRequired Existing key policy, or null when unknown.
 * @property {'not-probed'} readability No encrypted values were decrypted by this inspection.
 */
/**
 * Inspects existing Android Keystore protection without creating a key, exporting
 * key material, or decrypting application values. A present key does not prove
 * every stored value is readable. Other platforms reject with ENOSYS.
 * @returns {Promise<SecureStorageStatus>}
 */
export function getStatus(): Promise<SecureStorageStatus>;
export default api;
export type SecureStorageStatus = {
    /**
     * Whether the Android Keystore metadata query succeeded.
     */
    available: boolean;
    /**
     * Whether an existing runtime key was found.
     */
    keyPresent: boolean;
    protection: "unknown" | "not-created" | "key-missing" | "software" | "hardware" | "tee" | "strongbox";
    /**
     * Encrypted values exist without their key.
     */
    requiresRecovery: boolean;
    /**
     * Existing key policy, or null when unknown.
     */
    userAuthenticationRequired: boolean | null;
    /**
     * No encrypted values were decrypted by this inspection.
     */
    readability: "not-probed";
};
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
    getStatus: typeof getStatus;
};
import { Buffer } from './buffer.js';
/**
 * @typedef {Object} SecureStorageModule
 * @property {typeof setItem} setItem
 * @property {typeof getItem} getItem
 * @property {typeof removeItem} removeItem
 * @property {typeof clear} clear
 * @property {typeof keys} keys
 * @property {typeof getStatus} getStatus
 */
/** @type {SecureStorageModule} */
declare const api: SecureStorageModule;
