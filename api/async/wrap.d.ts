/**
 * Returns `true` if a given function `fn` has the "async" wrapped tag,
 * meaning it was "tagged" in a `wrap(fn)` call before, otherwise this
 * function will return `false`.
 * @ignore
 * @param {function} fn
 * @param {boolean}
 */
export function isTagged(fn: Function): boolean;
/**
 * Tags a function `fn` as being "async wrapped" so subsequent calls to
 * `wrap(fn)` do not wrap an already wrapped function.
 * @ignore
 * @param {function} fn
 * @return {function}
 */
export function tag(fn: Function): Function;
/**
 * Wraps a function `fn` that captures a snapshot of the current async
 * context. This function is idempotent and will not wrap a function more
 * than once.
 * @ignore
 * @param {function} fn
 * @return {function}
 */
export function wrap(fn: Function): Function;
export const symbol: unique symbol;
export default wrap;
