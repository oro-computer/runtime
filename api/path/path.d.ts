/**
 * The path.resolve() method resolves a sequence of paths or path segments into an absolute path.
 * @param {object} options
 * @param {...PathComponent} components
 * @returns {string}
 * @see {@link https://nodejs.org/api/path.html#path_path_resolve_paths}
 */
export function resolve(options: object, ...components: PathComponent[]): string;
/**
 * Computes current working directory for a path
 * @param {object=} [opts]
 * @param {boolean=} [opts.posix] Set to `true` to force POSIX style path
 * @return {string}
 */
export function cwd(opts?: object | undefined): string;
/**
 * Computed location origin. Defaults to `oro:///` if not available.
 * @return {string}
 */
export function origin(): string;
/**
 * Computes the relative path from `from` to `to`.
 * @param {object} options
 * @param {PathComponent} from
 * @param {PathComponent} to
 * @return {string}
 */
export function relative(options: object, from: PathComponent, to: PathComponent): string;
/**
 * Joins path components. This function may not return an absolute path.
 * @param {object} options
 * @param {...PathComponent} components
 * @return {string}
 */
export function join(options: object, ...components: PathComponent[]): string;
/**
 * Computes directory name of path.
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function dirname(options: object, path: PathComponent): string;
/**
 * Computes base name of path.
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function basename(options: object, path: PathComponent): string;
/**
 * Computes extension name of path.
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function extname(options: object, path: PathComponent): string;
/**
 * Computes normalized path
 * @param {object} options
 * @param {PathComponent} path
 * @return {string}
 */
export function normalize(options: object, path: PathComponent): string;
/**
 * Formats `Path` object into a string.
 * @param {object} options
 * @param {object|Path} path
 * @return {string}
 */
export function format(options: object, path: object | Path): string;
/**
 * Parses input `path` into a `Path` instance.
 * @param {PathComponent} path
 * @return {object}
 */
export function parse(path: PathComponent): object;
/**
 * @typedef {(string|Path|URL|{ pathname: string }|{ url: string)} PathComponent
 */
/**
 * A container for a parsed Path.
 */
export class Path {
    /**
     * Creates a `Path` instance from `input` and optional `cwd`.
     * @param {PathComponent} input
     * @param {string} [cwd]
     */
    static from(input: PathComponent, cwd?: string): any;
    /**
     * `Path` class constructor.
     * @protected
     * @param {string} pathname
     * @param {string} [cwd = Path.cwd()]
     */
    protected constructor();
    pattern: {
        "__#private@#i": any;
        "__#private@#n": {};
        "__#private@#t": {};
        "__#private@#e": {};
        "__#private@#s": {};
        "__#private@#l": boolean;
        test(t: {}, r: any): boolean;
        exec(t: {}, r: any): {
            inputs: any[] | {}[];
        };
        get protocol(): any;
        get username(): any;
        get password(): any;
        get hostname(): any;
        get port(): any;
        get pathname(): any;
        get search(): any;
        get hash(): any;
        get hasRegExpGroups(): boolean;
    };
    url: any;
    get pathname(): any;
    get protocol(): any;
    get href(): any;
    /**
     * `true` if the path is relative, otherwise `false.
     * @type {boolean}
     */
    get isRelative(): boolean;
    /**
     * The working value of this path.
     */
    get value(): any;
    /**
     * The original source, unresolved.
     * @type {string}
     */
    get source(): string;
    /**
     * Computed parent path.
     * @type {string}
     */
    get parent(): string;
    /**
     * Computed root in path.
     * @type {string}
     */
    get root(): string;
    /**
     * Computed directory name in path.
     * @type {string}
     */
    get dir(): string;
    /**
     * Computed base name in path.
     * @type {string}
     */
    get base(): string;
    /**
     * Computed base name in path without path extension.
     * @type {string}
     */
    get name(): string;
    /**
     * Computed extension name in path.
     * @type {string}
     */
    get ext(): string;
    /**
     * The computed drive, if given in the path.
     * @type {string?}
     */
    get drive(): string | null;
    /**
     * @return {URL}
     */
    toURL(): URL;
    /**
     * Converts this `Path` instance to a string.
     * @return {string}
     */
    toString(): string;
    /**
     * @ignore
     */
    inspect(): {
        root: string;
        dir: string;
        base: string;
        ext: string;
        name: string;
    };
    /**
     * @ignore
     */
    [Symbol.toStringTag](): string;
    #private;
}
export default Path;
export type PathComponent = (string | Path | URL | {
    pathname: string;
} | {
    url: string;
});
import { URL } from '../url.js';
