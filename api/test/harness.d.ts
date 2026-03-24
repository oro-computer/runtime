/**
 * @typedef {import('./index').Test} Test
 * @typedef {(t: Test) => Promise<void> | void} TestCase
 * @typedef {{
 *    bootstrap(): Promise<void>
 *    close(): Promise<void>
 * }} Harness
 */
/**
 * @template {Harness} T
 * @typedef {{
 *    (
 *      name: string,
 *      cb?: (harness: T, test: Test) => (void | Promise<void>)
 *    ): void;
 *    (
 *      name: string,
 *      opts: object,
 *      cb: (harness: T, test: Test) => (void | Promise<void>)
 *    ): void;
 *    only(
 *      name: string,
 *      cb?: (harness: T, test: Test) => (void | Promise<void>)
 *    ): void;
 *    only(
 *      name: string,
 *      opts: object,
 *      cb: (harness: T, test: Test) => (void | Promise<void>)
 *    ): void;
 *    skip(
 *      name: string,
 *      cb?: (harness: T, test: Test) => (void | Promise<void>)
 *    ): void;
 *    skip(
 *      name: string,
 *      opts: object,
 *      cb: (harness: T, test: Test) => (void | Promise<void>)
 *    ): void;
 * }} TapeTestFn
 */
/**
 * @template {Harness} T
 * @param {import('./index.js')} tapzero
 * @param {new (options: object) => T} harnessClass
 * @returns {TapeTestFn<T>}
 */
export function wrapHarness<T extends Harness>(tapzero: typeof import("./index"), harnessClass: new (options: object) => T): TapeTestFn<T>;
export default exports;
/**
 * @template {Harness} T
 */
export class TapeHarness<T extends Harness> {
    /**
     * @param {import('./index.js')} tapzero
     * @param {new (options: object) => T} harnessClass
     */
    constructor(tapzero: typeof import("./index"), harnessClass: new (options: object) => T);
    /** @type {import('./index.js')} */
    tapzero: typeof import("./index");
    /** @type {new (options: object) => T} */
    harnessClass: new (options: object) => T;
    /**
     * @param {string} testName
     * @param {object} [options]
     * @param {(harness: T, test: Test) => (void | Promise<void>)} [fn]
     * @returns {void}
     */
    test(testName: string, options?: object, fn?: (harness: T, test: Test) => (void | Promise<void>)): void;
    /**
     * @param {string} testName
     * @param {object} [options]
     * @param {(harness: T, test: Test) => (void | Promise<void>)} [fn]
     * @returns {void}
     */
    only(testName: string, options?: object, fn?: (harness: T, test: Test) => (void | Promise<void>)): void;
    /**
     * @param {string} testName
     * @param {object} [options]
     * @param {(harness: T, test: Test) => (void | Promise<void>)} [fn]
     * @returns {void}
     */
    skip(testName: string, options?: object, fn?: (harness: T, test: Test) => (void | Promise<void>)): void;
    /**
     * @param {(str: string, fn?: TestCase) => void} tapzeroFn
     * @param {string} testName
     * @param {object} [options]
     * @param {(harness: T, test: Test) => (void | Promise<void>)} [fn]
     * @returns {void}
     */
    _test(tapzeroFn: (str: string, fn?: TestCase) => void, testName: string, options?: object, fn?: (harness: T, test: Test) => (void | Promise<void>)): void;
    /**
     * @param {Test} assert
     * @param {object} options
     * @param {(harness: T, test: Test) => (void | Promise<void>)} fn
     * @returns {Promise<void>}
     */
    _onAssert(assert: Test, options: object, fn: (harness: T, test: Test) => (void | Promise<void>)): Promise<void>;
}
export type Test = import("./index").Test;
export type TestCase = (t: Test) => Promise<void> | void;
export type Harness = {
    bootstrap(): Promise<void>;
    close(): Promise<void>;
};
export type TapeTestFn<T extends Harness> = {
    (name: string, cb?: (harness: T, test: Test) => (void | Promise<void>)): void;
    (name: string, opts: object, cb: (harness: T, test: Test) => (void | Promise<void>)): void;
    only(name: string, cb?: (harness: T, test: Test) => (void | Promise<void>)): void;
    only(name: string, opts: object, cb: (harness: T, test: Test) => (void | Promise<void>)): void;
    skip(name: string, cb?: (harness: T, test: Test) => (void | Promise<void>)): void;
    skip(name: string, opts: object, cb: (harness: T, test: Test) => (void | Promise<void>)): void;
};
import * as exports from './harness.js';
