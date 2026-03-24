/**
 * Normalizes an expression string.
 * @param {string} expression
 * @return {string}
 */
export function normalizeExpression(expression: string): string;
/**
 * Bind a global hotkey expression.
 * @param {string} expression
 * @param {{ passive?: boolean }} [options]
 * @return {Promise<Binding>}
 */
export function bind(expression: string, options?: {
    passive?: boolean;
}): Promise<Binding>;
/**
 * Bind a global hotkey expression.
 * @param {string} expression
 * @param {object=} [options]
 * @return {Promise<Binding>}
 */
export function unbind(id: any, options?: object | undefined): Promise<Binding>;
/**
 * Get all known globally register hotkey bindings.
 * @param {object=} [options]
 * @return {Promise<Binding[]>}
 */
export function getBindings(options?: object | undefined): Promise<Binding[]>;
/**
 * Get all known possible keyboard modifier and key mappings for
 * expression bindings.
 * @param {object=} [options]
 * @return {Promise<{ keys: object, modifiers: object }>}
 */
export function getMappings(options?: object | undefined): Promise<{
    keys: object;
    modifiers: object;
}>;
/**
 * Adds an event listener to the global active bindings. This function is just
 * proxy to `bindings.addEventListener`.
 * @param {string} type
 * @param {function(Event)} listener
 * @param {(boolean|object)=} [optionsOrUseCapture]
 */
export function addEventListener(type: string, listener: (arg0: Event) => any, optionsOrUseCapture?: (boolean | object) | undefined): void;
/**
 * Removes  an event listener to the global active bindings. This function is
 * just a proxy to `bindings.removeEventListener`
 * @param {string} type
 * @param {function(Event)} listener
 * @param {(boolean|object)=} [optionsOrUseCapture]
 */
export function removeEventListener(type: string, listener: (arg0: Event) => any, optionsOrUseCapture?: (boolean | object) | undefined): void;
/**
 * A high level bindings container map that dispatches events.
 */
export class Bindings extends EventTarget {
    [x: number]: () => import("../gc.js").Finalizer;
    /**
     * `Bindings` class constructor.
     * @ignore
     * @param {EventTarget} [sourceEventTarget]
     */
    constructor(sourceEventTarget?: EventTarget);
    /**
     * Global `HotKeyEvent` event listener for `Binding` instance event dispatch.
     * @ignore
     * @param {import('../internal/events.js').HotKeyEvent} event
     */
    onHotKey(event: import("../internal/events.js").HotKeyEvent): boolean;
    /**
     * The number of `Binding` instances in the mapping.
     * @type {number}
     */
    get size(): number;
    /**
     * Setter for the level 1 'error'` event listener.
     * @ignore
     * @type {function(ErrorEvent)?}
     */
    set onerror(onerror: ((arg0: ErrorEvent) => any) | null);
    /**
     * Level 1 'error'` event listener.
     * @type {function(ErrorEvent)?}
     */
    get onerror(): ((arg0: ErrorEvent) => any) | null;
    /**
     * Setter for the level 1 'hotkey'` event listener.
     * @ignore
     * @type {function(import('../internal/events.js').HotKeyEvent)?}
     */
    set onhotkey(onhotkey: ((arg0: import("../internal/events.js").HotKeyEvent) => any) | null);
    /**
     * Level 1 'hotkey'` event listener.
     * @type {function(import('../internal/events.js').HotKeyEvent)?}
     */
    get onhotkey(): ((arg0: import("../internal/events.js").HotKeyEvent) => any) | null;
    /**
     * Initializes bindings from global context.
     * @ignore
     * @return {Promise}
     */
    init(): Promise<any>;
    /**
     * Get a binding by `id`
     * @param {number} id
     * @return {Binding}
     */
    get(id: number): Binding;
    /**
     * Set a `binding` a by `id`.
     * @param {number} id
     * @param {Binding} binding
     */
    set(id: number, binding: Binding): void;
    /**
     * Delete a binding by `id`
     * @param {number} id
     * @return {boolean}
     */
    delete(id: number): boolean;
    /**
     * Returns `true` if a binding exists in the mapping, otherwise `false`.
     * @return {boolean}
     */
    has(id: any): boolean;
    /**
     * Known `Binding` values in the mapping.
     * @return {{ next: function(): { value: Binding|undefined, done: boolean } }}
     */
    values(): {
        next: () => {
            value: Binding | undefined;
            done: boolean;
        };
    };
    /**
     * Known `Binding` keys in the mapping.
     * @return {{ next: function(): { value: number|undefined, done: boolean } }}
     */
    keys(): {
        next: () => {
            value: number | undefined;
            done: boolean;
        };
    };
    /**
     * Known `Binding` ids in the mapping.
     * @return {{ next: function(): { value: number|undefined, done: boolean } }}
     */
    ids(): {
        next: () => {
            value: number | undefined;
            done: boolean;
        };
    };
    /**
     * Known `Binding` ids and values in the mapping.
     * @return {{ next: function(): { value: [number, Binding]|undefined, done: boolean } }}
     */
    entries(): {
        next: () => {
            value: [number, Binding] | undefined;
            done: boolean;
        };
    };
    /**
     * Bind a global hotkey expression.
     * @param {string} expression
     * @return {Promise<Binding>}
     */
    bind(expression: string): Promise<Binding>;
    /**
     * Bind a global hotkey expression.
     * @param {string} expression
     * @return {Promise<Binding>}
     */
    unbind(expression: string): Promise<Binding>;
    /**
     * Returns an array of all active bindings for the application.
     * @return {Promise<Binding[]>}
     */
    active(): Promise<Binding[]>;
    /**
     * Resets all active bindings in the application.
     * @param {boolean=} [currentContextOnly]
     * @return {Promise}
     */
    reset(currentContextOnly?: boolean | undefined): Promise<any>;
    /**
     * Implements the `Iterator` protocol for each currently registered
     * active binding in this window context. The `AsyncIterator` protocol
     * will probe for all gloally active bindings.
     * @return {Iterator<Binding>}
     */
    [Symbol.iterator](): Iterator<Binding>;
    /**
     * Implements the `AsyncIterator` protocol for each globally active
     * binding registered to the application. This differs from the `Iterator`
     * protocol as this will probe for _all_ active bindings in the entire
     * application context.
     * @return {AsyncGenerator<Binding>}
     */
    [Symbol.asyncIterator](): AsyncGenerator<Binding>;
    #private;
}
/**
 * An `EventTarget` container for a hotkey binding.
 */
