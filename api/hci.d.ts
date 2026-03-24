/**
 * Enumerate available HCI adapters on the host.
 * @return {HCIAdapter[]}
 */
export function listAdapters(): HCIAdapter[];
/**
 * Retrieve information for a specific adapter.
 * @param {number} devId Adapter identifier (e.g. `0` for `hci0`)
 * @return {HCIAdapter}
 */
export function getAdapter(devId: number): HCIAdapter;
/**
 * Enable or disable an adapter by bringing it up or down.
 * @param {number} devId Adapter identifier
 * @param {boolean} up When `true`, powers the adapter; otherwise powers it off
 * @return {{ devId: number, up: boolean }}
 */
export function setAdapterState(devId: number, up: boolean): {
    devId: number;
    up: boolean;
};
/**
 * Low-level socket for interacting with a Bluetooth controller via HCI.
 * @extends EventEmitter
 */
export class HCISocket extends EventEmitter {
    /**
     * Convenience helper mirroring {@link listAdapters}.
     * @return {HCIAdapter[]}
     */
    static listAdapters(): HCIAdapter[];
    /**
     * Convenience helper mirroring {@link getAdapter}.
     * @param {number} devId
     * @return {HCIAdapter}
     */
    static getAdapter(devId: number): HCIAdapter;
    /**
     * Convenience helper mirroring {@link setAdapterState}.
     * @param {number} devId
     * @param {boolean} up
     * @return {{ devId: number, up: boolean }}
     */
    static setAdapterState(devId: number, up: boolean): {
        devId: number;
        up: boolean;
    };
    /**
     * @param {(number|{ devId?: number })} [options] Optional adapter identifier or configuration object.
     */
    constructor(options?: (number | {
        devId?: number;
    }));
    id: bigint;
    devId: any;
    _closed: boolean;
    /**
     * Indicates whether the socket has been closed.
     * @return {boolean}
     */
    get closed(): boolean;
    /**
     * Write an HCI packet to the controller.
     * @param {ArrayBufferView|ArrayBuffer|Buffer|string|number[]} chunk HCI packet bytes.
     * @return {number} Number of bytes written.
     */
    write(chunk: ArrayBufferView | ArrayBuffer | Buffer | string | number[]): number;
    /**
     * Stop receiving data and close the underlying socket.
     */
    close(): void;
}
export default HCISocket;
export type HCIAdapter = {
    /**
     * Numeric adapter identifier (e.g. `0` for `hci0`)
     */
    devId: number;
    /**
     * System name reported by the controller
     */
    name: string;
    /**
     * Controller Bluetooth address in canonical form
     */
    bdaddr: string;
    /**
     * Raw adapter flags as reported by the kernel
     */
    flags: number;
    /**
     * Primary/AMP type label or numeric fallback
     */
    type: (string | number);
    /**
     * Transport the controller is attached to
     */
    bus: (string | number);
    /**
     * Indicates whether the adapter is currently powered
     */
    powered: boolean;
};
import { EventEmitter } from './events.js';
import { Buffer } from './buffer.js';
