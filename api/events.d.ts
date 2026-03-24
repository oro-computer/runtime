export const Event: {
    new (type: string, eventInitDict?: EventInit): Event;
    prototype: Event;
    readonly NONE: 0;
    readonly CAPTURING_PHASE: 1;
    readonly AT_TARGET: 2;
    readonly BUBBLING_PHASE: 3;
} | {
    new (): {};
};
export const EventTarget: {
    new (): {};
};
export const CustomEvent: {
    new <T>(type: string, eventInitDict?: CustomEventInit<T>): CustomEvent<T>;
    prototype: CustomEvent;
} | {
    new (type: any, options: any): {
        "__#private@#detail": any;
        get detail(): any;
    };
};
export const MessageEvent: {
    new <T>(type: string, eventInitDict?: MessageEventInit<T>): MessageEvent<T>;
    prototype: MessageEvent;
} | {
    new (type: any, options: any): {
        "__#private@#detail": any;
        "__#private@#data": any;
        get detail(): any;
        get data(): any;
    };
};
export const ErrorEvent: {
    new (type: string, eventInitDict?: ErrorEventInit): ErrorEvent;
    prototype: ErrorEvent;
} | {
    new (type: any, options: any): {
        "__#private@#detail": any;
        "__#private@#error": any;
        get detail(): any;
        get error(): any;
    };
};
export default EventEmitter;
export function EventEmitter(): void;
export class EventEmitter {
    _events: any;
    _contexts: any;
    _eventsCount: number;
    _maxListeners: number;
    setMaxListeners(n: any): this;
    getMaxListeners(): any;
    emit(type: any, ...args: any[]): boolean;
    addListener(type: any, listener: any): any;
    on(arg0: any, arg1: any): any;
    prependListener(type: any, listener: any): any;
    once(type: any, listener: any): this;
    prependOnceListener(type: any, listener: any): this;
    removeListener(type: any, listener: any): this;
    off(type: any, listener: any): this;
    removeAllListeners(type: any, ...args: any[]): this;
    listeners(type: any): any[];
    rawListeners(type: any): any[];
    listenerCount(type: any): any;
    eventNames(): (string | symbol)[];
}
export namespace EventEmitter {
    export { EventEmitter };
    export let defaultMaxListeners: number;
    export function init(): void;
    export function listenerCount(emitter: any, type: any): any;
    export { once };
}
export function once(emitter: any, name: any): Promise<any>;
