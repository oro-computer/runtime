export function onInstall(event: any): Promise<void>;
export function onUninstall(event: any): Promise<void>;
export function onConnect(event: any): Promise<void>;
export const workers: Map<any, any>;
export { channel };
export class SharedWorkerInstance extends Worker {
    constructor(filename: any, options: any);
    get info(): any;
    onMessage(event: any): Promise<void>;
    #private;
}
export class SharedWorkerInfo {
    constructor(data: any);
    id: any;
    port: any;
    client: any;
    scriptURL: any;
    url: any;
    hash: any;
    get pathname(): string;
}
declare const _default: any;
export default _default;
import { channel } from './index.js';
