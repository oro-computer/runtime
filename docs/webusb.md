# WebUSB in Oro Runtime

An application can discover, open and exchange data with a USB peripheral through `navigator.usb`. Oro supplies the picker, permission handling and transport. The [USB example](../examples/usb/main.js) uses only JavaScript. The same interface is available through `oro:usb`.

## Device selection and permissions

Call `requestDevice()` from a user gesture in the application window. Pass an empty filter array to show all eligible devices, or restrict the picker by vendor, product, serial number or USB class. A product filter requires a vendor, a subclass requires a class, and a protocol requires a subclass. Use `exclusionFilters` to remove matches. On Android the OS can withhold the serial number until it grants access, so use a vendor/product filter for first-time selection.

```js
button.addEventListener('click', async () => {
  const device = await navigator.usb.requestDevice({ filters: [] })
  await device.open()
  try {
    const result = await device.controlTransferIn({
      requestType: 'standard', recipient: 'device', request: 6, value: 0x0100, index: 0
    }, 18)
    if (result.status === 'ok') console.log('Descriptor length', result.data.byteLength)
  } finally {
    await device.close()
  }
})
```

The native picker handles new grants, including a single match. When every candidate is already granted, Oro can also dispatch `usb.chooserequest` with `devices`, `select(device)` and `cancel()`. Calling `event.preventDefault()` selects the application chooser. An overlay handles the event by default. Final approval takes place in native UI.

`getDevices()` returns connected, granted devices for the application. `navigator.permissions.query({ name: 'usb', filters, exclusionFilters })` reports `prompt` when device selection is available and exposes matching grants in `devices`. Set `allow_usb = false` under `[permissions]` to disable access. Feature-detect `navigator.usb` and `requestDevice` separately for workers and unsupported platforms.

Desktop grants with a readable serial number survive restart and reattachment. The grant identity includes vendor, product and serial. Serialless grants last for the observed attachment. Upgrading from the earlier automatic-grant implementation requires selecting the device again. Android requires both application selection and OS permission for the attachment. Call `device.forget()` to remove Oro's grant. A document owns its open handle and interface claims. Navigating or closing the window releases them.

## Discovery and transfers

