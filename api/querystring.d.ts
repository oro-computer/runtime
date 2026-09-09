export function unescapeBuffer(s: any, decodeSpaces: any): any;
/**
 * Decodes percent escapes, replacing malformed UTF-8 sequences.
 * @param {string} s
 * @param {boolean} [decodeSpaces]
 * @returns {string}
 */
export function unescape(s: string, decodeSpaces?: boolean): string;
export function escape(str: any): any;
export function stringify(obj: any, sep: any, eq: any, options: any): string;
/**
 * @typedef {object} ParseOptions
 * @property {number} [maxKeys]
 * @property {(value: string) => string} [decodeURIComponent]
 */
/**
 * Parses key/value pairs separated by the supplied delimiters.
 * @param {string} qs
 * @param {string} [sep]
 * @param {string} [eq]
 * @param {ParseOptions} [options]
 * @returns {Record<string, string|string[]>}
 */
export function parse(qs: string, sep?: string, eq?: string, options?: ParseOptions): Record<string, string | string[]>;
/**
 * @typedef {object} ParseOptions
 * @property {number} [maxKeys]
 * @property {(value: string) => string} [decodeURIComponent]
 */
/**
 * Parses key/value pairs separated by the supplied delimiters.
 * @param {string} qs
 * @param {string} [sep]
 * @param {string} [eq]
 * @param {ParseOptions} [options]
 * @returns {Record<string, string|string[]>}
 */
export function decode(qs: string, sep?: string, eq?: string, options?: ParseOptions): Record<string, string | string[]>;
export function encode(obj: any, sep: any, eq: any, options: any): string;
declare namespace _default {
    export { decode };
    export { encode };
    export { parse };
    export { stringify };
    export { escape };
    export { unescape };
}
export default _default;
export type ParseOptions = {
    maxKeys?: number;
    decodeURIComponent?: (value: string) => string;
};
