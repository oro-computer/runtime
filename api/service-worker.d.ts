/**
 * A reference to the opened environment. This value is an instance of an
 * `Environment` if the scope is a ServiceWorker scope.
 * @type {Environment|null}
 */
export const env: Environment | null;
declare namespace _default {
    export { ExtendableEvent };
    export { FetchEvent };
    export { Environment };
    export { Context };
    export { env };
}
export default _default;
import { Environment } from './service-worker/env.js';
import { ExtendableEvent } from './service-worker/events.js';
import { FetchEvent } from './service-worker/events.js';
import { Context } from './service-worker/context.js';
export { ExtendableEvent, FetchEvent, Environment, Context };
