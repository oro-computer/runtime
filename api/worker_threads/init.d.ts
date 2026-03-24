export const SHARE_ENV: unique symbol;
export const isMainThread: boolean;
export namespace state {
    export { isMainThread };
    export let parentPort: any;
    export let mainPort: any;
    export let workerData: any;
    export let url: any;
    export let env: {};
    export let id: number;
}
declare namespace _default {
    export { state };
}
export default _default;
