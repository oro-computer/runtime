/**
 * Lists persisted grants. This does not establish that the provider is reachable.
 * Other platforms reject with `ENOSYS`.
 * @returns {Promise<DocumentGrant[]>}
 */
export function list(): Promise<DocumentGrant[]>;
/**
 * Retains an existing user-selected document grant across process death/reboot.
 * Android must have granted the requested persistable modes; this never opens
 * a permission prompt or obtains access to an arbitrary URI. At most 128 grants.
 * @param {string} uri
 * @param {DocumentGrantOptions} [options]
 * @returns {Promise<void>}
 */
export function persist(uri: string, options?: DocumentGrantOptions): Promise<void>;
/**
 * Releases retained access. Close open files and stop using the URI first.
 * @param {string} uri
 * @param {DocumentGrantOptions} [options]
 * @returns {Promise<void>}
 */
export function release(uri: string, options?: DocumentGrantOptions): Promise<void>;
/**
 * Queries a selected document or tree root without resolving a filesystem path.
 * Unknown provider sizes remain null. Queries have a five-second result deadline;
 * a timed-out provider may still occupy one of the two bounded worker slots.
 * Providers enforce existing grants. Other platforms reject with ENOSYS.
 * @param {string} uri
 * @returns {Promise<DocumentMetadata>}
 */
export function stat(uri: string): Promise<DocumentMetadata>;
/**
 * Lists children within an existing selected tree grant. Entries contain proper
 * child document URIs. Pages follow provider cursor order, which is not a durable
 * snapshot: after edits or process loss, restart and reconcile by URI. At most
 * 128 entries and 1 MiB of metadata per response, with two concurrent queries.
 * This does not broaden grants or emulate ordinary filesystem path traversal.
 * Other platforms reject with ENOSYS.
 * @param {string} uri
 * @param {DocumentChildrenOptions} [options]
 * @returns {Promise<DocumentChildren>}
 */
export function readDirectory(uri: string, options?: DocumentChildrenOptions): Promise<DocumentChildren>;
declare namespace _default {
    export { list };
    export { persist };
    export { release };
    export { stat };
    export { readDirectory };
}
export default _default;
export type DocumentGrant = {
    /**
     * A scoped content URI, never a filesystem path.
     */
    uri: string;
    read: boolean;
    write: boolean;
    /**
     * Unix milliseconds.
     */
    persistedAt: number;
};
export type DocumentGrantOptions = {
    read?: boolean;
    write?: boolean;
};
export type DocumentMetadata = {
    /**
     * Provider URI; pass this URI directly to oro:fs.
     */
    uri: string;
    /**
     * Provider display name; never a filesystem path.
     */
    name: string | null;
    /**
     * A directory uses vnd.android.document/directory.
     */
    mimeType: string | null;
    /**
     * Bytes, or null when unknown.
     */
    size: number | null;
    /**
     * Unix milliseconds, or null when unknown.
     */
    lastModified: number | null;
    /**
     * Android DocumentsContract provider flags, not an access grant.
     */
    flags: number | null;
};
export type DocumentChildrenOptions = {
    /**
     * Cursor offset from 0 through 10000.
     */
    offset?: number;
    /**
     * Maximum entries, from 1 through 128.
     */
    limit?: number;
};
export type DocumentChildren = {
    entries: DocumentMetadata[];
    /**
     * More entries exist beyond this page.
     */
    truncated: boolean;
    /**
     * Null at EOF or the 10000 offset limit.
     */
    nextOffset: number | null;
};
