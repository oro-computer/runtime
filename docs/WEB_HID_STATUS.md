# WebHID Runtime Status

_Last updated: September 24, 2025_

This document tracks the native runtime implementation status for the WebHID API. Use it as a checklist when extending platform coverage and as a hand-off for integration testing with real devices.

## Overview

The JavaScript scaffold (`navigator.hid`) and core service plumbing are in place. The runtime currently uses:

- A libusb-based backend on Linux.
- An IOHIDManager backend on macOS.
- A SetupAPI/HidD backend on Windows.

Additional work (Android, hardware validation, entitlements) is still required for full parity.

On iOS/iPadOS, Apple does not expose public APIs for generic HID access. The runtime therefore disables WebHID on those platforms and `navigator.hid` calls reject with `NotSupportedError`.

## Platform Status

| Platform                | Enumeration | Request / Chooser | Open / Close | Input Reports | Feature / Output Reports | Notes                                                                                                                     |
| ----------------------- | ----------- | ----------------- | ------------ | ------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------- |
| Linux (libusb)          | ✅          | ✅                | ✅           | ✅            | ✅                       | Uses libusb interrupt transfers. Requires udev rules for permissioning.                                                   |
| macOS (IOHIDManager)    | ✅          | ✅                | ✅           | ✅            | ✅                       | Backend built on IOHIDManager; pending validation on real devices and entitlement audit.                                  |
| Windows (SetupAPI/HidD) | ✅          | ✅                | ✅           | ✅            | ✅                       | Backend implemented via SetupAPI/HidD; pending hardware validation and packaging checks.                                  |
| Android                 | ⏳          | ⏳                | ⏳           | ⏳            | ⏳                       | Use `android.hardware.usb.UsbManager` with asynchronous bulk endpoints. Coordinate with permission prompts.               |
| iOS / iPadOS            | ❌          | ❌                | ❌           | ❌            | ❌                       | Unsupported: Apple does not expose public HID APIs; apps must fall back to Web Bluetooth or platform-specific frameworks. |

Legend: ✅ Ready │ ⏳ Planned │ ❌ Missing / blocked

## Implementation Roadmap

1. **macOS (IOHIDManager):** _Initial backend complete_
   - Follow-up: validate IOHIDElement-derived report metadata with real hardware and ensure multi-collection devices map correctly.
   - Confirm entitlement requirements for distribution builds and add build-system toggles if needed.

2. **Windows (Win32 HID):** _Backend implemented_
   - Follow-up: validate overlapped reads/report parsing with real HID hardware and document driver/permission requirements.

3. **Android (UsbManager):**
   - Kotlin service in `src/runtime/hid/android/` using `UsbDeviceConnection`.
   - Register broadcast receiver for `ACTION_USB_DEVICE_DETACHED`.
   - Forward input reports via `InputStream` reads on interrupt endpoints.

4. **iOS:**
   - Investigate `CoreHID` private APIs vs. accessory support; determine viability.
   - Prototype using `IOHIDManager` on macOS Catalyst as an intermediate step.

5. **Common tasks:**
   - Gate runtime permissions with `[permissions] allow_hid`.
   - Extend chooser UI to reflect device names per platform.
   - Add telemetry hooks for debugging (optional).

## Integration Testing Plan

1. **Device Matrix:**
   - Keyboard (report ID 0) – verifies zero-report handling.
   - Gamepad (multiple reports) – validates report ID routing.
   - Vendor-specific device – exercises feature reports.

2. **Test Harness:**
   - Add Oro desktop integration tests to open a HID device, listen for input, send a feature report, and assert payloads (legacy Socket suites remain available during the rename).
   - For Android, leverage instrumentation tests that dispatch synthetic USB intents.

3. **Manual Procedures:**
   - Document per-platform steps for granting OS-level permissions (e.g., `udev` rule snippets, Windows driver requirements).
   - Create troubleshooting guide (common errors, log locations).

4. **CI Considerations:**
   - Simulators lack HID hardware, so smoke tests should mock IPC responses.
   - Real-device regression tests can run on dedicated lab machines using the Oro test runner (legacy Socket mode sticks around for older pipelines).

## Automated Coverage Strategy

- **JavaScript scaffold:** continue expanding `test/src/hid/web-hid.test.js` with IPC-mocked scenarios that exercise report-ID edge cases (e.g., zero vs. non-zero report IDs, feature report truncation). The Oro test runner executes these in CI today (and still accepts `socket` invocations).
- **Backend-level shims:** introduce lightweight native unit tests per backend that feed synthetic descriptors into `parseReportDescriptor` (libusb) and IOHID/HIDP helpers. We can build these as `npm run test:runtime-core` fixtures compiled on each desktop target.
- **Descriptor fixtures:** define JSON fixtures under `test/fixtures/hid/` that represent common device classes (keyboard, gamepad, vendor-specific). The native tests and JS scaffold can both import these fixtures to ensure consistent expectations.
- **Mock transport hook:** expose a compile-time flag that swaps the platform HID APIs with a deterministic fake (e.g., a fake `libusb_device_handle`). This enables continuous testing without hardware and lets us assert on report payloads and event dispatch ordering.

## Open Questions

- How should we persist user grants across sessions? (Currently tied to runtime permissions only.)
- Should we expose additional events (e.g., `inputreporterror`) for parity with Chromium?
- Coordination with WebUSB / Bluetooth permissions to avoid overlapping prompts.

Please keep this document updated as platform backends move forward.
