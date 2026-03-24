/**
 * @param {string} value
 * @returns {boolean}
 */
export function isRuntimeSpecifier(value: string): boolean;
/**
 * @param {string} value
 * @returns {boolean}
 */
export function isRuntimeURL(value: string): boolean;
/**
 * Rewrites the provided specifier or URL so it uses the preferred runtime scheme.
 * When `options.url === true`, the `://` delimiter is assumed. Otherwise `:` is used.
 * @param {string} value
 * @param {{ url?: boolean, scheme?: string }} [options]
 * @returns {string}
 */
export function withPreferredRuntimeScheme(value: string, { url, scheme }?: {
    url?: boolean;
    scheme?: string;
}): string;
/**
 * Builds a runtime origin string (e.g., `oro://com.example.app`).
 * @param {string} bundleIdentifier
 * @param {{ scheme?: string }} [options]
 * @returns {string}
 */
export function runtimeOrigin(bundleIdentifier: string, { scheme }?: {
    scheme?: string;
}): string;
/**
 * Normalizes a scheme string (with/without the trailing colon) to the runtime value.
 * Returns an empty string for unrecognised schemes.
 * @param {string} scheme
 * @returns {string}
 */
export function normalizeRuntimeScheme(scheme: string): string;
export const PRIMARY_SCHEME: "oro";
export const RUNTIME_SCHEMES: readonly string[];
