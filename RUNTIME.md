**Overview**

- For a comprehensive deep dive with diagrams, see `docs/RUNTIME_ARCHITECTURE.md`.
- Oro Runtime embeds a platform WebView and exposes native capabilities to JavaScript via an IPC bridge. It remains lean, dependency-light, and consistent across desktop and mobile.

**Key Concepts**

- Loop: A unified event loop built on libuv and integrated with platform main loops.
- Context: Shared runtime state. Includes dispatchers, queues, and queued responses used to communicate with JS.
- Services: Modular native capabilities (FS, DNS, Timers, UDP, OS, Notifications, etc.) with feature gating.
- Bridge: The glue between a window’s WebView and the runtime; implements scheme handlers and IPC.
- Window: Platform window creation/management and WebView lifecycle/configuration.
- IPC: Message router for JS↔native calls (supports standard requests, queued responses, event streams, and chunked bodies).
- Resources: Access to packaged/static assets and file watching for dev workflows.

**Event Loop and Dispatching**

- `runtime::loop::Loop`
  - Wraps a libuv loop and bridges to platform loops:
    - Linux: GLib `GSource` integrates uv backend fd with GTK main loop.
    - Apple: GCD dispatch queues; loop may run on a dedicated thread.
    - Windows: Win32 message pump coordination (see Dispatcher) with uv backend.
    - Android: App looper and uv cooperate.
  - States: `None → Init → Idle ↔ Polling → Paused/Stopped → Shutdown`.
  - Dispatch: `Loop::dispatch()` enqueues a callback and pokes an `uv_async_t` to run it on the loop thread.

- `context::Dispatcher`
  - A platform‑aware means of marshaling callbacks onto the UI/main thread:
    - Linux: `g_main_context_invoke`.
    - Apple: `dispatch_sync` to the main queue from a global queue.
    - Windows: posts a thread message to the main thread (fixes pending; see STATUS/PLAN).
    - Android: uses the JVM/Looper to run work on the activity thread.

**Runtime Context**

- `context::RuntimeContext`
  - Owns the main `loop::Loop`, a `QueuedResponses` map, and a mutex for cross‑thread access.
  - `createQueuedResponse(seq, params, queuedResponse)` serializes a queued response to a JavaScript snippet and tracks it so JS can pull the body later via XHR to `ipc://queuedResponse`.

**Core Services**

- `core::Service`
  - Base class providing access to `context`, `dispatcher`, `loop`, a worker queue, and an enabled flag. Start/stop are no‑ops unless `enabled`.
  - Provides observer utilities for event emission to JS consumers.

- `core::Services`
  - Aggregates all concrete services with feature toggles:
    - AI (llama.cpp model/context/LoRA management)
    - BroadcastChannel
    - Conduit (internals for data channels)
    - Diagnostics
    - DNS
    - FS (fs/fs.promises APIs backed by native IO)
    - Geolocation
    - MediaDevices
    - NetworkStatus
    - Notifications
    - OTP (Web OTP SMS retrieval)
    - OS
    - Permissions
    - Platform
    - Process (child processes)
    - Timers (timeouts/intervals/immediates)
    - UDP (sockets + manager)
  - Lifecycle: `start()`/`stop()` call through to each enabled service.

**Bridge and Schemes**

- `bridge::Bridge`
  - Initialized per window (client). Mediates:
    - `evaluateJavaScript(source)`: runs code in WebView.
    - `dispatch(cb)`: ensures cb runs on the correct thread.
    - `send(seq, data[, queuedResponse])`: resolves an IPC request promise in JS.
    - `emit(event, data)`: triggers events inside the render process.
