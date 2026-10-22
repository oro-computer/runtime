/**
 * @typedef {object} RequestAnimationFrameMetricOptions
 * @property {number} [sampleSize=60] Rolling sample capacity, from 1 to 3600.
 */
/** Measures distinct animation frame timestamps using a bounded rolling buffer. */
export class RequestAnimationFrameMetric extends Metric {
    /** @param {RequestAnimationFrameMetricOptions} [options] */
    constructor(options?: RequestAnimationFrameMetricOptions);
    originalRequestAnimationFrame: typeof requestAnimationFrame;
    requestAnimationFrame(callback: any): any;
    sampleSize: number;
    sampleTick: number;
    channel: import("./channels.js").Channel;
    value: {
        rate: number;
        samples: number;
    };
    now: number;
    samples: Float64Array<ArrayBuffer>;
    /** @param {number} timestamp Animation frame timestamp in milliseconds. */
    update(timestamp: number): void;
    toJSON(): {
        sampleSize: number;
        sampleTick: number;
        samples: number[];
        rate: number;
        now: number;
    };
    #private;
}
export class FetchMetric extends Metric {
    constructor(_options: any);
    originalFetch: typeof fetch;
    channel: import("./channels.js").Channel;
    fetch(resource: any, options: any, extra: any): Promise<any>;
}
export class XMLHttpRequestMetric extends Metric {
    constructor(_options: any);
    channel: import("./channels.js").Channel;
    patched: {
        open: {
            (method: string, url: string | URL): void;
            (method: string, url: string | URL, async: boolean, username?: string | null, password?: string | null): void;
        };
        send: (body?: Document | XMLHttpRequestBodyInit | null) => void;
    };
}
export class WorkerMetric extends Metric {
    constructor(_options: any);
    GlobalWorker: {
        new (scriptURL: string | URL, options?: WorkerOptions): Worker;
        prototype: Worker;
    } | {
        new (): {};
    };
    channel: import("./channels.js").Channel;
    Worker: {
        new (url: any, options: any, ...args: any[]): {};
    };
}
export const metrics: {
    requestAnimationFrame: RequestAnimationFrameMetric;
    XMLHttpRequest: XMLHttpRequestMetric;
    Worker: WorkerMetric;
    fetch: FetchMetric;
    channel: import("./channels.js").ChannelGroup;
    subscribe(...args: any[]): boolean;
    unsubscribe(...args: any[]): boolean;
    start(which: any): void;
    stop(which: any): void;
};
declare namespace _default {
    export { metrics };
}
export default _default;
export type RequestAnimationFrameMetricOptions = {
    /**
     * Rolling sample capacity, from 1 to 3600.
     */
    sampleSize?: number;
};
import { Metric } from './metric.js';
