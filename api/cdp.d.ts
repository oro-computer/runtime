/**
 * @typedef {object} CDPListenOptions
 * @property {string} [hostname='127.0.0.1'] - Bind address (defaults to loopback).
 * @property {number} [port=0] - Port to listen on. Use `0` for a random port.
 */
/**
 * @typedef {object} CDPStatus
 * @property {boolean} listening
 * @property {string} hostname
 * @property {number} port
 * @property {string} browserId
 * @property {string} wsEndpoint
 * @property {string} httpEndpoint
 */
/**
 * Starts the runtime CDP (Chrome DevTools Protocol) server.
 *
 * This exposes Chromium-style endpoints such as:
 * - `GET /json/version`
 * - `ws://<hostname>:<port>/devtools/browser/<browserId>`
 *
 * @param {CDPListenOptions} [options]
 * @returns {Promise<CDPStatus>}
 */
export function listen(options?: CDPListenOptions): Promise<CDPStatus>;
/**
 * Stops the runtime CDP server.
 * @returns {Promise<void>}
 */
export function close(): Promise<void>;
/**
 * Gets the current CDP server status.
 * @returns {Promise<CDPStatus>}
 */
export function status(): Promise<CDPStatus>;
declare namespace _default {
    export { listen };
    export { close };
    export { status };
}
export default _default;
export type CDPListenOptions = {
    /**
     * - Bind address (defaults to loopback).
     */
    hostname?: string;
    /**
     * - Port to listen on. Use `0` for a random port.
     */
    port?: number;
};
export type CDPStatus = {
    listening: boolean;
    hostname: string;
    port: number;
    browserId: string;
    wsEndpoint: string;
    httpEndpoint: string;
};
