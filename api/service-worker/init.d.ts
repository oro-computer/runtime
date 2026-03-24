export function onRegister(event: any): Promise<void>;
export function onUnregister(event: any): Promise<void>;
export function onSkipWaiting(event: any): Promise<void>;
export function onActivate(event: any): Promise<void>;
export function onFetch(event: any): Promise<ipc.Result>;
export function onNotificationShow(event: any, target: any): any;
export function onNotificationClose(event: any): void;
export function onGetNotifications(event: any): void;
export const workers: Map<any, any>;
export const channel: BroadcastChannel;
export class ServiceWorkerInstance extends Worker {
    constructor(filename: any, options: any);
    get info(): any;
    get notifications(): any[];
    onMessage(event: any): Promise<void>;
    #private;
}
export class ServiceWorkerInfo {
    constructor(data: any);
    id: any;
    url: any;
    hash: any;
    scope: any;
    scriptURL: any;
    serializedWorkerArgs: any;
    priority: string;
    get pathname(): string;
    get promise(): any;
    #private;
}
declare const _default: any;
export default _default;
import ipc from '../ipc.js';
