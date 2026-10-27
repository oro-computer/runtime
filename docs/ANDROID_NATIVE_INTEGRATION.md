# Android native integration

Use the standard runtime Activity and JavaScript APIs for runtime features.
[Declarative workflows](DECLARATIVE_WORKFLOWS.md) provide bounded background
reads, push hints, encrypted checkpoints and notifications without application
Kotlin, C++, JNI, receiver classes or service implementations. Use a compiled
`NativeExtension` only for custom protocols or behavior outside the supported
runtime features. Missing platform backends belong in the runtime.

Oro owns document cancellation, Android navigation, scheduling, transport,
encryption and notification presentation. The application owns account
authorization, server enrollment, schema mapping and product policy.

These APIs are additive to Android bridge protocol 5. Rebuild and distribute
the CLI, native sources and JavaScript API together. Inspect
`oro:diagnostics/runtime.getCapabilities()` and each module's support response
before offering the corresponding workflow. Capability support does not prove
server enrollment, OS execution or delivery.

Native-only transactional records and conditional coordination, plus explicit
user-started `oro:foreground` execution, are documented in
[Native state and foreground execution](ANDROID_NATIVE_STATE.md). They provide
shared local atomicity and execution plumbing; application authorization,
protocol receipts and migration decisions remain application code.

| Requirement | Runtime API | Application supplies |
| --- | --- | --- |
| Modal/router Back | `oro:window/back.setHandler()` | Which modal, drawer or route consumes Back |
| Recurring catch-up | `oro:background/deferred.enqueue()` with `intervalMs` | Bundled workflow, or custom executor for an unsupported protocol; fresh authorization |
| Revocation/cleanup | `enqueueMaintenance()` | Explicit retry-safe native executor and durable reconciliation |
| Encrypted push | `oro:push` and `NativePushHandler` | Authenticated server enrollment, account binding and bundled workflow/payload policy |
| Notification presentation | `oro:notification` / `NativeNotifications` | Severity, channels, public text, groups and authorized destinations |
| Saved credentials | `oro:authentication`, `oro:credentials`, `oro:secure-storage` | Record formats, account context and historical migration |
| Appearance/layout | `setSystemAppearance()` and `webview.text_zoom` | Theme choice, one inset owner and responsive application layouts |
| Styled startup | Current window's `markPresented()`, then `markReady()` | Inline styled startup UI, inert application controls and usable-screen readiness |

## Startup presentation and readiness

With `[webview] require_ready = true`, Android initially covers the WebView with
native loading UI. Once your bundled document has installed its inline startup
styles and screen, call `(await application.getCurrentWindow()).markPresented()`.
The runtime waits for a native WebView visual-state fence before removing that
cover. This call does not mark the application ready, extend the 30-second startup
deadline, enable fullscreen, or request focus for a hidden window.

Keep unfinished application controls inert and announce progress accessibly.
Call `markReady()` only after essential styles and a usable screen are ready.
Until then, a timeout or load failure restores native recovery and its Reload
control. New navigation covers the new document again. Missing/corrupt package
assets, renderer loss and a WebView that cannot produce a frame retain native
recovery. Android's initial system launch splash is unchanged.

Presentation requires the current verified bundled HTTPS document, its live
native document token and the top-level bridge. Foreign approved origins,
subframes, headless windows, replaced renderers and stale requests cannot dismiss
the cover. Other platforms reject with `ENOSYS`. Gate adoption on
`nativeStartupPresentation` in the awaited `oro:diagnostics/runtime.getCapabilities()` result and rebuild
both native archives and the application; copying JavaScript or Kotlin alone
does not add the native IPC route. Qualify first frames, delayed/failed assets,
explicit reload, both themes, large text and keyboard/TalkBack focus on devices.

## Configuration

