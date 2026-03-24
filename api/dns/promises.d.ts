/**
 * @async
 * @typedef {import('./index.js').LookupAddress} LookupAddress
 * @typedef {import('./index.js').LookupOptions} LookupOptions
 *
 * @async
 * @see {@link https://nodejs.org/api/dns.html#dnspromiseslookuphostname-options}
 * @param {string} hostname - The host name to resolve.
 * @param {(LookupOptions|number|string)=} [opts] - Lookup options or family.
 * @returns {Promise<LookupAddress|LookupAddress[]>}
 */
export function lookup(hostname: string, opts?: (LookupOptions | number | string) | undefined): Promise<LookupAddress | LookupAddress[]>;
export default exports;
export type LookupAddress = import("./index.js").LookupAddress;
export type LookupOptions = import("./index.js").LookupOptions;
import * as exports from './promises.js';
import { ADDRCONFIG } from './constants.js';
import { ALL } from './constants.js';
import { V4MAPPED } from './constants.js';
import { constants } from './constants.js';
export { ADDRCONFIG, ALL, V4MAPPED, constants };
