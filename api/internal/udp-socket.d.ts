export class UDPSocket {
    /**
     * @typedef {Object} UDPSocketOptions
     * @property {string} [remoteAddress]
     * @property {number} [remotePort]
     * @property {string} [localAddress]
     * @property {number} [localPort]
     * @property {'ipv4'|'ipv6'} [dnsQueryType]
     * @property {number} [sendBufferSize]
     * @property {number} [receiveBufferSize]
     * @description Provide either remoteAddress/remotePort (connected mode) OR localAddress[/localPort] (bound mode). Options are mutually exclusive.
     */
    /**
     * @typedef {Object} UDPMessage
     * @property {BufferSource} data
     * @property {string} [remoteAddress] - Required in bound mode for send; omitted in connected mode
     * @property {number} [remotePort] - Required in bound mode for send; omitted in connected mode
     */
    constructor(options: any);
    /** @type {Promise<{ readable: ReadableStream<any>, writable: WritableStream<any>, remoteAddress: string, remotePort: number, localAddress: string, localPort: number }>} */
    get opened(): Promise<{
        readable: ReadableStream<any>;
        writable: WritableStream<any>;
        remoteAddress: string;
        remotePort: number;
        localAddress: string;
        localPort: number;
    }>;
    /** @type {Promise<void>} */
    get closed(): Promise<void>;
    close(): Promise<void>;
    #private;
}
export default UDPSocket;
