/**
 * Get cookies for a URL as a `Cookie` header value ("a=b; c=d").
 *
 * @param {string} url
 * @returns {Promise<{ value: string }>}
 */
export function get(url: string): Promise<{
    value: string;
}>;
/**
 * Set a cookie for a URL from a `Set-Cookie` header value.
 *
 * @param {string} url
 * @param {string} cookie - a Set-Cookie header value
 * @returns {Promise<{ ok: boolean }>}
 */
export function set(url: string, cookie: string): Promise<{
    ok: boolean;
}>;
/**
 * Remove cookies matching `name` for a URL.
 *
 * @param {string} url
 * @param {string} name
 * @returns {Promise<{ ok: boolean }>}
 */
export function remove(url: string, name: string): Promise<{
    ok: boolean;
}>;
/**
 * Clear all cookies in the current WebView data store.
 *
 * @returns {Promise<{ ok: boolean }>}
 */
export function clear(): Promise<{
    ok: boolean;
}>;
declare namespace _default {
    export { get };
    export { set };
    export { remove };
    export { clear };
}
export default _default;
