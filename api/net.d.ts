export function createServer(options: any, connectionListener: any): TCPServer;
export function createConnection(options: any, cb: any): TCPSocket;
export function connect(options: any, cb: any): TCPSocket;
declare class TCPServer extends EventEmitter {
    constructor(options: {}, connectionListener: any);
    id: bigint;
    _listening: boolean;
    _clients: Set<any>;
    _timeoutMs: number;
    _timeoutHandler: any;
    _defaults: {
        noDelay: boolean;
        keepAlive: boolean;
        keepAliveDelay: number;
    };
    listen(port: any, host: string, backlog: number, cb: any): this;
    _ondata: (ev: any) => void;
    setTimeout(ms: any, cb: any): this;
    close(cb: any): Promise<void>;
    getConnections(cb: any): number;
    waitClose(timeoutMs?: number): Promise<boolean>;
    address(): {
        address: any;
        port: any;
    };
}
declare class TCPSocket extends EventEmitter {
    constructor(id: any, opts: any);
    id: any;
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
    _timeoutMs: number;
    _timeoutTimer: any;
    _writeHandler: (ev: any) => void;
    _inflight: {
        cb: any;
    };
    _bumpTimeout(): void;
    connect(port: any, host: string, cb: any): this;
    _connectHandler: (ev: any) => void;
    _startRead(): void;
    _globalHandler: (ev: any) => void;
    write(chunk: any, cb: any): boolean;
    _flushQueue(): void;
    address(): {
        address: any;
        port: any;
    };
    remoteAddressInfo(): {
        address: any;
        port: any;
    };
    get remoteAddress(): any;
    get remotePort(): any;
    setNoDelay(on?: boolean): boolean;
    setKeepAlive(on?: boolean, initialDelaySec?: number): boolean;
    end(chunk: any, cb: any): void;
    _onShutdown: (ev: any) => void;
    _endTimer: number;
    setTimeout(ms: any, cb: any): this;
    destroy(): void;
}
import { EventEmitter } from './events.js';
export {};
