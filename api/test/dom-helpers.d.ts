/**
 * Converts querySelector string to an HTMLElement or validates an existing HTMLElement.
 *
 * @export
 * @param {string|Element} selector - A CSS selector string, or an instance of HTMLElement, or Element.
 * @returns {Element} The HTMLElement, Element, or Window that corresponds to the selector.
 * @throws {Error} Throws an error if the `selector` is not a string that resolves to an HTMLElement or not an instance of HTMLElement, Element, or Window.
 *
 */
export function toElement(selector: string | Element): Element;
/**
 * Waits for an element to appear in the DOM and resolves the promise when it does.
 *
 * @export
 * @param {Object} args - Configuration arguments.
 * @param {string} [args.selector] - The CSS selector to look for.
 * @param {boolean} [args.visible=true] - Whether the element should be visible.
 * @param {number} [args.timeout=defaultTimeout] - Time in milliseconds to wait before rejecting the promise.
 * @param {() => HTMLElement | Element | null | undefined} [lambda] - An optional function that returns the element. Used if the `selector` is not provided.
 * @returns {Promise<Element|HTMLElement|void>} - A promise that resolves to the found element.
 *
 * @throws {Error} - Throws an error if neither `lambda` nor `selector` is provided.
 * @throws {Error} - Throws an error if the element is not found within the timeout.
 *
 * @example
 * ```js
 * waitFor({ selector: '#my-element', visible: true, timeout: 5000 })
 *   .then(el => console.log('Element found:', el))
 *   .catch(err => console.log('Element not found:', err));
 * ```
 */
export function waitFor(args: {
    selector?: string;
    visible?: boolean;
    timeout?: number;
}, lambda?: () => HTMLElement | Element | null | undefined): Promise<Element | HTMLElement | void>;
/**
 * Waits for an element's text content to match a given string or regular expression.
 *
 * @export
 * @param {Object} args - Configuration arguments.
 * @param {Element} args.element - The root element from which to begin searching.
 * @param {string} [args.text] - The text to search for within elements.
 * @param {RegExp} [args.regex] - A regular expression to match against element text content.
 * @param {boolean} [args.multipleTags=false] - Whether to look for text across multiple sibling elements.
 * @param {number} [args.timeout=defaultTimeout] - Time in milliseconds to wait before rejecting the promise.
 * @returns {Promise<Element|HTMLElement|void>} - A promise that resolves to the found element or null.
 *
 * @example
 * ```js
 * waitForText({ element: document.body, text: 'Hello', timeout: 5000 })
 *   .then(el => console.log('Element found:', el))
 *   .catch(err => console.log('Element not found:', err));
 * ```
 */
export function waitForText(args: {
    element: Element;
    text?: string;
    regex?: RegExp;
    multipleTags?: boolean;
    timeout?: number;
}): Promise<Element | HTMLElement | void>;
/**
 * @export
 * @param {Object} args - Arguments
 * @param {string | Event} args.event - The event to dispatch.
 * @param {HTMLElement | Element | window} [args.element=window] - The element to dispatch the event on.
 * @returns {void}
 *
 * @throws {Error} Throws an error if the `event` is not a string that can be converted to a CustomEvent or not an instance of Event.
 */
export function event(args: {
    event: string | Event;
    element?: HTMLElement | Element | (Window & typeof globalThis);
}): void;
/**
 * @export
 * Copy pasted from https://raw.githubusercontent.com/testing-library/jest-dom/master/src/to-be-visible.js
 * @param {Element | HTMLElement} element
 * @param {Element | HTMLElement} [previousElement]
 * @returns {boolean}
 */
export function isElementVisible(element: Element | HTMLElement, previousElement?: Element | HTMLElement): boolean;
