export class TCPServerSocket {
    /**
     * @typedef {Object} TCPServerSocketOptions
     * @property {number} [localPort] - 0 to have OS pick a free port
     * @property {number} [backlog] - Size of accept queue; platform default if omitted
     */
    constructor(localAddress: any, options?: {});
    /** @type {Promise<{ readable: ReadableStream<any>, localAddress: string, localPort: number }>} */
    get opened(): Promise<{
        readable: ReadableStream<any>;
        localAddress: string;
        localPort: number;
    }>;
    /** @type {Promise<void>} */
    get closed(): Promise<void>;
    close(): Promise<void>;
    #private;
}
export default TCPServerSocket;
