/**
 * @typedef {{ options: object, payload: Uint8Array }} ReceiveMessage
 * @typedef {function(Error?, ReceiveMessage | undefined)} ReceiveCallback
 * @typedef {{ isActive: boolean, handles: { ids: string[], count: number }}} ConduitDiagnostics
 * @typedef {{ isActive: boolean, port: number, sharedKey: string }} ConduitStatus
 * @typedef {{
 *   id?: string|BigInt|number,
 *   sharedKey?: string
 *}} ConduitOptions
 */
export const DEFAULT_MAX_RECONNECT_RETRIES: 32;
export const DEFAULT_INITIAL_RECONNECT_TIMEOUT: 500;
export const DEFAULT_MAX_RECONNECT_TIMEOUT: 30000;
/**
 * A pool of known `Conduit` instances.
 * @type {Set<Conduit>}
 */
export const pool: Set<Conduit>;
/**
 * A container for managing a WebSocket connection to the internal runtime
 * Conduit WebSocket server.
 */
export class Conduit extends EventTarget {
    [x: number]: () => import("./gc.js").Finalizer;
    static set port(port: number);
    /**
     * The global `Conduit` port
     * @type {number}
     */
    static get port(): number;
    /**
     * Returns diagnostics information about the conduit server
     * @return {Promise<ConduitDiagnostics>}
     */
    static diagnostics(): Promise<ConduitDiagnostics>;
    /**
     * Returns the current Conduit server status
     * @return {Promise<ConduitStatus>}
     */
    static status(): Promise<ConduitStatus>;
    /**
     * Waits for conduit to be active
     * @param {{ maxQueriesForStatus?: number }=} [options]
     * @return {Promise}
     */
    static waitForActiveState(options?: {
        maxQueriesForStatus?: number;
    } | undefined): Promise<any>;
    /**
     * Gets the current conduit shared key.
     * @return {Promise<string>}
     */
    static getSharedKey(): Promise<string>;
    /**
     * Sets the conduit shared key.
     * @param {string} sharedKey
     * @return {Promise<string>}
     */
    static setSharedKey(sharedKey: string): Promise<string>;
    /**
     * Creates an instance of Conduit.
     *
     * @param {ConduitOptions} options
     */
    constructor(options: ConduitOptions);
    /**
     * @type {boolean}
     */
    shouldReconnect: boolean;
    /**
     * @type {boolean}
     */
    isConnecting: boolean;
    /**
     * @type {boolean}
     */
    isActive: boolean;
    /**
     * @type {WebSocket?}
     */
    socket: WebSocket | null;
    /**
     * @type {number}
     */
    port: number;
    /**
     * @type {string}
     */
    id: string;
    /**
     * @type {string}
     */
    sharedKey: string;
    isErroring: boolean;
    /**
     * The URL string for the WebSocket server.
     * @type {string}
     */
    get url(): string;
    set onmessage(onmessage: (arg0: MessageEvent) => any);
    /**
     * @type {function(MessageEvent)}
     */
    get onmessage(): (arg0: MessageEvent) => any;
    set onerror(onerror: (arg0: ErrorEvent) => any);
    /**
     * @type {function(ErrorEvent)}
     */
    get onerror(): (arg0: ErrorEvent) => any;
    set onclose(onclose: (arg0: CloseEvent) => any);
    /**
     * @type {function(CloseEvent)}
     */
    get onclose(): (arg0: CloseEvent) => any;
    set onopen(onopen: (arg0: Event) => any);
    /**
     * @type {function(Event)}
     */
    get onopen(): (arg0: Event) => any;
    /**
     * Connects the underlying conduit `WebSocket`.
     * @param {function(Error?)=} [callback]
     * @return {Promise<Conduit>}
     */
    connect(callback?: ((arg0: Error | null) => any) | undefined): Promise<Conduit>;
    /**
     * Reconnects a `Conduit` socket.
     * @param {{retries?: number, timeout?: number}} [options]
     * @return {Promise<Conduit>}
     */
    reconnect(options?: {
        retries?: number;
        timeout?: number;
    }): Promise<Conduit>;
    /**
     * Encodes a single header into a Uint8Array.
     *
     * @private
     * @param {string} key - The header key.
     * @param {string} value - The header value.
     * @returns {Uint8Array} The encoded header.
     */
    private encodeOption;
    /**
     * Encodes options and payload into a single Uint8Array message.
     *
     * @private
     * @param {object} options - The options to encode.
     * @param {Uint8Array} payload - The payload to encode.
     * @returns {Uint8Array} The encoded message.
     */
    private encodeMessage;
    /**
     * Decodes a Uint8Array message into options and payload.
     * @param {Uint8Array} data - The data to decode.
     * @return {ReceiveMessage} The decoded message containing options and payload.
     * @throws Will throw an error if the data is invalid.
     */
    decodeMessage(data: Uint8Array): ReceiveMessage;
    /**
     * Registers a callback to handle incoming messages.
     * The callback will receive an error object and an object containing
     * decoded options and payload.
     * @param {ReceiveCallback} callback - The callback function to handle incoming messages.
     */
    receive(callback: ReceiveCallback): void;
    _receiveCleanup: () => void;
    /**
     * Sends a message with the specified options and payload over the
     * WebSocket connection.
     * @param {object} options - The options to send.
     * @param {Uint8Array=} [payload] - The payload to send.
     * @return {boolean}
     */
    send(options: object, payload?: Uint8Array | undefined): boolean;
    /**
     * Closes the WebSocket connection, preventing reconnects.
     */
    close(): void;
    #private;
}
export default Conduit;
export type ReceiveMessage = {
    options: object;
    payload: Uint8Array;
};
export type ReceiveCallback = (arg0: Error | null, arg1: ReceiveMessage | undefined) => any;
export type ConduitDiagnostics = {
    isActive: boolean;
    handles: {
        ids: string[];
        count: number;
    };
};
export type ConduitStatus = {
    isActive: boolean;
    port: number;
    sharedKey: string;
};
export type ConduitOptions = {
    id?: string | bigint | number;
    sharedKey?: string;
};
