export function patchGlobalConsole(globalConsole: any, options?: {}): any;
export const globalConsole: globalThis.Console;
export class Console {
    /**
     * @ignore
     */
    constructor(options: any);
    /**
     * @type {import('dom').Console}
     */
    console: any;
    /**
     * @type {Map}
     */
    timers: Map<any, any>;
    /**
     * @type {Map}
     */
    counters: Map<any, any>;
    /**
     * @type {function?}
     */
    postMessage: Function | null;
    write(destination: any, ...args: any[]): any;
    assert(assertion: any, ...args: any[]): void;
    clear(): void;
    count(label?: string): void;
    countReset(label?: string): void;
    debug(...args: any[]): void;
    dir(...args: any[]): void;
    dirxml(...args: any[]): void;
    error(...args: any[]): void;
    info(...args: any[]): void;
    log(...args: any[]): void;
    table(...args: any[]): any;
    time(label?: string): void;
    timeEnd(label?: string): void;
    timeLog(label?: string): void;
    trace(...objects: any[]): void;
    warn(...args: any[]): void;
}
declare const _default: Console & {
    Console: typeof Console;
    globalConsole: globalThis.Console;
};
export default _default;
