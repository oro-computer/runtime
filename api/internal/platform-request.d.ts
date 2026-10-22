/**
 * Runs a bounded document-owned Android operation and cancels its native work
 * when the caller aborts. Native deadlines also release callers that disappear.
 * @param {string} feature
 * @param {string} action
 * @param {object} options
 * @param {AbortSignal} [signal]
 * @returns {Promise<any>}
 */
export function platformRequest(feature: string, action: string, options: object, signal?: AbortSignal): Promise<any>;
export default platformRequest;
