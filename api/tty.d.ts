export function WriteStream(fd: any): Writable;
export function ReadStream(fd: any): Readable;
export function isatty(fd: any): boolean;
declare namespace _default {
    export { WriteStream };
    export { ReadStream };
    export { isatty };
}
export default _default;
import { Writable } from './stream.js';
import { Readable } from './stream.js';
