/**
 * A reference to the opened environment. This value is an instance of an
 * `Environment` if the scope is a ServiceWorker scope.
 * @type {Environment|null}
 */
export const env: Environment | null;
export default SharedWorker;
import { SharedWorker } from './shared-worker/index.js';
export { Environment, SharedWorker };
