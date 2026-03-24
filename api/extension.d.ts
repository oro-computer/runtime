/**
 * Load an extension by name.
 * @template {Record<string, any> T}
 * @param {string} name
 * @param {ExtensionLoadOptions} [options]
 * @return {Promise<Extension<T>>}
 */
export function load<T extends Record<string, any>>(name: string, options?: ExtensionLoadOptions): Promise<Extension<T>>;
/**
 * Provides current stats about the loaded extensions.
 * @return {Promise<ExtensionStats>}
 */
export function stats(): Promise<ExtensionStats>;
/**
 * @typedef {{
 *   allow: string[] | string,
 *   imports?: object,
 *   type?: 'shared' | 'wasm32',
 *   path?: string,
 *   stats?: object,
 *   instance?: WebAssembly.Instance,
 *   adapter?: WebAssemblyExtensionAdapter
 * }} ExtensionLoadOptions
 */
/**
 * @typedef {{ abi: number, version: string, description: string }} ExtensionInfo
 */
/**
 * @typedef {{ abi: number, loaded: number }} ExtensionStats
 */
/**
 * A interface for a native extension.
 * @template {Record<string, any> T}
 */
export class Extension<T extends Record<string, any>> extends EventTarget {
    /**
     * Load an extension by name.
     * @template {Record<string, any> T}
     * @param {string} name
     * @param {ExtensionLoadOptions} [options]
     * @return {Promise<Extension<T>>}
     */
    static load<T_1 extends Record<string, any>>(name: string, options?: ExtensionLoadOptions): Promise<Extension<T_1>>;
    /**
     * Query type of extension by name.
     * @param {string} name
     * @return {Promise<'shared'|'wasm32'|'unknown'|null>}
     */
    static type(name: string): Promise<"shared" | "wasm32" | "unknown" | null>;
    /**
     * Provides current stats about the loaded extensions or one by name.
     * @param {?string} name
     * @return {Promise<ExtensionStats|null>}
     */
    static stats(name: string | null): Promise<ExtensionStats | null>;
    /**
     * `Extension` class constructor.
     * @param {string} name
     * @param {ExtensionInfo} info
     * @param {ExtensionLoadOptions} [options]
     */
    constructor(name: string, info: ExtensionInfo, options?: ExtensionLoadOptions);
    /**
     * The name of the extension
     * @type {string?}
     */
    name: string | null;
    /**
     * The version of the extension
     * @type {string?}
     */
    version: string | null;
    /**
     * The description of the extension
     * @type {string?}
     */
    description: string | null;
    /**
     * The abi of the extension
     * @type {number}
     */
    abi: number;
    /**
     * @type {object}
     */
    options: object;
    /**
     * @type {T}
     */
    binding: T;
    /**
     * Not `null` if extension is of type 'wasm32'
     * @type {?WebAssemblyExtensionAdapter}
     */
    adapter: WebAssemblyExtensionAdapter | null;
    /**
     * `true` if the extension was loaded, otherwise `false`
     * @type {boolean}
     */
    get loaded(): boolean;
    /**
     * The extension type: 'shared' or  'wasm32'
     * @type {'shared'|'wasm32'}
     */
    get type(): "shared" | "wasm32";
    /**
     * Unloads the loaded extension.
     * @throws Error
     */
    unload(): Promise<boolean>;
    instance: any;
    [$type]: "shared" | "wasm32";
    [$loaded]: boolean;
}
declare namespace _default {
    export { load };
    export { stats };
}
export default _default;
export type Pointer = number;
export type ExtensionLoadOptions = {
    allow: string[] | string;
    imports?: object;
    type?: "shared" | "wasm32";
    path?: string;
    stats?: object;
    instance?: WebAssembly.Instance;
    adapter?: WebAssemblyExtensionAdapter;
};
export type ExtensionInfo = {
    abi: number;
    version: string;
    description: string;
};
export type ExtensionStats = {
    abi: number;
    loaded: number;
};
/**
 * An adapter for reading and writing various values from a WebAssembly instance's
 * memory buffer.
 * @ignore
 */
declare class WebAssemblyExtensionAdapter {
    constructor({ instance, module, table, memory, policies }: {
        instance: any;
        module: any;
        table: any;
        memory: any;
        policies: any;
    });
    view: any;
    heap: any;
    table: any;
    stack: any;
    buffer: any;
    module: any;
    memory: any;
    context: any;
    policies: any[];
    externalReferences: Map<any, any>;
    instance: any;
    exitStatus: any;
    textDecoder: TextDecoder;
    textEncoder: TextEncoder;
    errorMessagePointers: {};
    indirectFunctionTable: any;
    get globalBaseOffset(): any;
    destroy(): void;
    init(): boolean;
    getExtensionExport(...names: any[]): any;
    get(pointer: any, size?: number): any;
    set(pointer: any, value: any): void;
    createExternalReferenceValue(value: any): any;
    getExternalReferenceValue(pointer: any): any;
    setExternalReferenceValue(pointer: any, value: any): Map<any, any>;
    removeExternalReferenceValue(pointer: any): void;
    getExternalReferencePointer(value: any): any;
    getFloat32(pointer: any): any;
    setFloat32(pointer: any, value: any): boolean;
    getFloat64(pointer: any): any;
    setFloat64(pointer: any, value: any): boolean;
    getInt8(pointer: any): any;
    setInt8(pointer: any, value: any): boolean;
    getInt16(pointer: any): any;
    setInt16(pointer: any, value: any): boolean;
    getInt32(pointer: any): any;
    setInt32(pointer: any, value: any): boolean;
    getUint8(pointer: any): any;
    setUint8(pointer: any, value: any): boolean;
    getUint16(pointer: any): any;
    setUint16(pointer: any, value: any): boolean;
    getUint32(pointer: any): any;
    setUint32(pointer: any, value: any): boolean;
    getString(pointer: any, buffer: any, size: any): string;
    setString(pointer: any, string: any, buffer?: any): boolean;
}
declare const $type: unique symbol;
/**
 * @typedef {number} Pointer
 */
declare const $loaded: unique symbol;
