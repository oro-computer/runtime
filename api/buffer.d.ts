export default Buffer;
export const File: {
    new (fileBits: BlobPart[], fileName: string, options?: FilePropertyBag): File;
    prototype: File;
};
export const Blob: {
    new (blobParts?: BlobPart[], options?: BlobPropertyBag): Blob;
    prototype: Blob;
};
export namespace constants {
    export { kMaxLength as MAX_LENGTH };
    export { kMaxLength as MAX_STRING_LENGTH };
}
export const btoa: any;
export const atob: any;
/**
 * The Buffer constructor returns instances of `Uint8Array` that have their
 * prototype changed to `Buffer.prototype`. Furthermore, `Buffer` is a subclass of
 * `Uint8Array`, so the returned instances will have all the node `Buffer` methods
 * and the `Uint8Array` methods. Square bracket notation works as expected -- it
 * returns a single octet.
 *
 * The `Uint8Array` prototype remains unmodified.
 */
/**
 * @name Buffer
 * @extends {Uint8Array}
 */
export function Buffer(arg: any, encodingOrOffset: any, length: any): any;
export class Buffer {
    /**
     * The Buffer constructor returns instances of `Uint8Array` that have their
     * prototype changed to `Buffer.prototype`. Furthermore, `Buffer` is a subclass of
     * `Uint8Array`, so the returned instances will have all the node `Buffer` methods
     * and the `Uint8Array` methods. Square bracket notation works as expected -- it
     * returns a single octet.
     *
     * The `Uint8Array` prototype remains unmodified.
     */
    /**
     * @name Buffer
     * @extends {Uint8Array}
     */
    constructor(arg: any, encodingOrOffset: any, length: any);
    get parent(): any;
    get offset(): any;
    _isBuffer: boolean;
    swap16(): this;
    swap32(): this;
    swap64(): this;
    toString(...args: any[]): any;
    toLocaleString: any;
    equals(b: any): boolean;
    inspect(): string;
    compare(target: any, start: any, end: any, thisStart: any, thisEnd: any): 0 | 1 | -1;
    includes(val: any, byteOffset: any, encoding: any): boolean;
    indexOf(val: any, byteOffset: any, encoding: any): any;
    lastIndexOf(val: any, byteOffset: any, encoding: any): any;
    write(string: any, offset: any, length: any, encoding: any): number;
    toJSON(): {
        type: string;
        data: any;
    };
    slice(start: any, end: any): any;
    readUintLE: (offset: any, byteLength: any, noAssert: any) => any;
    readUIntLE(offset: any, byteLength: any, noAssert: any): any;
    readUintBE: (offset: any, byteLength: any, noAssert: any) => any;
    readUIntBE(offset: any, byteLength: any, noAssert: any): any;
    readUint8: (offset: any, noAssert: any) => any;
    readUInt8(offset: any, noAssert: any): any;
    readUint16LE: (offset: any, noAssert: any) => number;
    readUInt16LE(offset: any, noAssert: any): number;
    readUint16BE: (offset: any, noAssert: any) => number;
    readUInt16BE(offset: any, noAssert: any): number;
    readUint32LE: (offset: any, noAssert: any) => number;
    readUInt32LE(offset: any, noAssert: any): number;
    readUint32BE: (offset: any, noAssert: any) => number;
    readUInt32BE(offset: any, noAssert: any): number;
    readBigUInt64LE: any;
    readBigUInt64BE: any;
    readIntLE(offset: any, byteLength: any, noAssert: any): any;
    readIntBE(offset: any, byteLength: any, noAssert: any): any;
    readInt8(offset: any, noAssert: any): any;
    readInt16LE(offset: any, noAssert: any): number;
    readInt16BE(offset: any, noAssert: any): number;
    readInt32LE(offset: any, noAssert: any): number;
    readInt32BE(offset: any, noAssert: any): number;
    readBigInt64LE: any;
    readBigInt64BE: any;
    readFloatLE(offset: any, noAssert: any): number;
    readFloatBE(offset: any, noAssert: any): number;
    readDoubleLE(offset: any, noAssert: any): number;
    readDoubleBE(offset: any, noAssert: any): number;
    writeUintLE: (value: any, offset: any, byteLength: any, noAssert: any) => any;
    writeUIntLE(value: any, offset: any, byteLength: any, noAssert: any): any;
    writeUintBE: (value: any, offset: any, byteLength: any, noAssert: any) => any;
    writeUIntBE(value: any, offset: any, byteLength: any, noAssert: any): any;
    writeUint8: (value: any, offset: any, noAssert: any) => any;
    writeUInt8(value: any, offset: any, noAssert: any): any;
    writeUint16LE: (value: any, offset: any, noAssert: any) => any;
    writeUInt16LE(value: any, offset: any, noAssert: any): any;
    writeUint16BE: (value: any, offset: any, noAssert: any) => any;
    writeUInt16BE(value: any, offset: any, noAssert: any): any;
    writeUint32LE: (value: any, offset: any, noAssert: any) => any;
    writeUInt32LE(value: any, offset: any, noAssert: any): any;
    writeUint32BE: (value: any, offset: any, noAssert: any) => any;
    writeUInt32BE(value: any, offset: any, noAssert: any): any;
    writeBigUInt64LE: any;
    writeBigUInt64BE: any;
    writeIntLE(value: any, offset: any, byteLength: any, noAssert: any): any;
    writeIntBE(value: any, offset: any, byteLength: any, noAssert: any): any;
    writeInt8(value: any, offset: any, noAssert: any): any;
    writeInt16LE(value: any, offset: any, noAssert: any): any;
    writeInt16BE(value: any, offset: any, noAssert: any): any;
    writeInt32LE(value: any, offset: any, noAssert: any): any;
    writeInt32BE(value: any, offset: any, noAssert: any): any;
    writeBigInt64LE: any;
    writeBigInt64BE: any;
    writeFloatLE(value: any, offset: any, noAssert: any): any;
    writeFloatBE(value: any, offset: any, noAssert: any): any;
    writeDoubleLE(value: any, offset: any, noAssert: any): any;
    writeDoubleBE(value: any, offset: any, noAssert: any): any;
    copy(target: any, targetStart: any, start: any, end: any): number;
    fill(val: any, start: any, end: any, encoding: any): this;
}
export namespace Buffer {
    export let TYPED_ARRAY_SUPPORT: boolean;
    export let poolSize: number;
    /**
     * Functionally equivalent to Buffer(arg, encoding) but throws a TypeError
     * if value is a number.
     * Buffer.from(str[, encoding])
     * Buffer.from(array)
     * Buffer.from(buffer)
     * Buffer.from(arrayBuffer[, byteOffset[, length]])
     **/
    export function from(value: any, encodingOrOffset?: any, length?: any): any;
    /**
     * Creates a new filled Buffer instance.
     * alloc(size[, fill[, encoding]])
     **/
    export function alloc(size: any, fill: any, encoding: any): Uint8Array<any>;
    /**
     * Equivalent to Buffer(num), by default creates a non-zero-filled Buffer instance.
     * */
    export function allocUnsafe(size: any): Uint8Array<any>;
    /**
     * Equivalent to SlowBuffer(num), by default creates a non-zero-filled Buffer instance.
     */
    export function allocUnsafeSlow(size: any): Uint8Array<any>;
    export function isBuffer(b: any): boolean;
    export function compare(a: any, b: any): 0 | 1 | -1;
    export function isEncoding(encoding: any): boolean;
    export function concat(list: any, length?: any): Uint8Array<any>;
    export { byteLength };
}
export const kMaxLength: 2147483647;
export function SlowBuffer(length: any): Uint8Array<any>;
export const INSPECT_MAX_BYTES: 50;
declare function byteLength(string: any, encoding: any, ...args: any[]): any;