Read `device.configurations`, then the configuration's `interfaces`, each interface's `alternates`, and each alternate's `endpoints`. Select a configuration, claim the interface, and select the alternate before transferring. Endpoint numbers are direction-independent. Use the endpoint's advertised direction and type. Oro rejects claims on protected interface classes and excludes blocklisted devices from discovery. The device blocklist comes from the [WebUSB specification](https://usb.spec.whatwg.org/blocklist.txt).

Control, bulk, interrupt and isochronous operations are implemented on desktop and Android. Control payloads are limited to 65,535 bytes. The native data-transfer limit is 16 MiB, with up to 1,024 packets for an isochronous call. The Android bridge can impose a smaller binary message limit. Inspect the returned status, byte count or per-packet result. An IN transfer returns a `DataView`. Call `clearHalt(direction, endpointNumber)` for a stalled endpoint.

Listen for `connect` and `disconnect` on `navigator.usb` to observe authorized attachments. The backend samples the device list at one-second intervals while the service is active. An unplug and replug wholly between two observations can be missed.

## Platform qualification

| Platform | Implementation and acceptance requirements |
| --- | --- |
| Linux | libusb and GTK picker. The OS account needs access to the peripheral. |
| macOS | libusb and AppKit picker. Exercise a device available to the signed application. |
| Windows | libusb and native picker. Exercise a device/interface with a compatible USB driver. |
| Android | USB host mode, native device picker and `UsbManager` permission. The runtime opens the OS-authorized file descriptor through libusb. This workflow requires no application-native code, foreground service or notification permission. |
| iOS / iPadOS | Oro does not expose generic WebUSB. |

On Linux, choosing a device in the GTK picker grants the app permission to request it. The desktop user also needs read/write access to the corresponding `/dev/bus/usb` node for libusb to open it, even for a descriptor read. If `open()` reports `LIBUSB_ERROR_ACCESS`, use `lsusb` to identify the bus and device number and inspect that node with `getfacl`. On a systemd desktop, an administrator can use a udev rule matching the USB device's vendor, product and serial number with `TAG+="uaccess"` before the seat's access rules run. The permission then follows the drive when its device number changes on reconnect. [udev rule syntax and locations](https://www.freedesktop.org/software/systemd/man/latest/udev.html)

On November 17, 2026, a JavaScript-only app exercised the GTK picker, renderer IPC and native Linux transport with a simulated libusb peripheral. Canceling left the grant list empty. After selection, the app read the device descriptor, checked bulk and interrupt echoes with short reads, and verified isochronous packet lengths and offsets. The serial grant survived an app restart. After `forget()` the grant was absent on a further restart. Simulated removal during a pending read rejected with `NotFoundError`. The app observed `disconnect` followed by `connect`, rejected reopening the old device object, and read the descriptor from the new one. When a configuration change failed after releasing interfaces, the renderer reported `NetworkError` and showed which interfaces stayed claimed. A separate native fixture exercised endpoint stalls and `clearHalt()`.

On a Pixel 11 Pro XL running Android 17 (API 37), a JavaScript-only app selected a mounted USB DISK 3.2 (`13fe:6500`). Canceling the Android permission prompt rejected with `NotFoundError`, left `getDevices()` empty and kept the permission state at `prompt`. A fresh approval completed after the prompt paused and resumed the app. The app read the 18-byte descriptor and verified both IDs. Claiming the mass-storage interface rejected with `SecurityError`. A second window's `open()` rejected with `InvalidStateError` while the first owned the drive. After the first window navigated, the second opened it and read the descriptor. After `close()` and `forget()`, enumeration was empty and the volume remained mounted.

On November 18, 2026, the Pixel app issued one descriptor read at a time. After 721 successful reads, unplugging the drive interrupted the pending read with `NotFoundError` (`LIBUSB_ERROR_NO_DEVICE`). The app then received `disconnect` for the original device. Its `opened` flag was false, reopening it rejected with `NotFoundError`, and `getDevices()` returned no matching grants. After reconnection, Android requested fresh permission. The app enumerated a new device object, opened it and verified the 18-byte descriptor and both USB IDs again. After `close()` and `forget()`, the grant list was empty and Android still reported the volume mounted.

On November 18, 2026, the Linux GTK app selected the same serial-numbered drive. Opening it initially returned `LIBUSB_ERROR_ACCESS`. After a udev rule granted the desktop user device-node access, the app verified the 18-byte descriptor and both USB IDs. An early repeat read failed validation without recording the returned status or bytes, so its cause is unknown.

In the completed Linux run, the app reused the saved grant, verified the descriptor and made 19 successful repeat reads. On unplug, the next read rejected with `NotFoundError` (`LIBUSB_ERROR_NO_DEVICE`). The app received `disconnect`, and reopening the old device object rejected with `NotFoundError`. On reconnect, the app received `connect` and read the descriptor from a new device object. After an app restart, it enumerated and opened the drive using the saved grant. After `forget()` and another restart, it enumerated zero matching grants.

In a follow-up Linux run, the Phison drive and the Pixel in ordinary USB mode each returned 256 consecutive device descriptors that matched byte for byte. A request for a nonexistent configuration descriptor timed out on the drive and returned `stall` on the Pixel. On both devices, the next valid descriptor matched the original 18 bytes.

On November 18, 2026, a JavaScript-only Oro app on Linux completed four physical bulk round trips with the Pixel 11 Pro XL in Android accessory mode (`18d1:2d01`). Opening the accessory initially returned `LIBUSB_ERROR_ACCESS`. After the desktop user gained access to the new USB device node, the app claimed interface 0 in configuration 1, alternate 0. Bulk OUT endpoint `0x01` and bulk IN endpoint `0x81` each advertised a 512-byte maximum packet size. The Pixel logged echoes of 17, 31, 257 and 1,025 bytes. For each echo, Linux requested seven extra bytes on IN and verified the returned length and every byte. The app then released the interface and closed the device. [Reproduce the bulk echo with a Pixel](../test/fixtures/webusb-accessory/README.md)

In a second physical run, the Linux fixture helper halted the Pixel's bulk IN endpoint (`0x81`). The Oro app read the set halt bit, received `stall` from `transferIn()`, called `clearHalt('in', 1)`, and read back a cleared halt bit. The Pixel then echoed 17, 31, 257 and 1,025 bytes. Linux verified every byte on all four short reads, released interface 0 and closed the device. [Reproduce physical bulk stall recovery](../test/fixtures/webusb-accessory/README.md#physical-bulk-stall-recovery)

On November 18, 2026, the Oro app also ran in a QEMU Linux guest (kernel `6.16.3-76061603-generic`) against a USB/IP peripheral attached to the kernel's `vhci_hcd` controller. From the GTK picker, the app opened the device, claimed interface 0 and selected alternate 1. Bulk and interrupt IN each returned 23 matching bytes on a 32-byte request. A deliberately halted bulk IN endpoint reported `stall`. After `clearHalt()`, another echo matched. Isochronous OUT and IN matched three packets of 11, 16 and 7 bytes, and the short IN packets retained the requested buffer offsets of 0, 16 and 32. The app released the interface and closed the device. This run exercised the Linux kernel, libusb and Oro with a virtual peripheral. [Reproduce the Linux endpoint checks](../test/fixtures/webusb-endpoints/README.md)

In a separate run, the virtual peripheral disconnected after the kernel submitted an isochronous IN request. The app initially reported `NetworkError`. With libusb's device-removal status propagated by the native transport, the same pending read rejected with `NotFoundError`.

A repeated-read check against a virtual Pixel reproduced an empty IPC response on the 24th device descriptor. The Linux handler now leaves the response stream open for WebKit to finish reading, and the renderer reports an IPC transport failure as `NetworkError`. With both changes, 2,048 consecutive 18-byte descriptors matched. The absent configuration descriptor returned `stall`, and the next valid read matched.

Linux physical interrupt and isochronous loopback checks remain open. They require a peripheral with those endpoint pairs. Physical bulk loopback with Android as the host and native macOS and Windows hardware runs also remain open.

For a hardware acceptance run, open **USB** in the examples application on each target. Record the OS, device, configuration, alternate, endpoint type and observed results.

1. Cancel first-time selection and confirm the list stays empty. Approve the device, open it and select **Read descriptor and list endpoints**. Check that the read returns the 18-byte device descriptor.
2. With a dedicated vendor-specific echo fixture, choose the loopback action for its bulk or interrupt endpoint pair. The example sends a known payload, checks the returned bytes and releases the interface. Exercise isochronous packet lengths, short packets and stalls separately with a suitable fixture.
3. Open the device in a second window while the first owns it. Confirm the second open fails, then close or navigate the first window and retry.
4. Unplug and reconnect the peripheral. Check event ordering, the lifetime of the grant, `forget()`, and desktop relaunch with a serial-numbered device. On Android, check that a request resolves after the OS prompt pauses and resumes the app. Also exercise denial and detachment during a pending transfer.

The [renderer tests](../test/src/usb/web-usb.test.js) cover public API behavior with mocked IPC. The [native tests](../test/src/runtime-core/usb.cc) exercise IPC message parsing and binary control payloads with an in-memory device. They also restart the desktop backend to check grant persistence and revocation. Use the hardware run above to verify OS permissions, drivers and physical transfers on each target.
