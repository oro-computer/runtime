/**
 * Factory for creating a `require()` function based on a module context.
 * @param {CreateRequireOptions} options
 * @return {RequireFunction}
 */
export function createRequire(options: CreateRequireOptions): RequireFunction;
/**
 * @typedef {function(string, import('./module.js').Module, function(string): any): any} RequireResolver
 */
/**
 * @typedef {{
 *   module: import('./module.js').Module,
 *   prefix?: string,
 *   request?: import('./loader.js').RequestOptions,
 *   builtins?: object,
 *   resolvers?: RequireFunction[]
 * }} CreateRequireOptions
 */
/**
 * @typedef {function(string): any} RequireFunction
 */
/**
 * @typedef {import('./package.js').PackageOptions} PackageOptions
 */
/**
 * @typedef {import('./package.js').PackageResolveOptions} PackageResolveOptions
 */
/**
 * @typedef {
 *   PackageResolveOptions &
 *   PackageOptions &
 *   { origins?: string[] | URL[] }
 * } ResolveOptions
 */
/**
 * @typedef {ResolveOptions & {
 *   resolvers?: RequireResolver[],
 *   importmap?: import('./module.js').ImportMap,
 *   cache?: boolean
 * }} RequireOptions
 */
/**
 * An array of global require paths, relative to the origin.
 * @type {string[]}
 */
export const globalPaths: string[];
/**
 * An object attached to a `require()` function that contains metadata
 * about the current module context.
 */
export class Meta {
    /**
     * `Meta` class constructor.
     * @param {import('./module.js').Module} module
     */
    constructor(module: import("./module.js").Module);
    /**
     * The referrer (parent) of this module.
     * @type {string}
     */
    get referrer(): string;
    /**
     * The referrer (parent) of this module.
     * @type {string}
     */
    get url(): string;
    #private;
}
export default createRequire;
export type RequireResolver = (arg0: string, arg1: import("./module.js").Module, arg2: (arg0: string) => any) => any;
export type CreateRequireOptions = {
    module: import("./module.js").Module;
    prefix?: string;
    request?: import("./loader.js").RequestOptions;
    builtins?: object;
    resolvers?: RequireFunction[];
};
export type RequireFunction = (arg0: string) => any;
export type PackageOptions = import("./package.js").PackageOptions;
export type PackageResolveOptions = import("./package.js").PackageResolveOptions;
export type RequireOptions = ResolveOptions & {
    resolvers?: RequireResolver[];
    importmap?: import("./module.js").ImportMap;
    cache?: boolean;
};
