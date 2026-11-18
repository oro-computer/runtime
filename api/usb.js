import {
  installNavigatorUSB as installNavigatorUSBInternal,
  NavigatorUSB,
  USBDevice,
  USBConfiguration,
  USBInterface,
  USBAlternateInterface,
  USBEndpoint,
  USBIsochronousInTransferResult,
  USBIsochronousOutTransferResult,
  USBIsochronousInTransferPacket,
  USBIsochronousOutTransferPacket,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
} from './internal/usb-web.js'

/** @typedef {import('./internal/usb-web.js').USBDeviceFilter} USBDeviceFilter */
/** @typedef {import('./internal/usb-web.js').USBDeviceRequestOptions} USBDeviceRequestOptions */
/** @typedef {import('./internal/usb-web.js').USBControlTransferParameters} USBControlTransferParameters */
/** @typedef {import('./internal/usb-web.js').USBTransferStatus} USBTransferStatus */

/**
 * Install the WebUSB API for a supported secure application window or worker.
 * Only windows can request a new device grant.
 * @returns {NavigatorUSB|undefined}
 */
export function installNavigatorUSB () {
  return installNavigatorUSBInternal()
}

export {
  NavigatorUSB,
  USBDevice,
  USBConfiguration,
  USBInterface,
  USBAlternateInterface,
  USBEndpoint,
  USBIsochronousInTransferResult,
  USBIsochronousOutTransferResult,
  USBIsochronousInTransferPacket,
  USBIsochronousOutTransferPacket,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
}

export default {
  installNavigatorUSB,
  NavigatorUSB,
  USBDevice,
  USBConfiguration,
  USBInterface,
  USBAlternateInterface,
  USBEndpoint,
  USBIsochronousInTransferResult,
  USBIsochronousOutTransferResult,
  USBIsochronousInTransferPacket,
  USBIsochronousOutTransferPacket,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
}
