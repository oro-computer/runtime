# CDP (Chrome DevTools Protocol)

Status: Experimental. Intended for browser automation tooling (Playwright + Puppeteer) and internal debugging workflows, not Chrome-parity DevTools.

This document is the source of truth for what Oro’s CDP server:
- **Supports today** (implemented and expected to work)
- **Can support in the future** (plausible with additional runtime/engine work)
- **Will never support** (requires Chromium/V8 internals or Chrome-specific infrastructure)

## Goals

- **Connectivity-complete**: stable HTTP + WebSocket endpoints, sane errors, no crashes.
- **Tooling-complete**: enough protocol coverage for real-world Playwright/Puppeteer automation flows.
- **Safe by default**: bind to loopback by default; make “remote debugging” clearly insecure when exposed off-machine.

Non-goals:
- Full Chrome/Chromium CDP parity.
- A bundled DevTools frontend UI.

## Enabling CDP

### CLI / Desktop flag

The desktop app binary supports a Chromium-style flag:

```
--remote-debugging-port=<port>
```

- `<port> == 0` selects a random available port.
- When enabled, the runtime prints a Chrome-like line to stderr:
  - `DevTools listening on ws://127.0.0.1:<port>/devtools/browser/<browserId>`
- If the bind address is **not loopback**, the runtime prints a warning.

The `oroc` CLI forwards this flag to the app binary, so you can use the same option when launching via `oroc`.

### JavaScript API (`oro:cdp`)

Apps can start/stop/query CDP from JS:

```js
import * as cdp from 'oro:cdp'

const status = await cdp.listen({ hostname: '127.0.0.1', port: 0 })
console.log(status.httpEndpoint) // http://127.0.0.1:<port>
console.log(status.wsEndpoint)   // ws://127.0.0.1:<port>/devtools/browser/<browserId>
```

## Server endpoints

### HTTP

- `GET /json` and `GET /json/list`: returns page targets with `webSocketDebuggerUrl`.
- `GET /json/version`: returns `webSocketDebuggerUrl` plus basic version fields.
- `GET /json/protocol`: minimal schema (many tools ship their own protocol definitions).
- `GET /json/new?<url>` (also supports `?url=<url>`): creates a new window and navigates.
- `GET /json/activate/<targetId>`: attempts to focus the window for the target.
- `GET /json/close/<targetId>`: requests closing the window for the target.

### WebSocket

- `ws://<host>:<port>/devtools/browser/<browserId>`: browser session (root).
- `ws://<host>:<port>/devtools/page/<targetId>`: binds directly to a page target.

## Target model

- Each desktop window maps to a CDP `TargetInfo` with:
  - `type: "page"`
  - `browserContextId` is always present for tooling compatibility; the default context uses `default-<browserId>` and is not returned by `Target.getBrowserContexts`
- Non-default browser contexts are tracked for automation tooling:
  - `Target.getBrowserContexts` returns IDs created via `Target.createBrowserContext` (does not include default)
  - `Target.disposeBrowserContext` forgets the context and closes windows created in that context

Note: Oro currently tracks browser contexts for protocol compatibility, but does not yet guarantee Chromium-like storage/process isolation between contexts.

## Supported protocol surface (today)

Notes:
- Unknown methods in known CDP domains return `code: -32000` (`"Not supported"`). Unknown domains return `code: -32601` (`"Method not found"`).
- Any method name ending with `.enable` or `.disable` is accepted as a **no-op** unless explicitly implemented below.
- `Runtime.evaluate` / `Runtime.callFunctionOn` provide a compatibility-focused subset and do not aim to emulate Chromium/V8 evaluation semantics in full (some edge cases around multi-statement inputs may differ).
- `Runtime.executionContextCreated` includes `auxData.frameId` and `auxData.isDefault`, and isolated worlds created via `Page.createIsolatedWorld` surface as distinct execution contexts via `context.name` (required for Puppeteer/Playwright frame/world bookkeeping).
- `DOM.describeNode` includes `node.frameId` for iframe/frame elements (content frame id) and `documentElement` nodes so Puppeteer/Playwright can map handles to frames.
- Network instrumentation covers in-page `fetch`/`XMLHttpRequest` plus runtime-handled scheme requests (SchemeHandlers). This is still not full “subresource parity” with Chromium.
- Fetch interception (`Fetch.*`) supports runtime-handled scheme requests and in-page `fetch()` requests (instrumented). It does not intercept browser-network subresources and it does not currently intercept `XMLHttpRequest`.
- Input injection is implemented via in-page event synthesis (DOM/Pointer events + `element.click()` for default actions), not native OS-level input; some sites may behave differently.
- Frame model: `Page.getFrameTree` includes iframe entries discovered in the DOM, but frames do not have separate CDP sessions, and iframe execution contexts are compatibility-only (treat the top-level frame as the only fully addressable context).
- `Page.addScriptToEvaluateOnNewDocument`: scripts are applied at the start of a document load (via `readystatechange`), but this does not guarantee true Chromium “pre-page-script” semantics on all platforms.
- CDP discovery endpoints accept a trailing slash (e.g. Playwright probes `GET /json/version/`).

