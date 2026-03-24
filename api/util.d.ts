export function debug(section: any): {
    (...args: any[]): void;
    enabled: boolean;
};
export function hasOwnProperty(object: any, property: any): any;
export function isDate(object: any): boolean;
export function isTypedArray(object: any): boolean;
export function isArrayLike(input: any): boolean;
export function isError(object: any): boolean;
export function isSymbol(value: any): value is symbol;
export function isNumber(value: any): boolean;
export function isBoolean(value: any): boolean;
export function isArrayBufferView(buf: any): boolean;
export function isAsyncFunction(object: any): boolean;
export function isArgumentsObject(object: any): boolean;
export function isEmptyObject(object: any): boolean;
export function isObject(object: any): boolean;
export function isUndefined(value: any): boolean;
export function isNull(value: any): boolean;
export function isNullOrUndefined(value: any): boolean;
export function isPrimitive(value: any): boolean;
export function isRegExp(value: any): boolean;
export function isPlainObject(object: any): boolean;
export function isArrayBuffer(object: any): boolean;
export function isBufferLike(object: any): boolean;
export function isFunction(value: any): boolean;
export function isErrorLike(error: any): boolean;
export function isClass(value: any): boolean;
export function isBuffer(value: any): boolean;
export function isPromiseLike(object: any): boolean;
export function toString(object: any): any;
export function toBuffer(object: any, encoding?: any): any;
export function toProperCase(string: any): any;
export function splitBuffer(buffer: any, highWaterMark: any): any[];
export function clamp(value: any, min: any, max: any): number;
export function promisify(original: any): any;
export function inspect(value: any, options: any): any;
export namespace inspect {
    let ignore: symbol;
    let custom: symbol;
}
export function format(format: any, ...args: any[]): string;
export function parseJSON(string: any): any;
export function parseHeaders(headers: any): string[][];
export function noop(): void;
export function isValidPercentageValue(input: any): boolean;
export function compareBuffers(a: any, b: any): any;
export function inherits(Constructor: any, Super: any): void;
/**
 * @ignore
 * @param {string} source
 * @return {boolean}
 */
export function isESMSource(source: string): boolean;
export function deprecate(..._args: any[]): void;
export const TextDecoder: {
    new (label?: string, options?: TextDecoderOptions): TextDecoder;
    prototype: TextDecoder;
};
export const TextEncoder: {
    new (): TextEncoder;
    prototype: TextEncoder;
};
export const isArray: any;
export const inspectSymbols: symbol[];
export class IllegalConstructor {
}
export const ESM_TEST_REGEX: RegExp;
export default exports;
import types from './util/types.js';
import { MIMEType } from './mime/type.js';
import { MIMEParams } from './mime/params.js';
import * as exports from './util.js';
export { types, MIMEType, MIMEParams };