export class Binding extends EventTarget {
    /**
     * `Binding` class constructor.
     * @ignore
     * @param {object} data
     */
    constructor(data: object);
    /**
     * `true` if the binding is valid, otherwise `false`.
     * @type {boolean}
     */
    get isValid(): boolean;
    /**
     * `true` if the binding is considered active, otherwise `false`.
     * @type {boolean}
     */
    get isActive(): boolean;
    /**
     * The global unique ID for this binding.
     * @type {number?}
     */
    get id(): number | null;
    /**
     * The computed hash for this binding expression.
     * @type {number?}
     */
    get hash(): number | null;
    /**
     * The normalized expression as a sequence of tokens.
     * @type {string[]}
     */
    get sequence(): string[];
    /**
     * The original expression of the binding.
     * @type {string?}
     */
    get expression(): string | null;
    /**
     * Setter for the level 1 'hotkey'` event listener.
     * @ignore
     * @type {function(import('../internal/events.js').HotKeyEvent)?}
     */
    set onhotkey(onhotkey: ((arg0: import("../internal/events.js").HotKeyEvent) => any) | null);
    /**
     * Level 1 'hotkey'` event listener.
     * @type {function(import('../internal/events.js').HotKeyEvent)?}
     */
    get onhotkey(): ((arg0: import("../internal/events.js").HotKeyEvent) => any) | null;
    /**
     * Binds this hotkey expression.
     * @return {Promise<Binding>}
     */
    bind(): Promise<Binding>;
    /**
     * Unbinds this hotkey expression.
     * @return {Promise}
     */
    unbind(): Promise<any>;
    /**
     * Implements the `AsyncIterator` protocol for async 'hotkey' events
     * on this binding instance.
     * @return {AsyncGenerator}
     */
    [Symbol.asyncIterator](): AsyncGenerator;
    #private;
}
/**
 * A container for all the bindings currently bound
 * by this window context.
 * @type {Bindings}
 */
export const bindings: Bindings;
export default bindings;
