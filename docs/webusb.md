# WebUSB in Oro Runtime

Oro Runtime (formerly Socket) provides a WebUSB scaffold that exposes a `navigator.usb` API mirroring the browser specification. It bridges to the native libusb backend for enumeration, permission checks, hotplug events, and bulk/control transfers.

This document describes current runtime behavior. Remaining gaps are documented
in [Runtime Limitations](./LEGACY_LIMITATIONS.md).

## Installing navigator.usb

The scaffold initializes automatically; simply call `navigator.usb.getDevices()` or `navigator.usb.requestDevice()` from your renderer. Devices returned include descriptors (vendor/product IDs, interface metadata) and support the usual WebUSB methods like `open`, `selectConfiguration`, `transferIn`, and `transferOut`.

If you need the public module surface directly, import `oro:usb`:

```js
import { installNavigatorUSB, NavigatorUSB } from 'oro:usb'

const usb = installNavigatorUSB()
console.log(usb instanceof NavigatorUSB)
```

## Platform support

The native WebUSB backend currently targets desktop platforms (macOS, Windows, Linux) where libusb integrations are available. Mobile builds ship the JS scaffold for API consistency, but the runtime service is disabled on iOS. Android builds expose device enumeration and permission flows through the foreground USB service (which requires `POST_NOTIFICATIONS` on Android 13+); data transfer APIs (`open`, `transferIn`, etc.) still return `NotSupportedError` until the remaining bindings land.

## Chooser Flow

When more than one device matches `requestDevice`, the scaffold fires a `usb.chooserequest` event whose detail object exposes the candidates plus `select(device)` and `cancel()` helpers.

```js
window.addEventListener('usb.chooserequest', (event) => {
  const { devices, select, cancel } = event.detail
  renderChooser(devices, select, cancel)
})
```

If no listener handles it, a default overlay is shown. The overlay honors `prefers-color-scheme` but you can override it entirely by handling the event yourself.

## Hotplug Events

The native backend emits `usb.deviceconnect` and `usb.devicedisconnect`; the scaffold transforms them into `navigator.usb` events:

```js
navigator.usb.addEventListener('connect', ({ device }) => {
  console.log('USB connected:', device.deviceId)
})
```

Existing devices in the cache update in place when descriptors change.

## Persistent grants

When a device is authorized, its identifier is stored in the runtime state database. On the next launch, the backend restores the grant so `navigator.usb.getDevices()` immediately returns previously-approved devices and `requestDevice()` can skip the chooser when only one persisted match exists.

## Permission queries

You can check the runtime's USB permission state without prompting:

```js
const status = await navigator.permissions.query({ name: 'usb' })
if (status.state === 'denied') {
  // surface your own UI before calling navigator.usb.requestDevice()
}
```

## Transfers

Control/Bulk OUT flows use binary IPC; IN transfers return `USBInTransferResult` with `DataView` payloads. `clearHalt` requires an explicit endpoint direction.

## Testing

See `test/src/usb/web-usb.test.js` for a minimal integration test demonstrating mocked IPC and chooser resolution.
