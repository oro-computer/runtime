/**
 * CommonJS module scope with module scoped globals.
 * @ignore
 * @param {object} exports
 * @param {function(string): any} require
 * @param {Module} module
 * @param {string} __filename
 * @param {string} __dirname
 * @param {typeof process} _process
 * @param {object} _global
 */
export function CommonJSModuleScope(exports: object, require: (arg0: string) => any, module: Module, __filename: string, __dirname: string, _process: typeof process, _global: object): void;
/**
 * Creates a `require` function from a given module URL.
 * @param {string|URL} url
 * @param {ModuleOptions=} [options]
 * @return {RequireFunction}
 */
export function createRequire(url: string | URL, options?: ModuleOptions | undefined): RequireFunction;
/**
 * @typedef {function(string, Module, function(string): any): any} ModuleResolver
 */
/**
 * @typedef {import('./require.js').RequireFunction} RequireFunction
 */
/**
 * @typedef {import('./package.js').PackageOptions} PackageOptions
 */
/**
 * @typedef {import('./require.js').RequireOptions} RequireOptions
 */
/**
 * @typedef {{
 *   prefix?: string,
 *   request?: import('./loader.js').RequestOptions,
 *   builtins?: object
 * } CreateRequireOptions
 */
/**
 * @typedef {{
 *   resolvers?: ModuleResolver[],
 *   importmap?: ImportMap,
 *   loader?: Loader | object,
 *   loaders?: object,
 *   package?: Package | PackageOptions
 *   parent?: Module,
 *   state?: State
 * }} ModuleOptions
 */
/**
 * @typedef {{
 *   extensions?: object
 * }} ModuleLoadOptions
 */
export const builtinModules: any;
/**
 * CommonJS module scope source wrapper.
 * @type {string}
 */
export const COMMONJS_WRAPPER: string;
/**
 * A container for imports.
 * @see {@link https://developer.mozilla.org/en-US/docs/Web/HTML/Element/script/type/importmap}
 */
export class ImportMap {
    set imports(imports: object);
    /**
     * The imports object for the importmap.
     * @type {object}
     */
    get imports(): object;
    /**
     * Extends the current imports object.
     * @param {object} imports
     * @return {ImportMap}
     */
    extend(importmap: any): ImportMap;
    #private;
}
/**
 * A container for `Module` instance state.
 */
export class State {
    /**
     * `State` class constructor.
     * @ignore
     * @param {object|State=} [state]
     */
    constructor(state?: (object | State) | undefined);
    loading: boolean;
    loaded: boolean;
    error: any;
}
/**
 * The module scope for a loaded module.
 * This is a special object that is seal, frozen, and only exposes an
 * accessor the 'exports' field.
 * @ignore
 */
export class ModuleScope {
    /**
     * `ModuleScope` class constructor.
     * @param {Module} module
     */
    constructor(module: Module);
    get id(): any;
    get filename(): any;
    get loaded(): any;
    get children(): any;
    set exports(exports: any);
    get exports(): any;
    toJSON(): {
        id: any;
        filename: any;
        children: any;
        exports: any;
    };
    #private;
}
/**
 * An abstract base class for loading a module.
 */
export class ModuleLoader {
    /**
     * Creates a `ModuleLoader` instance from the `module` currently being loaded.
     * @param {Module} module
     * @param {ModuleLoadOptions=} [options]
     * @return {ModuleLoader}
     */
    static from(module: Module, options?: ModuleLoadOptions | undefined): ModuleLoader;
    /**
     * Creates a new `ModuleLoader` instance from the `module` currently
     * being loaded with the `source` string to parse and load with optional
     * `ModuleLoadOptions` options.
     * @param {Module} module
     * @param {ModuleLoadOptions=} [options]
     * @return {boolean}
     */
    static load(module: Module, options?: ModuleLoadOptions | undefined): boolean;
    /**
     * @param {Module} module
     * @param {ModuleLoadOptions=} [options]
     * @return {boolean}
     */
    load(module: Module, options?: ModuleLoadOptions | undefined): boolean;
}
/**
 * A JavaScript module loader
 */
export class JavaScriptModuleLoader extends ModuleLoader {
}
/**
 * A JSON module loader.
 */
export class JSONModuleLoader extends ModuleLoader {
}
/**
 * A WASM module loader

 */
export class WASMModuleLoader extends ModuleLoader {
}
/**
 * A container for a loaded CommonJS module. All errors bubble
 * to the "main" module and global object (if possible).
 */
