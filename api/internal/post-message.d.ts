/**
 * Sends a window message from the current realm, preserving its source window.
 * Calling the target's JavaScript wrapper changes the incumbent realm in Chromium.
 * @ignore
 * @param {Window} target
 * @param {any} message
 * @param {string} targetOrigin
 * @returns {void}
 */
export function postWindowMessage(target: Window, message: any, targetOrigin: string): void;
declare const _default: any;
export default _default;
