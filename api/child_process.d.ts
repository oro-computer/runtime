/**
 * Spawns a child process executing `command` directly with `args`.
 * @param {string} command
 * @param {string[]|ChildProcessOptions} [args]
 * @param {ChildProcessOptions} [options]
 * @return {ChildProcess}
 */
export function spawn(command: string, args?: string[] | ChildProcessOptions, options?: ChildProcessOptions): ChildProcess;
/**
 * Executes a command string through the platform shell.
 * @param {string} command
 * @param {ExecOptions|ExecCallback} [options]
 * @param {ExecCallback} [callback]
 * @return {ChildProcess & PromiseLike<{ stdout: string | Buffer, stderr: string | Buffer }>}
 */
export function exec(command: string, options?: ExecOptions | ExecCallback, callback?: ExecCallback): ChildProcess & PromiseLike<{
    stdout: string | Buffer;
    stderr: string | Buffer;
}>;
/**
 * Executes a file directly with tokenized arguments.
 * @param {string} file
 * @param {string[]|ExecFileOptions|ExecCallback} [args]
 * @param {ExecFileOptions|ExecCallback} [options]
 * @param {ExecCallback} [callback]
 * @return {ChildProcess & PromiseLike<{ stdout: string | Buffer, stderr: string | Buffer }>}
 */
export function execFile(file: string, args?: string[] | ExecFileOptions | ExecCallback, options?: ExecFileOptions | ExecCallback, callback?: ExecCallback): ChildProcess & PromiseLike<{
    stdout: string | Buffer;
    stderr: string | Buffer;
}>;
/**
 * Executes a command string synchronously through the platform shell.
 * @param {string} command
 * @param {ExecSyncOptions} [options]
 * @return {string|Buffer}
 */
export function execSync(command: string, options?: ExecSyncOptions): string | Buffer;
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
     * @param {ChildProcessOptions} [options]
     */
    constructor(options?: ChildProcessOptions);
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
     * @param {string} command Executable name or path.
     * @param {string[]|ChildProcessOptions} [args] Tokenized arguments or options.
     * @param {ChildProcessOptions} [options] Spawn options.
     * @return {ChildProcess}
     */
    spawn(command: string, args?: string[] | ChildProcessOptions, options?: ChildProcessOptions): ChildProcess;
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
declare namespace _default {
    export { ChildProcess };
    export { spawn };
    export { execFile };
    export { exec };
    export { execSync };
}
export default _default;
export type ChildProcessOptions = {
    /**
     * Complete child environment. Replaces the inherited environment when provided.
     */
    env?: Record<string, string | number | boolean | null | undefined>;
    /**
     * Working directory for the child.
     */
    cwd?: string;
    /**
     * Open a writable stdin pipe.
     */
    stdin?: boolean;
    /**
     * Open a readable stdout pipe.
     */
    stdout?: boolean;
    /**
     * Open a readable stderr pipe.
     */
    stderr?: boolean;
    /**
     * Signal that terminates the child when aborted.
     */
    signal?: AbortSignal;
    /**
     * Signal used for timeout or abort termination.
     */
    killSignal?: number | string;
    /**
     * Milliseconds before terminating the child.
     */
    timeout?: number;
};
export type ExecOptions = ChildProcessOptions & {
    encoding?: string;
    shell?: string;
};
export type ExecFileOptions = ChildProcessOptions & {
    encoding?: string;
};
export type ExecSyncOptions = Omit<ChildProcessOptions, "signal" | "stdin"> & {
    encoding?: string;
};
export type ExecCallback = (error: Error | null, stdout: string | Buffer | null, stderr: string | Buffer | null) => void;
import { Buffer } from './buffer.js';
import { AsyncResource } from './async/resource.js';
import { EventEmitter } from './events.js';
