/**
 * Query UDP capabilities from the runtime (multicast, broadcast, ipv6only, ssm).
 * @returns {Promise<{ multicast: boolean, broadcast: boolean, ipv6only: boolean, ssm: boolean }>}
 */
export function getCapabilities(): Promise<{
    multicast: boolean;
    broadcast: boolean;
    ipv6only: boolean;
    ssm: boolean;
}>;
/**
 * Convenience helper for Source-Specific Multicast support.
 * @returns {Promise<boolean>}
 */
export function isSSMSupported(): Promise<boolean>;
export function createSocket(options: string | any, callback?: ((arg0: Buffer, arg1: RemoteInfo) => any) | undefined): Socket;
/**
 * New instances of dgram.Socket are created using dgram.createSocket().
 * The new keyword is not to be used to create dgram.Socket instances.
 */
/**
 * Emitted when a new datagram is available to read.
 * @event Socket#message
 * @type {(msg: Buffer, rinfo: RemoteInfo) => void}
 */
/**
 * Emitted once the socket has been bound and is ready to receive messages.
 * @event Socket#listening
 * @type {() => void}
 */
/**
 * Emitted when an error occurs on the socket.
 * @event Socket#error
 * @type {(err: Error) => void}
 */
/**
 * Emitted when the socket has been closed.
 * @event Socket#close
 * @type {() => void}
 */
