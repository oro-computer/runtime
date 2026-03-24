export function normalizeLogLevel(level: any): {
    value: any;
    name: any;
};
export function init(): Promise<boolean>;
export function shutdown(): Promise<boolean>;
export function status(): Promise<{
    initialized: boolean;
    version: any;
    logLevel: {
        value: any;
        name: any;
    };
}>;
export function ensureInitialized(): Promise<{
    initialized: boolean;
    version: any;
    logLevel: {
        value: any;
        name: any;
    };
}>;
export function version(): Promise<any>;
export function setLogLevel(level: any): Promise<{
    value: any;
    name: any;
}>;
export function getLogLevelName(level: any): any;
export function getLogLevelValue(level: any): any;
export function pathToKey(path: any, options?: {}): Promise<Uint8Array<any>>;
export function keyToPath(key: any, options?: {}): Promise<string>;
export const LOG_LEVELS: Readonly<{
    trace: 0;
    debug: 1;
    info: 2;
    warn: 3;
    error: 4;
    off: 5;
}>;
export class Endpoint {
    static create(options?: {}): Promise<Endpoint>;
    constructor(id: any);
    id: any;
    closed: boolean;
    bind(options?: {}): Promise<void>;
    homeRelay(): Promise<string>;
    nodeAddr(): Promise<string>;
    connect(options: any): Promise<Connection>;
    accept(options?: {}): Promise<Connection>;
    acceptAny(options?: {}): Promise<Connection>;
    close(): Promise<void>;
}
export class Connection {
    constructor(endpoint: any, id: any);
    endpoint: any;
    id: any;
    closed: boolean;
    remoteAlpn: any;
    close(): Promise<void>;
    waitClosed(): Promise<void>;
    stats(): Promise<{
        connectionId: any;
        maxDatagramSize: number;
        rtt: number;
        packetLoss: number;
    }>;
    writeDatagram(data: any, options?: {}): Promise<void>;
    readDatagram(options?: {}): Promise<Uint8Array<any>>;
    openBidirectionalStream(options?: {}): Promise<(SendStream | RecvStream)[]>;
    openUnidirectionalStream(options?: {}): Promise<SendStream>;
    acceptBidirectionalStream(options?: {}): Promise<(SendStream | RecvStream)[]>;
    acceptUnidirectionalStream(options?: {}): Promise<RecvStream>;
    watchConnectionType(nodeId: any, listener: any): Promise<() => void>;
}
export class SendStream {
    constructor(connection: any, id: any);
    connection: any;
    id: any;
    closed: boolean;
    write(data: any, options?: {}): Promise<void>;
    finish(): Promise<void>;
}
export class RecvStream {
    constructor(connection: any, id: any);
    connection: any;
    id: any;
    read(length: any, options?: {}): Promise<Uint8Array<any>>;
    readToEnd(sizeLimit: any, timeoutMs: any): Promise<Uint8Array<any>>;
}
export default api;
declare const api: Readonly<{
    LOG_LEVELS: Readonly<{
        trace: 0;
        debug: 1;
        info: 2;
        warn: 3;
        error: 4;
        off: 5;
    }>;
    init: typeof init;
    shutdown: typeof shutdown;
    status: typeof status;
    ensureInitialized: typeof ensureInitialized;
    version: typeof version;
    setLogLevel: typeof setLogLevel;
    getLogLevelName: typeof getLogLevelName;
    getLogLevelValue: typeof getLogLevelValue;
    normalizeLogLevel: typeof normalizeLogLevel;
    pathToKey: typeof pathToKey;
    keyToPath: typeof keyToPath;
    Endpoint: typeof Endpoint;
    Connection: typeof Connection;
    SendStream: typeof SendStream;
    RecvStream: typeof RecvStream;
}>;
