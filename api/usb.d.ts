/** @typedef {import('./internal/usb-web.js').USBDeviceFilter} USBDeviceFilter */
/** @typedef {import('./internal/usb-web.js').USBDeviceRequestOptions} USBDeviceRequestOptions */
/** @typedef {import('./internal/usb-web.js').USBControlTransferParameters} USBControlTransferParameters */
/** @typedef {import('./internal/usb-web.js').USBTransferStatus} USBTransferStatus */
/**
 * Install the WebUSB API for a supported secure application window or worker.
 * Only windows can request a new device grant.
 * @returns {NavigatorUSB|undefined}
 */
export function installNavigatorUSB(): NavigatorUSB | undefined;
declare namespace _default {
    export { installNavigatorUSB };
    export { NavigatorUSB };
    export { USBDevice };
    export { USBConfiguration };
    export { USBInterface };
    export { USBAlternateInterface };
    export { USBEndpoint };
    export { USBIsochronousInTransferResult };
    export { USBIsochronousOutTransferResult };
    export { USBIsochronousInTransferPacket };
    export { USBIsochronousOutTransferPacket };
    export { USBInTransferResult };
    export { USBOutTransferResult };
    export { USBConnectionEvent };
}
export default _default;
export type USBDeviceFilter = import("./internal/usb-web.js").USBDeviceFilter;
export type USBDeviceRequestOptions = import("./internal/usb-web.js").USBDeviceRequestOptions;
export type USBControlTransferParameters = import("./internal/usb-web.js").USBControlTransferParameters;
export type USBTransferStatus = import("./internal/usb-web.js").USBTransferStatus;
import { NavigatorUSB } from './internal/usb-web.js';
import { USBDevice } from './internal/usb-web.js';
import { USBConfiguration } from './internal/usb-web.js';
import { USBInterface } from './internal/usb-web.js';
import { USBAlternateInterface } from './internal/usb-web.js';
import { USBEndpoint } from './internal/usb-web.js';
import { USBIsochronousInTransferResult } from './internal/usb-web.js';
import { USBIsochronousOutTransferResult } from './internal/usb-web.js';
import { USBIsochronousInTransferPacket } from './internal/usb-web.js';
import { USBIsochronousOutTransferPacket } from './internal/usb-web.js';
import { USBInTransferResult } from './internal/usb-web.js';
import { USBOutTransferResult } from './internal/usb-web.js';
import { USBConnectionEvent } from './internal/usb-web.js';
export { NavigatorUSB, USBDevice, USBConfiguration, USBInterface, USBAlternateInterface, USBEndpoint, USBIsochronousInTransferResult, USBIsochronousOutTransferResult, USBIsochronousInTransferPacket, USBIsochronousOutTransferPacket, USBInTransferResult, USBOutTransferResult, USBConnectionEvent };
