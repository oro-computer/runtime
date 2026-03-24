export function setTimeout(delay?: number, value?: any, options?: any): Promise<any>;
export function setInterval(delay?: number, value?: any, options?: any): AsyncGenerator<any, void, unknown>;
export function setImmediate(value?: any, options?: any): Promise<any>;
declare namespace _default {
    export { setImmediate };
    export { setInterval };
    export { setTimeout };
}
export default _default;
