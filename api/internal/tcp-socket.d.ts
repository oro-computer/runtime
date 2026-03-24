export class TCPSocket {
    static kFromNetSocket: symbol;
    /**
     * @param {string} remoteAddress - Hostname or IP.
     * @param {number} remotePort - Destination port (0..65535).
     * @param {TCPSocketOptions} [options]
     *
     * Notes
     * - Gating: if disabled by policy, `opened` rejects immediately and no
     *   underlying socket is created.
     * - 'opened' resolution: deferred until native emits 'connect'; errors
     *   before that reject `opened`.
     * - Readable semantics: enqueues Uint8Array; closes on 'end'.
     * - Writable semantics: resolves per-write callback; backpressure is
     *   handled by the underlying socket and surfaced via the callback.
     */
    constructor(remoteAddress: string, remotePort: number, options?: {
        /**
         * - Enable/disable Nagle’s algorithm
         */
        noDelay?: boolean;
        /**
         * - Alias for enabling TCP keepalive
         */
        keepAlive?: boolean;
        /**
         * - Seconds between TCP keepalive probes
         */
        keepAliveDelay?: number;
        /**
         * - Not currently used by the runtime
         */
        sendBufferSize?: number;
        /**
         * - Not currently used by the runtime
         */
        receiveBufferSize?: number;
        /**
         * - Hint for name resolution
         */
        dnsQueryType?: "ipv4" | "ipv6";
    });
    /** @type {Promise<{ readable: ReadableStream<Uint8Array>, writable: WritableStream<BufferSource>, remoteAddress: string, remotePort: number, localAddress: string, localPort: number }>} */
    get opened(): Promise<{
        readable: ReadableStream<Uint8Array>;
        writable: WritableStream<BufferSource>;
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
export default TCPSocket;
