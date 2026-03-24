export function connect(options: any, cb: any, ...args: any[]): {
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
    connect(port: any, host: string, cb: any): /*elided*/ any;
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
    setTimeout(ms: any, cb: any): /*elided*/ any;
    destroy(): void;
    _events: any;
    _contexts: any;
    _eventsCount: number;
    _maxListeners: number;
    setMaxListeners(n: any): /*elided*/ any;
    getMaxListeners(): any;
    emit(type: any, ...args: any[]): boolean;
    addListener(type: any, listener: any): any;
    on(arg0: any, arg1: any): any;
    prependListener(type: any, listener: any): any;
    once(type: any, listener: any): /*elided*/ any;
    prependOnceListener(type: any, listener: any): /*elided*/ any;
    removeListener(type: any, listener: any): /*elided*/ any;
    off(type: any, listener: any): /*elided*/ any;
    removeAllListeners(type: any, ...args: any[]): /*elided*/ any;
    listeners(type: any): any[];
    rawListeners(type: any): any[];
    listenerCount(type: any): any;
    eventNames(): (string | symbol)[];
};
export const createServer: typeof _createServer;
declare namespace _default {
    export { connect };
    export { createServer };
}
export default _default;
import { createServer as _createServer } from './net.js';