export class Socket extends EventEmitter {
    [x: number]: (options: any) => import("./gc.js").Finalizer;
    constructor(options: any, callback: any);
    knownIdWasGivenInSocketConstruction: boolean;
    dataListener: any;
    conduit: any;
    signal: any;
    usingDataEventFallback: boolean;
    type: string;
    id: string;
    state: {
        recvBufferSize: number;
        sendBufferSize: number;
        bindState: number;
        connectState: number;
        reuseAddr: boolean;
        ipv6Only: boolean;
        remoteAddress: {};
    };
    enableDataEventFallback(): void;
    disableDataEventFallback(): void;
    /**
     * Listen for datagram messages on a named port and optional address
     * If the address is not specified, the operating system will attempt to
     * listen on all addresses. Once the binding is complete, a 'listening'
     * event is emitted and the optional callback function is called.
     *
     * If binding fails, an 'error' event is emitted.
     *
     * @param {number} port - The port to listen for messages on
     * @param {string} address - The address to bind to (0.0.0.0)
     * @param {function} callback - With no parameters. Called when binding is complete.
     * @see {@link https://nodejs.org/api/dgram.html#socketbindport-address-callback}
     */
    bind(arg1: any, arg2: any, arg3: any): this;
    /**
     * Associates the dgram.Socket to a remote address and port. Every message sent
     * by this handle is automatically sent to that destination. Also, the socket
     * will only receive messages from that remote peer. Trying to call connect()
     * on an already connected socket will result in an ERR_SOCKET_DGRAM_IS_CONNECTED
     * exception. If the address is not provided, '0.0.0.0' (for udp4 sockets) or '::1'
     * (for udp6 sockets) will be used by default. Once the connection is complete,
     * a 'connect' event is emitted and the optional callback function is called.
     * In case of failure, the callback is called or, failing this, an 'error' event
     * is emitted.
     *
     * @param {number} port - Port the client should connect to.
     * @param {string=} host - Host the client should connect to.
     * @param {function=} connectListener - Common parameter of socket.connect() methods. Will be added as a listener for the 'connect' event once.
     * @see {@link https://nodejs.org/api/dgram.html#socketconnectport-address-callback}
     */
    connect(arg1: any, arg2: any, arg3: any): void;
    /**
     * A synchronous function that disassociates a connected dgram.Socket from
     * its remote address. Trying to call disconnect() on an unbound or already
     * disconnected socket will result in an ERR_SOCKET_DGRAM_NOT_CONNECTED exception.
     *
     * @see {@link https://nodejs.org/api/dgram.html#socketdisconnect}
     */
    disconnect(): void;
    /**
     * Broadcasts a datagram on the socket. For connectionless sockets, the
     * destination port and address must be specified. Connected sockets, on the
     * other hand, will use their associated remote endpoint, so the port and
     * address arguments must not be set.
     *
     * > The msg argument contains the message to be sent. Depending on its type,
     * different behavior can apply. If msg is a Buffer, any TypedArray, or a
     * DataView, the offset and length specify the offset within the Buffer where
     * the message begins and the number of bytes in the message, respectively.
     * If msg is a String, then it is automatically converted to a Buffer with
     * 'utf8' encoding. With messages that contain multi-byte characters, offset,
     * and length will be calculated with respect to byte length and not the
     * character position. If msg is an array, offset and length must not be
     * specified.
     *
     * > The address argument is a string. If the value of the address is a hostname,
     * DNS will be used to resolve the address of the host. If the address is not
     * provided or otherwise nullish, '0.0.0.0' (for udp4 sockets) or '::'
     * (for udp6 sockets) will be used by default.
     *
     * > If the socket has not been previously bound with a call to bind, the socket
     * is assigned a random port number and is bound to the "all interfaces"
     * address ('0.0.0.0' for udp4 sockets, '::' for udp6 sockets.)
     *
     * > An optional callback function may be specified as a way of reporting DNS
     * errors or for determining when it is safe to reuse the buf object. DNS
     * lookups delay the time to send for at least one tick of the Node.js event
     * loop.
     *
     * > The only way to know for sure that the datagram has been sent is by using a
     * callback. If an error occurs and a callback is given, the error will be
     * passed as the first argument to the callback. If a callback is not given,
     * the error is emitted as an 'error' event on the socket object.
     *
     * > Offset and length are optional but both must be set if either is used.
     * They are supported only when the first argument is a Buffer, a TypedArray,
     * or a DataView.
     *
     * @param {Buffer | TypedArray | DataView | string | Array} msg - Message to be sent.
     * @param {number=} offset - Offset in the buffer where the message starts.
     * @param {number=} length - Number of bytes in the message.
     * @param {number=} port - Destination port.
     * @param {string=} address - Destination host name or IP address.
     * @param {Function=} callback - Called when the message has been sent.
     * @see {@link https://nodejs.org/api/dgram.html#socketsendmsg-offset-length-port-address-callback}
     */
    send(buffer: any, ...args: any[]): any;
    /**
     * Close the underlying socket and stop listening for data on it. If a
     * callback is provided, it is added as a listener for the 'close' event.
     *
     * @param {function(Error?)} callback - Called when the connection is completed or on error.
     *
     * @see {@link https://nodejs.org/api/dgram.html#socketclosecallback}
     */
    close(cb?: any): this;
    /**
     *
     * Returns an object containing the address information for a socket. For
     * UDP sockets, this object will contain address, family, and port properties.
     *
     * This method throws EBADF if called on an unbound socket.
     * @returns {Object} socketInfo - Information about the local socket
     * @returns {string} socketInfo.address - The IP address of the socket
     * @returns {string} socketInfo.port - The port of the socket
     * @returns {string} socketInfo.family - The IP family of the socket
     *
     * @see {@link https://nodejs.org/api/dgram.html#socketaddress}
     */
    address(): any;
    /**
     * Returns an object containing the address, family, and port of the remote
     * endpoint. This method throws an ERR_SOCKET_DGRAM_NOT_CONNECTED exception
     * if the socket is not connected.
     *
     * @returns {Object} socketInfo - Information about the remote socket
     * @returns {string} socketInfo.address - The IP address of the socket
     * @returns {string} socketInfo.port - The port of the socket
     * @returns {string} socketInfo.family - The IP family of the socket
     * @see {@link https://nodejs.org/api/dgram.html#socketremoteaddress}
     */
    remoteAddress(): any;
    /**
     * Sets the SO_RCVBUF socket option. Sets the maximum socket receive buffer in
     * bytes.
     *
     * @param {number} size - The size of the new receive buffer
     * @see {@link https://nodejs.org/api/dgram.html#socketsetrecvbuffersizesize}
     */
    setRecvBufferSize(size: number): Promise<void>;
    /**
     * Sets the SO_SNDBUF socket option. Sets the maximum socket send buffer in
     * bytes.
     *
     * @param {number} size - The size of the new send buffer
     * @see {@link https://nodejs.org/api/dgram.html#socketsetsendbuffersizesize}
     */
    setSendBufferSize(size: number): Promise<void>;
    /**
     * @see {@link https://nodejs.org/api/dgram.html#socketgetrecvbuffersize}
     */
    getRecvBufferSize(): number;
    /**
     * @returns {number} the SO_SNDBUF socket send buffer size in bytes.
     * @see {@link https://nodejs.org/api/dgram.html#socketgetsendbuffersize}
     */
    getSendBufferSize(): number;
    /**
     * Enable or disable SO_BROADCAST on the socket.
     * @param {boolean=} [on=true]
     * @return {Promise<void>}
     */
    setBroadcast(on?: boolean | undefined): Promise<void>;
    /**
     * Set unicast TTL for outgoing packets.
     * @param {number=} [ttl=64]
     * @return {Promise<void>}
     */
    setTTL(ttl?: number | undefined): Promise<void>;
    /**
     * Set multicast TTL for outgoing multicast packets.
     * @param {number=} [ttl=1]
     * @return {Promise<void>}
     */
    setMulticastTTL(ttl?: number | undefined): Promise<void>;
    /**
     * Enable or disable multicast loopback.
     * @param {boolean=} [on=true]
     * @return {Promise<void>}
     */
    setMulticastLoopback(on?: boolean | undefined): Promise<void>;
    /**
     * Set the default network interface for multicast.
     * @param {string=} [iface=''] network interface name or address
     * @return {Promise<void>}
     */
    setMulticastInterface(iface?: string | undefined): Promise<void>;
    /**
     * Join a multicast group.
     * @param {string} address multicast group address
     * @param {string=} [iface=''] optional interface name or address
     * @return {Promise<void>}
     */
    addMembership(address: string, iface?: string | undefined): Promise<void>;
    /**
     * Leave a multicast group.
     * @param {string} address multicast group address
     * @param {string=} [iface=''] optional interface name or address
     * @return {Promise<void>}
     */
    dropMembership(address: string, iface?: string | undefined): Promise<void>;
    /**
     * Add source-specific multicast membership (if supported).
     * @param {string} address multicast group address (SSM range)
     * @param {string} source source address
     * @param {string=} [iface=''] optional interface name or address
     * @return {Promise<void>}
     */
    addSourceSpecificMembership(address: string, source: string, iface?: string | undefined): Promise<void>;
    /**
     * Drop source-specific multicast membership (if supported).
     * @param {string} address multicast group address (SSM range)
     * @param {string} source source address
     * @param {string=} [iface=''] optional interface name or address
     * @return {Promise<void>}
     */
    dropSourceSpecificMembership(address: string, source: string, iface?: string | undefined): Promise<void>;
    /**
     * Configure the socket as IPv6-only.
     * @param {boolean=} [on=true]
     * @return {Promise<void>}
     */
    setIPv6Only(on?: boolean | undefined): Promise<void>;
    ref(): this;
    unref(): this;
    #private;
}
/**
 * Generic error class for an error occurring on a `Socket` instance.
 * @ignore
 */
