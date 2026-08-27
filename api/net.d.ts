/**
 * Creates a TCP server.
 * @param {TCPServerOptions|TCPConnectionListener} [options]
 * @param {TCPConnectionListener} [connectionListener]
 * @returns {TCPServer}
 */
export function createServer(options?: TCPServerOptions | TCPConnectionListener, connectionListener?: TCPConnectionListener): TCPServer;
/**
 * Creates and connects a TCP socket.
 * @param {number|TCPConnectOptions} options
 * @param {string|(() => void)} [host]
 * @param {() => void} [cb]
 * @returns {TCPSocket}
 */
export function createConnection(options: number | TCPConnectOptions, host?: string | (() => void), cb?: () => void): TCPSocket;
/**
 * Alias for {@link createConnection}.
 * @param {number|TCPConnectOptions} options
 * @param {string|(() => void)} [host]
 * @param {() => void} [cb]
 * @returns {TCPSocket}
 */
export function connect(options: number | TCPConnectOptions, host?: string | (() => void), cb?: () => void): TCPSocket;
export class TCPSocket extends EventEmitter {
    /**
     * @param {string|number|bigint|TCPSocketOptions} [id]
     * @param {TCPSocketOptions} [opts]
     */
    constructor(id?: string | number | bigint | TCPSocketOptions, opts?: TCPSocketOptions);
    id: string;
    _reading: boolean;
    _destroyed: boolean;
    _connected: boolean;
    _connecting: boolean;
    _ended: boolean;
    _remote: {
        address: any;
        port: any;
    };
    _local: {
        address: any;
        port: any;
    };
    _writing: boolean;
    _queue: any[];
    _bufferedBytes: number;
    _needDrain: boolean;
    _ending: boolean;
    _shutdownStarted: boolean;
    writableHighWaterMark: any;
    _timeoutMs: number;
    _timeoutTimer: any;
    _writeHandler: (ev: any) => void;
    _inflight: {
        cb: any;
        length: any;
    };
    _bumpTimeout(): void;
    /**
     * Connects this socket to a remote TCP endpoint.
     * @param {number|TCPConnectOptions} port
     * @param {string|(() => void)} [host]
     * @param {() => void} [cb]
     * @returns {TCPSocket}
     */
    connect(port: number | TCPConnectOptions, host?: string | (() => void), cb?: () => void): TCPSocket;
    _connectHandler: (ev: any) => void;
    _startRead(): void;
    _globalHandler: (ev: any) => void;
    /**
     * Queues bytes for writing.
     * @param {string|Buffer|Uint8Array|ArrayBuffer|DataView} chunk
     * @param {string|TCPCallback} [encoding]
     * @param {TCPCallback} [cb]
     * @returns {boolean}
     */
    write(chunk: string | Buffer | Uint8Array | ArrayBuffer | DataView, encoding?: string | TCPCallback, cb?: TCPCallback): boolean;
    _emitDrainIfNeeded(): void;
    _flushQueue(): void;
    /**
     * Returns this socket's local address, when available.
     * @returns {TCPAddress|null}
     */
    address(): TCPAddress | null;
    /**
     * Returns this socket's remote address, when connected.
     * @returns {TCPAddress|null}
     */
    remoteAddressInfo(): TCPAddress | null;
    get remoteAddress(): string;
    get remotePort(): number;
    /** @returns {string|undefined} */
    get localAddress(): string | undefined;
    /** @returns {number|undefined} */
    get localPort(): number | undefined;
    /** @returns {boolean} */
    get destroyed(): boolean;
    /** @returns {boolean} */
    get connecting(): boolean;
    /** @returns {boolean} */
    get pending(): boolean;
    /** @returns {number} */
    get writableLength(): number;
    /**
     * Enables or disables TCP_NODELAY.
     * @param {boolean} [on=true]
     * @returns {boolean}
     */
    setNoDelay(on?: boolean): boolean;
    /**
     * Enables or disables TCP keepalive.
     * @param {boolean} [on=true]
     * @param {number} [initialDelaySec=0]
     * @returns {boolean}
     */
    setKeepAlive(on?: boolean, initialDelaySec?: number): boolean;
    /**
     * Optionally writes a final chunk and half-closes the socket.
     * @param {string|Buffer|Uint8Array|ArrayBuffer|DataView} [chunk]
     * @param {string|(() => void)} [encoding]
     * @param {() => void} [cb]
     * @returns {TCPSocket}
     */
    end(chunk?: string | Buffer | Uint8Array | ArrayBuffer | DataView, encoding?: string | (() => void), cb?: () => void): TCPSocket;
    _endCallback: () => void;
    _shutdown(): void;
    _onShutdown: (ev: any) => void;
    _endTimer: number;
    /**
     * Sets the inactivity timeout.
     * @param {number} ms
     * @param {() => void} [cb]
     * @returns {TCPSocket}
     */
    setTimeout(ms: number, cb?: () => void): TCPSocket;
    /**
     * Closes the socket and releases its native handle.
     * @returns {TCPSocket}
     */
    destroy(): TCPSocket;
}
export class TCPServer extends EventEmitter {
    /**
     * @param {TCPServerOptions} [options]
     * @param {TCPConnectionListener} [connectionListener]
     */
    constructor(options?: TCPServerOptions, connectionListener?: TCPConnectionListener);
    id: string;
    _listening: boolean;
    _clients: Set<any>;
    _timeoutMs: any;
    _timeoutHandler: (socket: TCPSocket) => void;
    _defaults: {
        noDelay: boolean;
        keepAlive: boolean;
        keepAliveDelay: any;
        writableHighWaterMark: any;
    };
    /**
     * Starts accepting connections.
     * @param {number|TCPListenOptions} port
     * @param {string|TCPCallback} [host]
     * @param {number|TCPCallback} [backlog]
     * @param {TCPCallback} [cb]
     * @returns {TCPServer}
     */
    listen(port: number | TCPListenOptions, host?: string | TCPCallback, backlog?: number | TCPCallback, cb?: TCPCallback): TCPServer;
    _ondata: (ev: any) => void;
    /**
     * Sets the inactivity timeout applied to subsequently accepted sockets.
     * @param {number} ms
     * @param {(socket: TCPSocket) => void} [cb]
     * @returns {TCPServer}
     */
    setTimeout(ms: number, cb?: (socket: TCPSocket) => void): TCPServer;
    /**
     * Stops accepting connections and closes tracked client sockets.
     * @param {TCPCallback} [cb]
     * @returns {Promise<void>}
     */
    close(cb?: TCPCallback): Promise<void>;
    /**
     * Returns the number of tracked client connections.
     * @param {(err: Error|null, count: number) => void} [cb]
     * @returns {number}
     */
    getConnections(cb?: (err: Error | null, count: number) => void): number;
    /**
     * Waits for tracked clients to close, up to a bounded timeout.
     * @param {number} [timeoutMs=2000]
     * @returns {Promise<boolean>}
     */
    waitClose(timeoutMs?: number): Promise<boolean>;
    /**
     * Returns the server's bound address, when available.
     * @returns {TCPAddress|null}
     */
    address(): TCPAddress | null;
    /** @returns {boolean} */
    get listening(): boolean;
}
declare namespace _default {
    export { connect };
    export { createConnection };
    export { createServer };
    export { TCPSocket as Socket };
    export { TCPServer as Server };
}
export default _default;
export type TCPAddress = {
    address: string;
    port: number;
};
export type TCPConnectOptions = {
    port: number;
    host?: string;
    timeout?: number;
    writableHighWaterMark?: number;
};
export type TCPServerOptions = {
    noDelay?: boolean;
    keepAlive?: boolean;
    keepAliveDelay?: number;
    timeout?: number;
    writableHighWaterMark?: number;
};
export type TCPListenOptions = {
    port: number;
    host?: string;
    backlog?: number;
};
export type TCPSocketOptions = {
    id?: string | number | bigint;
    existing?: boolean;
    writableHighWaterMark?: number;
};
export type TCPConnectionListener = (socket: TCPSocket) => void;
export type TCPCallback = (err?: Error) => void;
import { EventEmitter } from './events.js';
import { Buffer } from './buffer.js';
export { TCPSocket as Socket, TCPServer as Server };
