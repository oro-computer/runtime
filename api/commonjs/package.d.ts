/**
 * @ignore
 * @param {string} source
 * @return {boolean}
 */
export function detectESMSource(source: string): boolean;
/**
 * @typedef {{
 *   manifest?: string,
 *   index?: string,
 *   description?: string,
 *   version?: string,
 *   license?: string,
 *   exports?: object,
 *   type?: 'commonjs' | 'module',
 *   info?: object,
 *   origin?: string,
 *   dependencies?: Dependencies | object | Map
 * }} PackageOptions
 */
/**
 * @typedef {import('./loader.js').RequestOptions & {
 *   type?: 'commonjs' | 'module'
 *   prefix?: string
 * }} PackageLoadOptions
 */
/**
 * @typedef {import('./loader.js').RequestOptions & {
 *   load?: boolean,
 *   type?: 'commonjs' | 'module',
 *   browser?: boolean,
 *   children?: string[]
 *   extensions?: string[] | Set<string>
 * }} PackageResolveOptions
 */
/**
 * @typedef {ParsedPackageName} NameOptions
 */
/**
 * @typedef {{
 *   organization: string | null,
 *   name: string,
 *   version: string | null,
 *   pathname: string,
 *   url: URL,
 *   isRelative: boolean,
 *   hasManifest: boolean
 * }} ParsedPackageName
 */
/**
 * @typedef {{
 *   require?: string | string[],
 *   import?: string | string[],
 *   default?: string | string[],
 *   worker?: string | string[],
 *   browser?: string | string[]
 * }} PackageExports

/**
 * The default package index file such as 'index.js'
 * @type {string}
 */
export const DEFAULT_PACKAGE_INDEX: string;
/**
 * The default package manifest file name such as 'package.json'
 * @type {string}
 */
export const DEFAULT_PACKAGE_MANIFEST_FILE_NAME: string;
/**
 * The default package path prefix such as 'node_modules/'
 * @type {string}
 */
export const DEFAULT_PACKAGE_PREFIX: string;
/**
 * The default package version, when one is not provided
 * @type {string}
 */
export const DEFAULT_PACKAGE_VERSION: string;
/**
 * The default license for a package'
 * @type {string}
 */
export const DEFAULT_LICENSE: string;
/**
 * A container for a package name that includes a package organization identifier,
 * its fully qualified name, or for relative package names, its pathname
 */
export class Name {
    /**
     * Parses a package name input resolving the actual module name, including an
     * organization name given. If a path includes a manifest file
     * ('package.json'), then the directory containing that file is considered a
     * valid package and it will be included in the returned value. If a relative
     * path is given, then the path is returned if it is a valid pathname. This
     * function returns `null` for bad input.
     * @param {string|URL} input
     * @param {{ origin?: string | URL, manifest?: string }=} [options]
     * @return {ParsedPackageName?}
     */
    static parse(input: string | URL, options?: {
        origin?: string | URL;
        manifest?: string;
    } | undefined): ParsedPackageName | null;
    /**
     * Returns `true` if the given `input` can be parsed by `Name.parse` or given
     * as input to the `Name` class constructor.
     * @param {string|URL} input
     * @param {{ origin?: string | URL, manifest?: string }=} [options]
     * @return {boolean}
     */
    static canParse(input: string | URL, options?: {
        origin?: string | URL;
        manifest?: string;
    } | undefined): boolean;
    /**
     * Creates a new `Name` from input.
     * @param {string|URL} input
     * @param {{ origin?: string | URL, manifest?: string }=} [options]
     * @return {Name}
     */
    static from(input: string | URL, options?: {
        origin?: string | URL;
        manifest?: string;
    } | undefined): Name;
    /**
     * `Name` class constructor.
     * @param {string|URL|NameOptions|Name} name
     * @param {{ origin?: string | URL, manifest?: string }=} [options]
     * @throws TypeError
     */
    constructor(name: string | URL | NameOptions | Name, options?: {
        origin?: string | URL;
        manifest?: string;
    } | undefined);
    /**
     * The id of this package name.
     * @type {string}
     */
    get id(): string;
    /**
     * The actual package name.
     * @type {string}
     */
    get name(): string;
    /**
     * An alias for 'name'.
     * @type {string}
     */
    get value(): string;
    /**
     * The origin of the package, if available.
     * This value may be `null`.
     * @type {string?}
     */
    get origin(): string | null;
    /**
     * The package version if available.
     * This value may be `null`.
     * @type {string?}
     */
    get version(): string | null;
    /**
     * The actual package pathname, if given in name string.
     * This value is always a string defaulting to '.' if no path
     * was given in name string.
     * @type {string}
     */
    get pathname(): string;
    /**
     * The organization name.
     * This value may be `null`.
     * @type {string?}
     */
    get organization(): string | null;
    /**
     * `true` if the package name was relative, otherwise `false`.
     * @type {boolean}
     */
    get isRelative(): boolean;
    /**
     * Converts this package name to a string.
     * @ignore
     * @return {string}
     */
    toString(): string;
    /**
     * Converts this `Name` instance to JSON.
     * @ignore
     * @return {object}
     */
    toJSON(): object;
    #private;
}
/**
 * A container for package dependencies that map a package name to a `Package` instance.
 */