### Safety limits

To avoid OOMs and giant WebSocket frames, Oro applies safety caps:

- **Network response bodies**: captured for in-page `fetch`/`XMLHttpRequest` and for runtime-handled scheme responses; bounded to **5 MiB per request**. A global **20 MiB total** and **256 request** cap is applied for stored bodies used by `Network.getResponseBody`.
- **Base64 response bodies**: when a response body is binary, it is returned base64-encoded; due to encoding overhead, the `Network.getResponseBody` payload can be larger than 5 MiB even when the decoded body is within the 5 MiB cap.
- **Network request bodies (`postData`)**: captured for in-page `fetch`/`XMLHttpRequest` and runtime-handled scheme requests; bounded to **256 KiB per request**. When caps are hit, `postData` may be omitted.
- **Screenshots**: `Page.captureScreenshot` is guarded (dimensions/pixels + payload size) and may return `"Screenshot too large"` for very large pages or clips.
- **WebSocket messages**: inbound and outbound frames are capped at **32 MiB**; oversized frames will close the connection to avoid OOM.
- **IO streams**: `Fetch.takeResponseBodyAsStream` and `Network.takeResponseBodyForInterceptionAsStream` store at most **64** active streams, **20 MiB total**, and serve at most **256 KiB** per `IO.read` call.
- **`Page.addScriptToEvaluateOnNewDocument`**: bounded to **128 scripts per target**, **1 MiB per script**, and **4 MiB total** per target.
- **`Runtime.addBinding`**: bounded to **256 bindings per target**.
- **DOM snapshots**: `DOM.getDocument` / `DOM.describeNode` `depth` is clamped (max **3**); `DOM.querySelectorAll` returns at most **2048** nodeIds.
- **DOM serialization**: `DOM.getDocument` / `DOM.describeNode` are additionally capped to **4096** serialized nodes per call to avoid huge payloads.
- **`Runtime.getProperties`**: returns at most **2048** property descriptors per call.
- **Navigation history**: bounded to **128 entries per window**.
- **Telemetry**: unknown method log-dedup is bounded to **1024 unique method names** per server run.

### Implemented methods (expected to work)

- `Browser.getVersion`
- `Browser.getBrowserCommandLine` (stub: returns empty `arguments`)
- `Browser.getWindowForTarget` (bounds-only)
- `Browser.getWindowBounds` (bounds-only)
- `Browser.setContentsSize` (resize; may be ignored by platform)
- `Browser.setWindowBounds` (move/resize/windowState; platform-dependent)
- `Browser.close` (closes window `0`, which exits on most desktop builds)

- `Target.setDiscoverTargets`
- `Target.getTargets`
- `Target.setAutoAttach`
- `Target.attachToBrowserTarget` (returns a new `sessionId`)
- `Target.attachToTarget`
- `Target.detachFromTarget`
- `Target.sendMessageToTarget` (flattened sessions)
- `Target.getTargetInfo`
- `Target.createTarget`
- `Target.closeTarget`
- `Target.activateTarget`
- `Target.getBrowserContexts`
- `Target.createBrowserContext` (returns a unique id)
- `Target.disposeBrowserContext` (closes windows in that context)