export class Module extends EventTarget {
    /**
     * A reference to the currently scoped module.
     * @type {Module?}
     */
    static current: Module | null;
    /**
     * A reference to the previously scoped module.
     * @type {Module?}
     */
    static previous: Module | null;
    /**
     * A cache of loaded modules
     * @type {Map<string, Module>}
     */
    static cache: Map<string, Module>;
    /**
     * An array of globally available module loader resolvers.
     * @type {ModuleResolver[]}
     */
    static resolvers: ModuleResolver[];
    /**
     * Globally available 'importmap' for all loaded modules.
     * @type {ImportMap}
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/HTML/Element/script/type/importmap}
     */
    static importmap: ImportMap;
    /**
     * A limited set of builtins exposed to CommonJS modules.
     * @type {object}
     */
    static builtins: object;
    /**
     * A limited set of builtins exposed to CommonJS modules.
     * @type {object}
     */
    static builtinModules: object;
    /**
     * CommonJS module scope source wrapper components.
     * @type {string[]}
     */
    static wrapper: string[];
    /**
     * An array of global require paths, relative to the origin.
     * @type {string[]}
     */
    static globalPaths: string[];
    /**
     * Globabl module loaders
     * @type {object}
     */
    static loaders: object;
    /**
     * The main entry module, lazily created.
     * @type {Module}
     */
    static get main(): Module;
    /**
     * Wraps source in a CommonJS module scope.
     * @param {string} source
     */
    static wrap(source: string): string;
    /**
     * Compiles given JavaScript module source.
     * @param {string} source
     * @param {{ url?: URL | string }=} [options]
     * @return {function(
     *   object,
     *   function(string): any,
     *   Module,
     *   string,
     *   string,
     *   typeof process,
     *   object
     * ): any}
     */
    static compile(source: string, options?: {
        url?: URL | string;
    } | undefined): (arg0: object, arg1: (arg0: string) => any, arg2: Module, arg3: string, arg4: string, arg5: typeof process, arg6: object) => any;
    /**
     * Creates a `Module` from source URL and optionally a parent module.
     * @param {string|URL|Module} url
     * @param {ModuleOptions=} [options]
     */
    static from(url: string | URL | Module, options?: ModuleOptions | undefined): any;
    /**
     * Creates a `require` function from a given module URL.
     * @param {string|URL} url
     * @param {ModuleOptions=} [options]
     */
    static createRequire(url: string | URL, options?: ModuleOptions | undefined): any;
    /**
     * `Module` class constructor.
     * @param {string|URL} url
     * @param {ModuleOptions=} [options]
     */
    constructor(url: string | URL, options?: ModuleOptions | undefined);
    /**
     * A unique ID for this module.
     * @type {string}
     */
    get id(): string;
    /**
     * A reference to the "main" module.
     * @type {Module}
     */
    get main(): Module;
    /**
     * Child modules of this module.
     * @type {Module[]}
     */
    get children(): Module[];
    /**
     * A reference to the module cache. Possibly shared with all
     * children modules.
     * @type {object}
     */
    get cache(): object;
    /**
     * A reference to the module package.
     * @type {Package}
     */
    get package(): Package;
    /**
     * The `ImportMap` for this module.
     * @type {ImportMap}
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/HTML/Element/script/type/importmap}
     */
    get importmap(): ImportMap;
    /**
     * The module level resolvers.
     * @type {ModuleResolver[]}
     */
    get resolvers(): ModuleResolver[];
    /**
     * `true` if the module is currently loading, otherwise `false`.
     * @type {boolean}
     */
    get loading(): boolean;
    /**
     * `true` if the module is currently loaded, otherwise `false`.
     * @type {boolean}
     */
    get loaded(): boolean;
    /**
     * An error associated with the module if it failed to load.
     * @type {Error?}
     */
    get error(): Error | null;
    /**
     * The exports of the module
     * @type {object}
     */
    get exports(): object;
    /**
     * The scope of the module given to parsed modules.
     * @type {ModuleScope}
     */
    get scope(): ModuleScope;
    /**
     * The origin of the loaded module.
     * @type {string}
     */
    get origin(): string;
    /**
     * The parent module for this module.
     * @type {Module?}
     */
    get parent(): Module | null;
    /**
     * The `Loader` for this module.
     * @type {Loader}
     */
    get loader(): Loader;
    /**
     * The filename of the module.
     * @type {string}
     */
    get filename(): string;
    /**
     * Known source loaders for this module keyed by file extension.
     * @type {object}
     */
    get loaders(): object;
    /**
     * Factory for creating a `require()` function based on a module context.
     * @param {CreateRequireOptions=} [options]
     * @return {RequireFunction}
     */
    createRequire(options?: CreateRequireOptions | undefined): RequireFunction;
    /**
     * Creates a `Module` from source the URL with this module as
     * the parent.
     * @param {string|URL|Module} url
     * @param {ModuleOptions=} [options]
     */
    createModule(url: string | URL | Module, options?: ModuleOptions | undefined): any;
    /**
     * Requires a module at for a given `input` which can be a relative file,
     * named module, or an absolute URL within the context of this odule.
     * @param {string|URL} input
     * @param {RequireOptions=} [options]
     * @throws ModuleNotFoundError
     * @throws ReferenceError
     * @throws SyntaxError
     * @throws TypeError
     * @return {any}
     */
    require(url: any, options?: RequireOptions | undefined): any;
    /**
     * Loads the module
     * @param {ModuleLoadOptions=} [options]
     * @return {boolean}
     */
    load(options?: ModuleLoadOptions | undefined): boolean;
    resolve(input: any): string;
    /**
     * @ignore
     */
    [Symbol.toStringTag](): string;
    #private;
}
export namespace Module {
    export { Module };
}
export default Module;
export type ModuleResolver = (arg0: string, arg1: Module, arg2: (arg0: string) => any) => any;
export type RequireFunction = import("./require.js").RequireFunction;
export type PackageOptions = import("./package.js").PackageOptions;
export type RequireOptions = import("./require.js").RequireOptions;
export type CreateRequireOptions = {
    prefix?: string;
    request?: import("./loader.js").RequestOptions;
    builtins?: object;
};
export type ModuleOptions = {
    resolvers?: ModuleResolver[];
    importmap?: ImportMap;
    loader?: Loader | object;
    loaders?: object;
    package?: Package | PackageOptions;
    parent?: Module;
    state?: State;
};
export type ModuleLoadOptions = {
    extensions?: object;
};
import process from '../process.js';
import { Package } from './package.js';
import { Loader } from './loader.js';
