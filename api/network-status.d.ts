/**
 * @typedef {object} NetworkStatus
 * @property {string} [platform] Linux reports its platform explicitly.
 * @property {boolean} available Whether the OS permits reading network state.
 * @property {boolean} connected Whether a default network exists.
 * @property {boolean} validated Whether the OS monitor reports full Internet connectivity.
 * @property {boolean} captivePortal Whether Android detected a captive portal.
 * @property {boolean} metered Whether the current path is metered.
 * @property {string[]} transports Reported wifi, cellular, ethernet, bluetooth, or vpn transports.
 * @property {'online'|'offline'} name Online means the path is validated.
 * @property {boolean} [transportDetailsAvailable] False on Linux; transports is empty rather than inferred.
 * @property {boolean} [serverReachabilityKnown] False on Linux. Application servers require their own probes.
 */
/**
 * Reads the OS monitor's current default path without making a network request.
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
     * Linux reports its platform explicitly.
     */
    platform?: string;
    /**
     * Whether the OS permits reading network state.
     */
    available: boolean;
    /**
     * Whether a default network exists.
     */
    connected: boolean;
    /**
     * Whether the OS monitor reports full Internet connectivity.
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
    /**
     * False on Linux; transports is empty rather than inferred.
     */
    transportDetailsAvailable?: boolean;
    /**
     * False on Linux. Application servers require their own probes.
     */
    serverReachabilityKnown?: boolean;
};
