/**
 * @typedef {object} NetworkStatus
 * @property {boolean} available Whether Android permits reading network state.
 * @property {boolean} connected Whether a default network exists.
 * @property {boolean} validated Whether Android validated Internet connectivity.
 * @property {boolean} captivePortal Whether Android detected a captive portal.
 * @property {boolean} metered Whether the current path is metered.
 * @property {string[]} transports Reported wifi, cellular, ethernet, bluetooth, or vpn transports.
 * @property {'online'|'offline'} name Online means the path is validated.
 */
/**
 * Reads Android's current default path without making a network request.
 * Other platforms and disabled monitoring reject with `ENOSYS`.
 * @returns {Promise<NetworkStatus>}
 */
export function getStatus(): Promise<NetworkStatus>;
/**
 * Observes native network path changes. Subscribe before reading `getStatus()`
 * to obtain an initial snapshot without missing later changes. This function
 * does not start polling or probe any application server.
 * @param {(status: NetworkStatus) => void} callback
 * @returns {() => void} Removes the listener.
 */
export function onChange(callback: (status: NetworkStatus) => void): () => void;
declare namespace _default {
    export { getStatus };
    export { onChange };
}
export default _default;
export type NetworkStatus = {
    /**
     * Whether Android permits reading network state.
     */
    available: boolean;
    /**
     * Whether a default network exists.
     */
    connected: boolean;
    /**
     * Whether Android validated Internet connectivity.
     */
    validated: boolean;
    /**
     * Whether Android detected a captive portal.
     */
    captivePortal: boolean;
    /**
     * Whether the current path is metered.
     */
    metered: boolean;
    /**
     * Reported wifi, cellular, ethernet, bluetooth, or vpn transports.
     */
    transports: string[];
    /**
     * Online means the path is validated.
     */
    name: "online" | "offline";
};
