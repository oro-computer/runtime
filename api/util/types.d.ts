/**
 * Returns `true` if input is a plan `Object` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isPlainObject(input: any): boolean;
/**
 * Returns `true` if input is an `AsyncFunction`
 * @param {any} input
 * @return {boolean}
 */
export function isAsyncFunction(input: any): boolean;
/**
 * Returns `true` if input is an `Function`
 * @param {any} input
 * @return {boolean}
 */
export function isFunction(input: any): boolean;
/**
 * Returns `true` if input is an `AsyncFunction` object.
 * @param {any} input
 * @return {boolean}
 */
export function isAsyncFunctionObject(input: any): boolean;
/**
 * Returns `true` if input is an `Function` object.
 * @param {any} input
 * @return {boolean}
 */
export function isFunctionObject(input: any): boolean;
/**
 * Always returns `false`.
 * @param {any} input
 * @return {boolean}
 */
export function isExternal(_input: any): boolean;
/**
 * Returns `true` if input is a `Date` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isDate(input: any): boolean;
/**
 * Returns `true` if input is an `arguments` object.
 * @param {any} input
 * @return {boolean}
 */
export function isArgumentsObject(input: any): boolean;
/**
 * Returns `true` if input is a `BigInt` object.
 * @param {any} input
 * @return {boolean}
 */
export function isBigIntObject(input: any): boolean;
/**
 * Returns `true` if input is a `Boolean` object.
 * @param {any} input
 * @return {boolean}
 */
export function isBooleanObject(input: any): boolean;
/**
 * Returns `true` if input is a `Number` object.
 * @param {any} input
 * @return {boolean}
 */
export function isNumberObject(input: any): boolean;
/**
 * Returns `true` if input is a `String` object.
 * @param {any} input
 * @return {boolean}
 */
export function isStringObject(input: any): boolean;
/**
 * Returns `true` if input is a `Symbol` object.
 * @param {any} input
 * @return {boolean}
 */
export function isSymbolObject(input: any): boolean;
/**
 * Returns `true` if input is native `Error` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isNativeError(input: any): boolean;
/**
 * Returns `true` if input is a `RegExp` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isRegExp(input: any): boolean;
/**
 * Returns `true` if input is a `GeneratorFunction`.
 * @param {any} input
 * @return {boolean}
 */
export function isGeneratorFunction(input: any): boolean;
/**
 * Returns `true` if input is an `AsyncGeneratorFunction`.
 * @param {any} input
 * @return {boolean}
 */
export function isAsyncGeneratorFunction(input: any): boolean;
/**
 * Returns `true` if input is an instance of a `Generator`.
 * @param {any} input
 * @return {boolean}
 */
export function isGeneratorObject(input: any): boolean;
/**
 * Returns `true` if input is a `Promise` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isPromise(input: any): boolean;
/**
 * Returns `true` if input is a `Map` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isMap(input: any): boolean;
/**
 * Returns `true` if input is a `Set` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isSet(input: any): boolean;
/**
 * Returns `true` if input is an instance of an `Iterator`.
 * @param {any} input
 * @return {boolean}
 */
export function isIterator(input: any): boolean;
/**
 * Returns `true` if input is an instance of an `AsyncIterator`.
 * @param {any} input
 * @return {boolean}
 */
export function isAsyncIterator(input: any): boolean;
/**
 * Returns `true` if input is an instance of a `MapIterator`.
 * @param {any} input
 * @return {boolean}
 */
export function isMapIterator(input: any): boolean;
/**
 * Returns `true` if input is an instance of a `SetIterator`.
 * @param {any} input
 * @return {boolean}
 */
export function isSetIterator(input: any): boolean;
/**
 * Returns `true` if input is a `WeakMap` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isWeakMap(input: any): boolean;
/**
 * Returns `true` if input is a `WeakSet` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isWeakSet(input: any): boolean;
/**
 * Returns `true` if input is an `ArrayBuffer` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isArrayBuffer(input: any): boolean;
/**
 * Returns `true` if input is an `DataView` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isDataView(input: any): boolean;
/**
 * Returns `true` if input is a `SharedArrayBuffer`.
 * This will always return `false` if a `SharedArrayBuffer`
 * type is not available.
 * @param {any} input
 * @return {boolean}
 */
export function isSharedArrayBuffer(input: any): boolean;
/**
 * Not supported. This function will return `false` always.
 * @param {any} input
 * @return {boolean}
 */
export function isProxy(_input: any): boolean;
/**
 * Returns `true` if input looks like a module namespace object.
 * @param {any} input
 * @return {boolean}
 */
export function isModuleNamespaceObject(input: any): boolean;
/**
 * Returns `true` if input is an `ArrayBuffer` of `SharedArrayBuffer`.
 * @param {any} input
 * @return {boolean}
 */
export function isAnyArrayBuffer(input: any): boolean;
/**
 * Returns `true` if input is a "boxed" primitive.
 * @param {any} input
 * @return {boolean}
 */
export function isBoxedPrimitive(input: any): boolean;
/**
 * Returns `true` if input is an `ArrayBuffer` view.
 * @param {any} input
 * @return {boolean}
 */
export function isArrayBufferView(input: any): boolean;
/**
 * Returns `true` if input is a `TypedArray` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isTypedArray(input: any): boolean;
/**
 * Returns `true` if input is an `Uint8Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isUint8Array(input: any): boolean;
/**
 * Returns `true` if input is an `Uint8ClampedArray` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isUint8ClampedArray(input: any): boolean;
/**
 * Returns `true` if input is an `Uint16Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isUint16Array(input: any): boolean;
/**
 * Returns `true` if input is an `Uint32Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isUint32Array(input: any): boolean;
/**
 * Returns `true` if input is an Int8Array`` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isInt8Array(input: any): boolean;
/**
 * Returns `true` if input is an `Int16Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isInt16Array(input: any): boolean;
/**
 * Returns `true` if input is an `Int32Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isInt32Array(input: any): boolean;
/**
 * Returns `true` if input is an `Float32Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isFloat32Array(input: any): boolean;
/**
 * Returns `true` if input is an `Float64Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isFloat64Array(input: any): boolean;
/**
 * Returns `true` if input is an `BigInt64Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isBigInt64Array(input: any): boolean;
/**
 * Returns `true` if input is an `BigUint64Array` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isBigUint64Array(input: any): boolean;
/**
 * @ignore
 * @param {any} input
 * @return {boolean}
 */
export function isKeyObject(_input: any): boolean;
/**
 * Returns `true` if input is a `CryptoKey` instance.
 * @param {any} input
 * @return {boolean}
 */
export function isCryptoKey(_input: any): boolean;
/**
 * Returns `true` if input is an `Array`.
 * @param {any} input
 * @return {boolean}
 */
export const isArray: any;
export default exports;
import * as exports from './types.js';
