# Background Services Architecture

## Overview

Applications need to run JavaScript work while the UI is suspended or the process is in the background. This document proposes a cross-platform background service subsystem that gives developers a worker-like JavaScript context hosted by platform-native primitives:

- **Android** — Android `Service` (foreground optional) driven by WorkManager jobs.
- **iOS / iPadOS** — `BGTaskScheduler` (`BGProcessingTask`/`BGAppRefreshTask`) runners.
- **Desktop** — headless webview contexts owned by the runtime.

The goal is a single JavaScript surface that hides platform differences while still exposing enough scheduling controls to satisfy entitlement-driven flows (notably iOS).

## Goals

- Provide a simple JS API for defining one-off or long-lived background services.
- Ensure service lifecycle can be managed from configuration (`oro.toml`) so iOS static declarations are respected.
- Allow services to communicate with the foreground runtime via existing IPC primitives.
- Minimise platform-specific code duplication and reuse the existing runtime service infrastructure.
- Support graceful degradation when a platform does not allow persistent execution.

## Non-Goals

- Replace existing `ServiceWorker`/`SharedWorker` APIs.
- Guarantee infinite background execution on platforms with strict limits (iOS).
- Provide UI surfaces; background services are headless.

## JavaScript API

### Module

New module `api/background.js` exposing:

```js
import background from 'oro:background'

await background.register({
  id: 'sync-notifications',
  entry: 'background/notifications.js',
  trigger: {
    type: 'interval',
    minimumInterval: 15 * 60 * 1000,
  },
  keepAlive: false,
  permissions: ['notifications', 'network'],
})

await background.schedule('sync-notifications')
```

API surface:

- `register(options)` — declare a background service. Persists definition in runtime state.
- `schedule(id, overrides?)` — request execution respecting platform rules.
- `cancel(id)` — cancel pending runs.
- `status(id)` — retrieve last run metadata.

Background scripts behave like dedicated workers:

- Use `self.postMessage`, `self.addEventListener('message', ...)`.
- Access to a restricted API surface identical to worker threads plus any explicitly granted permissions.
- Receive lifecycle events (`activate`, `run`, `abort`, `complete`) dispatched via `runtime:events`.

### IPC Integration

- `api/background.js` communicates over `ipc.request('background:*')`.
- The runtime service translates registration into platform-specific schedulers.
- Foreground contexts can subscribe to background events via `ipc.subscribe('background:events')`.

## Configuration

Extend `oro.toml` with a `[background]` namespace. Example:

```
[background]
enabled = true
default_entry = background/index.js

[background.service.sync_notifications]
entry = background/notifications.js
required = ios
trigger.type = interval
trigger.minimum_interval = 900000
keep_alive = false
permissions = notifications,network
```

Key points:

- `enabled` gates the entire subsystem.
- `default_entry` used when JS registers without an explicit `entry`.
- Each `[background.service.<id>]` block pre-declares a service (needed for iOS so `BGTaskScheduler` identifiers exist at build time).
- `required = ios` enforces that builds targeting that platform must provide the service (fails fast during build).
- Additional platform-specific overrides live under `[background.service.<id>.platform.<platform>]`.

`npm run gen` will surface typings for the JS API; configuration changes require updates to the config parser (new slice `config.background`).

## Runtime Architecture

### Core Service

- Add `core::BackgroundService` to `src/runtime/core/services.cc`.
- Responsibilities:
  - Persist registered services in `core::state`.
  - Coordinate scheduling requests across platforms.
  - Launch headless JavaScript isolates using the existing bridge (`Runtime::dispatcher` and `javascript::createJavaScript`).
  - Route `postMessage` traffic (background ↔ foreground).
  - Emit service lifecycle events to JS observers.

### Execution Model

