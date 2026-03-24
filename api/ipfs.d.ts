/**
 * Start the embedded IPFS node.
 *
 * @param {Object} [options]
 * @param {string} [options.repoPath] Absolute path to the repo directory.
 * @param {number|string} [options.port] Swarm port to bind.
 * @returns {Promise<{repoPath: string, port: number, peerId: string}>}
 */
export function start(options?: {
    repoPath?: string;
    port?: number | string;
}): Promise<{
    repoPath: string;
    port: number;
    peerId: string;
}>;
/**
 * Stop the running IPFS node.
 * @returns {Promise<boolean>} Resolves to true when the node is stopped.
 */
export function stop(): Promise<boolean>;
/**
 * Fetch the current runtime status of the IPFS subsystem.
 * @returns {Promise<{available: boolean, started: boolean, repoPath: string, port: number, peerId: string}>}
 */
export function status(): Promise<{
    available: boolean;
    started: boolean;
    repoPath: string;
    port: number;
    peerId: string;
}>;
/**
 * Ensure that the embedded IPFS node has been started. When the node is not
 * already running the helper will start it with the given options.
 *
 * @param {Object} [options] Passed to {@link start} if the node is inactive.
 * @returns {Promise<{repoPath: string, port: number, peerId: string}>}
 */
export function ensureStarted(options?: any): Promise<{
    repoPath: string;
    port: number;
    peerId: string;
}>;
/**
 * Add a UnixFS file or directory tree to the node and return its CID.
 *
 * @param {string} path Absolute path to the file or directory to add.
 * @returns {Promise<{cid: string}>}
 */
export function add(path: string): Promise<{
    cid: string;
}>;
/**
 * Retrieve a CID or path from the network.
 *
 * @param {string} cid CID or IPFS/IPNS path to fetch.
 * @param {Object} options
 * @param {string} options.destination Filesystem path where the payload should be written.
 * @param {boolean} [options.pin=false] Whether the fetched content should be pinned after saving.
 * @returns {Promise<{cid: string, path: string, pinned: boolean}>}
 */
export function get(cid: string, { destination, pin }?: {
    destination: string;
    pin?: boolean;
}): Promise<{
    cid: string;
    path: string;
    pinned: boolean;
}>;
/**
 * Pin a CID so it is retained locally.
 * @param {string} cid CID or path to pin.
 * @returns {Promise<{cid: string, pinned: boolean}>}
 */
export function pin(cid: string): Promise<{
    cid: string;
    pinned: boolean;
}>;
/**
 * Remove a CID from the local pinset.
 * @param {string} cid CID or path to unpin.
 * @returns {Promise<{cid: string, pinned: boolean}>}
 */
export function unpin(cid: string): Promise<{
    cid: string;
    pinned: boolean;
}>;
/**
 * Trigger repository garbage collection.
 * @returns {Promise<boolean>} Resolves to true when the sweep completes.
 */
export function garbageCollect(): Promise<boolean>;
/**
 * Query the current peer ID for the running node.
 * @returns {Promise<string>} The node's peer ID (may be empty if unavailable).
 */
export function peerId(): Promise<string>;
/**
 * Connect to a remote peer using a multiaddress.
 * @param {string} address Multiaddress of the peer to connect.
 * @returns {Promise<{peer: string, removed: boolean}>}
 */
export function addPeer(address: string): Promise<{
    peer: string;
    removed: boolean;
}>;
/**
 * Disconnect a previously connected peer.
 * @param {string} address Multiaddress of the peer to remove.
 * @returns {Promise<{peer: string, added: boolean}>}
 */
export function removePeer(address: string): Promise<{
    peer: string;
    added: boolean;
}>;
declare namespace _default {
    export { start };
    export { stop };
    export { status };
    export { ensureStarted };
    export { add };
    export { get };
    export { pin };
    export { unpin };
    export { garbageCollect };
    export { peerId };
    export { addPeer };
    export { removePeer };
}
export default _default;
