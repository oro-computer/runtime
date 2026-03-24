export function setTimeout(callback: any, delay: any, ...args: any[]): import("./timer.js").Timer;
export function clearTimeout(timeout: any): void;
export function setInterval(callback: any, delay: any, ...args: any[]): import("./timer.js").Timer;
export function clearInterval(interval: any): void;
export function setImmediate(callback: any, ...args: any[]): import("./timer.js").Timer;
export function clearImmediate(immediate: any): void;
/**
 * Pause async execution for `timeout` milliseconds.
 * @param {number} timeout
 * @return {Promise}
 */
export function sleep(timeout: number): Promise<any>;
export namespace sleep {
    /**
     * Pause sync execution for `timeout` milliseconds.
     * @param {number} timeout
     */
    function sync(timeout: number): void;
}
export { platform };
declare namespace _default {
    export { platform };
    export { promises };
    export { scheduler };
    export { setTimeout };
    export { clearTimeout };
    export { setInterval };
    export { clearInterval };
    export { setImmediate };
    export { clearImmediate };
}
export default _default;
import platform from './platform.js';
import promises from './promises.js';
import scheduler from './scheduler.js';
