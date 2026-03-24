declare const _default: {
    types: Map<any, any>;
    fds: Map<any, any>;
    ids: Map<any, any>;
    get size(): number;
    get(id: any): any;
    syncOpenDescriptors(): Promise<void>;
    set(id: any, fd: any, type: any): void;
    has(id: any): boolean;
    fd(id: any): any;
    id(fd: any): any;
    release(id: any, closeDescriptor?: boolean): Promise<void>;
    retain(id: any): Promise<any>;
    delete(id: any): void;
    clear(): void;
    typeof(id: any): any;
    entries(): MapIterator<[any, any]>;
};
export default _default;
