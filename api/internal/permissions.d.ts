/**
 * Query for a permission status.
 * @param {PermissionDescriptor} descriptor
 * @param {object=} [options]
 * @param {?AbortSignal} [options.signal = null]
 * @return {Promise<PermissionStatus>}
 */
export function query(descriptor: PermissionDescriptor, options?: object | undefined, ...args: any[]): Promise<PermissionStatus>;
/**
 * Request a permission to be granted.
 * @param {PermissionDescriptor} descriptor
 * @param {object=} [options]
 * @param {?AbortSignal} [options.signal = null]
 * @return {Promise<PermissionStatus>}
 */
export function request(descriptor: PermissionDescriptor, options?: object | undefined, ...args: any[]): Promise<PermissionStatus>;
/**
 * An enumeration of the permission types.
 * - 'geolocation'
 * - 'notifications'
 * - 'push'
 * - 'persistent-storage'
 * - 'midi'
 * - 'storage-access'
 * @type {Enumeration}
 * @ignore
 */
export const types: Enumeration;
declare const _default: any;
export default _default;
export type PermissionDescriptor = {
    name: string;
};
/**
 * A container that provides the state of an object and an event handler
 * for monitoring changes permission changes.
 * @ignore
 */
declare class PermissionStatus extends EventTarget {
    [x: number]: () => import("../gc.js").Finalizer;
    /**
     * `PermissionStatus` class constructor.
     * @param {string} name
     * @param {string} initialState
     * @param {object=} [options]
     * @param {?AbortSignal} [options.signal = null]
     */
    constructor(name: string, initialState: string, options?: object | undefined);
    /**
     * The name of this permission this status is for.
     * @type {string}
     */
    get name(): string;
    /**
     * The current state of the permission status.
     * @type {string}
     */
    get state(): string;
    set onchange(onchange: (arg0: Event) => any);
    /**
     * Level 0 event target 'change' event listener accessor
     * @type {function(Event)}
     */
    get onchange(): (arg0: Event) => any;
    /**
     * Non-standard method for unsubscribing to status state updates.
     * @ignore
     */
    unsubscribe(): void;
    /**
     * String tag for `PermissionStatus`.
     * @ignore
     */
    get [Symbol.toStringTag](): string;
    #private;
}
import Enumeration from '../enumeration.js';