- `Page.enable` (emits initial `Page.frameNavigated` for the attached target)
- `Page.navigate`
- `Page.setDocumentContent`
- `Page.getFrameTree`
- `Page.getResourceTree` (returns document + `<script src>` + `<link rel="stylesheet">` URLs discovered in DOM)
- `Page.getResourceContent` (returns main document HTML when `url` matches current page; otherwise empty content)
- `Page.getLayoutMetrics`
- `Page.getNavigationHistory`
- `Page.navigateToHistoryEntry`
- `Page.bringToFront`
- `Page.reload`
- `Page.stopLoading`
- `Page.close`
- `Page.addScriptToEvaluateOnNewDocument`
- `Page.removeScriptToEvaluateOnNewDocument`
- `Page.createIsolatedWorld` (returns an executionContextId; not true world isolation)
- `Page.captureScreenshot` (Linux/WebKitGTK only; `png` only; may reject huge pages)

- `Runtime.enable` (emits initial `Runtime.executionContextCreated` for the attached target)
- `Runtime.evaluate`
- `Runtime.callFunctionOn`
- `Runtime.getProperties`
- `Runtime.releaseObject`
- `Runtime.releaseObjectGroup`
- `Runtime.addBinding`
- `Runtime.removeBinding`
- `Runtime.runIfWaitingForDebugger` (no-op)
- `Runtime.getIsolateId` (returns a stable id for this runtime instance)

- `Log.enable` (enables `Log.entryAdded` events for console/errors)
- `Log.disable`
- `Log.clear` (no-op)

- `DOM.getDocument`
- `DOM.getOuterHTML`
- `DOM.setOuterHTML`
- `DOM.querySelector`
- `DOM.querySelectorAll`
- `DOM.getAttributes`
- `DOM.requestChildNodes` (emits `DOM.setChildNodes`)
- `DOM.setAttributeValue`
- `DOM.removeAttribute`
- `DOM.setNodeValue`
- `DOM.describeNode`
- `DOM.resolveNode`
- `DOM.getContentQuads`
- `DOM.getNodeForLocation`
- `DOM.getBoxModel`
- `DOM.scrollIntoViewIfNeeded`
- `DOM.focus`
- `DOM.getFrameOwner`
- `DOM.setFileInputFiles`

- `CSS.getComputedStyleForNode`
- `CSS.getInlineStylesForNode` (returns `style` attribute; no rule/source mapping)
- `CSS.getMatchedStylesForNode` (returns empty `matchedCSSRules`; includes `inlineStyle`)

- `Input.dispatchMouseEvent`
- `Input.dispatchKeyEvent`
- `Input.insertText`

- `Emulation.setDeviceMetricsOverride` (may resize the window on desktop)
- `Emulation.clearDeviceMetricsOverride`

- `Network.getResponseBody` (fetch/XHR, synthetic navigation HTML, and runtime-handled scheme responses)
- `Network.getRequestPostData` (fetch/XHR, plus runtime-handled scheme requests)
- `Network.setExtraHTTPHeaders` (applies to in-page `fetch`/`XMLHttpRequest` plus runtime-handled scheme requests)
- `Network.getResponseBodyForInterception` (delegates to `Network.getResponseBody`)
- `Network.takeResponseBodyForInterceptionAsStream` (IO stream; keyed by `interceptionId`)
- `Network.setBlockedURLs` (blocks in-page `fetch`/`XMLHttpRequest` plus runtime-handled scheme requests)
- `Network.setRequestInterception` (compat layer; enables `Fetch.requestPaused` + emits `Network.requestIntercepted` for the same requests)
- `Network.continueInterceptedRequest` (resolves requests paused via `Fetch.requestPaused`; supports `errorReason`, request overrides, and `rawResponse` fulfill)

- `Performance.getMetrics` (compat subset; values may be 0 on non-Chromium engines)
- `Memory.getDOMCounters` (compat subset)
- `Memory.getBrowserCounters` (compat subset)
- `Runtime.getHeapUsage` (compat subset)

- `Fetch.enable` (runtime-handled scheme requests + in-page `fetch()` (instrumented); request-stage only)
- `Fetch.disable` (same scope as `Fetch.enable`)
- `Fetch.continueRequest` (same scope as `Fetch.enable`)
- `Fetch.continueResponse` (accepted; treated like continue)
- `Fetch.fulfillRequest` (same scope as `Fetch.enable`; `body` treated as base64)
- `Fetch.failRequest` (same scope as `Fetch.enable`)
- `Fetch.continueWithAuth` (accepted; auth challenges not modeled)
- `Fetch.getResponseBody` (delegates to `Network.getResponseBody`)
- `Fetch.takeResponseBodyAsStream` (IO stream; keyed by `requestId`)

