/**
 * @param {string} name
 * @return {import('./channels.js').Channel}
 */
export function channel(name: string): import("./channels.js").Channel;
export default exports;
import * as exports from './index.js';
import channels from './channels.js';
import window from './window.js';
import runtime from './runtime.js';
export { channels, window, runtime };
