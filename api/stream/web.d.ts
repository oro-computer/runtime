export const TextEncoderStream: typeof UnsupportedStreamInterface;
export const TextDecoderStream: {
    new (label?: string, options?: TextDecoderOptions): TextDecoderStream;
    prototype: TextDecoderStream;
} | typeof UnsupportedStreamInterface;
export const CompressionStream: {
    new (format: CompressionFormat): CompressionStream;
    prototype: CompressionStream;
} | typeof UnsupportedStreamInterface;
export const DecompressionStream: {
    new (format: CompressionFormat): DecompressionStream;
    prototype: DecompressionStream;
} | typeof UnsupportedStreamInterface;
export default exports;
import { ReadableStream } from '../internal/streams.js';
import { ReadableStreamBYOBReader } from '../internal/streams.js';
import { ReadableByteStreamController } from '../internal/streams.js';
import { ReadableStreamBYOBRequest } from '../internal/streams.js';
import { ReadableStreamDefaultController } from '../internal/streams.js';
import { ReadableStreamDefaultReader } from '../internal/streams.js';
import { WritableStream } from '../internal/streams.js';
import { WritableStreamDefaultController } from '../internal/streams.js';
import { WritableStreamDefaultWriter } from '../internal/streams.js';
import { TransformStream } from '../internal/streams.js';
import { TransformStreamDefaultController } from '../internal/streams.js';
import { ByteLengthQueuingStrategy } from '../internal/streams.js';
import { CountQueuingStrategy } from '../internal/streams.js';
declare class UnsupportedStreamInterface {
}
import * as exports from './web.js';
export { ReadableStream, ReadableStreamBYOBReader, ReadableByteStreamController, ReadableStreamBYOBRequest, ReadableStreamDefaultController, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultController, WritableStreamDefaultWriter, TransformStream, TransformStreamDefaultController, ByteLengthQueuingStrategy, CountQueuingStrategy };
