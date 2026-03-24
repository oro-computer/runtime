/**
 * @ignore
 * @param {object[]} transfer
 * @param {object} object
 * @param {object=} [options]
 * @return {object[]}
 */
export function findMessageTransfers(transfers: any, object: object, options?: object | undefined): object[];
/**
 * @ignore
 * @param {object} context
 */
export function applyInputContextReferences(context: object): void;
/**
 * @ignore
 * @param {object} context
 */
export function applyOutputContextReferences(context: object): void;
/**
 * @ignore
 * @param {object} context
 */
export function filterNonTransferableValues(context: object): void;
/**
 * @ignore
 * @param {object=} [currentContext]
 * @param {object=} [updatedContext]
 * @param {object=} [contextReference]
 * @return {{ deletions: string[], merges: string[] }}
 */
export function applyContextDifferences(currentContext?: object | undefined, updatedContext?: object | undefined, contextReference?: object | undefined, preserveScriptArgs?: boolean): {
    deletions: string[];
    merges: string[];
};
/**
 * Wrap a JavaScript function source.
 * @ignore
 * @param {string} source
 * @param {object=} [options]
 */
export function wrapFunctionSource(source: string, options?: object | undefined): string;
/**
 * Gets the VM context window.
 * This function will create it if it does not already exist.
 * @return {Promise<import('./window.js').ApplicationWindow}
 */
export function getContextWindow(): Promise<import("./window.js").ApplicationWindow>;
/**
 * Gets the `SharedWorker` that for the VM context.
 * @return {Promise<SharedWorker>}
 */
export function getContextWorker(): Promise<SharedWorker>;
/**
 * Terminates the VM script context window.
 * @ignore
 */
export function terminateContextWindow(): Promise<void>;
/**
 * Terminates the VM script context worker.
 * @ignore
 */
export function terminateContextWorker(): Promise<void>;
/**
 * Creates a prototype object of known global reserved intrinsics.
 * @ignore
 */
export function createIntrinsics(options: any): any;
/**
 * Returns `true` if value is an intrinsic, otherwise `false`.
 * @param {any} value
 * @return {boolean}
 */
export function isIntrinsic(value: any): boolean;
/**
 * Get the intrinsic type of a given `value`.
 * @param {any}
 * @return {function|object|null|undefined}
 */
export function getIntrinsicType(value: any): Function | object | null | undefined;
/**
 * Get the intrinsic type string of a given `value`.
 * @param {any}
 * @return {string|null}
 */
export function getIntrinsicTypeString(value: any): string | null;
/**
 * Creates a global proxy object for context execution.
 * @ignore
 * @param {object} context
 * @param {object=} [options]
 * @return {Proxy}
 */
export function createGlobalObject(context: object, options?: object | undefined): ProxyConstructor;
/**
 * @ignore
 * @param {string} source
 * @return {boolean}
 */
export function detectFunctionSourceType(source: string): boolean;
/**
 * Compiles `source`  with `options` into a function.
 * @ignore
 * @param {string} source
 * @param {object=} [options]
 * @return {function}
 */
export function compileFunction(source: string, options?: object | undefined): Function;
/**
 * Run `source` JavaScript in given context. The script context execution
 * context is preserved until the `context` object that points to it is
 * garbage collected or there are no longer any references to it and its
 * associated `Script` instance.
 * @param {string|object|function} source
 * @param {object=} [context]
 * @param {ScriptOptions=} [options]
 * @return {Promise<any>}
 */
export function runInContext(source: string | object | Function, context?: object | undefined, options?: ScriptOptions | undefined): Promise<any>;
/**
 * Run `source` JavaScript in new context. The script context is destroyed after
 * execution. This is typically a "one off" isolated run.
 * @param {string} source
 * @param {object=} [context]
 * @param {ScriptOptions=} [options]
 * @return {Promise<any>}
 */
export function runInNewContext(source: string, context?: object | undefined, options?: ScriptOptions | undefined): Promise<any>;
/**
 * Run `source` JavaScript in this current context (`globalThis`).
 * @param {string} source
 * @param {ScriptOptions=} [options]
 * @return {Promise<any>}
 */
export function runInThisContext(source: string, options?: ScriptOptions | undefined): Promise<any>;
/**
 * @ignore
 * @param {Reference} reference
 */
export function putReference(reference: Reference): void;
/**
 * Create a `Reference` for a `value` in a script `context`.
 * @param {any} value
 * @param {object} context
 * @param {object=} [options]
 * @return {Reference}
 */
export function createReference(value: any, context: object, options?: object | undefined): Reference;
/**
 * Get a script context by ID or values
 * @param {string|object|function} id
 * @return {Reference?}
 */
export function getReference(id: string | object | Function): Reference | null;
/**
 * Remove a script context reference by ID.
 * @param {string} id
 */
export function removeReference(id: string): void;
/**
 * Get all transferable values in the `object` hierarchy.
 * @param {object} object
 * @return {object[]}
 */
export function getTransferables(object: object): object[];
/**
 * @ignore
 * @param {object} object
 * @return {object}
 */
export function createContext(object: object): object;
/**
 * Returns `true` if `object` is a "context" object.
 * @param {object}
 * @return {boolean}
 */
export function isContext(object: any): boolean;
/**
 * Shared broadcast for virtual machaines
 * @type {BroadcastChannel}
 */
export const channel: BroadcastChannel;
/**
 * A container for a context worker message channel that looks like a "worker".
 * @ignore
 */
export class ContextWorkerInterface extends EventTarget {
    get channel(): any;
    get port(): any;
    destroy(): void;
    #private;
}
/**
 * A container proxy for a context worker message channel that
 * looks like a "worker".
 * @ignore
 */