- Scheme Handlers via `webview::SchemeHandlers`:
  - `ipc:`: IPC requests from JS. Parses messages, routes to `ipc::Router`, supports queued responses, event streams, and chunked bodies.
  - `oro:`: serve application resources (from packaged assets or dev resources directory). Integrate with the Service Worker when present. Templates emit `oro:` URLs.
  - `node:`: Proxies imports of allowed Node core modules to packaged static shims under `/oro/<module>.js`.
  - Module specifiers: the loader treats `import … from 'oro:<module>'` and `require('oro:<module>')` as first-class runtime module specifiers.
  - User-Agent branding: WebViews on macOS, iOS, Linux, Windows, and Android append `OroRuntime/<version>` to their user agents.

- **Runtime Detection Flags**
  - `globalThis.isOroRuntime`: preferred detection helper, always `true`, non-configurable, and documented for third-party apps so they can branch on Oro-specific capabilities without checking module specifiers.

- Router/IPC
  - `ipc::Message`, `ipc::Router`, `ipc::Result`: Compose request/response metadata (headers, body, cancellation token, queued response ids).
  - Streams: EventSource (text/event-stream) and chunked responses implemented using callbacks that write progressively to a `SchemeHandlers::Response`.
    - Platforms: Apple, Linux, and Android support true incremental streaming. Windows WebView2 may buffer custom-scheme responses (SSE/chunked) until finish; incremental delivery is not guaranteed there.

**Web OTP**

- The OTP core service powers `navigator.credentials.get({ otp: … })` calls.
- Android: delegates to Google Play Services’ SMS Retriever API and routes retrieved codes through the IPC bridge. Disabled automatically when `permissions_allow_otp = false`.
- iOS: currently relies on the platform WebView implementation; the JS polyfill forwards to the native `navigator.credentials` implementation when available.
- Desktop/Other: responds with `NotSupportedError`.

**Windows and WebView**

- Window Manager (`window::Manager`)
  - Tracks `ManagedWindow` instances in an indexed array with status transitions: `NONE → CREATED → SHOWN/HIDDEN → CLOSING → EXITING`.
  - Creates default or indexed windows with options for dimensions, titlebar style, background color, headless mode, and feature toggles.
  - Maps a `bridge::Bridge` to each window; can lookup by `Bridge`, `WebView`, or client id.
  - Emits events (e.g., permission changes, notifications) to each active window via its bridge.

- WebView (`runtime/webview`)
  - Platform specific layers for:
    - Navigation policy and events.
    - JS dialogs and permissions (media capture, orientation/motion, file picker).
    - Preload injection (runtime hooks; can be disabled by header).
    - Drag & drop (macOS) with payload emission to JS.
  - `webview::Navigator`: Manages location, origin, and resolving resource paths/mounts.

**Resources and File System**

- `filesystem::Resource`
  - Wraps packaged and dev asset access; MIME type detection; reading as bytes or string.
- `filesystem::Watcher`
  - Watches files or directories using `uv_fs_event` and `uv_fs_poll` and debounces events before emitting `rename`/`change` to consumers.

**Networking (UDP)**

- Socket Manager (`udp::SocketManager`)
  - Owns sockets keyed by an id. Pause/resume orchestrates socket lifecycle on loop resume (rebind, restart recv) or pause (stop recv, close handle).
- Sockets (`udp::Socket`)
  - Bind/connect/recv/send; propagate `recv` via callback with peer information.
  - Ephemeral sockets auto‑close post‑send.
  - Resume logic rebinds or reconnects and restarts recv if previously active.

**Timers and Worker Queue**

- Timers (`core::services::Timers`)
  - setTimeout/setInterval/setImmediate with cancellation. Implemented with libuv timers; cancellation should free resources and avoid double free (see STATUS/PLAN).
- Worker Queue (`concurrent::WorkerQueue`)
  - Queued work runs on detached threads constrained by a semaphore to a max concurrency. Consider upgrading to a fixed pool.

**Process**

- `process::Process`
  - Spawns child processes (Unix) with stdout/stderr polling on a background thread; environment handling; stdin writing with locking; termination signals and wait.
  - Windows and iOS have constrained paths (iOS disabled; Windows provides alternative implementation elsewhere).

**JSON and Bytes**

