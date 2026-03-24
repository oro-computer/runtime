/**
 * @typedef {Packet} CacheEntry
 * @typedef {function(CacheEntry, CacheEntry): number} CacheEntrySiblingResolver
 */
/**
 * Default cache sibling resolver that computes a delta between
 * two entries clocks.
 * @param {CacheEntry} a
 * @param {CacheEntry} b
 * @return {number}
 */
export function defaultSiblingResolver(a: CacheEntry, b: CacheEntry): number;
/**
 * Default max size of a `Cache` instance.
 */
export const DEFAULT_MAX_SIZE: number;
/**
 * Internal mapping of packet IDs to packet data used by `Cache`.
 */
export class CacheData extends Map<any, any> {
    constructor();
    constructor(entries?: readonly (readonly [any, any])[]);
    constructor();
    constructor(iterable?: Iterable<readonly [any, any]>);
}
/**
 * A class for storing a cache of packets by ID. This class includes a scheme
 * for reconciling disjointed packet caches in a large distributed system. The
 * following are key design characteristics.
 *
 * Space Efficiency: This scheme can be space-efficient because it summarizes
 * the cache's contents in a compact binary format. By sharing these summaries,
 * two computers can quickly determine whether their caches have common data or
 * differences.
 *
 * Bandwidth Efficiency: Sharing summaries instead of the full data can save
 * bandwidth. If the differences between the caches are small, sharing summaries
 * allows for more efficient data synchronization.
 *
 * Time Efficiency: The time efficiency of this scheme depends on the size of
 * the cache and the differences between the two caches. Generating summaries
 * and comparing them can be faster than transferring and comparing the entire
 * dataset, especially for large caches.
 *
 * Complexity: The scheme introduces some complexity due to the need to encode
 * and decode summaries. In some cases, the overhead introduced by this
 * complexity might outweigh the benefits, especially if the caches are
 * relatively small. In this case, you should be using a query.
 *
 * Data Synchronization Needs: The efficiency also depends on the data
 * synchronization needs. If the data needs to be synchronized in real-time,
 * this scheme might not be suitable. It's more appropriate for cases where
 * periodic or batch synchronization is acceptable.
 *
 * Scalability: The scheme's efficiency can vary depending on the scalability
 * of the system. As the number of cache entries or computers involved
 * increases, the complexity of generating and comparing summaries will stay
 * bound to a maximum of 16Mb.
 *
 */
export class Cache {
    static HASH_SIZE_BYTES: number;
    static HASH_EMPTY: string;
    /**
     * The encodeSummary method provides a compact binary encoding of the output
     * of summary()
     *
     * @param {Object} summary - the output of calling summary()
     * @return {Buffer}
     **/
    static encodeSummary(summary: any): Buffer;
    /**
     * The decodeSummary method decodes the output of encodeSummary()
     *
     * @param {Buffer} bin - the output of calling encodeSummary()
     * @return {Object} summary
     **/
    static decodeSummary(bin: Buffer): any;
    /**
     * Test a summary hash format is valid
     *
     * @param {string} hash
     * @returns boolean
     */
    static isValidSummaryHashFormat(hash: string): boolean;
    /**
     * `Cache` class constructor.
     * @param {CacheData?} [data]
     */
    constructor(data?: CacheData | null, siblingResolver?: typeof defaultSiblingResolver);
    data: CacheData;
    maxSize: number;
    siblingResolver: typeof defaultSiblingResolver;
    /**
     * Readonly count of the number of cache entries.
     * @type {number}
     */
    get size(): number;
    /**
     * Readonly size of the cache in bytes.
     * @type {number}
     */
    get bytes(): number;
    /**
     * Inserts a `CacheEntry` value `v` into the cache at key `k`.
     * @param {string} k
     * @param {CacheEntry} v
     * @return {boolean}
     */
    insert(k: string, v: CacheEntry): boolean;
    /**
     * Gets a `CacheEntry` value at key `k`.
     * @param {string} k
     * @return {CacheEntry?}
     */
    get(k: string): CacheEntry | null;
    /**
     * @param {string} k
     * @return {boolean}
     */
    delete(k: string): boolean;
    /**
     * Predicate to determine if cache contains an entry at key `k`.
     * @param {string} k
     * @return {boolean}
     */
    has(k: string): boolean;
    /**
     * Composes an indexed packet into a new `Packet`
     * @param {Packet} packet
     */
    compose(packet: Packet, source?: CacheData): Promise<Packet>;
    sha1(value: any, toHex: any): Promise<any>;
    /**
     *
     * The summarize method returns a terse yet comparable summary of the cache
     * contents.
     *
     * Think of the cache as a trie of hex characters, the summary returns a
     * checksum for the current level of the trie and for its 16 children.
     *
     * This is similar to a merkel tree as equal subtrees can easily be detected
     * without the need for further recursion. When the subtree checksums are
     * inequivalent then further negotiation at lower levels may be required, this
     * process continues until the two trees become synchonized.
     *
     * When the prefix is empty, the summary will return an array of 16 checksums
     * these checksums provide a way of comparing that subtree with other peers.
     *
     * When a variable-length hexidecimal prefix is provided, then only cache
     * member hashes sharing this prefix will be considered.
     *
     * For each hex character provided in the prefix, the trie will decend by one
     * level, each level divides the 2^128 address space by 16. For exmaple...
     *
     * ```
     * Level  0   1   2
     * ----------------
     * 2b00
     * aa0e  ━┓  ━┓
     * aa1b   ┃   ┃
     * aae3   ┃   ┃  ━┓
     * aaea   ┃   ┃   ┃
     * aaeb   ┃  ━┛  ━┛
     * ab00   ┃  ━┓
     * ab1e   ┃   ┃
     * ab2a   ┃   ┃
     * abef   ┃   ┃
     * abf0  ━┛  ━┛
     * bff9
     * ```
     *
     * @param {string} prefix - a string of lowercased hexidecimal characters
     * @return {Object}
     *
     */
    summarize(prefix?: string, predicate?: () => boolean): any;
}
export default Cache;
export type CacheEntry = Packet;
export type CacheEntrySiblingResolver = (arg0: CacheEntry, arg1: CacheEntry) => number;
import { Packet } from './packets.js';
import { Buffer } from '../buffer.js';
