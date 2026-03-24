/**
 * @typedef {object} LookupAddress
 * @property {string} address - Resolved IP address.
 * @property {4|6} family - Address family of the result.
 *
 * @typedef {object} LookupOptions
 * @property {0|4|6|'IPv4'|'IPv6'} [family=0] - Address family preference.
 * @property {number} [hints=0] - Optional getaddrinfo flags (combine dns.ADDRCONFIG, dns.V4MAPPED, dns.ALL).
 * @property {boolean} [all=false] - Return all resolved addresses if true.
 * @property {boolean} [verbatim=false] - Preserve the original address ordering.
 */
/**
 * Resolves a host name (e.g. `example.org`) into the first found A (IPv4) or
 * AAAA (IPv6) record. All option properties are optional. If options is an
 * integer, then it must be 4 or 6 – if options is 0 or not provided, then IPv4
 * and IPv6 addresses are both returned if found.
 *
 * From the node.js website...
 *
 * > With the all option set to true, the arguments for callback change to (err,
 * addresses), with addresses being an array of objects with the properties
 * address and family.
 *
 * > On error, err is an Error object, where err.code is the error code. Keep in
 * mind that err.code will be set to 'ENOTFOUND' not only when the host name does
 * not exist but also when the lookup fails in other ways such as no available
 * file descriptors. dns.lookup() does not necessarily have anything to do with
 * the DNS protocol. The implementation uses an operating system facility that
 * can associate names with addresses and vice versa. This implementation can
 * have subtle but important consequences on the behavior of any Node.js program.
 * Please take some time to consult the Implementation considerations section
 * before using dns.lookup().
 *
 * @see {@link https://nodejs.org/api/dns.html#dns_dns_lookup_hostname_options_callback}
 * @param {string} hostname - The host name to resolve.
 * @param {(LookupOptions|number|string)=} [options] - Lookup options or the record family.
 * @param {function(Error, string|LookupAddress[], 4|6=):void} cb - Invoked when the lookup completes.
 * @returns {void}
 */
export function lookup(hostname: string, options?: (LookupOptions | number | string) | undefined, cb: (arg0: Error, arg1: string | LookupAddress[], arg2: (4 | 6) | undefined) => void): void;
export default exports;
export type LookupAddress = {
    /**
     * - Resolved IP address.
     */
    address: string;
    /**
     * - Address family of the result.
     */
    family: 4 | 6;
};
export type LookupOptions = {
    /**
     * - Address family preference.
     */
    family?: 0 | 4 | 6 | "IPv4" | "IPv6";
    /**
     * - Optional getaddrinfo flags (combine dns.ADDRCONFIG, dns.V4MAPPED, dns.ALL).
     */
    hints?: number;
    /**
     * - Return all resolved addresses if true.
     */
    all?: boolean;
    /**
     * - Preserve the original address ordering.
     */
    verbatim?: boolean;
};
import * as promises from './promises.js';
import { ADDRCONFIG } from './constants.js';
import { ALL } from './constants.js';
import { V4MAPPED } from './constants.js';
import { constants } from './constants.js';
import * as exports from './index.js';
export { promises, ADDRCONFIG, ALL, V4MAPPED, constants };
