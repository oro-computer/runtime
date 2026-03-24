# Conduit Transport

Conduit is the Oro Runtime-managed binary channel used for high-frequency or low-latency communication between JavaScript and the native services. It complements the request/response style `ipc://` bridge by exposing a WebSocket endpoint that stays open across the runtime lifetime, reuses a single framing format, and avoids work on the UI thread whenever possible.

The transport is implemented in `runtime::core::services::Conduit` and surfaced to JavaScript through the `oro:conduit` module (`api/conduit.js`). Modules such as UDP, TLS, AI streaming, and future high-throughput features should attempt to use Conduit first, falling back to `ipc.send`/`ipc.write` only when the socket is unavailable.

## Lifecycle

- When the runtime boots, the Conduit service launches a local WebSocket server bound to an ephemeral port. The port, hostname, and optional shared key are exported to JavaScript via `globalThis.__args.conduit`.
- `Conduit.port` tracks the current server port. The helpers `Conduit.status()` and `Conduit.waitForActiveState()` let code query or await activation.
- Instances (`new Conduit({ id })`) register themselves in an internal pool. The pool is used when the application is paused or resumed:
  - `hooks.onApplicationPause` stops the server and marks every client inactive.
  - `hooks.onApplicationResume` restarts the server, updates the known port, and calls `reconnect()` on clients that opted in (`shouldReconnect = true`).
- The WebSocket URL is `ws://localhost:<port>/<client-id>/<top-window-id>?key=<sharedKey>`. The shared key is optional and can be updated at runtime via `Conduit.setSharedKey()`.
- Conduit emits standard DOM events (`open`, `message`, `error`, `close`). Reconnects dispatch a synthetic `reopen` event so higher layers can resume subscriptions after the socket returns.

## Message Framing

All messages are binary `Uint8Array` payloads with a compact header section.

```
┌──────────────┬─────────────────────────────┐
│ Byte Offset  │ Meaning                     │
├──────────────┼─────────────────────────────┤
│ 0            │ Number of headers (uint8)   │
│ 1..n         │ Repeated header entries     │
│              │   - key length (uint8)      │
│              │   - key bytes (UTF-8)       │
│              │   - value length (uint16 BE)│
│              │   - value bytes (UTF-8)     │
│ n+1..n+2     │ Payload length (uint16 BE)  │
│ rest         │ Payload bytes               │
└──────────────┴─────────────────────────────┘
```

Values in the header section are decoded back into JavaScript primitives. Strings matching `true`, `false`, or `null` return the corresponding primitive; decimal strings are converted to numbers. The payload is exposed as a `Uint8Array` and is left untouched.

Recommended header keys include `route` (identifies the native handler), `port`, `address`, and any other domain-specific metadata consumers need to interpret the payload.

## JavaScript API Overview

Static helpers:

- `Conduit.status()` → `{ port, isActive, sharedKey }`
- `Conduit.diagnostics()` → runtime level stats (active handles, count)
- `Conduit.waitForActiveState({ maxQueriesForStatus })` → resolves when the server reports `isActive`.
- `Conduit.getSharedKey()` / `Conduit.setSharedKey()`

Instance methods and patterns:

- `new Conduit({ id, sharedKey })` immediately attempts to connect. Instances keep themselves alive with GC finalizers; call `close()` to opt out and prevent reconnects.
- `connect(callback)` establishes the WebSocket and resolves once the `open` event fires. The callback receives an `Error` if the attempt fails.
- `reconnect({ retries, timeout })` wraps `connect()` in an exponential backoff loop (default: 32 retries, capped at 30s delay). A successful reconnection dispatches a `reopen` event.
- `receive(handler)` registers a message callback (`handler(error | null, { options, payload })`). Only one receive handler is active at a time; calling `receive` replaces the previous hooks.
- `send(options, payload?)` frames the provided metadata and optional payload. It returns `false` when the socket is paused, disconnected, or otherwise unusable. Callers should treat a `false` return as a signal to enter their fallback path and trigger `reconnect()`.
- `close()` prevents further reconnects, removes event listeners, and severs the WebSocket if one is active.

## Integration Guidelines

1. **Prefer Conduit for streaming workloads.** For example, UDP uses it to start/stop reads and deliver datagrams without blocking `ipc://` handlers.
2. **Set up `receive` before issuing commands.** In modules that expect inbound data (`message` events, AI streaming segments, etc.), register the `receive` handler immediately after constructing the Conduit instance.
3. **Gracefully degrade.** If `send()` returns `false`, log the failure, invoke any module-specific fallback (`ipc.send`/`ipc.write`), and call `conduit.reconnect().catch(() => {})` to restore the preferred transport in the background.
4. **Handle `reopen`.** When the transport reconnects the runtime dispatches a `reopen` event. Reissue any outstanding subscriptions (e.g., `udp.readStart`) inside this handler so delivery resumes without requiring user action.
5. **Pause/Resume aware.** During application pause events the transport is torn down intentionally. Modules should expect a burst of `error`/`close` events and rely on the `reopen` path to restart delivery when the app resumes.
6. **Keep IDs stable.** The `id` passed to `new Conduit({ id })` is part of the WebSocket routing path and is used server-side to demultiplex clients. Reusing the same ID across reconnects allows the native side to resume stateful streams cleanly.

## Diagnostics and Troubleshooting

- Enable `DEBUG=conduit` to surface verbose logs during development. The transport logs connection attempts, retries, and failures.
- `Conduit.diagnostics()` aggregates runtime statistics, including active handles and the IDs currently subscribed.
- Use `internal.conduit.status`/`stop`/`start` (via `ipc.request`) for low-level testing or when running in headless environments.
- A `false` return from `send()` indicates the WebSocket is not open. Check whether the application is paused (`hooks.onApplicationPause` currently sets a global flag) or whether the shared key is out of sync.

## Related Reading

- `api/conduit.js` – JavaScript implementation and hooks.
- `src/runtime/core/services/conduit.cc` – native service and client management.
- `api/dgram.js` – example of a module that mixes Conduit and IPC pathways for resilience.
