/**
 * Retrieves global DBus availability metadata.
 * @returns {Promise<{ available: boolean, reason?: string }>}
 */
export function availability(): Promise<{
    available: boolean;
    reason?: string;
}>;
/**
 * Establishes a DBus connection via the runtime.
 * @param {Record<string, any>} [options]
 * @returns {Promise<Connection>}
 */
export function connect(options?: Record<string, any>): Promise<Connection>;
/**
 * Closes every tracked DBus connection.
 * @returns {Promise<void>}
 */
export function disconnectAll(): Promise<void>;
/**
 * Helper for constructing DBus variant payloads.
 * @param {string} signature
 * @param {any} value
 * @returns {VariantBody}
 */
export function variant(signature: string, value: any): VariantBody;
/**
 * Helper for constructing DBus dictionary entries.
 * @param {any} key
 * @param {any} value
 * @returns {{ key: any, value: any }}
 */
export function dictEntry(key: any, value: any): {
    key: any;
    value: any;
};
/**
 * Enumerates buses understood by the runtime.
 * @type {{ readonly SESSION: 'session', readonly SYSTEM: 'system', readonly STARTER: 'starter', readonly ADDRESS: 'address' }}
 */
export const BUS: {
    readonly SESSION: "session";
    readonly SYSTEM: "system";
    readonly STARTER: "starter";
    readonly ADDRESS: "address";
};
/**
 * Flags for `requestName` calls, mirroring `DBUS_NAME_FLAG_*` constants.
 * @type {{ readonly NONE: 0, readonly ALLOW_REPLACEMENT: 0x1, readonly REPLACE_EXISTING: 0x2, readonly DO_NOT_QUEUE: 0x4 }}
 */
export const NAME_FLAGS: {
    readonly NONE: 0;
    readonly ALLOW_REPLACEMENT: 1;
    readonly REPLACE_EXISTING: 2;
    readonly DO_NOT_QUEUE: 4;
};
/**
 * Replies for `requestName`, mirroring `DBUS_REQUEST_NAME_REPLY_*` constants.
 * @type {{ readonly PRIMARY_OWNER: 1, readonly IN_QUEUE: 2, readonly EXISTS: 3, readonly ALREADY_OWNER: 4 }}
 */
export const REQUEST_NAME_REPLY: {
    readonly PRIMARY_OWNER: 1;
    readonly IN_QUEUE: 2;
    readonly EXISTS: 3;
    readonly ALREADY_OWNER: 4;
};
/**
 * Replies for `releaseName`, mirroring `DBUS_RELEASE_NAME_REPLY_*` constants.
 * @type {{ readonly RELEASED: 1, readonly NON_EXISTENT: 2, readonly NOT_OWNER: 3 }}
 */
export const RELEASE_NAME_REPLY: {
    readonly RELEASED: 1;
    readonly NON_EXISTENT: 2;
    readonly NOT_OWNER: 3;
};
/**
 * Message type codes per the DBus specification.
 * @type {{ readonly METHOD_CALL: 1, readonly METHOD_RETURN: 2, readonly ERROR: 3, readonly SIGNAL: 4 }}
 */
export const MESSAGE_TYPE: {
    readonly METHOD_CALL: 1;
    readonly METHOD_RETURN: 2;
    readonly ERROR: 3;
    readonly SIGNAL: 4;
};
/**
 * Common well-known names.
 * @type {{ readonly DBUS: 'org.freedesktop.DBus' }}
 */
export const WELL_KNOWN_NAMES: {
    readonly DBUS: "org.freedesktop.DBus";
};
/**
 * Common well-known object paths.
 * @type {{ readonly DBUS: '/org/freedesktop/DBus' }}
 */
export const WELL_KNOWN_PATHS: {
    readonly DBUS: "/org/freedesktop/DBus";
};
/**
 * Common well-known interfaces.
 * @type {{ readonly DBUS: 'org.freedesktop.DBus' }}
 */
export const WELL_KNOWN_INTERFACES: {
    readonly DBUS: "org.freedesktop.DBus";
};
/**
 * Common well-known members.
 * @type {{ readonly NAME_OWNER_CHANGED: 'NameOwnerChanged', readonly LIST_NAMES: 'ListNames' }}
 */
export const WELL_KNOWN_MEMBERS: {
    readonly NAME_OWNER_CHANGED: "NameOwnerChanged";
    readonly LIST_NAMES: "ListNames";
};
/**
 * Common well-known error names.
 * @type {{ readonly FAILED: 'org.freedesktop.DBus.Error.Failed', readonly UNKNOWN_OBJECT: 'org.freedesktop.DBus.Error.UnknownObject', readonly UNKNOWN_METHOD: 'org.freedesktop.DBus.Error.UnknownMethod', readonly SERVICE_UNKNOWN: 'org.freedesktop.DBus.Error.ServiceUnknown' }}
 */
export const WELL_KNOWN_ERRORS: {
    readonly FAILED: "org.freedesktop.DBus.Error.Failed";
    readonly UNKNOWN_OBJECT: "org.freedesktop.DBus.Error.UnknownObject";
    readonly UNKNOWN_METHOD: "org.freedesktop.DBus.Error.UnknownMethod";
    readonly SERVICE_UNKNOWN: "org.freedesktop.DBus.Error.ServiceUnknown";
};
/**
 * Runtime DBus connection wrapper.
 */
