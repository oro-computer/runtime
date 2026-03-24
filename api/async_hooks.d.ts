export default exports;
import { AsyncLocalStorage } from './async/storage.js';
import { AsyncResource } from './async/resource.js';
import { executionAsyncResource } from './async/hooks.js';
import { executionAsyncId } from './async/hooks.js';
import { triggerAsyncId } from './async/hooks.js';
import { createHook } from './async/hooks.js';
import * as exports from './async_hooks.js';
export { AsyncLocalStorage, AsyncResource, executionAsyncResource, executionAsyncId, triggerAsyncId, createHook };
