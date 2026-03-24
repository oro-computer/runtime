export function wait(delay: any, options?: any): Promise<any>;
export function postTask(callback: any, options?: any): Promise<any>;
declare namespace _default {
    export { postTask };
    export { setImmediate as yield };
    export { wait };
}
export default _default;
import { setImmediate } from './promises.js';