export class Dependencies {
    constructor(parent: any, options?: any);
    get map(): Map<any, any>;
    get origin(): any;
    add(name: any, info?: any): void;
    get(name: any, options?: any): any;
    entries(): MapIterator<[any, any]>;
    keys(): MapIterator<any>;
    values(): MapIterator<any>;
    load(options?: any): void;
    [Symbol.iterator](): MapIterator<[any, any]>;
    #private;
}
/**
 * A container for CommonJS module metadata, often in a `package.json` file.
 */
export class Package {
    /**
     * A high level class for a package name.
     * @type {typeof Name}
     */
    static Name: typeof Name;
    /**
     * A high level container for package dependencies.
     * @type {typeof Dependencies}
     */
    static Dependencies: typeof Dependencies;
    /**
     * Creates and loads a package
     * @param {string|URL|NameOptions|Name} name
     * @param {PackageOptions & PackageLoadOptions=} [options]
     * @return {Package}
     */
    static load(name: string | URL | NameOptions | Name, options?: (PackageOptions & PackageLoadOptions) | undefined): Package;
    /**
     * `Package` class constructor.
     * @param {string|URL|NameOptions|Name} name
     * @param {PackageOptions=} [options]
     */
    constructor(name: string | URL | NameOptions | Name, options?: PackageOptions | undefined);
    /**
     * The unique ID of this `Package`, which is the absolute
     * URL of the directory that contains its manifest file.
     * @type {string}
     */
    get id(): string;
    /**
     * The absolute URL to the package manifest file
     * @type {string}
     */
    get url(): string;
    /**
     * A reference to the package subpath imports and browser mappings.
     * These values are typically used with its corresponding `Module`
     * instance require resolvers.
     * @type {object}
     */
    get imports(): object;
    /**
     * A loader for this package, if available. This value may be `null`.
     * @type {Loader}
     */
    get loader(): Loader;
    /**
     * `true` if the package was actually "loaded", otherwise `false`.
     * @type {boolean}
     */
    get loaded(): boolean;
    /**
     * The name of the package.
     * @type {string}
     */
    get name(): string;
    /**
     * The description of the package.
     * @type {string}
     */
    get description(): string;
    /**
     * The organization of the package. This value may be `null`.
     * @type {string?}
     */
    get organization(): string | null;
    /**
     * The license of the package.
     * @type {string}
     */
    get license(): string;
    /**
     * The version of the package.
     * @type {string}
     */
    get version(): string;
    /**
     * The origin for this package.
     * @type {string}
     */
    get origin(): string;
    /**
     * The exports mappings for the package
     * @type {object}
     */
    get exports(): object;
    /**
     * The package type.
     * @type {'commonjs'|'module'}
     */
    get type(): "commonjs" | "module";
    /**
     * The raw package metadata object.
     * @type {object?}
     */
    get info(): object | null;
    /**
     * @type {Dependencies}
     */
    get dependencies(): Dependencies;
    /**
     * An alias for `entry`
     * @type {string?}
     */
    get main(): string | null;
    /**
     * The entry to the package
     * @type {string?}
     */
    get entry(): string | null;
    /**
     * Load the package information at an optional `origin` with
     * optional request `options`.
     * @param {PackageLoadOptions=} [options]
     * @throws SyntaxError
     * @return {boolean}
     */
    load(origin?: any, options?: PackageLoadOptions | undefined): boolean;
    /**
     * Resolve a file's `pathname` within the package.
     * @param {string|URL} pathname
     * @param {PackageResolveOptions=} [options]
     * @return {string}
     */
    resolve(pathname: string | URL, options?: PackageResolveOptions | undefined): string;
    #private;
}
export default Package;
export type PackageOptions = {
    manifest?: string;
    index?: string;
    description?: string;
    version?: string;
    license?: string;
    exports?: object;
    type?: "commonjs" | "module";
    info?: object;
    origin?: string;
    dependencies?: Dependencies | object | Map<any, any>;
};
export type PackageLoadOptions = import("./loader.js").RequestOptions & {
    type?: "commonjs" | "module";
    prefix?: string;
};
export type PackageResolveOptions = import("./loader.js").RequestOptions & {
    load?: boolean;
    type?: "commonjs" | "module";
    browser?: boolean;
    children?: string[];
    extensions?: string[] | Set<string>;
};
export type NameOptions = ParsedPackageName;
export type ParsedPackageName = {
    organization: string | null;
    name: string;
    version: string | null;
    pathname: string;
    url: URL;
    isRelative: boolean;
    hasManifest: boolean;
};
/**
 * /**
 * The default package index file such as 'index.js'
 */
export type PackageExports = {
    require?: string | string[];
    import?: string | string[];
    default?: string | string[];
    worker?: string | string[];
    browser?: string | string[];
};
import URL from '../url.js';
import { Loader } from './loader.js';
