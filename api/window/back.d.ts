/**
 * @typedef {object} BackRequest
 * @property {number} deadlineMs Native fallback deadline, 750 milliseconds.
 */
/**
 * @typedef {object} BackHandlerOptions
 * @property {AbortSignal} [signal] Removes the handler when aborted.
 */
/**
 * Installs one handler for this document. Return true to consume Back, or false
 * to use native navigation. Promise results are accepted within 750 ms. Throws,
 * missing replies and stalled renderers use native navigation. Navigation or
 * renderer replacement invalidates registration; install again in the new page.
 * Application code decides modal, drawer and router priority. Gesture progress
 * remains available through the existing backgesture event.
 * @param {(request: BackRequest) => boolean|Promise<boolean>} handler
 * @param {BackHandlerOptions} [options]
 * @returns {Promise<() => Promise<void>>} Idempotent handler removal.
 */
export function setHandler(handler: (request: BackRequest) => boolean | Promise<boolean>, options?: BackHandlerOptions): Promise<() => Promise<void>>;
declare namespace _default {
    export { setHandler };
}
export default _default;
export type BackRequest = {
    /**
     * Native fallback deadline, 750 milliseconds.
     */
    deadlineMs: number;
};
export type BackHandlerOptions = {
    /**
     * Removes the handler when aborted.
     */
    signal?: AbortSignal;
};
