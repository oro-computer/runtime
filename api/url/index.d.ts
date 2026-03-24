/**
 * Parse a URL-like input into a structured object.
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
export function parse(input: any, options?: any): {
    hash: any;
    host: any;
    hostname: any;
    origin: any;
    auth: string;
    password: any;
    pathname: any;
    path: any;
    port: any;
    protocol: any;
    search: any;
    searchParams: any;
    username: any;
    [Symbol.toStringTag]: string;
};
/**
 * Resolve a target URL/path `to` against a base `from`.
 * Mirrors Node's legacy `url.resolve()` semantics.
 *
 * Example:
 * ```js
 * resolve('http://example.com/a/b', '../c') // => 'http://example.com/c'
 * resolve('/a/b', 'c') // => '/a/b/c'
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
 */
export function format(input: any): any;
export function fileURLToPath(url: any): any;
/**
 * @type {Set & { handlers: Set<string> }}
 */
export const protocols: Set<any> & {
    handlers: Set<string>;
};
export default URL;
export class URL {
    private constructor();
}
export const URLSearchParams: any;
export const parseURL: any;
import { URLPattern } from './urlpattern/urlpattern.js';
export { URLPattern };