- `JSON::Any` and friends implement a small JSON model used throughout for events, IPC payloads, and configuration. Strongly typed wrappers with `.str()` serialization and `.as<T>()` casting.
- `bytes::Buffer` wraps byte arrays with helpers (base64, hex, etc.).

**Configuration and Permissions**

- `config::getUserConfig()`: loads `oro.toml` plus per-platform overrides, then merges into services and windows.
- Permissions: `Runtime::hasPermission(permission)` checks `permissions_allow_<...>` keys; default allow unless explicitly `false`.
- Capabilities: `globalThis.__args.capabilities.streaming` exposes streaming support flags to JS:
  - `sseIncremental`: true on Apple/Linux/Android; false on Windows (WebView2 may buffer custom‑scheme responses).
  - `chunkedIncremental`: true on Apple/Linux/Android; false on Windows for the same reason.

**Lifecycle**

- Runtime (`runtime::Runtime`)
  - Construct with options (features, loop options, user config). `start()` initializes the loop and resumes services.
  - `resume()` starts the loop (if needed) and `Services::start()`.
  - `pause()` pauses the loop (non‑Android), optionally stops services.
  - `stop()` pauses and stops the loop; `destroy()` calls stop + loop shutdown.
  - `dispatch(cb)` proxies to `context::Dispatcher` for thread‑correct execution.

**Lifecycle Model**

- High‑level app events are emitted via `windowManager.emit()`:
  - `applicationpause` — app moved to background or deactivated
  - `applicationresume` — app returned to foreground or activated
  - `applicationstop` — app is tearing down (before exit)
- Platform mapping:
  - iOS: `applicationDidEnterBackground` → pause; `applicationWillEnterForeground` → resume; `applicationWillTerminate` → stop
  - macOS: `applicationWillResignActive` → pause; `applicationWillBecomeActive` → resume; `applicationWillTerminate` → stop
  - Android: `ProcessLifecycleOwner` onPause/onStop → pause; onResume/onStart → resume; activity recreation reuses or recreates fragments while preserving native windows
  - Windows: `WM_ACTIVATEAPP` and `WM_SIZE(SIZE_MINIMIZED)` → pause; `WM_ACTIVATEAPP` restore and `WM_SIZE(SIZE_RESTORED/MAXIMIZED)` → resume; `WM_QUERYENDSESSION/WM_ENDSESSION` → stop
  - Linux (GTK): focus‑out/window iconified → pause; focus‑in/deiconify → resume

- Service nuances
  - FS watchers: are stopped on pause and not automatically re‑established on resume. Applications should recreate file watchers after receiving `applicationresume`.

Desktop always‑running behavior

- By default on desktop (Linux/macOS/Windows), the runtime remains running and does not perform native pause/resume on lifecycle transitions. The web layer still receives `applicationpause`/`applicationresume` events.
- DOM focus parity: desktop windows mirror host activation/minimize with `window.focus()`/`window.blur()` so in‑page listeners behave consistently across platforms.
- Config toggle: set `lifecycle_desktop_always_running = false` in `oro.toml` to enable real native pause/resume on desktop. On Linux, pause is executed asynchronously to avoid GTK deadlocks.

**Security Model**

- Scheme isolation: Only whitelisted schemes and module imports are permitted; Node core module exposure is limited to static shims.
- Sandboxed WebView: Native capabilities are only exposed via IPC routes and services honoring permission checks and user config.

**Security Configuration**

- File System Sandbox (non‑Apple platforms)
  - Config: `filesystem_sandbox_enabled = true`
- Env: `FS_SANDBOX=1` or `ORO_FS_SANDBOX=1`
  - Limits access to mounted roots (`webview_navigator_mounts_*`) and well‑known paths (resources/tmp/etc.).
  - FS routes enforce the sandbox and return a SecurityError on violations.

- No‑Follow Symlinks
  - Config: `filesystem_no_follow_symlinks = true`
- Env: `FS_NOFOLLOW=1` or `ORO_FS_NOFOLLOW=1`
  - Denies traversing symlinks when resolving resource paths.

