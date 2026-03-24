import {
  installNavigatorUSB as installNavigatorUSBInternal,
  NavigatorUSB,
  USBDevice,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
} from './internal/usb-web.js'

/**
 * Install the WebUSB scaffold on `navigator.usb` for the current global scope.
 *
 * This is the public `oro:usb` entrypoint for the downstream WebUSB surface.
 * It returns the singleton `NavigatorUSB` instance when `navigator` is
 * available, and `undefined` in non-window contexts.
 *
 * @returns {NavigatorUSB|undefined}
 */
export function installNavigatorUSB () {
  return installNavigatorUSBInternal()
}

export {
  NavigatorUSB,
  USBDevice,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
}

export default {
  installNavigatorUSB,
  NavigatorUSB,
  USBDevice,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
}
