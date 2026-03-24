/**
 * Spawns a child process exeucting `command` with `args`
 * @param {string} command
 * @param {string[]|object=} [args]
 * @param {object=} [options
 * @return {ChildProcess}
 */
export function spawn(command: string, args?: (string[] | object) | undefined, options?: object | undefined): ChildProcess;
export function exec(command: any, options: any, callback: any): ChildProcess & {
    then(resolve: any, reject: any): Promise<any>;
    catch(reject: any): Promise<any>;
    finally(next: any): Promise<any>;
};
export function execSync(command: any, options: any): any;
export class Pipe extends AsyncResource {
    /**
     * `Pipe` class constructor.
     * @param {ChildProcess} process
     * @ignore
     */
    constructor(process: ChildProcess);
    /**
     * `true` if the pipe is still reading, otherwise `false`.
     * @type {boolean}
     */
    get reading(): boolean;
    /**
     * @type {import('./process')}
     */
    get process(): typeof import("./process.js");
    /**
     * Destroys the pipe
     */
    destroy(): void;
    #private;
}
export class ChildProcess extends EventEmitter {
    [x: number]: () => import("./gc.js").Finalizer;
    /**
     * `ChildProcess` class constructor.
     * @param {{
     *   env?: object,
     *   stdin?: boolean,
     *   stdout?: boolean,
     *   stderr?: boolean,
     *   signal?: AbortSignal,
     * }=} [options]
     */
    constructor(options?: {
        env?: object;
        stdin?: boolean;
        stdout?: boolean;
        stderr?: boolean;
        signal?: AbortSignal;
    } | undefined);
    /**
     * @ignore
     * @type {Pipe}
     */
    get pipe(): Pipe;
    /**
     * `true` if the child process was killed with kill()`,
     * otherwise `false`.
     * @type {boolean}
     */
    get killed(): boolean;
    /**
     * The process identifier for the child process. This value is
     * `> 0` if the process was spawned successfully, otherwise `0`.
     * @type {number}
     */
    get pid(): number;
    /**
     * The executable file name of the child process that is launched. This
     * value is `null` until the child process has successfully been spawned.
     * @type {string?}
     */
    get spawnfile(): string | null;
    /**
     * The full list of command-line arguments the child process was spawned with.
     * This value is an empty array until the child process has successfully been
     * spawned.
     * @type {string[]}
     */
    get spawnargs(): string[];
    /**
     * Always `false` as the IPC messaging is not supported.
     * @type {boolean}
     */
    get connected(): boolean;
    /**
     * The child process exit code. This value is `null` if the child process
     * is still running, otherwise it is a positive integer.
     * @type {number?}
     */
    get exitCode(): number | null;
    /**
     * If available, the underlying `stdin` writable stream for
     * the child process.
     * @type {import('./stream').Writable?}
     */
    get stdin(): import("./stream").Writable | null;
    /**
     * If available, the underlying `stdout` readable stream for
     * the child process.
     * @type {import('./stream').Readable?}
     */
    get stdout(): import("./stream").Readable | null;
    /**
     * If available, the underlying `stderr` readable stream for
     * the child process.
     * @type {import('./stream').Readable?}
     */
    get stderr(): import("./stream").Readable | null;
    /**
     * The underlying worker thread.
     * @ignore
     * @type {import('./worker_threads').Worker}
     */
    get worker(): import("./worker_threads").Worker;
    /**
     * This function does nothing, but is present for nodejs compat.
     */
    disconnect(): boolean;
    /**
     * This function does nothing, but is present for nodejs compat.
     * @return {boolean}
     */
    send(): boolean;
    /**
     * This function does nothing, but is present for nodejs compat.
     */
    ref(): boolean;
    /**
     * This function does nothing, but is present for nodejs compat.
     */
    unref(): boolean;
    /**
     * Kills the child process. This function throws an error if the child
     * process has not been spawned or is already killed.
     * @param {number|string} signal
     */
    kill(...args: any[]): this;
    /**
     * Spawns the child process. This function will throw an error if the process
     * is already spawned.
     * @param {string} command
     * @param {string[]=} [args]
     * @return {ChildProcess}
     */
    spawn(...args?: string[] | undefined): ChildProcess;
    /**
     * `EventTarget` based `addEventListener` method.
     * @param {string} event
     * @param {function(Event)} callback
     * @param {{ once?: false }} [options]
     */
    addEventListener(event: string, callback: (arg0: Event) => any, options?: {
        once?: false;
    }): void;
    /**
     * `EventTarget` based `removeEventListener` method.
     * @param {string} event
     * @param {function(Event)} callback
     * @param {{ once?: false }} [options]
     */
    removeEventListener(event: string, callback: (arg0: Event) => any): void;
    #private;
}
export function execFile(command: any, options: any, callback: any): ChildProcess & {
    then(resolve: any, reject: any): Promise<any>;
    catch(reject: any): Promise<any>;
    finally(next: any): Promise<any>;
};
declare namespace _default {
    export { ChildProcess };
    export { spawn };
    export { execFile };
    export { exec };
}
export default _default;
import { AsyncResource } from './async/resource.js';
import { EventEmitter } from './events.js';
