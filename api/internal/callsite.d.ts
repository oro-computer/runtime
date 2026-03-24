/**
 * Creates an ordered and link array of `CallSite` instances from a
 * given `Error`.
 * @param {Error} error
 * @param {string} source
 * @return {CallSite[]}
 */
export function createCallSites(error: Error, source: string): CallSite[];
/**
 * @typedef {{
 *   sourceURL: string | null,
 *   symbol: string,
 *   column: number | undefined,
 *   line: number | undefined,
 *   native: boolean
 * }} ParsedStackFrame
 */
/**
 * A container for location data related to a `StackFrame`
 */
export class StackFrameLocation {
    [x: number]: () => {
        __type__: "StackFrameLocation";
        lineNumber: number | undefined;
        columnNumber: number | undefined;
        sourceURL: string | null;
        isNative: boolean;
    };
    /**
     * Creates a `StackFrameLocation` from JSON input.
     * @param {object=} json
     * @return {StackFrameLocation}
     */
    static from(json?: object | undefined): StackFrameLocation;
    /**
     * The line number of the location of the stack frame, if available.
     * @type {number | undefined}
     */
    lineNumber: number | undefined;
    /**
     * The column number of the location of the stack frame, if available.
     * @type {number | undefined}
     */
    columnNumber: number | undefined;
    /**
     * The source URL of the location of the stack frame, if available. This value
     * may be `null`.
     * @type {string?}
     */
    sourceURL: string | null;
    /**
     * `true` if the stack frame location is in native location, otherwise
     * this value `false` (default).
     * @type
     */
    isNative: any;
    /**
     * Converts this `StackFrameLocation` to a JSON object.
     * @ignore
     * @return {{
     *   lineNumber: number | undefined,
     *   columnNumber: number | undefined,
     *   sourceURL: string | null,
     *   isNative: boolean
     * }}
     */
    toJSON(): {
        lineNumber: number | undefined;
        columnNumber: number | undefined;
        sourceURL: string | null;
        isNative: boolean;
    };
}
/**
 * A stack frame container related to a `CallSite`.
 */
export class StackFrame {
    [x: number]: () => {
        __type__: "StackFrame";
        location: {
            __type__: "StackFrameLocation";
            lineNumber: number | undefined;
            columnNumber: number | undefined;
            sourceURL: string | null;
            isNative: boolean;
        };
        isNative: boolean;
        symbol: string | null;
        source: string | null;
        error: {
            message: string;
            name: string;
            stack: string;
        } | null;
    };
    /**
     * Parses a raw stack frame string into structured data.
     * @param {string} rawStackFrame
     * @return {ParsedStackFrame}
     */
    static parse(rawStackFrame: string): ParsedStackFrame;
    /**
     * Creates a new `StackFrame` from an `Error` and raw stack frame
     * source `rawStackFrame`.
     * @param {Error} error
     * @param {string} rawStackFrame
     * @return {StackFrame}
     */
    static from(error: Error, rawStackFrame: string): StackFrame;
    /**
     * `StackFrame` class constructor.
     * @param {Error} error
     * @param {ParsedStackFrame=} [frame]
     * @param {string=} [source]
     */
    constructor(error: Error, frame?: ParsedStackFrame | undefined, source?: string | undefined);
    /**
     * The stack frame location data.
     * @type {StackFrameLocation}
     */
    location: StackFrameLocation;
    /**
     * The `Error` associated with this `StackFrame` instance.
     * @type {Error?}
     */
    error: Error | null;
    /**
     * The name of the function where the stack frame is located.
     * @type {string?}
     */
    symbol: string | null;
    /**
     * The raw stack frame source string.
     * @type {string?}
     */
    source: string | null;
    /**
     * Converts this `StackFrameLocation` to a JSON object.
     * @ignore
     * @return {{
     *   location: {
     *     lineNumber: number | undefined,
     *     columnNumber: number | undefined,
     *     sourceURL: string | null,
     *     isNative: boolean
     *   },
     *   isNative: boolean,
     *   symbol: string | null,
     *   source: string | null,
     *   error: { message: string, name: string, stack: string } | null
     * }}
     */
    toJSON(): {
        location: {
            lineNumber: number | undefined;
            columnNumber: number | undefined;
            sourceURL: string | null;
            isNative: boolean;
        };
        isNative: boolean;
        symbol: string | null;
        source: string | null;
        error: {
            message: string;
            name: string;
            stack: string;
        } | null;
    };
}
/**
 * A v8 compatible interface and container for call site information.
 */
