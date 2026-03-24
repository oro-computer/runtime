export function setTimeout(callback: any, ...args: any[]): number;
export function clearTimeout(timeout: any): any;
export function setInterval(callback: any, ...args: any[]): number;
export function clearInterval(interval: any): any;
export function setImmediate(callback: any, ...args: any[]): number;
export function clearImmediate(immediate: any): any;
declare namespace _default {
    export { setTimeout };
    export { setInterval };
    export { setImmediate };
    export { clearTimeout };
    export { clearInterval };
    export { clearImmediate };
}
export default _default;
