/**
 * @param {string} dest - file path
 * @param {string} hash - hash string
 * @param {string} hashAlgorithm - hash algorithm
 * @returns {Promise<boolean>}
 */
export function checkHash(dest: string, hash: string, hashAlgorithm: string): Promise<boolean>;
export function bootstrap(options: any): Bootstrap;
declare namespace _default {
    export { bootstrap };
    export { checkHash };
}
export default _default;
declare class Bootstrap extends EventEmitter {
    constructor(options: any);
    options: any;
    run(): Promise<void>;
    /**
     * @param {object} options
     * @param {Uint8Array} options.fileBuffer
     * @param {string} options.dest
     * @returns {Promise<void>}
     */
    write({ fileBuffer, dest }: {
        fileBuffer: Uint8Array;
        dest: string;
    }): Promise<void>;
    /**
     * @param {string} url - url to download
     * @returns {Promise<Uint8Array>}
     * @throws {Error} - if status code is not 200
     */
    download(url: string): Promise<Uint8Array>;
    cleanup(): void;
}
import { EventEmitter } from './events.js';
