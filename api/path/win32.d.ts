/**
 * Computes current working directory for a path
 * @param {string}
 * @return {string}
 */
export function cwd(): string;
/**
 * Resolves path components to an absolute path.
 * @param {...PathComponent} components
 * @return {string}
 */
export function resolve(...components: PathComponent[]): string;
/**
 * Joins path components. This function may not return an absolute path.
 * @param {...PathComponent} components
 * @return {string}
 */
export function join(...components: PathComponent[]): string;
/**
 * Computes directory name of path.
 * @param {PathComponent} path
 * @return {string}
 */
export function dirname(path: PathComponent): string;
/**
 * Computes base name of path.
 * @param {PathComponent} path
 * @param {string=} [suffix]
 * @return {string}
 */
export function basename(path: PathComponent, suffix?: string | undefined): string;
/**
 * Computes extension name of path.
 * @param {PathComponent} path
 * @return {string}
 */
export function extname(path: PathComponent): string;
/**
 * Predicate helper to determine if path is absolute.
 * @param {PathComponent} path
 * @return {boolean}
 */
export function isAbsolute(path: PathComponent): boolean;
/**
 * Parses input `path` into a `Path` instance.
 * @param {PathComponent} path
 * @return {{ root: string, dir: string, base: string, ext: string, name: string }}
 */
export function parse(path: PathComponent): {
    root: string;
    dir: string;
    base: string;
    ext: string;
    name: string;
};
/**
 * Formats `Path` object into a string.
 * @param {object|Path} path
 * @return {string}
 */
export function format(path: object | Path): string;
/**
 * Normalizes `path` resolving `..` and `.\` preserving trailing
 * slashes.
 * @param {string} path
 * @return {string}
 */
export function normalize(path: string): string;
/**
 * Computes the relative path from `from` to `to`.
 * @param {string} from
 * @param {string} to
 * @return {string}
 */
export function relative(from: string, to: string): string;
export default exports;
export namespace win32 {
    let sep: "\\";
    let delimiter: ";";
}
export type PathComponent = import("./path.js").PathComponent;
import { Path } from './path.js';
import * as mounts from './mounts.js';
import * as posix from './posix.js';
import { DOWNLOADS } from './well-known.js';
import { DOCUMENTS } from './well-known.js';
import { RESOURCES } from './well-known.js';
import { PICTURES } from './well-known.js';
import { DESKTOP } from './well-known.js';
import { VIDEOS } from './well-known.js';
import { CONFIG } from './well-known.js';
import { MEDIA } from './well-known.js';
import { MUSIC } from './well-known.js';
import { HOME } from './well-known.js';
import { DATA } from './well-known.js';
import { LOG } from './well-known.js';
import { TMP } from './well-known.js';
import * as exports from './win32.js';
export { mounts, posix, Path, DOWNLOADS, DOCUMENTS, RESOURCES, PICTURES, DESKTOP, VIDEOS, CONFIG, MEDIA, MUSIC, HOME, DATA, LOG, TMP };
