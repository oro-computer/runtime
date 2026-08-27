/**
 * @returns {number} - The default timeout for tests in milliseconds.
 */
export function getDefaultTestRunnerTimeout(): number;
/**
 * @param {string} name
 * @param {TestFn} [fn]
 * @returns {void}
 */
export function only(name: string, fn?: TestFn): void;
/**
 * @param {string} _name
 * @param {TestFn} [_fn]
 * @returns {void}
 */
export function skip(_name: string, _fn?: TestFn): void;
/**
 * @param {boolean} strict
 * @returns {void}
 */
export function setStrict(strict: boolean): void;
/**
 * @typedef {{
 *    (name: string, fn?: TestFn): void
 *    only(name: string, fn?: TestFn): void
 *    skip(name: string, fn?: TestFn): void
 * }} testWithProperties
 * @ignore
 */
/**
 * @type {testWithProperties}
 * @param {string} name
 * @param {TestFn} [fn]
 * @returns {void}
 */
export function test(name: string, fn?: TestFn): void;
export namespace test {
    export { only };
    export { skip };
    export function linux(name: any, fn: any): void;
    export function windows(name: any, fn: any): void;
    export function win32(name: any, fn: any): void;
    export function unix(name: any, fn: any): void;
    export function macosx(name: any, fn: any): void;
    export function macos(name: any, fn: any): void;
    export function mac(name: any, fn: any): void;
    export function darwin(name: any, fn: any): void;
    export function iphone(name: any, fn: any): void;
    export namespace iphone {
        function simulator(name: any, fn: any): void;
    }
    export function ios(name: any, fn: any): void;
    export namespace ios {
        function simulator(name: any, fn: any): void;
    }
    export function android(name: any, fn: any): void;
    export namespace android {
        function emulator(name: any, fn: any): void;
    }
    export function desktop(name: any, fn: any): void;
    export function mobile(name: any, fn: any): void;
}
/**
 * @typedef {(t: Test) => (void | Promise<void>)} TestFn
 */
/**
 * @class
 */
