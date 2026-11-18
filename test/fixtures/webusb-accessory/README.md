# Physical WebUSB bulk echo

A Linux Oro app sends USB bulk transfers to a Pixel connected by a USB data cable. The Android app echoes each transfer over the same cable. The phone needs Android 16 (API 36) or later and Android Open Accessory support. Linux needs read/write access to the phone's USB device node in both ordinary and accessory modes. [Android accessory protocol](https://source.android.com/docs/core/interaction/accessories/aoa)

1. Build the Android peer with Android SDK 37, JDK 17 or later and Gradle 9.5. From the `android` directory run `gradle assembleDebug`, then install the APK on the intended Pixel with `adb -s <device> install -r build/outputs/apk/debug/OroUsbEcho-debug.apk`.
2. Unlock the Pixel and connect it to Linux with a USB data cable. From the `host` directory run `oroc build -r` in a graphical desktop terminal. Select **Select Pixel** and choose the phone in the native picker.
3. The phone reconnects in Android accessory mode. Accept **Oro USB Echo** on the Pixel. When it displays **Ready for bulk echo**, select **Run bulk echo** on Linux and choose the accessory in the picker.
4. The host reports the interface, bulk endpoint numbers and maximum packet sizes. It writes 17, 31, 257 and 1,025 bytes, requests seven extra bytes on each read and checks the actual length and every byte. A pass also requires releasing the interface and closing the device. Unplug the cable to leave accessory mode.

Each setup generates a new session identifier that the two apps exchange during Android accessory negotiation. The Android app echoes payloads that start with this identifier. The host uses only the accessory's bulk interface. Android acts as the USB peripheral during this check.

## Physical bulk stall recovery

After a successful echo, leave both apps open and the Pixel connected. In a second Linux terminal, build the fixture helper against libusb 1.0 and pass the USB serial of that Pixel. This helper claims accessory interface 0, sets the halt on its bulk IN endpoint, checks its status and releases the interface.

```sh
cc -std=c11 -Wall -Wextra -o pixel-halt pixel-halt.c $(pkg-config --cflags --libs libusb-1.0)
./pixel-halt arm "$PIXEL_USB_SERIAL"
```

Click **Check prepared bulk IN stall** in the host app. A pass requires a set halt bit, a bulk IN result of `stall`, a cleared bit after `clearHalt()`, and four matching echoes. To disarm an unused check, run `./pixel-halt clear "$PIXEL_USB_SERIAL"`.

If **Select Pixel** succeeds and **Run bulk echo** reports `LIBUSB_ERROR_ACCESS`, leave the phone connected. Run `lsusb -d 18d1:` on Linux. Accessory mode creates a new USB device node with product ID `2d00` or `2d01`. Check the desktop user's read/write access to that node. If using a per-device udev rule, match the Pixel's serial and the accessory-mode product ID, reload the rule and trigger it for the attached device. Retry **Run bulk echo** in the same window to reuse the negotiated session. [Linux USB permissions](../../../docs/webusb.md#platform-qualification)