- WebView HTTP Response Headers (custom scheme)
  - Optional headers from config:
    - `webview_csp` → `Content‑Security‑Policy`
    - `webview_referrer_policy` → `Referrer‑Policy`
  - CORS:
    - `webview_cors_allow_all` (default true)
    - `webview_cors_allow_credentials` (default true)
    - `webview_cors_allow_headers`, `webview_cors_allow_methods`
    - `webview_cors_allowed_origins` (space‑separated allowlist; reflected when `allow_all=false`)

- Native Extensions Loading
  - Safe names enforced: `[A‑Za‑z0‑9_‑]+`.
  - Optional allowlist of extension roots: `extensions_allowed_roots = "/abs/path1 /abs/path2"`.

**Testing Hooks**

- Built‑in test runner outputs TAP. Desktop/mobile scripts under `test/scripts/*` orchestrate app builds and run suites under `test/src/*`.
- Tests exercise FS, dgram, process, VM, window/webview, IPC/router resolution, etc.

**Known Gaps (see STATUS/PLAN)**

- Timers ownership and `uv_close` semantics need tightening.
- Windows dispatcher requires fixes and validation.
- Window manager read locks missing on some enumerations.
- FS watcher and other uv handle lifecycles should be audited for explicit close on shutdown.

**Cross‑References**

- STATUS.md: current health assessment and hotspots.
- PLAN.md: sequenced work to improve safety, performance, and completeness.
  **HTTP IPC Bridge (opt‑in)**
- Exposes the IPC router over an HTTP server using bundled `cpp-httplib`.
- Entirely opt-in and configured via `oro.toml`.

oro.toml

```
[http.ipc]
enable = true            # default: false (disabled)
host = 127.0.0.1         # default: 127.0.0.1
port = 27718             # required (> 0) to start the server
path = /ipc              # default: /ipc
allow_cors = false       # default: false
shared_key =             # optional; if set, require header X-ORO-Auth: <shared_key>
timeout_ms = 32000       # optional; max wait for an IPC result
```

Usage

- Path mapping: `GET/POST/PUT/DELETE http://<host>:<port>/ipc/<route>?k=v` → `ipc://<route>?k=v&seq=0`
- Header mapping: send `X-IPC-URI: ipc://<route>?k=v&seq=0` to the exact prefix path (`/ipc`).
- Window selection: optionally set `X-IPC-Window-Index: <n>` to target a specific window bridge (defaults to 0 if available).
- Request body is forwarded as raw bytes to the route.
- Response is JSON (`application/json`) or raw bytes when the route replies with a body.

Notes

- If a primary window (index 0) exists, window-specific routes use that bridge by default. Routes requiring a window will no-op if no window is yet associated.
- Streaming (SSE and chunked) is supported:
  - SSE: `diagnostics.stream.sse?count=N&interval=ms` yields `text/event-stream` with `event:` and `data:` lines. `count` is limited to 256 and `interval` to 1–1000 ms.
  - Chunked: `diagnostics.stream.chunks?chunks=N&chunkSize=K&interval=ms` yields `application/octet-stream` via chunked transfer. Streams are limited to 256 chunks, 64 KiB per chunk, and 4 MiB total.

**Embedded LLaMA Server (AI, OpenAI-compatible)**

- A llama.cpp-based server is embedded and exposed via the in-process `oro:` scheme.
- No external sockets are opened; endpoints are only available as custom-scheme requests.
- Default prefix: `/ai/llama` under your app origin `oro://<bundle>`.
- Endpoints:
  - `GET /ai/llama/health` — readiness + loaded models + pool stats + KV-clear telemetry; when `ai_llm_debug_route_metrics=true`, includes `routes.counts`, `routes.recent` (time‑windowed), and `routes.status`
  - `GET /ai/llama/v1/models` — list of loaded models
  - `POST/GET /ai/llama/v1/chat/completions` — OpenAI-style chat; add `?stream=true` for SSE
  - `POST/GET /ai/llama/v1/completions` — text completions; `?stream=true` for SSE
  - `POST/GET /ai/llama/v1/embeddings` — embeddings
  - `POST/GET /ai/llama/tokenize`, `/ai/llama/detokenize`