export class CallSite {
    [x: number]: () => {
        __type__: "CallSite";
        frame: {
            __type__: "StackFrame";
            location: {
                __type__: "StackFrameLocation";
                lineNumber: number | undefined;
                columnNumber: number | undefined;
                sourceURL: string | null;
                isNative: boolean;
            };
            isNative: boolean;
            symbol: string | null;
            source: string | null;
            error: {
                message: string;
                name: string;
                stack: string;
            } | null;
        };
    };
    /**
     * An internal symbol used to refer to the index of a promise in
     * `Promise.all` or `Promise.any` function call site.
     * @ignore
     * @type {symbol}
     */
    static PromiseElementIndexSymbol: symbol;
    /**
     * An internal symbol used to indicate that a call site is in a `Promise.all`
     * function call.
     * @ignore
     * @type {symbol}
     */
    static PromiseAllSymbol: symbol;
    /**
     * An internal symbol used to indicate that a call site is in a `Promise.any`
     * function call.
     * @ignore
     * @type {symbol}
     */
    static PromiseAnySymbol: symbol;
    /**
     * An internal source symbol used to store the original `Error` stack source.
     * @ignore
     * @type {symbol}
     */
    static StackSourceSymbol: symbol;
    /**
     * `CallSite` class constructor
     * @param {Error} error
     * @param {string} rawStackFrame
     * @param {CallSite=} previous
     */
    constructor(error: Error, rawStackFrame: string, previous?: CallSite | undefined);
    /**
     * The `Error` associated with the call site.
     * @type {Error}
     */
    get error(): Error;
    /**
     * The previous `CallSite` instance, if available.
     * @type {CallSite?}
     */
    get previous(): CallSite | null;
    /**
     * A reference to the `StackFrame` data.
     * @type {StackFrame}
     */
    get frame(): StackFrame;
    /**
     * This function _ALWAYS__ returns `globalThis` as `this` cannot be determined.
     * @return {object}
     */
    getThis(): object;
    /**
     * This function _ALWAYS__ returns `null` as the type name of `this`
     * cannot be determined.
     * @return {null}
     */
    getTypeName(): null;
    /**
     * This function _ALWAYS__ returns `undefined` as the current function
     * reference cannot be determined.
     * @return {undefined}
     */
    getFunction(): undefined;
    /**
     * Returns the name of the function in at the call site, if available.
     * @return {string|undefined}
     */
    getFunctionName(): string | undefined;
    /**
     * An alias to `getFunctionName()
     * @return {string}
     */
    getMethodName(): string;
    /**
     * Get the filename of the call site location, if available, otherwise this
     * function returns 'unknown location'.
     * @return {string}
     */
    getFileName(): string;
    /**
     * Returns the location source URL defaulting to the global location.
     * @return {string}
     */
    getScriptNameOrSourceURL(): string;
    /**
     * Returns a hash value of the source URL return by `getScriptNameOrSourceURL()`
     * @return {string}
     */
    getScriptHash(): string;
    /**
     * Returns the line number of the call site location.
     * This value may be `undefined`.
     * @return {number|undefined}
     */
    getLineNumber(): number | undefined;
    /**
     * @ignore
     * @return {number}
     */
    getPosition(): number;
    /**
     * Attempts to get an "enclosing" line number, potentially the previous
     * line number of the call site
     * @param {number|undefined}
     */
    getEnclosingLineNumber(): any;
    /**
     * Returns the column number of the call site location.
     * This value may be `undefined`.
     * @return {number|undefined}
     */
    getColumnNumber(): number | undefined;
    /**
     * Attempts to get an "enclosing" column number, potentially the previous
     * line number of the call site
     * @param {number|undefined}
     */
    getEnclosingColumnNumber(): any;
    /**
     * Gets the origin of where `eval()` was called if this call site function
     * originated from a call to `eval()`. This function may return `undefined`.
     * @return {string|undefined}
     */
    getEvalOrigin(): string | undefined;
    /**
     * This function _ALWAYS__ returns `false` as `this` cannot be determined so
     * "top level" detection is not possible.
     * @return {boolean}
     */
    isTopLevel(): boolean;
    /**
     * Returns `true` if this call site originated from a call to `eval()`.
     * @return {boolean}
     */
    isEval(): boolean;
    /**
     * Returns `true` if the call site is in a native location, otherwise `false`.
     * @return {boolean}
     */
    isNative(): boolean;
    /**
     * This function _ALWAYS_ returns `false` as constructor detection
     * is not possible.
     * @return {boolean}
     */
    isConstructor(): boolean;
    /**
     * Returns `true` if the call site is in async context, otherwise `false`.
     * @return {boolean}
     */
    isAsync(): boolean;
    /**
     * Returns `true` if the call site is in a `Promise.all()` function call,
     * otherwise `false.
     * @return {boolean}
     */
    isPromiseAll(): boolean;
    /**
     * Gets the index of the promise element that was followed in a
     * `Promise.all()` or `Promise.any()` function call. If not available, then
     * this function returns `null`.
     * @return {number|null}
     */
    getPromiseIndex(): number | null;
    /**
     * Converts this call site to a string.
     * @return {string}
     */
    toString(): string;
    /**
     * Converts this `CallSite` to a JSON object.
     * @ignore
     * @return {{
     *   frame: {
     *     location: {
     *       lineNumber: number | undefined,
     *       columnNumber: number | undefined,
     *       sourceURL: string | null,
     *       isNative: boolean
     *     },
     *     isNative: boolean,
     *     symbol: string | null,
     *     source: string | null,
     *     error: { message: string, name: string, stack: string } | null
     *   }
     * }}
     */
    toJSON(): {
        frame: {
            location: {
                lineNumber: number | undefined;
                columnNumber: number | undefined;
                sourceURL: string | null;
                isNative: boolean;
            };
            isNative: boolean;
            symbol: string | null;
            source: string | null;
            error: {
                message: string;
                name: string;
                stack: string;
            } | null;
        };
    };
    set [$previous](previous: any);
    /**
     * Private accessor to "friend class" `CallSiteList`.
     * @ignore
     */
    get [$previous](): any;
    #private;
}
/**
 * An array based list container for `CallSite` instances.
 */
