/**
 * Creates a new `File` instance from `filename`.
 * @param {string} filename
 * @param {{ fd: fs.FileHandle, highWaterMark?: number }=} [options]
 * @return {File}
 */
export function createFile(filename: string, options?: {
    fd: fs.FileHandle;
    highWaterMark?: number;
} | undefined): File;
/**
 * Creates a `FileSystemWritableFileStream` instance backed
 * by `oro:fs:` module from a given `FileSystemFileHandle` instance.
 * @param {string|File} file
 * @return {Promise<FileSystemFileHandle>}
 */
export function createFileSystemWritableFileStream(handle: any, options: any): Promise<FileSystemFileHandle>;
/**
 * Creates a `FileSystemFileHandle` instance backed by `oro:fs:` module from
 * a given `File` instance or filename string.
 * @param {string|File} file
 * @param {object} [options]
 * @return {Promise<FileSystemFileHandle>}
 */
export function createFileSystemFileHandle(file: string | File, options?: object): Promise<FileSystemFileHandle>;
/**
 * Creates a `FileSystemDirectoryHandle` instance backed by `oro:fs:` module
 * from a given directory name string.
 * @param {string} dirname
 * @return {Promise<FileSystemFileHandle>}
 */
export function createFileSystemDirectoryHandle(dirname: string, options?: any): Promise<FileSystemFileHandle>;
export const kFileSystemHandleFullName: unique symbol;
export const kFileDescriptor: unique symbol;
export const kFileFullName: unique symbol;
export const File: {
    new (fileBits: BlobPart[], fileName: string, options?: FilePropertyBag): File;
    prototype: File;
} | {
    new (): {
        get lastModifiedDate(): Date;
        get lastModified(): number;
        get name(): any;
        get size(): number;
        get type(): string;
        slice(): void;
        arrayBuffer(): Promise<void>;
        bytes(): Promise<void>;
        text(): Promise<void>;
        stream(): void;
    };
};
export const FileSystemHandle: {
    new (): {
        get name(): any;
        get kind(): any;
    };
};
export const FileSystemFileHandle: {
    new (): FileSystemFileHandle;
    prototype: FileSystemFileHandle;
} | {
    new (): {
        getFile(): Promise<void>;
        createWritable(_options?: any): Promise<void>;
        createSyncAccessHandle(): Promise<void>;
        get name(): any;
        get kind(): any;
    };
};
export const FileSystemDirectoryHandle: {
    new (): FileSystemDirectoryHandle;
    prototype: FileSystemDirectoryHandle;
} | {
    new (): {
        entries(): AsyncGenerator<never, void, unknown>;
        values(): AsyncGenerator<never, void, unknown>;
        keys(): AsyncGenerator<never, void, unknown>;
        resolve(_possibleDescendant: any): Promise<void>;
        removeEntry(_name: any, _options?: any): Promise<void>;
        getDirectoryHandle(_name: any, _options?: any): Promise<void>;
        getFileHandle(_name: any, _options?: any): Promise<void>;
        get name(): any;
        get kind(): any;
    };
};
export const FileSystemWritableFileStream: {
    new (underlyingSink?: UnderlyingSink<any>, strategy?: QueuingStrategy<any>): {
        seek(_position: any): Promise<void>;
        truncate(_size: any): Promise<void>;
        write(_data: any): Promise<void>;
        readonly locked: boolean;
        abort(reason?: any): Promise<void>;
        close(): Promise<void>;
        getWriter(): WritableStreamDefaultWriter<any>;
    };
};
declare namespace _default {
    export { createFileSystemWritableFileStream };
    export { createFileSystemDirectoryHandle };
    export { createFileSystemFileHandle };
    export { createFile };
}
export default _default;
import fs from './promises.js';
