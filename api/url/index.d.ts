/**
 * @typedef {{ strict?: boolean }} URLParseOptions
 */
/**
 * @typedef {object} ParsedURL
 * @property {string|null} protocol
 * @property {string|null} host
 * @property {string|null} hostname
 * @property {string|null} origin
 * @property {string|null} auth
 * @property {string|null} username
 * @property {string|null} password
 * @property {string|null} port
 * @property {string|null} pathname
 * @property {string|null} path
 * @property {string|null} search
 * @property {string|null} hash
 * @property {string} href
 * @property {URLSearchParams} searchParams
 * @property {string|Record<string, any>} [query]
 */
/**
 * @typedef {Partial<ParsedURL>} URLFormatOptions
 */
/**
 * Parse a URL-like input into a structured object.
 * @param {string} input
 * @param {boolean|URLParseOptions|null} [options]
 * @returns {ParsedURL|null}
 * - When `options === true`, includes a Node-compatible `query` object.
 * - When `options?.strict === true`, returns `null` if input cannot be parsed.
 *
 * Example:
 * ```js
 * parse('https://user:pass@example.com:8080/a/b?x=1#h')
 * // => {
 * //   protocol: 'https:', hostname: 'example.com', origin: 'https://example.com:8080',
 * //   username: 'user', password: 'pass', port: '8080', pathname: '/a/b',
 * //   search: '?x=1', hash: '#h', path: '/a/b', href: 'https://user:pass@example.com:8080/a/b?x=1#h',
 * //   auth: 'user:pass', searchParams: URLSearchParams, query: 'x=1'
 * // }
 * ```
 */
export function parse(input: string, options?: boolean | URLParseOptions | null): ParsedURL | null;
/**
 * Resolve a target URL/path `to` against a base `from`.
 * Mirrors Node.js `url.resolve()` semantics.
 *
 * Example:
 * ```js
 * resolve('http://example.com/a/b', '../c') // => 'http://example.com/c'
 * resolve('/a/b', 'c') // => '/a/c'
 * ```
 */
export function resolve(from: any, to: any): any;
/**
 * Format a URL from either a string or a partial object containing URL fields.
 * Returns an empty string if the input is invalid or insufficient.
 *
 * Example (object):
 * ```js
 * format({ protocol: 'https:', hostname: 'example.com', pathname: '/a/b' })
 * // => 'https://example.com/a/b'
 * ```
 *
 * Example (string):
 * ```js
 * format('https://example.com/a/b') // => 'https://example.com/a/b'
 * ```
 *
 * Notes
 * - When specifying `hostname` with an IPv6 literal, brackets are added automatically.
 *   Alternatively, you can pass `host` directly as `[2001:db8::1]:8080`.
 * @param {string|URLFormatOptions} input
 * @returns {string}
 */
export function format(input: string | URLFormatOptions): string;
export function fileURLToPath(url: any): any;
/**
 * @type {Set & { handlers: Set<string> }}
 */
export const protocols: Set<any> & {
    handlers: Set<string>;
};
export default URL;
export type URLParseOptions = {
    strict?: boolean;
};
export type ParsedURL = {
    protocol: string | null;
    host: string | null;
    hostname: string | null;
    origin: string | null;
    auth: string | null;
    username: string | null;
    password: string | null;
    port: string | null;
    pathname: string | null;
    path: string | null;
    search: string | null;
    hash: string | null;
    href: string;
    searchParams: URLSearchParams;
    query?: string | Record<string, any>;
};
export type URLFormatOptions = Partial<ParsedURL>;
export class URL {
    private constructor();
}
export const URLSearchParams: any;
export const parseURL: any;
import { URLPattern } from './urlpattern/urlpattern.js';
export { URLPattern };
