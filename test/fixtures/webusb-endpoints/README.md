# WebUSB endpoint checks

The JavaScript host in `host` exercises Oro's USB picker and native transfer APIs. For a repeatable Linux run, `vhci.py` supplies a full-speed USB peripheral to the guest kernel over a local socket pair. The guest uses its normal USB device nodes and libusb.

## Linux virtual peripheral

1. In a Linux VM with the Oro desktop SDK, Python 3, and the kernel's USB/IP virtual host controller module, run `sudo modprobe vhci_hcd`.
2. From this fixture directory, run `sudo python3 vhci.py` and leave it running. Use `lsusb -d cafe:4030` to locate the new device. The desktop account needs read/write permission on its device node. For a single attachment, an administrator can grant an ACL on that specific node. [Linux USB permissions](../../../docs/webusb.md#platform-qualification)
3. Run `oroc build -r` in `host` as the desktop user. Click **Test endpoint fixture** and choose **WebUSB endpoint fixture**. A pass requires matching bulk, interrupt and isochronous payloads, recovery after a bulk IN stall, interface release and device close.
4. With the fixture still connected, click **Test virtual disconnect**. The peripheral disconnects when it receives the submitted isochronous IN request. The app must report `NotFoundError` for that read. Restart `vhci.py` to attach a fresh instance. Ctrl+C also disconnects a running fixture.

The fixture uses serial `ORO-ENDPOINT-1`, configuration 1 and vendor-specific interface 0 (class `ff`, subclass `53`, protocol `01`). Alternate 0 has no endpoints. Alternate 1 has one IN/OUT pair each for bulk (1), interrupt (2) and isochronous (3). Vendor request `0x40` directed to interface 0 sets the bulk IN halt. Each isochronous IN packet returns the bytes of the corresponding OUT packet. The host checks both packet lengths and offsets after short reads.

## Physical endpoint fixture

The firmware in `pico` implements the same interface on a dedicated Raspberry Pi Pico (RP2040). It buffers each OUT packet until its matching IN completes. Build with the Pico SDK 2.2.0 including its TinyUSB submodule, an Arm embedded GCC toolchain and CMake. Set `PICO_SDK_PATH` to the SDK directory.

```sh
cmake -S pico -B build/pico -DPICO_BOARD=pico
cmake --build build/pico
```

To provision a test board, copy `build/pico/oro_usb_endpoints.uf2` to its BOOTSEL volume. This replaces the firmware on that board. Connect the provisioned board to the Linux computer, run the host app and select **Test endpoint fixture**. For an Android-host test, build and run the same host app on a USB-host-capable Android device, then plug the board into the phone's data port and approve the USB picker. The board returns the bytes in both setups.

The UF2 image was built and exercised in rp2040js 1.4.2. The emulated firmware enumerated, selected and reselected alternate 1, echoed bulk, interrupt and isochronous packets, and reported the halt bit set and cleared. Physical board qualification requires running the host against a cabled board.

## Physical descriptor checks

The same host app can check the Phison USB drive (`13fe:6500`) and a Pixel in its ordinary USB mode (including file transfer, `18d1:4ee2`) or Android accessory mode (`18d1:2d00` or `18d1:2d01`). **Check Pixel descriptors** and **Check drive descriptors** perform 256 read-only device descriptor requests, verify both USB IDs, and compare every response with the first. The app then requests an absent configuration descriptor. It records a returned stall or a libusb timeout separately and requires the next valid device descriptor to match the baseline. On a descriptor mismatch it reports the iteration, status, length and returned bytes. Leave the device connected throughout.