export class SocketError extends InternalError {
    /**
     * @type {string}
     */
    get code(): string;
}
/**
 * Thrown when a socket is already bound.
 */
export class ERR_SOCKET_ALREADY_BOUND extends exports.SocketError {
    constructor();
}
/**
 * @ignore
 */
export class ERR_SOCKET_BAD_BUFFER_SIZE extends exports.SocketError {
    constructor(message: any);
}
/**
 * @ignore
 */
export class ERR_SOCKET_BUFFER_SIZE extends exports.SocketError {
    constructor(message: any);
}
/**
 * Thrown when the socket is already connected.
 */
export class ERR_SOCKET_DGRAM_IS_CONNECTED extends exports.SocketError {
    constructor();
}
/**
 * Thrown when the socket is not connected.
 */
export class ERR_SOCKET_DGRAM_NOT_CONNECTED extends exports.SocketError {
    constructor();
    syscall: string;
}
/**
 * Thrown when the socket is not running (not bound or connected).
 */
export class ERR_SOCKET_DGRAM_NOT_RUNNING extends exports.SocketError {
    constructor();
}
/**
 * Thrown when a bad socket type is used in an argument.
 */
export class ERR_SOCKET_BAD_TYPE extends TypeError {
    constructor();
    code: string;
}
/**
 * Thrown when a bad port is given.
 */
export class ERR_SOCKET_BAD_PORT extends RangeError {
    constructor(message: any);
    code: string;
}
export default exports;
export type RemoteInfo = {
    /**
     * - The IP address of the socket
     */
    address: string;
    /**
     * - The port of the socket
     */
    port: number;
    /**
     * - The IP family of the socket
     */
    family: "IPv4" | "IPv6";
};
export type SocketOptions = any;
import { Buffer } from './buffer.js';
import { EventEmitter } from './events.js';
import { InternalError } from './errors.js';
import * as exports from './dgram.js';
