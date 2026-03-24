# Web Bluetooth – Implementation Plan

This plan outlines the architecture, milestones, and detailed task list to deliver a fully featured Web Bluetooth implementation across Apple, Android, Windows, and Linux.

Last updated: 2025-09-25

## Objectives

- Provide a spec-compliant Web Bluetooth API at `navigator.bluetooth`:
  - `requestDevice`, `BluetoothDevice`, `BluetoothRemoteGATTServer/Service/Characteristic`
  - GATT operations: connect/disconnect, getPrimaryService(s), getCharacteristic(s), read/write, start/stopNotifications
- Ensure strong performance and memory safety:
  - No main-thread blocking (except platform-required)
  - Zero-copy data paths where possible
  - Notification backpressure and efficient event delivery
  - RAII/ARC, deterministic callback cleanup

## Architecture

### Layers

- JS Surface (`api/internal/bluetooth-web.js`)
  - Spec validation for `requestDevice` options
  - Event dispatch (‘characteristicvaluechanged’) to the correct characteristic instance
  - Default chooser overlay; can be replaced by apps
- IPC (`src/runtime/ipc/routes.cc`)
  - Full command set for `bluetooth.*` endpoints
  - QueuedResponse support for binary payloads (readValue)
- Core Service (`src/runtime/core/services/web_bluetooth.{hh,cc}`)
  - Platform Backend interface with strictly typed entry points
  - Notification observer registry + event bridge
- Platform Backends
  - Apple (CoreBluetooth) – Objective‑C++
  - Android (Kotlin + JNI + GATT)
  - Windows (WinRT)
  - Linux (BlueZ / D‑Bus)

### Permissions & Chooser

- Gated by `[permissions] allow_bluetooth` and platform permissions
- `requestDevice` requires a user gesture and shows a chooser: renderer overlay by default
- Apple: Info.plist usage strings, entitlements, background modes configured
- Android: runtime perms (API 31+: BLUETOOTH_SCAN/CONNECT)

## API Mapping (Spec)

- `Bluetooth.requestDevice(options)` — filters vs acceptAllDevices; optionalServices UUID list
- `BluetoothDevice.gatt.connect()` / `disconnect()` — disconnection events dispatched
- `getPrimaryService(s)` / `getCharacteristic(s)` — cached discovery
- `BluetoothRemoteGATTCharacteristic.readValue()` → ArrayBuffer/DataView
- `BluetoothRemoteGATTCharacteristic.writeValue(value)` — with/without response
- `startNotifications()` / `stopNotifications()` — event delivery semantics

## Platform Workstreams

### Shared infrastructure – IN PROGRESS

- [x] Centralised requestDevice parsing (`parseRequestDeviceOptions`) with UUID normalisation and manufacturer data masks
- [x] Per-window notification observer tracking + JS chooser dedupe
- [x] `options.servicesMatch` toggles ALL vs ANY service matching (default ALL)
- [x] Config override via `web_bluetooth_services_match`
- [ ] Document filter semantics / add cross-platform integration tests (beyond JS option validation); expose timeout defaults + emit interval configs on remaining platforms; add chooser automation hooks

### Apple (CoreBluetooth) – COMPLETE

- [x] Availability (CBCentralManager PoweredOn)
- [x] requestDevice with filters + chooser events
- [x] Connect/Disconnect (delegates)
- [x] Discover services/characteristics
- [x] readValue/writeValue (buffered)
- [x] start/stopNotifications
- [x] Notification event bridge to JS characteristic instance
- [x] Memory safety: ARC + RAII; main-queue dispatch; loop-thread marshaling

### Android – IN PROGRESS

- [x] Availability (BluetoothAdapter.isEnabled)
- [x] BLE scanning (Kotlin) + JNI devicefound → chooser
- [x] Permissions (API 31+: BLUETOOTH_SCAN/CONNECT) request path
- [x] requestDevice filters → ScanFilters synthesis (UUID union) + shared matcher (services/name/prefix/manufacturerData)
- [x] Device registry keyed by MAC/Address; chooser returns cached metadata
- [x] GATT connect/disconnect + disconnection events (existing)
- [x] Discover services/characteristics (cache)
- [ ] MTU negotiation
- [ ] readValue/writeValue (with/without response) zero‑copy path
- [x] start/stopNotifications wiring + notification bridge
- [ ] Memory safety: audit JNI local ref lifetimes + loop-thread marshaling
- [ ] Document permission prompt behaviour and add automated coverage (emulator)

### Windows (WinRT/Classic) – IN PROGRESS

- [x] Availability probe (classic radio detection)
- [x] requestDevice chooser events (classic cached devices)
- [ ] Switch to WinRT BLE enumeration + live discovery; adopt shared matcher for services/manufacturer data
- [ ] BluetoothLEDevice + GATT connect/discovery from WinRT APIs
- [ ] readValue/writeValue; start/stopNotifications (WinRT)
- [ ] Disconnection events; caching; perf tuning
- [ ] Document permission requirements / WinRT packaging steps

### Linux (BlueZ / D‑Bus) – IN PROGRESS

- [x] Availability probe (Adapter PoweredOn)
- [x] requestDevice chooser events + shared matcher (services/name/namePrefix/manufacturerData)
- [x] LE scan via org.bluez; device lifecycle dedupe and timeout handling
- [x] GATT service/characteristic ops; StartNotify/StopNotify; queued read/write responses
- [ ] Event delivery backpressure (batch notifications, rssi/name change streaming); allow config of discovery timeout defaults

## Performance & Safety

- Zero-copy read/write where possible (QueuedResponse for reads)
- Notification coalescing/backpressure on loop thread
- Timeouts and cancellation paths for each async operation
- ARC + RAII; no dangling delegates or JNI refs; deterministic cleanup

## Testing Strategy

- Unit/Integration: service discovery, characteristic IO, notifications
- Cross-platform manual:
  - `navigator.bluetooth.getAvailability()`
  - `requestDevice` with filtered UUIDs
  - GATT ops: connect → discover → read/write → notify
- End-to-end in CI: desktop + iOS simulator + Android emulator

## Rollout & Flags

- Gated by `[permissions] allow_bluetooth`
- Platform maturity toggles per backend, surfaced through `getAvailability()`

## Tracking & Ownership

- Owners: Runtime team
- Source: `src/runtime/core/services/web_bluetooth*`, `api/internal/bluetooth-web.js`
- Build:
  - Apple: `.mm` included by runtime library build
  - Android: Kotlin under `src/` via Gradle sourceSets; JNI C++ via core globs
  - Windows/Linux: standard core service compilation