- `IO.read`
- `IO.close`
- `IO.resolveBlob` (stub: returns a UUID but does not expose blob retrieval by UUID)

### Implemented events (limited coverage)

- `Target.targetCreated`
- `Target.targetDestroyed`
- `Target.targetInfoChanged`
- `Target.attachedToTarget`
- `Target.detachedFromTarget`

- `Page.frameNavigated`
- `Page.frameStartedLoading` (synthesized)
- `Page.frameStoppedLoading` (synthesized)
- `Page.lifecycleEvent`
- `Page.domContentEventFired`
- `Page.loadEventFired`
- `Page.javascriptDialogOpening` (in-page wrappers)

- `Runtime.executionContextsCleared`
- `Runtime.executionContextCreated`
- `Runtime.bindingCalled`
- `Runtime.consoleAPICalled` (in-page wrappers)
- `Runtime.exceptionThrown` (in-page wrappers)
- `Log.entryAdded` (when `Log.enable` has been called)

- `Network.requestWillBeSent` (fetch/XHR + synthetic navigation + runtime-handled scheme requests)
- `Network.responseReceived` (fetch/XHR + synthetic navigation + runtime-handled scheme requests)
- `Network.loadingFinished` (fetch/XHR + synthetic navigation + runtime-handled scheme requests)
- `Network.loadingFailed` (fetch/XHR + runtime-handled scheme failures)
- `Network.dataReceived` (runtime-handled scheme requests only)
- `Network.requestIntercepted` (when `Network.setRequestInterception` is enabled; mirrors `Fetch.requestPaused`)

- `Fetch.requestPaused` (runtime-handled scheme requests + in-page `fetch()` (instrumented))

### No-op stubs (accepted, but currently do nothing)

- `Schema.getDomains` (returns empty)
- `Security.setIgnoreCertificateErrors`
- `Browser.grantPermissions` / `Browser.resetPermissions`
- `Browser.setDownloadBehavior`
- `Browser.cancelDownload` (no-op)
- `Animation.getPlaybackRate` / `Animation.setPlaybackRate` (stored/returned for compatibility; does not affect engine playback)
- `Overlay.*` (no-op)

- `Network.clearBrowserCache` (no-op)
- `Network.clearBrowserCookies` (no-op)
- `Network.setCacheDisabled`
- `Network.setBypassServiceWorker`
- `Network.emulateNetworkConditions`
- `Network.getCookies` (returns empty)
- `Network.setCookies` (no-op)
- `Network.deleteCookies` (no-op)
- `Storage.getCookies` (returns empty)
- `Storage.setCookies` (no-op)
- `Storage.clearCookies` (no-op)

- `Emulation.setUserAgentOverride`
- `Emulation.setTimezoneOverride`
- `Emulation.setLocaleOverride`
- `Emulation.setTouchEmulationEnabled`
- `Emulation.setFocusEmulationEnabled`

- `Page.setLifecycleEventsEnabled`
- `Page.setBypassCSP`
- `Page.setFontFamilies` (no-op)
- `Page.setInterceptFileChooserDialog`
- `Page.handleJavaScriptDialog` (no-op)
- `Page.getAppManifest` (returns empty)
- `Page.getInstallabilityErrors` (returns empty)
- `Page.captureSnapshot` (returns empty)
- `Page.startScreencast` / `Page.stopScreencast` / `Page.screencastFrameAck` (no-op)

