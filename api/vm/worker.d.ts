declare function isTypedArray(object: any): boolean;
declare function isArrayBuffer(object: any): object is ArrayBuffer;
declare function findMessageTransfers(transfers: any, object: any, options?: any): any;
declare const Uint8ArrayPrototype: Uint8Array<ArrayBufferLike>;
declare const TypedArrayPrototype: any;
declare const TypedArray: any;
declare class Client extends EventTarget {
    constructor(id: any, port: any);
    id: any;
    port: any;
    onMessage(event: any): void;
    postMessage(...args: any[]): any;
    destroy(): void;
}
declare class Realm {
    constructor(state: any, port: any);
    /**
     * The `MessagePort` for the VM realm.
     * @type {MessagePort}
     */
    port: MessagePort;
    /**
     * A reference to the top level worker statae
     * @type {State}
     */
    state: State;
    /**
     * Known content worlds that exist in a realm
     * @type {Map<String, World>}
     */
    worlds: Map<string, World>;
    get clients(): Map<any, any>;
    postMessage(...args: any[]): void;
}
declare class State {
    static init(): State;
    /**
     * All known connected `MessagePort` instances
     * @type {MessagePort[]}
     */
    ports: MessagePort[];
    /**
     * Pending events to be dispatched to realm
     * @type {MessageEvent[]}
     */
    pending: MessageEvent[];
    /**
     * The realm for all virtual machines. This is a headless webview
     */
    realm: any;
    clients: Map<any, any>;
    onConnect(event: any): void;
    init(): void;
    onPortMessage(port: any, event: any): void;
}
