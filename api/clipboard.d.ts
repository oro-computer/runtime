/**
 * Write a string to the system clipboard.
 * @param {string} text
 * @returns {Promise<void>}
 */
export function writeText(text: string): Promise<void>;
/**
 * Read the current text contents from the system clipboard.
 * @returns {Promise<string>}
 */
export function readText(): Promise<string>;
/**
 * @returns {boolean} True when clipboard write operations are supported.
 */
export function canWriteText(): boolean;
/**
 * @returns {boolean} True when clipboard read operations are supported.
 */
export function canReadText(): boolean;
declare namespace _default {
    export { writeText };
    export { readText };
    export { canWriteText };
    export { canReadText };
}
export default _default;