export class CallSiteList extends Array<any> {
    [x: number]: () => Array<{
        __type__: "CallSite";
        frame: {
            __type__: "StackFrame";
            location: {
                __type__: "StackFrameLocation";
                lineNumber: number | undefined;
                columnNumber: number | undefined;
                sourceURL: string | null;
                isNative: boolean;
            };
            isNative: boolean;
            symbol: string | null;
            source: string | null;
            error: {
                message: string;
                name: string;
                stack: string;
            } | null;
        };
    }>;
    /**
     * Creates a `CallSiteList` instance from `Error` input.
     * @param {Error} error
     * @param {string} source
     * @return {CallSiteList}
     */
    static from(error: Error, source: string): CallSiteList;
    /**
     * `CallSiteList` class constructor.
     * @param {Error} error
     * @param {string[]=} [sources]
     */
    constructor(error: Error, sources?: string[] | undefined);
    /**
     * A reference to the `Error` for this `CallSiteList` instance.
     * @type {Error}
     */
    get error(): Error;
    /**
     * An array of stack frame source strings.
     * @type {string[]}
     */
    get sources(): string[];
    /**
     * The original stack string derived from the sources.
     * @type {string}
     */
    get stack(): string;
    /**
     * Adds `CallSite` instances to the top of the list, linking previous
     * instances to the next one.
     * @param {...CallSite} callsites
     * @return {number}
     */
    unshift(...callsites: CallSite[]): number;
    /**
     * A no-op function as `CallSite` instances cannot be added to the end
     * of the list.
     * @return {number}
     */
    push(): number;
    /**
     * Pops a `CallSite` off the end of the list.
     * @return {CallSite|undefined}
     */
    pop(): CallSite | undefined;
    /**
     * Converts this `CallSiteList` to a JSON object.
     * @return {{
     *   frame: {
     *     location: {
     *       lineNumber: number | undefined,
     *       columnNumber: number | undefined,
     *       sourceURL: string | null,
     *       isNative: boolean
     *     },
     *     isNative: boolean,
     *     symbol: string | null,
     *     source: string | null,
     *     error: { message: string, name: string, stack: string } | null
     *   }
     * }[]}
     */
    toJSON(): {
        frame: {
            location: {
                lineNumber: number | undefined;
                columnNumber: number | undefined;
                sourceURL: string | null;
                isNative: boolean;
            };
            isNative: boolean;
            symbol: string | null;
            source: string | null;
            error: {
                message: string;
                name: string;
                stack: string;
            } | null;
        };
    }[];
    #private;
}
export default CallSite;
export type ParsedStackFrame = {
    sourceURL: string | null;
    symbol: string;
    column: number | undefined;
    line: number | undefined;
    native: boolean;
};
declare const $previous: unique symbol;
