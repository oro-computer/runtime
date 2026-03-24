# Web Bluetooth Runtime Status

This document tracks the current implementation status for Web Bluetooth in the native runtime. Use it as a hand‑off to resume work in a new session.

## Cross-cutting

- `WebBluetooth::parseRequestDeviceOptions` centralises spec validation (acceptAllDevices vs filters, optionalServices, manufacturerData masks) and reuses a single matcher for all backends.
- Router/bridge keep notification observers per-window with ref counts so repeated `startNotifications()` calls do not duplicate events. Observers are dropped automatically when a bridge is destroyed.
- The JS scaffold now de-dupes chooser rows and live-updates RSSI/name while a scan is active.

## Platform snapshot

- **Apple (CoreBluetooth)**
  - Filters run through the shared matcher before presenting devices; advertisement manufacturer data is normalised for matching.
  - Chooser dedupe is driven by peripheral UUIDs. Timeout uses `options.timeoutMs`, `web_bluetooth_timeout_ms`, or defaults to 12s.

- **Android (Kotlin + JNI + GATT)**
  - Scanner forwards advertised service UUIDs; native backend filters locally via the shared matcher and caches name/RSSI/service info for chooser resolution.
  - RequestDevice builds ScanFilters from the union of requested service UUIDs. Notifications, reads, and writes were already implemented.
  - On API 31+, checks `android.permission.BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT` before scanning and triggers a runtime prompt when missing; returns `NotAllowedError` until granted.

- **Windows (classic + dynamic GATT)**
  - Classic radio enumeration still backs `requestDevice`; name/namePrefix filters now use the shared matcher data.
  - BLE GATT plumbing exists (BluetoothAPIs.dll) but discovery is limited to remembered devices; no manufacturer/service filtering yet.

- **Linux (BlueZ / D‑Bus)**
  - Discovery filter + matcher migrated to the shared helpers (services, names, prefixes, manufacturerData masks).
  - `requestDevice` emits chooser events only when the matcher passes; MAC-level dedupe remains in place.
  - GATT connect/discovery/read/write/notify already implemented; timeout/cancel flow unchanged (default 15s).

- **Tests**
  - `test/src/bluetooth/web-bluetooth.test.js` exercises JS option validation (including `servicesMatch`) and chooser cancellation logic. Platform E2E/device tests remain manual for now.

## Configuration quick reference

- `options.timeoutMs` or `[userConfig] web_bluetooth_timeout_ms` (number string) controls discovery timeout (default 15000ms on Linux, 12s on Apple fallback).
- `options.servicesMatch`: `'all'` (default) requires every service listed in a filter; `'any'` allows a match when any service is present. A userConfig override (`web_bluetooth_services_match=all|any`) sets the default mode for requests that omit the option.
- Linux-specific tuning: `web_bluetooth_emit_interval_ms` (default 1000) throttles repeated `devicefound` emissions, `web_bluetooth_emit_rssi_delta` (default 4) sets the RSSI delta required to emit early.

## Next steps / open items

1. Windows: switch requestDevice to WinRT BLE enumeration, honour service/manufacturer filters, and wire notifications to the shared matcher.
2. Android: add MTU negotiation, improve read/write data paths (zero-copy), audit JNI/loop hand-offs, and document the BLE permission flow.
3. Tests: add integration coverage for filter permutations and repeated notification subscriptions; document chooser override hooks for apps; automate chooser interaction for CI.
4. Configuration: surface per-platform timeout defaults and user-config overrides; ensure Linux notification backpressure can be tuned.

## Notes

- Manufacturer data matching uses optional masks when provided (shared helper); Linux obtains bytes from Device1 `ManufacturerData`, and Android now forwards manufacturer payload bytes through JNI so the shared matcher can evaluate masks there as well.
- All chooser events continue to emit on the runtime dispatcher to keep window interaction thread-safe.