Products with C++ policy can use [compiled callbacks and cold startup](#compiled-c-callbacks-and-cold-startup)
without an application Kotlin adapter. [Scoped push recovery](#scoped-push-recovery-from-compiled-policy)
provides native-only discovery and conditional cleanup after product authorization
and explicit user consent.

For Kotlin editor diagnostics, hover and navigation against original application
sources, use [Oro Language Server](LANGUAGE_SERVER.md). It documents `oro.toml`
project-root selection, server program overrides, Coc/VS Code setup and
`oroc tooling kotlin --prepare` for Android dependencies.

For foreground service status icons, configure `[foreground] notification_icon`
as a packaged drawable resource. See the [foreground configuration example and
icon rules](ANDROID_NATIVE_STATE.md#public-notification-artwork). This resource belongs
to the application; the shared controller owns service admission and lifecycle.

Paths below are relative to the application project root (`.`):

```toml
[background]
deferred_enabled = true

[push]
enabled = true
provider = "unifiedpush"

[android]
runtime_protocol = "5"
sources = "android/ProductExtension.kt"
extensions = "org.example.ProductExtension"

[permissions]
allow_notifications = true

[webview]
text_zoom = "system"
```

Omit `[push]` when the application does not adopt this transport. Enabling it
adds the runtime receiver, importance service and distributor visibility query.
Applications do not need to patch generated Kotlin/manifests or declare another
push receiver. Notification permission is separate from push registration.
Do not select a Service Worker mode merely to use these native contracts.

## Application Back

`oro:window/appearance.setGestureExclusion(regions)` requests Android 10+ edge
priority for a small visible handle in the current trusted top-level foreground
document. A region is `{ edge: 'left' | 'right', top, height }`, with top/height
fractions of the native WebView client height. Map the visible handle after
layout; do not assume CSS pixels equal native pixels or apply text scaling again.
The strip is at most 24dp wide and a quarter of the view. At most four regions
share a 200dp combined-height limit per edge, additionally capped at half the
view height. Oversized requests reject without replacing the current request.
Mandatory system gestures are clipped out. Android retains final admission;
`active` means a current view request, not guaranteed OS acceptance. See
[Android gesture navigation](https://developer.android.com/develop/ui/views/touch-and-input/gestures/gesturenav).

Pass `[]` to clear. Native navigation, hiding, pause, focus loss, detachment,
geometry/configuration change or document replacement clears the request and
its listeners. It does not return automatically on resume. Only reapply after
layout for a visible handle. This API owns the WebView's exclusion list; do not
mix it with custom native writes to that same list. Drawer design, swipe/scroll
arbitration, animation, accessibility and reduced motion remain app code. Keep
a reachable button and preserve system Back outside the handle. Other platforms
return ENOSYS; nonempty requests below Android 10 return ENOTSUP. Regression
coverage uses `test/unit/android-gesture-exclusion.test.js`; real edge arbitration
still requires Android navigation-mode/device qualification.

```js
import { setHandler } from 'oro:window/back'

const removeBackHandler = await setHandler(() => {
  if (closeTopModal()) return true
  if (closeNavigationDrawer()) return true
  return false
})

// When the application stops owning this handler:
await removeBackHandler()
```

`closeTopModal` and `closeNavigationDrawer` above are application functions that
return whether they handled Back. Only `true` consumes a request. A promise may
resolve within the native 750 ms deadline. False, exceptions or a missing reply
use the runtime's normal history/secondary-window/root-background behavior.
Repeated Back presses during a pending request coalesce. IME dismissal and
native fullscreen exit take priority over application interception.

Registration belongs to the current foreground document, not its reusable window
index. Navigation, renderer replacement and window destruction invalidate it.
Suspension discards pending handling without navigating another page. Reinstall
in a replacement document. An AbortSignal can remove the handler; the returned
removal function is idempotent. `backgesture` remains the progress/cancel signal
for predictive gesture presentation. Handling commit does not change the app's
responsibility for gesture animation or reduced-motion accessibility.

Removal is permitted for the same live trusted document after its window is
hidden or its Activity pauses. Enabling a handler and responding to Back still
require foreground ownership. A stale document cannot remove a replacement
document's registration; cleanup does not relax prompt or credential guards.

A custom Activity's own Back callback can intercept the gesture before Oro sees
it. Retire that callback or delegate to the runtime dispatcher before adopting
this API. Keep only the application's modal/router decisions in the JS handler.

## Recurring native reads

Register a `DeferredReadExecutor` with `DeferredSync.registerReadExecutor()` from
`NativeExtension.onCreate`. The executor resolves opaque references, checks
current authorization and returns an `AuthorizedDeferredRead`. Its `reconcile`
method performs the fixed read protocol; `close` releases credentials and network
resources. Registration runs in cold service processes too.

```js
import * as deferred from 'oro:background/deferred'

const support = await deferred.getSupport()
if (support.available && support.registeredKinds.includes('product.sync')) {
  await deferred.enqueue({
    id: 'account-status',
    kind: 'product.sync',
    authorityRef: 'saved-server-1',
    accountRef: 'saved-account-1',
    targetRef: 'alarm-subscription-1',
    operationRef: 'sync',
    network: 'any',
    intervalMs: 15 * 60 * 1000,
    flexMs: 5 * 60 * 1000
  })
}
```

Intervals range from 15 minutes through seven days. Flex defaults to the larger
of five minutes or 5% of the interval; an explicit value must range from that
minimum through the interval, matching
[Android's periodic job limits](https://developer.android.com/reference/android/app/job/JobInfo.Builder#setPeriodic(long,long)).
Periodic reads cannot also specify a nonzero `delayMs`. Android chooses execution
time; this is not a timer or a delivery
latency guarantee. Successful cycles return to `scheduled`, increment
`completedRuns`, retain `lastOutcome` and reset the attempt budget. Each cycle
allows at most five attempts and each execution has a 60-second deadline.
Terminal failure, unknown outcome or lost authorization suspends recurrence.
Cancel/release invalidates running work and its generation before OS callbacks
can settle a replacement task. Interrupted work needs explicit reconciliation
and `resume`; terminal work must be released and deliberately re-enqueued.

## Restricted maintenance

Native registration is deliberately separate from reads:

```kotlin
DeferredSync.registerMaintenanceExecutor(
  "product.subscription",
  setOf("revoke", "cleanup"),
  retrySafe = true,
  executor = DeferredMaintenanceExecutor { context, task, cancellation ->
    // Application code returns an AuthorizedDeferredMaintenance only after
    // resolving the references and checking the exact supported operation.
    subscriptions.authorizeMaintenance(context, task, cancellation)
  }
)
```

`subscriptions` is the application's native authorization/protocol service. It
must provide `authorizeMaintenance`, returning an `AuthorizedDeferredMaintenance`
or null. `perform(CancellationSignal)` returns COMPLETED, RETRY, UNAUTHORIZED or
UNKNOWN; `close()` releases all owned resources. Use UNKNOWN when an uncertain
outcome needs application reconciliation. RETRY explicitly asserts that another
attempt is safe. A revocation executor can authorize against its narrowly scoped
revocation grant after interactive sign-out; it must not revive the signed-out
account or require an unrestricted sign-in token.

```js
await deferred.enqueueMaintenance({
  id: 'revoke-subscription-1',
  kind: 'product.subscription',
  authorityRef: 'saved-server-1',
  accountRef: 'signed-out-account-1',
  targetRef: 'subscription-1',
  operationRef: 'revoke',
  network: 'any'
})
```

Only registered `cleanup` and `revoke` operations are accepted. These are
one-shot tasks using the common journal, capacity, deadlines and cancellation.
No arbitrary mutations, credentials, HTTP bodies, JavaScript closures or periodic
maintenance are accepted. The native retry-safety declaration is an application
contract, not server idempotency supplied by the runtime. General queued writes
remain unavailable.

## UnifiedPush enrollment and native delivery

For a local development transport, run `oroc push server`. The CLI downloads a
pinned, SHA-256-verified ntfy release and starts it in the foreground at
`http://127.0.0.1:8787`. Tools and the twelve-hour message cache live in the user's
writable Oro data directory, outside the SDK. `--data-dir` selects an isolated
workspace; `--offline` requires the verified cached release, and
`--ntfy-bin=/path/to/ntfy` uses an explicitly supplied executable. Node.js 22 or
later is required. Stop it with Ctrl-C.
Automatic server installation supports Linux x64/arm64 and Windows x64. The
upstream macOS release is client-only; on macOS, pass a server-enabled ntfy
development build through `--ntfy-bin` as described in the
[upstream installation notes](https://docs.ntfy.sh/install/#macos).

For an Android device, use the Oro Kotlin development distributor or an existing
[ntfy distributor](https://unifiedpush.org/users/distributors/). Both connect to
the same server and use the standard `oro:push` distributor selection flow.
To build and optionally install Oro Push Dev:

```sh
oroc push distributor --server=https://push.dev.example:8787 \
  --ca-cert=./dev-ca.pem --install --device=emulator-5554
```

The command uses the normal Android Gradle toolchain, built-in Kotlin support,
and an installed SDK platform 34 or newer. `--offline` requires cached build
dependencies. It retains a development signing key in writable user state so
updates preserve registrations. `--ca-cert` embeds only public certificates and
adds trust only inside this development APK; hostname verification remains
enabled. Open **Oro Push Dev**, review the HTTPS server and tap **Save and start**.
The distributor appears in `unifiedpush://link` only after it is configured.
Stop its connection explicitly from its settings when finished.

Oro Push Dev uses a visible foreground service, reconnects after socket/network
loss, retains ciphertext and the receive cursor atomically, retries delivery
until the receiving app acknowledges it, and binds the app's foreground-raise
service before broadcasting. It validates registration caller identity and
binds each token to the app's UID and signing certificate. Its private inbox is
bounded to 256 ciphertexts of at most 4096 bytes; 32 registrations and 512 recent
acknowledgement identities are retained. Intake pauses when the inbox is full.
It does not decrypt application payloads. Endpoint URLs contain 160 random bits.
Changing the server or resetting registrations unregisters the affected apps.

An enabled service requests restart after process loss or boot when Android
permits it. Force-stop requires reopening the distributor; a force-stopped
receiving app must also be reopened. A foreground service does not guarantee
delivery through Doze, network outages or device-vendor battery policies.
Reboot/Doze and distributor selection still need qualification on the intended
Android versions. This APK is for local development, not a production push
service. Application native handlers and authenticated backend enrollment are
still required; `oro:push` APIs and existing ntfy distributor support are unchanged.

Configure the selected distributor with a reachable HTTPS origin. For example:

```sh
oroc push server --host=0.0.0.0 --port=8787 \
  --base-url=https://push.dev.example:8787 \
  --cert-file=./cert.pem --key-file=./key.pem
```

Use a certificate trusted by the distributor, with the correct host/IP subject
alternative name. Alternatively, keep the default loopback HTTP listener behind
a local HTTPS reverse proxy and pass its origin as `--base-url`. LAN binding
requires an explicit HTTPS base URL; it does not configure DNS, firewall rules,
certificate trust or a reverse proxy. The host-only HTTP check does not qualify
Android delivery. Oro continues to require HTTPS push endpoints.
Both the distributor and the application server sending Web Push must reach and
trust the published HTTPS origin. Configure the sender's CA trust separately from
`--ca-cert`. The fixture's adb loopback mapping assumes the sender runs on the
adb host; a downstream sender needs a shared LAN address or DNS name.

This server allows anonymous development topic access. Use public test data on a
private development network. It uses its own configuration, strips inherited
`NTFY_*` overrides and configures no Firebase or upstream relay. A second server
should use a different `--data-dir` and port. See `oroc push server --help` for all
options and the [ntfy server documentation](https://docs.ntfy.sh/config/) for the
underlying transport. The cached release archive retains ntfy's license notices.

Local transport still needs an application server to authenticate the user,
enroll endpoint/key material and send RFC 8291 encrypted messages. Register the
native handler below before requesting a subscription. An HTTP publish response
establishes server acceptance; verify receipt and durable processing separately
in the application. The CLI server does not establish account authorization,
trusted backend enrollment, reboot/Doze delivery or notification display.

Register a `NativePushHandler` through `NativePush.registerHandler("product.events",
handler)` from the extension's `onCreate`. Implement:

- `authorize(Context, NativePushRegistration): Boolean`: resolve exact account,
  authority and target references and check current authorization on every
  delivery. This runs without an Activity.
- `onMessage(Context, NativePushRegistration, ByteArray, CancellationSignal):
  Boolean`: parse the authenticated payload, validate product scope and perform
  bounded, repeat-safe native processing. Return true after durable handling.
  The runtime clears the plaintext byte array after return; do not retain it.
- `onRegistrationChanged(Context, NativePushRegistration, JSONObject,
  CancellationSignal): Boolean`: optionally reconcile endpoint enrollment with
  the authenticated server in a cold process. Return true only after server
  confirmation. False retains pending enrollment. This has its own five-attempt
  budget, reset by an explicit `retryPending()` call.

Handlers may call `NativeNotifications.configureChannels()` / `submit()` or
enqueue a registered native read. Do not launch a WebView or replay a foreground
JavaScript session to process a background message.

```js
import * as push from 'oro:push'

await push.selectDistributor()
// After pushchange or foreground resume, check selectedDistributor.
const support = await push.getSupport()
if (support.selectedDistributor) {
  await push.register({
    id: 'account-events',
    kind: 'product.events',
    authorityRef: 'saved-server-1',
    accountRef: 'saved-account-1',
    targetRef: 'subscription-1',
    applicationServerKey: serverVapidPublicKey,
    description: 'Account notifications'
  })
}
```

Registration is asynchronous. On a later foreground resume or `pushchange`, call
`getRegistration('account-events')`. When phase is `registered`, send the endpoint
and `keys.p256dh` / `keys.auth` only to the authenticated enrollment server.
The auth value and endpoint are enrollment capabilities: keep them out of logs,
assistant arguments, action receipts and diagnostics. The runtime never returns
private decryption keys or distributor connection tokens. Endpoint changes need
application server enrollment reconciliation.
After foreground enrollment succeeds, call
`acknowledgeRegistration(id, enrollment.generation)`. A changed generation returns
EAGAIN so an older server response cannot acknowledge a replacement endpoint.
Native handlers can perform the same confirmation through `onRegistrationChanged`.
The durable `enrollmentPending` state survives process loss and is retried on the
next drain independently of message delivery.

The transport implements the [UnifiedPush Android protocol](https://unifiedpush.org/developers/spec/android/)
and [RFC 8291 single-record aes128gcm](https://www.rfc-editor.org/rfc/rfc8291.html).
It accepts at most 4096 encrypted bytes, authenticates ciphertext and persists a
Keystore-encrypted inbox entry before acknowledging reception. Unknown tokens,
invalid sender identities when supplied, malformed input and unauthenticated
payloads are rejected. An ACK establishes transport receipt, not notification
display or application acknowledgement. The app must use its own stable event
identity to deduplicate across foreground reads, catch-up and transport delivery.

There are at most 16 registrations, 64 retained messages and 256 recent transport
deduplication identities. Inbox and recent identities expire after seven days.
Each message has five processing attempts; `retryPending()` explicitly resets an
exhausted budget. Automatic drains run on process startup and new transport
input. Receiver work has an eight-second cancellation deadline, one worker and
a queue of 16. A native handler must cooperate with cancellation; blocked native
code can occupy that worker. No arbitrary background JavaScript is supported.

`unregister(id)` stops future local transport delivery before notifying the
distributor and signals active native handling. An already-running native handler
may have produced application effects and must cooperate with cancellation; it
must recheck current product authorization. Revoke grants separately using the
restricted maintenance contract. Unregister all transport registrations before
selecting a different distributor. Automatic distributor
fallback suggestions are not adopted; the user selects the replacement.

Android force-stop, missing distributor connectivity, expired authorization and
OS process/job policy can delay delivery. The retained inbox is recovered when
the application or receiver next starts. It does not schedule its own periodic
wakeups. Reboot/Doze, real distributors, provider changes, signed upgrades and
end-to-end server delivery need acceptance on the intended devices.

## Retiring existing application adapters

Adopt one primitive at a time. First enable its configuration, register native
product handlers, rebuild coherently and inspect the capabilities of that APK.
Then verify the same cold/warm, cancellation, navigation, rotation and historical
upgrade behavior before retiring its old adapter.

Omitting `android.main_activity` selects the generated standard Activity.
Extension lifecycle, intent and activity-result hooks replace the need for an
Activity subclass used only for native integration. Refactor extension code
that casts to a product Activity before switching; retain unrelated compatibility
behavior until its equivalent packaged runtime behavior has been accepted.

Push registrations use new runtime-owned tokens, encryption keys and storage.
Confirm new server enrollment, reconcile outstanding events and revoke the old
registration/grant before retiring an application-owned receiver. The runtime
does not import legacy push keys or register the old endpoint automatically.

Keep historical Keystore aliases, biometric envelopes, shortcut destinations,
notification receipts and outstanding jobs readable until their application
migrations pass. Runtime APIs do not migrate another implementation's stores.
Remove duplicate system-font-scale hooks after verifying `systemTextScaling`,
and keep exactly one native/CSS inset owner. Product assets, IAM, server grant
formats, alarm severity, target routing and group-summary policy remain app-owned.


For user-started native work, see [native state and foreground execution](./ANDROID_NATIVE_STATE.md).
Finite `oro:foreground` dataSync and continuous `oro:foreground/continuous`
specialUse work have separate compiled-executor registries, execution slots and
service declarations. Configure them in the project's `oro.toml`; the guide
covers project-root resolution, public notification/Stop, cancellation, process
loss, authorization boundaries and service-retirement acceptance.

## Compiled C++ callbacks and cold startup

Keep `oro.toml` at the project root. The opt-in C++ facade uses the same Android
foreground, continuous, deferred, push and extension controllers as Kotlin:

```toml
[android.native]
callbacks_enabled = true
callback_sources = "native/startup.cc native/product-policy.cc"
initializers = "initialize_product_native"
```

These paths are relative to the project root. The CLI rejects absolute paths,
traversal, symlink escapes, duplicate sources and malformed initializer names.
Each source compiles as a separate translation unit, preserving local includes
and anonymous namespaces. At most 16 sources and 16 initializer names are
accepted. This is separate from the older `[android] native_sources` setting.
The CLI captures source bytes and recursively referenced quoted project headers
into the JNI input inventory before compiling. Use quoted includes for project
headers; SDK/toolchain includes retain their ordinary include paths. Source and
local-header edits require rebuilding the APK.

```cpp
#include "runtime/native/android_platform.hh"

extern "C" void initialize_product_native(oro::runtime::Runtime* runtime) {
  using namespace oro::runtime::native;
  const auto backend = AndroidPlatform::fromRuntime(*runtime);
  backend->registerExecution(CallbackType::Continuous, "product.monitor", authorizeProductMonitor);
  backend->registerExecution(CallbackType::DeferredRead, "product.reconcile", authorizeProductRead);
  backend->registerExecution(CallbackType::Maintenance, "product.cleanup", authorizeProductCleanup, {"cleanup", "revoke"}, true);
  backend->registerPush("product.alert", productPushHandler);
  backend->registerExtension("product.settings", productSettingsHandler);
}
```

The example refers to application-provided functions and handlers. Initializers
register compiled code only; they must not open accounts, start work or perform
network operations. The `Runtime*` is opaque to this public facade. It does not
require internal Runtime headers or a custom Activity/Application. Initialization
runs once per Application process, before native receiver/job dispatch, including
cold starts. Registrations then freeze. Duplicate Kotlin/C++ registrations fail
startup; do not install both implementations for the same kind during handover.

`CallbackRegistry::Authorizer` resolves the exact task identity and returns an
owned `CallbackExecution`, or null for refusal. Its `run` observes the supplied
`ExecutionCancellation`; its destructor releases native credentials/connections.
It must recheck current server authorization and establish the remote outcome.
Deferred maintenance permits only registered cleanup/revoke operations with an
explicit retry-safety assertion. Push authorization runs with cancellation, and
message/enrollment handlers still establish their own trust and authorization.
Extension payloads/results are bounded JSON text; exceptions expose only fixed
codes. Raw exception messages never become extension responses.

Android continues to own service admission, notifications, deadlines and worker
capacity. The shared native registry additionally bounds active handles to 64,
requires authorization before execution and refuses a second run of an invocation.
Cancelling signals an owned callback; it never destroys code/resources that are
still executing. Uncooperative work retains its worker until it and cleanup
return. Activity/WebView backgrounding does not shut down native service callbacks.
Actual runtime destruction cancels and drains them before releasing runtime
resources. Callbacks own their non-UI connections; renderer-facing runtime services
may pause and must not be treated as independent background connections.

`AndroidPlatform::openRecords("product.name", identity)` supplies the shared C++
v2 encrypted/CAS store and coordinator primitives. It uses Application-owned
no-backup storage and Android Keystore wrapping, without an Activity or WebView.
Returned stores refuse key access after facade shutdown/drain. This is distinct
from Kotlin v1 `NativeRecords`; migration must preserve original ciphertext and
aliases and use an explicit authenticated handover. No implicit conversion or
missing-key regeneration occurs. Native build identity advertises
`nativeCallbacksProtocol: 1` and `nativeRecordsProtocol: 2`; the adapter refuses
an older native library when C++ callbacks are enabled. Rebuild the CLI, SDK and
both native ABIs together before adoption.

The `product.` prefix is mandatory for `AndroidPlatform::openRecords` and
`LinuxPlatform::openRecords`. Other prefixes return `EINVAL`. The entire name,
including that prefix, must match `[a-z][a-z0-9_.-]{0,63}`. For example,
`product.credentials` is valid; `credentials` and `public.credentials` are not.
This facade restriction is separate from the lower-level `RecordStore` naming
rules. It does not grant access to an older Kotlin namespace or migrate it.

`withApplication(callback)` supplies a scoped `JNIEnv*` and Application context
only during an owned callback or its cleanup. It rejects unrelated threads with
`EACCES`. Never retain either local reference or use an Activity. This supports
calling an existing runtime Kotlin API during an explicit native product
migration; it does not perform that migration or establish account trust.

Record scopes include the installed bundle ID. Provider names and record names
select storage inside that application identity; they do not grant another
bundle access to these records.

## Scoped push recovery from compiled policy

Android and Linux facades expose `discoverPushRegistrations(scope)` and
`unregisterPushIfCurrent(scope, id, revision)` to compiled native code. Android's
Kotlin `NativePush` exposes equivalent native-only methods. There is no general
renderer/diagnostic enumeration route.

A `PushRecoveryScope` requires exact authority, account and handler kind; it
cannot enumerate every account. Invoke it only after the product authenticates
the current account and obtains explicit recovery consent. Discovery returns at
most 16 records containing only `id`, `kind`, `phase` and an opaque `revision`.
It returns no endpoint, transport token, encryption/enrollment keys, grants or
account/target references. It does not reset a lost product registry or destroy
its historical originals. Legacy runtime records acquire separate revision
metadata without rewriting their encrypted registration values.

Conditional cleanup atomically verifies that scope and revision still match.
Renewal, endpoint/lifecycle changes, enrollment updates and ID reuse invalidate
old snapshots (`EAGAIN`). An already absent ID returns false. Local delivery and
retained inbox entries stop before the distributor is notified; a later transport
failure returns `EUNKNOWN` and does not restore local delivery. Server grant
revocation and reconciliation remain product/backend responsibilities. Discovery
fails if the original native registry key is unavailable; do not create new keys
or reset storage to make discovery succeed.

The isolated public fixture is in `test/fixtures/android-runtime/callbacks`.
Portable registry and compiled Kotlin recovery regressions are in
`test/unit/native-callbacks.test.js` and `test/unit/native-push-recovery.test.js`.
Software/host tests do not qualify OS service admission or physical key hardware.

Product adoption tests can retire a shim for registry discovery and revision
fencing. They do not retire account consent, authenticated server enrollment,
historical migration, revocation reconciliation or push payload authorization.
Linux discovery runs in compiled host policy, not a GUI-side remote-host client.

## Unreadable push journal recovery

`AndroidPlatform::inspectPushJournalRecovery()` and
`quarantineUnreadablePushJournal(consent)` are installation-wide native policy
operations. `LinuxPlatform` exposes the same methods in its compiled host. They
address an unreadable runtime transport journal, independently of a lost product
registry. They are not exposed as renderer enumeration or reset routes.

Inspection returns only `revision`, `reason`, `readable` and `recoverable`. It
does not provision a key or return endpoints, grants, tokens or decrypted values.
Recoverable reasons are `missing-key`, `invalidated-key`, `ciphertext-corrupt`,
`journal-format` and `uncertain-write`. Provider unavailability and a readable
journal cannot be quarantined. An unavailable/corrupt recovery selector fails
closed; the runtime does not infer an earlier journal or replace its keys.

Before calling quarantine, the application must obtain explicit consent covering
**every account's local push delivery in this installation**. An authenticated
single-account cleanup must instead use scoped discovery/conditional cleanup.
Both `installationScopeConfirmed` and
`archiveAndStopLocalDeliveryConfirmed` must be true. Pass the inspected
`expectedRevision` and a persisted, opaque `recoveryId` (1–128 ASCII letters,
digits, `_` or `-`). Keep that ID and revision for retries:

```cpp
const auto inspected = backend->inspectPushJournalRecovery();
// Set these flags only after the product's explicit installation-wide consent.
PushJournalConsent consent{inspected.revision, persistedRecoveryId,
                           installationScopeConfirmed,
                           archiveAndStopLocalDeliveryConfirmed};
const auto receipt = backend->quarantineUnreadablePushJournal(consent);
```

Quarantine copies the bounded encrypted snapshot into a private SQLite archive
and selects a fresh journal in the same durable transaction. It preserves the
original files and key aliases. Android captures the raw preference XML and
backup without asking SharedPreferences to repair malformed originals; after a
failed preference commit it also captures cached ciphertext. Fresh state has no
registrations, distributor selection or keys until explicitly configured again.
Old queued work is fenced by its journal identity. Running work is cancelled and
returns `EBUSY`; switching waits until it actually exits. An uncooperative handler
therefore keeps recovery busy. A stale inspection returns `EAGAIN`.

A receipt contains `recoveryId`, the selected `epoch`, and `changed`. Retrying the
same ID/revision after a committed switch returns `changed: false` and preserves
later fresh registrations. Reusing that receipt after a subsequent switch returns
`EAGAIN`. `EUNKNOWN` means the commit outcome requires reconciliation with the
same ID/revision; do not choose a new recovery ID on that error. The runtime never
prunes archives: at most 16 snapshots of 4 MiB each are accepted (`ENOSPC` beyond
those bounds). Recovery-control loss, whole-container corruption and removal of
the application installation remain separate support cases.

This operation sends **no remote unregister** and confirms **no server
revocation**. Private transport tokens may be unrecoverable; access may remain
until a server lease expires. The application/backend owns fresh authorization,
revocation reconciliation and explicit enrollment for each account. Do not import
old grants or claim that historical deliveries/transfers completed.

Android requires matching CLI/SDK/native archives advertising
`nativePushJournalRecoveryProtocol: 1`. The public fault-injection fixture is
`test/fixtures/android-runtime/push-journal`; its driver records exact APK/device
scope. Host coverage is in `test/unit/native-push-journal.test.js` and
`test/unit/android-push-journal.test.js`. Linux adapter acceptance is
`test/scripts/test-native-push-journal-linux.js`. These checks use public data
and do not prove deployed distributor/backend recovery or historical upgrades.
