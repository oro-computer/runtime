export function onReady(): void;
export function onMessage(event: any): Promise<any>;
declare const _default: any;
export default _default;
export namespace SERVICE_WORKER_READY_TOKEN {
    let __service_worker_ready: boolean;
}
export namespace module {
    let exports: {};
}
export const events: Set<any>;
export namespace stages {
    let register: Deferred;
    let install: Deferred;
    let activate: Deferred;
}
import { Deferred } from '../async.js';
