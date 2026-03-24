/**
 * Normalizes input as an IPv4 address string
 * @param {string|object|string[]|Uint8Array} input
 * @return {string}
 */
export function normalizeIPv4(input: string | object | string[] | Uint8Array): string;
/**
 * Determines if an input `string` is in IP address version 4 format.
 * @param {string|object|string[]|Uint8Array} input
 * @return {boolean}
 */
export function isIPv4(input: string | object | string[] | Uint8Array): boolean;
declare namespace _default {
    export { normalizeIPv4 };
    export { isIPv4 };
}
export default _default;
