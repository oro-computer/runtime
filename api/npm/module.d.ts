/**
 * @typedef {{
 * package: Package
 * origin: string,
 * type: 'commonjs' | 'module',
 * url: string
 * }} ModuleResolution
 */
/**
 * Resolves an NPM module for a given `specifier` and an optional `origin`.
 * @param {string|URL} specifier
 * @param {string|URL=} [origin]
 * @param {{ prefix?: string, type?: 'commonjs' | 'module' }} [options]
 * @return {ModuleResolution|null}
 */
export function resolve(specifier: string | URL, origin?: (string | URL) | undefined, options?: {
    prefix?: string;
    type?: "commonjs" | "module";
}): ModuleResolution | null;
declare namespace _default {
    export { resolve };
}
export default _default;
export type ModuleResolution = {
    package: Package;
    origin: string;
    type: "commonjs" | "module";
    url: string;
};
import { Package } from '../commonjs/package.js';