- `Accessibility.getFullAXTree` (returns empty)
- `Accessibility.queryAXTree` (returns empty)
- `DOMSnapshot.captureSnapshot` / `DOMSnapshot.getSnapshot` (returns empty)
- `DOMDebugger.*` (returns empty)
- `CSS.getStyleSheetText` (returns empty)
- `CSS.getPlatformFontsForNode` (returns empty)
- `CSS.getMediaQueries` (returns empty)
- `CSS.collectClassNames` (returns empty)
- `CSS.startRuleUsageTracking` (no-op)
- `CSS.stopRuleUsageTracking` (returns empty)
- `Debugger.getScriptSource` (returns empty)
- `Debugger.setSkipAllPauses` (no-op)
- `Debugger.getPossibleBreakpoints` (returns empty)
- `Debugger.setBreakpointByUrl` / `Debugger.setBreakpoint` (returns empty)
- `Debugger.removeBreakpoint` / `Debugger.pause` / `Debugger.resume` / `Debugger.stepOver` / `Debugger.stepInto` / `Debugger.stepOut` (no-op)
- `Debugger.searchInContent` (returns empty)
- `Profiler.startPreciseCoverage` (no-op)
- `Profiler.takePreciseCoverage` (returns empty)
- `Profiler.stopPreciseCoverage` (returns empty)
- `Profiler.setSamplingInterval` / `Profiler.start` (no-op)
- `Profiler.stop` (returns empty)
- `Profiler.getBestEffortCoverage` (returns empty)
- `Coverage.startJSCoverage` / `Coverage.stopJSCoverage` / `Coverage.startCSSCoverage` / `Coverage.stopCSSCoverage` (no-op)
- `Coverage.takePreciseCoverage` (returns empty)
- `HeapProfiler.collectGarbage` (no-op)
- `HeapProfiler.startSampling` (no-op)
- `HeapProfiler.getSamplingProfile` (returns empty)
- `HeapProfiler.stopSampling` (returns empty)
- `Tracing.start` (no-op)
- `Tracing.end` (no-op)

### Explicitly unsupported (returns `-32000 Not supported`)

- `Page.printToPDF`
- `Runtime.queryObjects`
- `HeapProfiler.takeHeapSnapshot`
- `Extensions.loadUnpacked` / `Extensions.uninstall`
- `DeviceAccess.enable` / `DeviceAccess.selectPrompt` / `DeviceAccess.cancelPrompt`
- `Autofill.trigger`
- `ServiceWorker.*` / `BackgroundService.*` / `BackgroundFetch.*` / `Audits.*` / `Audits2.*` / `WebAuthn.*` / `WebAudio.*` / `Media.*` / `Cast.*` / `DeviceOrientation.*`

## Playwright / Puppeteer compatibility

This CDP server is intended to support:
- Puppeteer connecting via `browserWSEndpoint` or `browserURL` (HTTP endpoint).
- Playwright connecting via `chromium.connectOverCDP(<httpEndpoint>)`.

Important: Playwright’s “CDP mode” is Chromium-oriented; Oro provides a CDP-compatible surface for automation, but it is not a Chromium engine and some features may behave differently or remain stubbed.

### Expectations checklist (what tools typically do)

This section is a practical checklist of the CDP surface that Puppeteer/Playwright commonly expect during `connect()` / `connectOverCDP()` and basic automation flows.

**Connectivity / discovery**
- HTTP endpoints used by tooling: `GET /json/version`, `GET /json`, `GET /json/list`.
- Browser WS endpoint: `ws://<host>:<port>/devtools/browser/<browserId>`.
- Page WS endpoint: `ws://<host>:<port>/devtools/page/<targetId>` (some tooling/debuggers use this directly).

**Target + session model (must not surprise clients)**
- `Target.setDiscoverTargets` should emit `Target.targetCreated` for existing targets *before* responding.
- `Target.getTargets` lists current targets.
- `Target.attachToTarget` with `flatten: true` (the preferred mode) yields a `sessionId` and emits `Target.attachedToTarget` before resolving.
- `Target.setAutoAttach` is accepted (Oro does not model worker/serviceworker subtargets today).
- `Target.sendMessageToTarget` works for flattened sessions (messages are processed as if they were sent with that `sessionId`).

**“Enable” dance + initial events**
- Automation clients typically issue many `*.enable` calls; Oro accepts them as no-ops unless explicitly implemented.
- `Runtime.enable` returns `{}` and emits `Runtime.executionContextCreated` for the attached page/session.
- `Page.enable` returns `{}` and emits `Page.frameNavigated` for the attached page/session.

**Diagnostics: execution contexts**
- `Runtime.executionContextCreated.context.auxData.frameId` and `auxData.isDefault` must be present so Puppeteer/Playwright can map contexts to frames.
- `Page.createIsolatedWorld` should surface as `Runtime.executionContextCreated` with `context.name === <worldName>` (e.g. Puppeteer’s `UTILITY_WORLD_NAME`).

