/**
 * Retrieves XPC availability information for the current platform.
 * @returns {Promise<{ available: boolean, reason?: string }>}
 */
export function availability(): Promise<{
    available: boolean;
    reason?: string;
}>;
/**
 * Establishes an XPC connection.
 * @param {object} options
 * @param {string} options.service
 * @param {'mach-service' | 'named'} [options.type='mach-service']
 * @param {boolean} [options.listener=false]
 * @param {boolean} [options.privileged=false]
 * @param {number} [options.flags=0]
 * @param {string} [options.label]
 * @param {number} [options.pendingReplyTimeout=30000]
 * @returns {Promise<Connection>}
 */
export function connect(options?: {
    service: string;
    type?: "mach-service" | "named";
    listener?: boolean;
    privileged?: boolean;
    flags?: number;
    label?: string;
    pendingReplyTimeout?: number;
}): Promise<Connection>;
/**
 * Closes all tracked XPC connections.
 * @returns {Promise<void>}
 */
export function disconnectAll(): Promise<void>;
/**
 * Helper to encode a 64-bit signed integer.
 * @param {bigint | number | string} value
 * @returns {object}
 */
export function int64(value: bigint | number | string): object;
/**
 * Helper to encode a 64-bit unsigned integer.
 * @param {bigint | number | string} value
 * @returns {object}
 */
export function uint64(value: bigint | number | string): object;
/**
 * Helper to encode binary payloads as XPC data.
 * @param {Buffer | ArrayBuffer | ArrayBufferView | string} value
 * @param {XPCBufferEncoding} [encoding='utf8']
 * @returns {object}
 */
export function data(value: Buffer | ArrayBuffer | ArrayBufferView | string, encoding?: XPCBufferEncoding): object;
/**
 * Helper to encode UUID payloads.
 * @param {string | { toString(): string }} value
 * @returns {XPCExplicitValue}
 */
export function uuid(value: string | {
    toString(): string;
}): XPCExplicitValue;
export type Connection = import("./events.js").EventEmitter & {
    id: string;
    send(message: any, options?: any): Promise<any>;
    sendAndForget(message: any): Promise<any>;
    suspend(): Promise<boolean>;
    resume(): Promise<boolean>;
    close(): Promise<boolean>;
};
/** @type {ConnectionConstructor} */
export const Connection: ConnectionConstructor;
export type Listener = Connection;
/** @type {ListenerConstructor} */
export const Listener: ListenerConstructor;
export type XPCExplicitValue = {
    type: string;
    value?: any;
    encoding?: string;
};
export type XPCBufferEncoding = "utf8" | "utf-8" | "hex" | "base64";
export type XPCMessageTimeoutDetail = {
    messageId: string | null;
    reason: string | null;
};
export type XPCMessageDroppedDetail = {
    reason: string | null;
};
export type ConnectionConstructor = new (id: string | number, options?: any) => Connection;
export type ListenerConstructor = new (id: string | number, options?: any) => Listener;
import { Buffer } from './buffer.js';
