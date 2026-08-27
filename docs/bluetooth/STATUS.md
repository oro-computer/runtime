# Web Bluetooth – Project Status

This document tracks the current end-to-end status of Web Bluetooth in Oro Runtime. It is updated as functionality lands across platforms.

Last updated: 2025-09-17

## Summary

- API Surface (JS): Implemented and spec-shaped at `navigator.bluetooth` with strict `requestDevice` validation. Notification events are delivered to the exact `BluetoothRemoteGATTCharacteristic` instance as a `DataView` value.
- IPC + Core Service: New `web_bluetooth` core service with a platform Backend interface. Full IPC routes are in place, including choose/cancel flows and a notification observer bridge.
- Chooser UX: A default in-page chooser overlay is available to keep `requestDevice` interactive and spec-compliant (user gesture). Apps can override this later.

## Platform Status

### Apple (macOS/iOS) – CoreBluetooth

- Availability: PoweredOn + `[permissions] allow_bluetooth` gate. ✓
- requestDevice: Filters (services/name/namePrefix) + chooser events; resolves on selection/cancel/timeout. ✓
- GATT
  - Connect/Disconnect with delegate lifecycle. ✓
  - Discover primary services + characteristics (cached). ✓
  - readValue → buffered ArrayBuffer path over IPC. ✓
  - writeValue (with response). ✓
  - start/stopNotifications with event delivery to JS characteristic instance. ✓
- Safety/Perf: ARC + RAII, CoreBluetooth on main queue; marshal to loop thread. ✓

Source highlights:

- `src/runtime/core/services/web_bluetooth/apple.mm`
- `src/runtime/core/services/web_bluetooth.{hh,cc}`

### Android – Bluetooth LE GATT

- Availability: `BluetoothAdapter.isEnabled()` + config gate. ✓
- Scan + chooser plumbing: Kotlin BLE scanner + JNI -> devicefound/chooserequest events; `requestDevice` resolves on choose/cancel. ✓
- GATT (connect/discovery/read/write/notify): Pending (in progress next).

Source highlights:

- `src/runtime/bluetooth/ble.kt` (BLE manager: startScan/stopScan)
- `src/runtime/core/services/web_bluetooth/android_jni.cc` (devicefound bridge)
- `src/runtime/core/services/web_bluetooth/android.cc` (backend routes)

### Windows – WinRT

- Availability: placeholder (returns false). Pending backend.
- Full GATT ops + chooser: Pending.

Source highlight:

- `src/runtime/core/services/web_bluetooth/win.cc` (stub backend)

### Linux – BlueZ (D‑Bus)

- Availability: placeholder (returns false). Pending backend.
- Full GATT ops + chooser: Pending.

Source highlight:

- `src/runtime/core/services/web_bluetooth/linux.cc` (stub backend)

## JS API + Chooser

- `api/internal/bluetooth-web.js` installs `navigator.bluetooth` and defines spec-shaped classes (`Bluetooth`, `BluetoothDevice`, `BluetoothRemoteGATTServer/Service/Characteristic`).
- `requestDevice` options are validated per spec:
  - `filters` vs `acceptAllDevices` exclusivity
  - Non-empty `filters` required if not accepting all devices
  - Each filter contains services/name/namePrefix/manufacturerData
  - `optionalServices` entries are UUID strings/numbers
- Chooser overlay listens to:
  - `bluetooth.chooserequest` – show overlay
  - `bluetooth.devicefound` – append device row
  - `bluetooth.chooseDevice` – finalize selection via IPC
  - `bluetooth.cancelRequest` – cancel/close overlay

## Build Integration

- Apple: Objective‑C++ backend compiled (`.mm`) via `bin/build-runtime-library.sh` glob inclusion.
- Android: Kotlin sources under `src/` included by Gradle sourceSets; no extra wiring required. JNI bridge C++ file is compiled by existing core globs.
- Entitlements/Plist (Apple): Restored CoreBluetooth frameworks/usage strings; background modes and capabilities gated by `[permissions] allow_bluetooth`.

## Known Limitations (Current)

- Android: GATT connect/discovery/read/write/notify pending. Permissions (API 31+) not yet wired.
- Windows/Linux: Backends are stubs pending full implementations.
- Chooser: Default overlay is minimal; product teams may want to style/override it.

## Immediate Next Steps

1. Android GATT end-to-end
   - Permissions (API 31+), ScanFilters per spec
   - Connect/discover (cache), MTU negotiation
   - readValue/writeValue (with/without response)
   - start/stopNotifications + disconnection events
   - RAII for JNI refs; marshaling to loop; notification backpressure
2. Windows (WinRT) backend
3. Linux (BlueZ) backend

## Validation

- Desktop/iOS: `npm test` / `npm run test:ios-simulator`
- Android: `npm run test:android`
- Manual:
  - Verify `navigator.bluetooth.getAvailability()`
  - Run `requestDevice()` with filtered UUIDs; confirm chooser and selection
  - Connect, list services/characteristics, read/write, notifications (Apple)
