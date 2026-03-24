/**
 * @typedef {object} SemVer
 * @property {number} major
 * @property {number} minor
 * @property {number} patch
 * @property {string[]} prerelease
 * @property {string[]} build
 * @property {string} version Canonical string form
 */
/**
 * Parse a semantic version string into its structured representation.
 * Returns `null` when the input is not a valid SemVer 2.0.0 version.
 *
 * @param {string} version
 * @returns {SemVer | null}
 */
export function parse(version: string): SemVer | null;
/**
 * Returns the canonical version string when `version` is valid, otherwise `null`.
 *
 * @param {string} version
 * @returns {string | null}
 */
export function valid(version: string): string | null;
/**
 * Compare two semantic versions.
 *
 * @param {string} a
 * @param {string} b
 * @returns {-1|0|1}
 */
export function compare(a: string, b: string): -1 | 0 | 1;
/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function eq(a: string, b: string): boolean;
/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function neq(a: string, b: string): boolean;
/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function lt(a: string, b: string): boolean;
/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function lte(a: string, b: string): boolean;
/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function gt(a: string, b: string): boolean;
/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function gte(a: string, b: string): boolean;
/**
 * Test whether a version satisfies a range expression.
 *
 * Supported range grammar includes:
 * - Simple comparators (`<`, `<=`, `>`, `>=`, `=`, or bare versions)
 * - Hyphen ranges: `1.2.3 - 2.3.4`
 * - Wildcard ranges: `1.x`, `1.2.x`, `1`, `1.2`, `*`
 * - Tilde ranges: `~1.2.3`, `~1.2`, `~1`
 * - Caret ranges: `^1.2.3`, `^0.2.3`, `^0.0.3`
 * - `||` for OR between sets of comparators
 *
 * @param {string} version
 * @param {string} range
 * @returns {boolean}
 */
export function satisfies(version: string, range: string): boolean;
/**
 * Increment a version according to the given release type.
 *
 * Release types:
 * - 'major', 'minor', 'patch'
 * - 'premajor', 'preminor', 'prepatch', 'prerelease'
 *
 * When a pre* release type is used, `preid` (when provided) becomes the
 * pre-release identifier (e.g., `beta` -> `1.2.3-beta.0`). If omitted,
 * `rc` is used by default.
 *
 * Returns `null` when the input version is invalid.
 *
 * @param {string} version
 * @param {'major'|'minor'|'patch'|'premajor'|'preminor'|'prepatch'|'prerelease'} release
 * @param {string} [preid]
 * @returns {string | null}
 */
export function inc(version: string, release: "major" | "minor" | "patch" | "premajor" | "preminor" | "prepatch" | "prerelease", preid?: string): string | null;
/**
 * Clean a version by returning its canonical form or `null` when invalid.
 *
 * @param {string} version
 * @returns {string | null}
 */
export function clean(version: string): string | null;
/**
 * Extract the major component of a version or `NaN` when invalid.
 * @param {string} version
 * @returns {number}
 */
export function major(version: string): number;
/**
 * Extract the minor component of a version or `NaN` when invalid.
 * @param {string} version
 * @returns {number}
 */
export function minor(version: string): number;
/**
 * Extract the patch component of a version or `NaN` when invalid.
 * @param {string} version
 * @returns {number}
 */
export function patch(version: string): number;
/**
 * Returns the prerelease components of a version or `null` when invalid
 * or when the version has no prerelease identifiers.
 *
 * @param {string} version
 * @returns {string[] | null}
 */
export function prerelease(version: string): string[] | null;
/**
 * Validate and normalize a range expression. Returns the normalized
 * comparator-based range or `null` when invalid.
 *
 * @param {string} range
 * @returns {string | null}
 */
export function validRange(range: string): string | null;
export default api;
export type SemVer = {
    major: number;
    minor: number;
    patch: number;
    prerelease: string[];
    build: string[];
    /**
     * Canonical string form
     */
    version: string;
};
declare namespace api {
    export { parse };
    export { valid };
    export { clean };
    export { compare };
    export { eq };
    export { neq };
    export { lt };
    export { lte };
    export { gt };
    export { gte };
    export { satisfies };
    export { validRange };
    export { major };
    export { minor };
    export { patch };
    export { prerelease };
    export { inc };
}
