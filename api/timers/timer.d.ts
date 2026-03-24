export class Timer extends AsyncResource {
    [x: number]: () => {
        args: any[];
        handle(id: any, destroy: any): void;
    };
    static from(...args: any[]): Timer;
    constructor(type: any, create: any, destroy: any);
    get id(): number;
    init(...args: any[]): this;
    close(): boolean;
    [Symbol.toPrimitive](): number;
    #private;
}
export class Timeout extends Timer {
    constructor();
}
export class Interval extends Timer {
    constructor();
}
export class Immediate extends Timer {
    constructor();
}
declare namespace _default {
    export { Timer };
    export { Immediate };
    export { Timeout };
    export { Interval };
}
export default _default;
import { AsyncResource } from '../async/resource.js';
