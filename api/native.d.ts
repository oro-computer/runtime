/**
 * @typedef {object} NativeRequestOptions
 * @property {AbortSignal} [signal] Abort native work and discard a late result.
 */
/**
 * Invokes a narrowly named native operation with a JSON object payload. Requests
 * require a foreground interactive document, expire after 90 seconds and share
 * native capacity limits. Results are limited to 512 KiB. The extension must
 * implement its own feature authorization; trusted origin is not server IAM.
 * Unregistered extensions and other platforms reject with ENOSYS.
 * @param {string} extension Configured extension name.
 * @param {string} action Operation name.
 * @param {object} [payload]
 * @param {NativeRequestOptions} [options]
 * @returns {Promise<any>}
 */
export function request(extension: string, action: string, payload?: object, options?: NativeRequestOptions): Promise<any>;
declare namespace _default {
    export { request };
}
export default _default;
export type NativeRequestOptions = {
    /**
     * Abort native work and discard a late result.
     */
    signal?: AbortSignal;
};
