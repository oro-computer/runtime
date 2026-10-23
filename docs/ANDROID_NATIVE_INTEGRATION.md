# Android native integration

Use the standard runtime Activity and a compiled `NativeExtension` for product
behavior. Oro owns document cancellation, Android navigation, scheduling,
transport, encryption and notification presentation. Your extension owns account
authorization, server enrollment, payload interpretation and product policy.

These APIs are additive to Android bridge protocol 5. Rebuild and distribute
the CLI, native sources and JavaScript API together. Inspect
`oro:diagnostics/runtime.getCapabilities()` and each module's support response
before offering the corresponding workflow. Capability support does not prove
server enrollment, OS execution or delivery.

| Requirement | Runtime API | Application supplies |
| --- | --- | --- |
| Modal/router Back | `oro:window/back.setHandler()` | Which modal, drawer or route consumes Back |
| Recurring catch-up | `oro:background/deferred.enqueue()` with `intervalMs` | Compiled read executor and fresh authorization |
| Revocation/cleanup | `enqueueMaintenance()` | Explicit retry-safe native executor and durable reconciliation |
| Encrypted push | `oro:push` and `NativePushHandler` | Authenticated server enrollment, account binding and payload policy |
| Notification presentation | `oro:notification` / `NativeNotifications` | Severity, channels, public text, groups and authorized destinations |
| Saved credentials | `oro:authentication`, `oro:credentials`, `oro:secure-storage` | Record formats, account context and historical migration |
| Appearance/layout | `setSystemAppearance()` and `webview.text_zoom` | Theme choice, one inset owner and responsive application layouts |

## Configuration

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