- Each run creates a `BackgroundExecution` instance:
  - Owns a libuv loop pinned to the runtime loop (desktop) or platform thread (Android/iOS).
  - Bootstraps with `background/runtime.js` to patch the worker environment.
  - Applies permission gating before exposing modules (leveraging existing `Services` toggles).
  - Enforces run timeout (configurable default 30 minutes; platform-specific minimums apply).

- Background runs reuse the existing `queued-response` infrastructure (`runtime/context/context.cc`) for IPC serialization.

### State Management

- Extend `core::state` storage with `background-service/<id>.json` entries storing metadata (triggers, last run, failure counts).
- Synchronise with config overrides on boot; runtime rejects registrations not whitelisted by config when `required` is set.

## Platform Notes

### Android

- Implement `BackgroundService` extending `android.app.Service`.
- Use `WorkManager` for deferred/scheduled work; `ForegroundService` when `keepAlive = true`.
- Spin up a headless runtime via existing bootstrap (`Runtime::Options` with hidden window manager).
- Tie lifecycle to `onStartCommand`; completion stops service unless scheduling recurring work.
- Respect Doze/App Standby by marking network requests as `NetworkType.CONNECTED` when needed.

### iOS / iPadOS

- Register task identifiers from config during app launch (`BGTaskScheduler.shared.register`).
- Support two trigger types:
  - `processing` → `BGProcessingTaskRequest`.
  - `refresh` → `BGAppRefreshTaskRequest`.
- Background execution spins up a lightweight runtime inside the app process (no separate extension). The headless webview (`WKWebView`) loads `oro:background`.
- Provide helper in `bin/generate-plist` to inject `PermittedBackgroundTaskSchedulerIdentifiers`.
- Enforce configuration: if a service is marked `required = ios` and not scheduled, warn during build.

### Desktop (macOS, Windows, Linux)

- Add `HeadlessWebView` to `src/runtime/window` for hidden contexts.
- Background executions hosted in that headless view, sharing the same process and IPC loop.
- Scheduling:
  - `interval` triggers emulate timers via existing `timers` service.
  - `on-demand` runs triggered only when JS calls `schedule`.
  - Persisted alarms survive restarts via `core::state`.

### Shared Behaviour

- `postMessage` and `message` events mirrored through the dispatcher.
- Graceful cancellation when foreground requests `cancel`.
- Debug logging gated by `debug_background` config.
- Metrics recorded via `diagnostics` (success/failure counters).

## Implementation Phases

1. **Scaffolding**
   - Add config parser support and runtime service skeleton.
   - Implement JS module stubs returning `Unimplemented` errors on unsupported platforms.

2. **Desktop Headless Runner**
   - Introduce headless webview execution and IPC.
   - Provide integration tests under `test/src/background`.

3. **Android Service Integration**
   - Implement Android service/WorkManager binding.
   - Validate with instrumentation tests (`npm run test:android`).

4. **iOS Background Tasks**
   - Hook BGTaskScheduler, update build scripts for plist generation.
   - Add simulator coverage (`npm run test:ios-simulator` scenario).

5. **Developer Experience**
   - Update docs (`api/README.md`) and examples (`examples/background-service`).
   - Ensure `npm run gen` produces typings and docs.

6. **Polish**
   - Add metrics, logging, timeout handling.
   - Harden permission gating and failure retries.

## Testing Strategy

- **Unit**: new runtime service tests covering registration validation and state persistence.
- **Integration**: Oro runner suites verifying message passing and timeout behaviours (legacy Socket suites still run until the deprecation window closes).
- **Platform**:
  - Android instrumentation verifying WorkManager scheduling and service restart.
  - iOS simulator tests using `BGTaskScheduler` debug APIs.
  - Desktop headless tests ensuring timers survive pause/resume.
- **Manual**: sample app demonstrating notifications sync in the background.

## Open Questions

- Should background services share the same JS bundle cache as foreground windows?
- How aggressively should we retry failed runs on iOS where background time is scarce?
- Do we expose platform-specific scheduling hints (e.g., `requiresExternalPower`) directly, or keep them in config only?
