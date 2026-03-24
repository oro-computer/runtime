/**
 * Install the WebUSB scaffold on `navigator.usb` for the current global scope.
 *
 * This is the public `oro:usb` entrypoint for the downstream WebUSB surface.
 * It returns the singleton `NavigatorUSB` instance when `navigator` is
 * available, and `undefined` in non-window contexts.
 *
 * @returns {NavigatorUSB|undefined}
 */
export function installNavigatorUSB(): NavigatorUSB | undefined;
declare namespace _default {
    export { installNavigatorUSB };
    export { NavigatorUSB };
    export { USBDevice };
    export { USBInTransferResult };
    export { USBOutTransferResult };
    export { USBConnectionEvent };
}
export default _default;
import { NavigatorUSB } from './internal/usb-web.js';
import { USBDevice } from './internal/usb-web.js';
import { USBInTransferResult } from './internal/usb-web.js';
import { USBOutTransferResult } from './internal/usb-web.js';
import { USBConnectionEvent } from './internal/usb-web.js';
export { NavigatorUSB, USBDevice, USBInTransferResult, USBOutTransferResult, USBConnectionEvent };
