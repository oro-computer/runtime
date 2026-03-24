export function assert(value: any, message?: any): void;
export function ok(value: any, message?: any): void;
export function equal(actual: any, expected: any, message?: any): void;
export function notEqual(actual: any, expected: any, message?: any): void;
export function strictEqual(actual: any, expected: any, message?: any): void;
export function notStrictEqual(actual: any, expected: any, message?: any): void;
export function deepEqual(actual: any, expected: any, message?: any): void;
export function notDeepEqual(actual: any, expected: any, message?: any): void;
export class AssertionError extends Error {
    constructor(options: any);
    actual: any;
    expected: any;
    operator: any;
}
declare const _default: typeof assert & {
    AssertionError: typeof AssertionError;
    ok: typeof ok;
    equal: typeof equal;
    notEqual: typeof notEqual;
    strictEqual: typeof strictEqual;
    notStrictEqual: typeof notStrictEqual;
    deepEqual: typeof deepEqual;
    notDeepEqual: typeof notDeepEqual;
};
export default _default;