**Core automation primitives**
- Navigation: `Page.navigate`, `Page.reload`, `Page.stopLoading`.
- DOM querying: `DOM.getDocument`, `DOM.querySelector`, `DOM.querySelectorAll`, `DOM.describeNode`, `DOM.resolveNode`.
- JS execution: `Runtime.evaluate`, `Runtime.callFunctionOn`, `Runtime.getProperties`, `Runtime.releaseObject`, `Runtime.releaseObjectGroup`.
- Input: `Input.dispatchMouseEvent`, `Input.dispatchKeyEvent`, `Input.insertText`.

**Observability (limited)**
- `Network.*` events and `Network.getResponseBody` / `Network.getRequestPostData` cover `fetch`/`XMLHttpRequest`, synthetic navigation events, and runtime-handled scheme requests (not full subresource parity).
- Console/errors may surface as `Runtime.consoleAPICalled` / `Runtime.exceptionThrown`.

**Known sharp edges (not Chrome parity)**
- Frame model: one top-level frame per window (frameId == targetId); iframes are not separate frames/targets today.
- Request interception: `Fetch.*` is supported for runtime-handled scheme requests (SchemeHandlers) and for in-page `fetch()` requests (instrumented). It ignores non-Request stages and it does not intercept browser-network subresources.
- Deprecated interception: `Network.setRequestInterception` is supported as a compatibility layer and forwards to the same underlying interception used by `Fetch.*` (emitting `Network.requestIntercepted` for `Fetch.requestPaused` requests). It does not currently intercept `XMLHttpRequest`.

### Example: Puppeteer

```js
import puppeteer from 'puppeteer'

const browser = await puppeteer.connect({ browserURL: 'http://127.0.0.1:<port>' })
const page = (await browser.pages())[0] || await browser.newPage()
await page.goto('https://example.com')
```

### Example: Playwright

```js
import { chromium } from 'playwright'

const browser = await chromium.connectOverCDP('http://127.0.0.1:<port>')
const context = browser.contexts()[0] || await browser.newContext()
const page = context.pages()[0] || await context.newPage()
await page.goto('https://example.com')
```

## Platform notes

### Linux (WebKitGTK)

When CDP is enabled, the runtime enables WebKit automation via `webkit_web_context_set_automation_allowed(...)` (when supported) so external tooling can attach.

## Things we can support (future candidates)

These are plausible, but require additional runtime/engine integration and will be implemented as needed by real tooling usage:

- **Network observability**: `Network.*` events (request/response lifecycle, headers, timing) and cookie access.
- **Request interception**: extend `Fetch.*` beyond SchemeHandlers to cover more request sources, if we can hook at the right layer.
- **Screenshots / PDFs**: cross-platform `Page.captureScreenshot` and `Page.printToPDF` via platform webview snapshot/print APIs.
- **Window management**: improve cross-platform parity for `Browser.getWindowForTarget` / `Browser.setWindowBounds` (positioning + window state).
- **More DOM helpers**: richer `DOM.*` and `DOMSnapshot.*` surfaces for robust selector/visibility tooling.
- **Frame/iframe parity**: richer `Page.*` frame events and per-frame execution contexts (requires deeper engine integration).

## Things we will never support

These require Chromium/V8 internals or Chrome-specific infrastructure, and are not realistic to emulate faithfully in Oro’s non-Chromium engines:

- **V8 debugging & profiling parity**:
  - `Debugger.*`, `Profiler.*`, `HeapProfiler.*`, `Coverage.*` (breakpoints, stepping, heap snapshots, precise CPU/heap profiling)
- **Chrome tracing infrastructure**:
  - `Tracing.*` and Chrome’s tracing model; Oro may provide small compatibility subsets (e.g. `Performance.getMetrics`, `Memory.getDOMCounters`) but does not implement Chrome’s tracing/memory panels
- **Chromium-only target/process models**:
  - Features that depend on Chromium’s multi-process architecture (OOPIF/process-per-site semantics, Chrome-specific worker/service worker inspection models)

## Security notes

Remote debugging gives full control over the app (evaluate JS, read DOM, drive input). Treat it like an admin interface:
- Prefer `hostname: 127.0.0.1` and local-only usage.
- Do not bind CDP to non-loopback on untrusted networks.
