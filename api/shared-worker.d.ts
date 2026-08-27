/**
 * A reference to the opened environment. This value is an instance of an
 * `Environment` if the scope is a ServiceWorker scope.
 * @type {import('./shared-worker/env.js').Environment|null}
 */
export const env: any | null;
export { SharedWorker };
export default SharedWorker;
import { SharedWorker } from './shared-worker/index.js';