## AI — Model Loading

The runtime searches for model files in:

- `ORO_AI_LLM_MODEL_PATH` (env var): directory containing models
- userConfig `ai_llm_model_path`
- explicit `directory` parameter

IPC routes (invoke via `ipc:` scheme):

```js
await fetch('ipc://ai.llm.model.load?name=model.gguf')
const list = await (await fetch('ipc://ai.llm.model.list')).json()
```

## AI — Server Configuration

- userConfig keys:
  - `ai_llm_server_prefix` (default `/ai/llama`)
  - `ai_llm_default_model`
  - `ai_llm_rate_limit_concurrency` (default 4)
  - `ai_llm_rate_limit_rps` (requests per second, 0 = disabled)
  - `ai_llm_rate_limit_burst` (max burst capacity, 0 = disabled)
  - `ai_llm_default_max_tokens` (default 128)
  - `ai_llm_default_temperature` (default 0.8)
  - `ai_llm_default_top_p` (default 0.95)
  - `ai_llm_default_top_k` (default 40)
  - `ai_llm_default_min_p` (default 0.05)
  - `ai_llm_hard_max_tokens` (absolute cap)
  - `ai_llm_max_prompt_bytes` (cap for prompt, default 131072)
  - `ai_llm_embed_max_total_bytes` (cap across embedding inputs)
  - `ai_llm_pool_capacity` (contexts kept warm per model/size)
  - `ai_llm_model_path` (directory to search for models)
  - `ai_llm_lora_path` (directory to search for LoRA adapters)
  - `ai_llm_gpu_layer_count` (default for `n_gpu_layers`)
  - `ai_llm_n_threads` (default llama CPU threads per context)
  - `ai_llm_context_n_batch` (override batch size)
  - `ai_llm_context_n_ubatch` (override micro-batch size)
  - `ai_llm_debug_kv_log` (enable KV-clear debug logs; true/1/on/yes)
  - `ai_llm_debug_kv_log_interval_ms` (min interval between KV logs; default 5000)
  - `ai_llm_debug_route_metrics` (enable route counters/recent list in `/health`; true/1/on/yes)
  - `ai_llm_debug_route_metrics_recent_ms` (recent list time window in ms; default 300000)
  - `ai_llm_debug_route_metrics_top_k` (limit counts/status in `/health` to top K routes by count; default 32)
- environment:
  - `ORO_AI_LLM_MODEL_PATH`: directory containing models
  - `ORO_AI_LLM_LORA_PATH`: directory containing LoRA adapters

## AI — Request Semantics

- Location: `oro://<bundle>/<prefix>/<endpoint>`
- Bodies: `application/json` supported up to 1 MiB; query params also supported
- Prompt-size cap: 128 KiB → 413 on exceed
- Defaults for sampling taken from userConfig/options when omitted
- Stop sequences:
  - Non-stream: output trimmed at earliest stop
  - Stream: rolling tail detects stop; emits finish_reason "stop"
- Tools/function-calling (stub):
  - If `tools` and required `tool_choice` are present, a minimal tool call is returned (non-stream) or emitted as a single streamed event (SSE), then finished. No code execution.

## AI — Security & Performance

- Internal-only exposure via `oro:`
- Prompt-size and JSON-parse caps to bound resource usage
- Concurrency guard to prevent oversubscription; heavy jobs run on AI worker queue
- SSE/chunk streams coalesce writes to reduce overhead
- Context reuse prefers in-place KV-cache clear when supported by llama.cpp;
  otherwise falls back to full reinitialization to maintain compatibility.
- Streaming endpoints record approximate status in route metrics (e.g., 200 stop/length, 408 timeout, 4xx/5xx early errors) when route metrics are enabled.