export class Connection extends EventEmitter {
    /**
     * @param {string | number} id
     */
    constructor(id: string | number);
    /**
     * Unique identifier of the underlying DBus connection.
     * @returns {string}
     */
    get id(): string;
    /**
     * Indicates whether the connection has been closed.
     * @returns {boolean}
     */
    get closed(): boolean;
    /**
     * Fetches global DBus availability metadata.
     * @returns {Promise<{ available: boolean, reason?: string }>}
     */
    availability(): Promise<{
        available: boolean;
        reason?: string;
    }>;
    /**
     * Terminates the connection and removes all local bookkeeping.
     * @returns {Promise<boolean>}
     */
    close(): Promise<boolean>;
    /**
     * Requests the provided bus name on the connection.
     * @param {string} name
     * @param {number} [flags]
     * @returns {Promise<void>}
     */
    requestName(name: string, flags?: number): Promise<void>;
    /**
     * Releases a previously requested bus name.
     * @param {string} name
     * @returns {Promise<void>}
     */
    releaseName(name: string): Promise<void>;
    /**
     * Adds a match rule for DBus signals.
     * @param {string} rule
     * @param {(signal: DBusSignal, matchId: string) => void} [handler]
     * @returns {Promise<string>}
     */
    addMatch(rule: string, handler?: (signal: DBusSignal, matchId: string) => void): Promise<string>;
    /**
     * Removes a previously installed match rule.
     * @param {string | number} matchId
     * @returns {Promise<void>}
     */
    removeMatch(matchId: string | number): Promise<void>;
    /**
     * Invokes a DBus method on the remote peer.
     * @param {MethodCallOptions} options
     * @returns {Promise<any>}
     */
    call(options: MethodCallOptions): Promise<any>;
    /**
     * Emits a custom signal to the bus.
     * @param {SignalOptions} options
     * @returns {Promise<void>}
     */
    emitSignal(options: SignalOptions): Promise<void>;
    _handleSignal(signal: any): void;
    /**
     * Exports an object path so native method calls are forwarded to JS listeners.
     * @param {ExportOptions} options
     * @returns {Promise<string>}
     */
    exportObject(options: ExportOptions): Promise<string>;
    /**
     * Removes a previously exported object path.
     * @param {string | number} exportId
     * @returns {Promise<void>}
     */
    unexportObject(exportId: string | number): Promise<void>;
    /**
     * Replies to a pending method call originating from the runtime.
     * @param {string | number} callId
     * @param {MethodResult | MethodError | Error} result
     * @returns {Promise<void>}
     */
    respond(callId: string | number, result: MethodResult | MethodError | Error): Promise<void>;
    /**
     * Convenience helper to send an error response.
     * @param {string | number} callId
     * @param {string} [name]
     * @param {string} [message]
     * @returns {Promise<void>}
     */
    respondError(callId: string | number, name?: string, message?: string): Promise<void>;
    /**
     * Internal handler invoked when the runtime forwards a method call into JS.
     * @param {DBusSignal & { callId: string }} payload
     * @returns {void}
     */
    _handleMethodCall(payload: DBusSignal & {
        callId: string;
    }): void;
    #private;
}
export default exports;
/**
 * Result payload returned from the native IPC bridge.
 */
export type IPCResult<TData, TError> = {
    data?: TData;
    err?: TError;
    source?: string;
};
/**
 * DBus message body expressed as signature + values tuple.
 */
export type DBusBody = {
    signature: string;
    values: any[];
};
/**
 * Signal payload forwarded from the runtime.
 */
export type DBusSignal = {
    connectionId: string;
    path: string;
    interface: string;
    member: string;
    sender: string;
    body?: DBusBody | any;
    signature: string;
    values: any[];
    callId?: string;
    raw: any;
};
/**
 * Options accepted when providing a structured DBus body.
 */
export type StructuredBody = {
    signature?: string;
    values: any[];
};
/**
 * Variant container helper used by {@link variant}.
 */
export type VariantBody = {
    signature: string;
    value: any;
};
/**
 * Options used when invoking {@link Connection#call}.
 */
export type MethodCallOptions = {
    member: string;
    destination?: string;
    path?: string;
    interface?: string;
    signature?: string;
    body?: any[] | StructuredBody | VariantBody;
    timeout?: number;
    noReply?: boolean;
};
/**
 * Options used when emitting custom signals via {@link Connection#emitSignal}.
 */
export type SignalOptions = {
    path: string;
    name: string;
    interface?: string;
    signature?: string;
    body?: any[] | StructuredBody | VariantBody;
};
/**
 * Options to describe an exported DBus object.
 */
export type ExportOptions = {
    path: string;
    interface?: string;
    methods?: string[];
};
/**
 * Result object accepted by {@link Connection#respond} when acknowledging a method call.
 */
export type MethodResult = {
    signature?: string;
    body?: any[] | StructuredBody | VariantBody;
};
/**
 * Error descriptor accepted by {@link Connection#respond} when rejecting a method call.
 */
export type MethodError = {
    error: true;
    name?: string;
    message?: string;
    body?: any[] | StructuredBody | VariantBody | any;
};
import { EventEmitter } from './events.js';
import * as exports from './dbus.js';
