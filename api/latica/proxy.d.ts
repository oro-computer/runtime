export default PeerWorkerProxy;
/**
 * `Proxy` class factory, returns a Proxy class that is a proxy to the Peer.
 * @param {{ createSocket: function('udp4', null, object?): object }} options
 */
export class PeerWorkerProxy {
    constructor(options: any, port: any, fn: any);
    init(): Promise<any>;
    reconnect(): Promise<any>;
    disconnect(): Promise<any>;
    getInfo(): Promise<any>;
    getMetrics(): Promise<any>;
    getState(): Promise<any>;
    open(...args: any[]): Promise<any>;
    seal(...args: any[]): Promise<any>;
    sealUnsigned(...args: any[]): Promise<any>;
    openUnsigned(...args: any[]): Promise<any>;
    addEncryptionKey(...args: any[]): Promise<any>;
    send(...args: any[]): Promise<any>;
    sendUnpublished(...args: any[]): Promise<any>;
    cacheInsert(...args: any[]): Promise<any>;
    mcast(...args: any[]): Promise<any>;
    requestReflection(...args: any[]): Promise<any>;
    stream(...args: any[]): Promise<any>;
    join(...args: any[]): Promise<any>;
    publish(...args: any[]): Promise<any>;
    sync(...args: any[]): Promise<any>;
    close(...args: any[]): Promise<any>;
    query(...args: any[]): Promise<any>;
    compileCachePredicate(src: any): Promise<any>;
    callWorkerThread(prop: any, data: any): any;
    callMainThread(prop: any, args: any): void;
    resolveMainThread(seq: any, result: any): any;
    #private;
}