export class Test {
    /**
     * @constructor
     * @param {string} name
     * @param {TestFn} fn
     * @param {TestRunner} runner
     */
    constructor(name: string, fn: TestFn, runner: TestRunner);
    /**
     * @type {string}
     * @ignore
     */
    name: string;
    /**
     * @type {null|number}
     * @ignore
     */
    _planned: null | number;
    /**
     * @type {null|number}
     * @ignore
     */
    _actual: null | number;
    /**
     * @type {TestFn}
     * @ignore
     */
    fn: TestFn;
    /**
     * @type {TestRunner}
     * @ignore
     */
    runner: TestRunner;
    /**
     * @type{{ pass: number, fail: number }}
     * @ignore
     */
    _result: {
        pass: number;
        fail: number;
    };
    /**
     * @type {boolean}
     * @ignore
     */
    done: boolean;
    /**
     * @type {boolean}
     * @ignore
     */
    strict: boolean;
    /**
     * @param {string} msg
     * @returns {void}
     */
    comment(msg: string): void;
    /**
     * Plan the number of assertions.
     *
     * @param {number} n
     * @returns {void}
     */
    plan(n: number): void;
    /**
     * @template T
     * @param {T} actual
     * @param {T} expected
     * @param {string} [msg]
     * @returns {void}
     */
    deepEqual<T>(actual: T, expected: T, msg?: string): void;
    /**
     * Assert that two values are deeply equivalent.
     *
     * @template T
     * @param {T} actual
     * @param {T} expected
     * @param {string} [msg]
     * @returns {void}
     */
    same<T>(actual: T, expected: T, msg?: string): void;
    /**
     * @template T
     * @param {T} actual
     * @param {T} expected
     * @param {string} [msg]
     * @returns {void}
     */
    notDeepEqual<T>(actual: T, expected: T, msg?: string): void;
    /**
     * @template T
     * @param {T} actual
     * @param {T} expected
     * @param {string} [msg]
     * @returns {void}
     */
    equal<T>(actual: T, expected: T, msg?: string): void;
    /**
     * @param {unknown} actual
     * @param {unknown} expected
     * @param {string} [msg]
     * @returns {void}
     */
    notEqual(actual: unknown, expected: unknown, msg?: string): void;
    /**
     * @param {string} [msg]
     * @returns {void}
     */
    fail(msg?: string): void;
    /**
     * @param {unknown} actual
     * @param {string} [msg]
     * @returns {void}
     */
    ok(actual: unknown, msg?: string): void;
    /**
     * Assert that a value is falsy.
     *
     * @param {unknown} actual
     * @param {string} [msg]
     * @returns {void}
     */
    notOk(actual: unknown, msg?: string): void;
    /**
     * Assert that a value matches a regular expression.
     *
     * @param {unknown} actual
     * @param {RegExp} expected
     * @param {string} [msg]
     * @returns {void}
     */
    match(actual: unknown, expected: RegExp, msg?: string): void;
    /**
     * @param {string} [msg]
     * @returns {void}
     */
    pass(msg?: string): void;
    /**
     * Mark the current test as skipped.
     *
     * @param {string} [msg]
     * @returns {void}
     */
    skip(msg?: string): void;
    /**
     * @param {Error | null | undefined} err
     * @param {string} [msg]
     * @returns {void}
     */
    ifError(err: Error | null | undefined, msg?: string): void;
    /**
     * @param {Function} fn
     * @param {RegExp | any} [expected]
     * @param {string} [message]
     * @returns {void}
     */
    throws(fn: Function, expected?: RegExp | any, message?: string): void;
    /**
     * Assert that a promise or async function rejects.
     * @param {PromiseLike<any>|(() => any)} input
     * @param {RegExp|((error: Error) => boolean)} [expected]
     * @param {string} [message]
     * @returns {Promise<void>}
     */
    rejects(input: PromiseLike<any> | (() => any), expected?: RegExp | ((error: Error) => boolean), message?: string): Promise<void>;
    /**
     * Sleep for ms with an optional msg
     *
     * @param {number} ms
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.sleep(100)
     * ```
     */
    sleep(ms: number, msg?: string): Promise<void>;
    /**
     * Request animation frame with an optional msg. Falls back to a 0ms setTimeout when
     * tests are run headlessly.
     *
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.requestAnimationFrame()
     * ```
     */
    requestAnimationFrame(msg?: string): Promise<void>;
    /**
     * Dispatch the `click` method on an element specified by selector.
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.click('.class button', 'Click a button')
     * ```
     */
    click(selector: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     * Dispatch the click window.MouseEvent on an element specified by selector.
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.eventClick('.class button', 'Click a button with an event')
     * ```
     */
    eventClick(selector: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     *  Dispatch an event on the target.
     *
     * @param {string | Event} event - The event name or Event instance to dispatch.
     * @param {string|HTMLElement|Element} target - A CSS selector string, or an instance of HTMLElement, or Element to dispatch the event on.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.dispatchEvent('my-event', '#my-div', 'Fire the my-event event')
     * ```
     */
    dispatchEvent(event: string | Event, target: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     *  Call the focus method on element specified by selector.
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.focus('#my-div')
     * ```
     */
    focus(selector: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     *  Call the blur method on element specified by selector.
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.blur('#my-div')
     * ```
     */
    blur(selector: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     * Consecutively set the str value of the element specified by selector to simulate typing.
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element.
     * @param {string} str - The string to type into the :focus element.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.typeValue('#my-div', 'Hello World', 'Type "Hello World" into #my-div')
     * ```
     */
    type(selector: string | HTMLElement | Element, str: string, msg?: string): Promise<void>;
    /**
     * appendChild an element el to a parent selector element.
     *
     * @param {string|HTMLElement|Element} parentSelector - A CSS selector string, or an instance of HTMLElement, or Element to appendChild on.
     * @param {HTMLElement|Element} el - A element to append to the parent element.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * const myElement = createElement('div')
     * await t.appendChild('#parent-selector', myElement, 'Append myElement into #parent-selector')
     * ```
     */
    appendChild(parentSelector: string | HTMLElement | Element, el: HTMLElement | Element, msg?: string): Promise<void>;
    /**
     * Remove an element from the DOM.
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element to remove from the DOM.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.removeElement('#dom-selector', 'Remove #dom-selector')
     * ```
     */
    removeElement(selector: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     * Test if an element is visible
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element to test visibility on.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.elementVisible('#dom-selector','Element is visible')
     * ```
     */
    elementVisible(selector: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     * Test if an element is invisible
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element to test visibility on.
     * @param {string} [msg]
     * @returns {Promise<void>}
     *
     * @example
     * ```js
     * await t.elementInvisible('#dom-selector','Element is invisible')
     * ```
     */
    elementInvisible(selector: string | HTMLElement | Element, msg?: string): Promise<void>;
    /**
     * Test if an element is invisible
     *
     * @param {string|(() => HTMLElement|Element|null|undefined)} querySelectorOrFn - A query string or a function that returns an element.
     * @param {Object} [opts]
     * @param {boolean} [opts.visible] - The element needs to be visible.
     * @param {number} [opts.timeout] - The maximum amount of time to wait.
     * @param {string} [msg]
     * @returns {Promise<HTMLElement|Element|void>}
     *
     * @example
     * ```js
     * await t.waitFor('#dom-selector', { visible: true },'#dom-selector is on the page and visible')
     * ```
     */
    waitFor(querySelectorOrFn: string | (() => HTMLElement | Element | null | undefined), opts?: {
        visible?: boolean;
        timeout?: number;
    }, msg?: string): Promise<HTMLElement | Element | void>;
    /**
     * @typedef {Object} WaitForTextOpts
     * @property {string} [text] - The text to wait for
     * @property {number} [timeout]
     * @property {Boolean} [multipleTags]
     * @property {RegExp} [regex] The regex to wait for
     */
    /**
     * Test if an element is invisible
     *
     * @param {string|HTMLElement|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element.
     * @param {WaitForTextOpts | string | RegExp} [opts]
     * @param {string} [msg]
     * @returns {Promise<HTMLElement|Element|void>}
     *
     * @example
     * ```js
     * await t.waitForText('#dom-selector', 'Text to wait for')
     * ```
     *
     * @example
     * ```js
     * await t.waitForText('#dom-selector', /hello/i)
     * ```
     *
     * @example
     * ```js
     * await t.waitForText('#dom-selector', {
     *   text: 'Text to wait for',
     *   multipleTags: true
     * })
     * ```
     */
    waitForText(selector: string | HTMLElement | Element, opts?: {
        /**
         * - The text to wait for
         */
        text?: string;
        timeout?: number;
        multipleTags?: boolean;
        /**
         * The regex to wait for
         */
        regex?: RegExp;
    } | string | RegExp, msg?: string): Promise<HTMLElement | Element | void>;
    /**
     * Run a querySelector as an assert and also get the results
     *
     * @param {string} selector - A CSS selector string, or an instance of HTMLElement, or Element to select.
     * @param {string} [msg]
     * @returns {HTMLElement | Element}
     *
     * @example
     * ```js
     * const element = await t.querySelector('#dom-selector')
     * ```
     */
    querySelector(selector: string, msg?: string): HTMLElement | Element;
    /**
     * Run a querySelectorAll as an assert and also get the results
     *
     * @param {string} selector - A CSS selector string, or an instance of HTMLElement, or Element to select.
     * @param {string} [msg]
     @returns {Array<HTMLElement | Element>}
     *
     * @example
     * ```js
     * const elements = await t.querySelectorAll('#dom-selector', '')
     * ```
     */
    querySelectorAll(selector: string, msg?: string): Array<HTMLElement | Element>;
    /**
     * Retrieves the computed styles for a given element.
     *
     * @param {string|Element} selector - The CSS selector or the Element object for which to get the computed styles.
     * @param {string} [msg] - An optional message to display when the operation is successful. Default message will be generated based on the type of selector.
     * @returns {CSSStyleDeclaration} - The computed styles of the element.
     * @throws {Error} - Throws an error if the element has no `ownerDocument` or if `ownerDocument.defaultView` is not available.
     *
     * @example
     * ```js
     * // Using CSS selector
     * const style = getComputedStyle('.my-element', 'Custom success message');
     * ```
     *
     * @example
     * ```js
     * // Using Element object
     * const el = document.querySelector('.my-element');
     * const style = getComputedStyle(el);
     * ```
     */
    getComputedStyle(selector: string | Element, msg?: string): CSSStyleDeclaration;
    /**
     * @param {boolean} pass
     * @param {unknown} actual
     * @param {unknown} expected
     * @param {string} description
     * @param {string} operator
     * @returns {void}
     * @ignore
     */
    _assert(pass: boolean, actual: unknown, expected: unknown, description: string, operator: string): void;
    /**
     * @returns {Promise<{
     *   pass: number,
     *   fail: number
     * }>}
     */
    run(): Promise<{
        pass: number;
        fail: number;
    }>;
}
/**
 * @class
 */
export class TestRunner {
    /**
     * @constructor
     * @param {(lines: string) => void} [report]
     */
    constructor(report?: (lines: string) => void);
    /**
     * @type {(lines: string) => void}
     * @ignore
     */
    report: (lines: string) => void;
    /**
     * @type {Test[]}
     * @ignore
     */
    tests: Test[];
    /**
     * @type {Test[]}
     * @ignore
     */
    onlyTests: Test[];
    /**
     * @type {boolean}
     * @ignore
     */
    scheduled: boolean;
    /**
     * @type {number}
     * @ignore
     */
    _id: number;
    /**
     * @type {boolean}
     * @ignore
     */
    completed: boolean;
    /**
     * @type {boolean}
     * @ignore
     */
    rethrowExceptions: boolean;
    /**
     * @type {boolean}
     * @ignore
     */
    strict: boolean;
    /**
     * @type {ReturnType<typeof createTestFilter> | null}
     * @ignore
     */
    filters: ReturnType<typeof createTestFilter> | null;
    /**
     * @type {boolean}
     * @ignore
     */
    _filtersAnnounced: boolean;
    /**
     * @type {number}
     * @ignore
     */
    _filteredCount: number;
    /**
     * @type {function | void}
     * @ignore
     */
    _onFinishCallback: Function | void;
    /**
     * @returns {string}
     */
    nextId(): string;
    /**
     * @type {number}
     */
    get length(): number;
    /**
     * @param {string} name
     * @param {TestFn} fn
     * @param {boolean} only
     * @returns {void}
     */
    add(name: string, fn: TestFn, only: boolean): void;
    /**
     * @returns {Promise<void>}
     */
    run(): Promise<void>;
    /**
     * @param {(result: { total: number, success: number, fail: number }) => void} callback
     * @returns {void}
     */
    onFinish(callback: (result: {
        total: number;
        success: number;
        fail: number;
    }) => void): void;
}
/**
 * @ignore
 */
export const GLOBAL_TEST_RUNNER: TestRunner;
export default test;
export type testWithProperties = {
    (name: string, fn?: TestFn): void;
    only(name: string, fn?: TestFn): void;
    skip(name: string, fn?: TestFn): void;
};
export type TestProcessEnv = Record<string, string | undefined>;
export type TestFn = (t: Test) => (void | Promise<void>);
/**
 * @param {TestProcessEnv} env
 * @returns {null | { description: string, shouldRun(name: string, meta: { isOnly: boolean }): boolean }}
 * @ignore
 */
declare function createTestFilter(env: TestProcessEnv): null | {
    description: string;
    shouldRun(name: string, meta: {
        isOnly: boolean;
    }): boolean;
};