export class ContextWorkerInterfaceProxy extends EventTarget {
    [x: number]: () => {
        args: any[];
        handle(port: any): Promise<void>;
    };
    constructor(globals: any);
    get port(): any;
    #private;
}
/**
 * Global reserved values that a script context may not modify.
 * @type {string[]}
 */
export const RESERVED_GLOBAL_INTRINSICS: string[];
/**
 * A unique reference to a value owner by a "context object" and a
 * `Script` instance.
 */
export class Reference {
    /**
     * Predicate function to determine if a `value` is an internal or external
     * script reference value.
     * @param {amy} value
     * @return {boolean}
     */
    static isReference(value: amy): boolean;
    /**
     * `Reference` class constructor.
     * @param {string} id
     * @param {any} value
     * @param {object=} [context]
     * @param {object=} [options]
     */
    constructor(id: string, value: any, context?: object | undefined, options?: object | undefined);
    /**
     * The unique id of the reference
     * @type {string}
     */
    get id(): string;
    /**
     * The underling primitive type of the reference value.
     * @ignore
     * @type {'undefined'|'object'|'number'|'boolean'|'function'|'symbol'}
     */
    get type(): "undefined" | "object" | "number" | "boolean" | "function" | "symbol";
    /**
     * The underlying value of the reference.
     * @type {any?}
     */
    get value(): any | null;
    /**
     * The name of the type.
     * @type {string?}
     */
    get name(): string | null;
    /**
     * The `Script` this value belongs to, if available.
     * @type {Script?}
     */
    get script(): Script | null;
    /**
     * The "context object" this reference value belongs to.
     * @type {object?}
     */
    get context(): object | null;
    /**
     * A boolean value to indicate if the underlying reference value is an
     * intrinsic value.
     * @type {boolean}
     */
    get isIntrinsic(): boolean;
    /**
     * A boolean value to indicate if the underlying reference value is an
     * external reference value.
     * @type {boolean}
     */
    get isExternal(): boolean;
    /**
     * The intrinsic type this reference may be an instance of or directly refer to.
     * @type {function|object}
     */
    get intrinsicType(): Function | object;
    /**
     * Releases strongly held value and weak references
     * to the "context object".
     */
    release(): void;
    /**
     * Converts this `Reference` to a JSON object.
     * @param {boolean=} [includeValue = false]
     */
    toJSON(includeValue?: boolean | undefined): {
        __vmScriptReference__: boolean;
        id: string;
        type: "number" | "boolean" | "symbol" | "undefined" | "object" | "function";
        name: string;
        isIntrinsic: boolean;
        intrinsicType: string;
    };
    #private;
}
/**
 * @typedef {{
 *  filename?: string,
 *  context?: object
 * }} ScriptOptions
 */
/**
 * A `Script` is a container for raw JavaScript to be executed in
 * a completely isolated virtual machine context, optionally with
 * user supplied context. Context objects references are not actually
 * shared, but instead provided to the script execution context using the
 * structured cloning algorithm used by the Message Channel API. Context
 * differences are computed and applied after execution so the user supplied
 * context object realizes context changes after script execution. All script
 * sources run in an "async" context so a "top level await" should work.
 */
export class Script extends EventTarget {
    [x: number]: () => import("./gc.js").Finalizer;
    /**
     * `Script` class constructor
     * @param {string} source
     * @param {ScriptOptions} [options]
     */
    constructor(source: string, options?: ScriptOptions);
    /**
     * The script identifier.
     */
    get id(): any;
    /**
     * The source for this script.
     * @type {string}
     */
    get source(): string;
    /**
     * The filename for this script.
     * @type {string}
     */
    get filename(): string;
    /**
     * A promise that resolves when the script is ready.
     * @type {Promise<Boolean>}
     */
    get ready(): Promise<boolean>;
    /**
     * The default script context object
     * @type {object}
     */
    get context(): object;
    /**
     * Destroy the script execution context.
     * @return {Promise}
     */
    destroy(): Promise<any>;
    /**
     * Run `source` JavaScript in given context. The script context execution
     * context is preserved until the `context` object that points to it is
     * garbage collected or there are no longer any references to it and its
     * associated `Script` instance.
     * @param {ScriptOptions=} [options]
     * @param {object=} [context]
     * @return {Promise<any>}
     */
    runInContext(context?: object | undefined, options?: ScriptOptions | undefined): Promise<any>;
    /**
     * Run `source` JavaScript in new context. The script context is destroyed after
     * execution. This is typically a "one off" isolated run.
     * @param {ScriptOptions=} [options]
     * @param {object=} [context]
     * @return {Promise<any>}
     */
    runInNewContext(context?: object | undefined, options?: ScriptOptions | undefined): Promise<any>;
    /**
     * Run `source` JavaScript in this current context (`globalThis`).
     * @param {ScriptOptions=} [options]
     * @return {Promise<any>}
     */
    runInThisContext(options?: ScriptOptions | undefined): Promise<any>;
    #private;
}
declare namespace _default {
    export { createGlobalObject };
    export { compileFunction };
    export { createReference };
    export { getContextWindow };
    export { getContextWorker };
    export { getReference };
    export { getTransferables };
    export { putReference };
    export { Reference };
    export { removeReference };
    export { runInContext };
    export { runInNewContext };
    export { runInThisContext };
    export { Script };
    export { createContext };
    export { isContext };
    export { channel };
}
export default _default;
export type ScriptOptions = {
    filename?: string;
    context?: object;
};
import { SharedWorker } from './shared-worker/index.js';
