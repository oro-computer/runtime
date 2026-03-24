/**
 * @ignore
 * @param {Request}
 * @param {object} env
 * @param {import('../service-worker/context.js').Context} ctx
 * @return {Promise<Response|null>}
 */
export function onRequest(request: any, env: object, ctx: import("../service-worker/context.js").Context): Promise<Response | null>;
/**
 * Handles incoming 'npm://<module_name>/<pathspec...>' requests.
 * @param {Request} request
 * @param {object} env
 * @param {import('../service-worker/context.js').Context} ctx
 * @return {Response?}
 */
export default function _default(request: Request, env: object, ctx: import("../service-worker/context.js").Context): Response | null;
