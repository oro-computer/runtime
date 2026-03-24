export class Client {
    constructor(options: any);
    get id(): any;
    get url(): any;
    get type(): any;
    get frameType(): any;
    postMessage(message: any, optionsOrTransferables?: any): void;
    #private;
}
export class WindowClient extends Client {
    get focused(): boolean;
    get ancestorOrigins(): any[];
    get visibilityState(): string;
    focus(): Promise<this>;
    navigate(url: any): Promise<this>;
    #private;
}
export class Clients {
    get(id: any): Promise<Client>;
    matchAll(options?: any): Promise<any>;
    openWindow(url: any, options?: any): Promise<WindowClient>;
    claim(): Promise<void>;
}
declare const _default: Clients;
export default _default;
