# Android runtime reliability

This guide documents runtime contracts, configuration and compatibility limits.
Application delivery plans and deployment-specific qualification records belong
with their owning applications. Source implementation and successful builds do
not establish production release or supported-device acceptance.

## Protocol-4 native contracts

The current source adds `oro:authentication`, `oro:launcher` and
`oro:background/deferred`. Native requests bind the window and document,
cancel on navigation/renderer loss/activity stop, settle once and have bounded
capacity/deadlines. Cancellation arriving before its request is retained briefly
so that dispatch ordering cannot resurrect an aborted prompt. Private results
cannot complete into a replacement document. Deterministic native fixtures cover
these boundaries, task retry/coalescing/generation changes and shortcut revocation.

Enable only the features the app adopts:

```toml
[permissions]
allow_biometrics = true

[background]
deferred_enabled = true

[launcher]
enabled = true
static_destinations = "home"

[launcher.static.home]
label = "Home"

[webview]
webauthn_enabled = true
```

Authentication requires Android 11+ and uses authentication-gated cryptographic
operations, with a maximum of 64 keys and 64 KiB per protected value. Keystore2 reports a per-operation timeout as zero; legacy metadata can report
minus one. Positive-duration windows and mismatched authenticator masks are
rejected. This follows the native
[Keystore2 metadata implementation](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/keystore/java/android/security/keystore2/AndroidKeyStoreSecretKeyFactorySpi.java),
whose zero default differs from the older API documentation. The default
strong-biometric policy invalidates keys when enrollment changes. The explicit
`biometric-or-device` policy allows device credential fallback and does not
invalidate on biometric enrollment changes. Invalidation never silently replaces
a key. Apps retain sealed records and provide explicit recovery and sign-out.
P-256 enrollment/sign-in returns a signature over the exact UTF-8 JSON payload,
encoded as Base64 alongside the DER signature and SubjectPublicKeyInfo public key.
The server must authenticate enrollment, compare every application/authority/
account/key/challenge/expiry field, verify the signature, enforce nonce single use
and current authorization, and support revocation. Local unlock does not create
server authority. System authentication requires the user's interaction.

Deferred tasks persist only opaque identities, never credentials or HTTP bodies.
The host app registers `DeferredReadExecutor` implementations with
`DeferredSync.registerReadExecutor` from `Application.onCreate`, including cold
service starts. `authorize` resolves saved identities and verifies current server
IAM; it returns an `AuthorizedDeferredRead` whose `close` releases credentials and
connections. `reconcile` returns a fixed completed/retry/conflict/unauthorized/
unknown outcome. The runtime retains at most 32 records, runs one worker, caps
attempts at five and result time at 60 seconds, and uses Android network/charging/
battery constraints. A blocked executor can occupy the worker after its deadline;
cancellation is cooperative, with independently bounded cancellation workers.
Android controls scheduling, including Doze/reboot restrictions. Force-stop can
remove jobs; status reports interruption and only explicit `resume` recreates
supported work. Cancellation/replacement prevents an obsolete callback from
completing the new record. Queued writes remain unsupported until the application
provides an explicitly approved, idempotent server/application contract.

Native requests, job start and completion share one journal monitor, including
calls through the Java static entry point. Cancellation persists the terminal
state before stopping the active run and scheduled job. A queued completion
cannot reschedule a cancelled or released record, and an old OS-stop callback
cannot stop a newer attempt. If journal persistence fails, the runtime still
stops the active run and reports the persistence failure to the caller.

IPC removes query transport encoding exactly once. Literal percent escapes in
secure-storage values and keys remain bytes of the application payload; UTF-8,
base64 and hex writes must resolve to the same stored bytes. Missing-record
`getItem` reads return `null`, including native IPC error instances. Existing
encrypted records are read without rewriting their contents. Previously altered
bytes require an intact application copy or explicit application recovery.

Launcher shortcuts hold opaque references and routing tokens. Apps resolve
receipts after restoring authentication and recheck exact target/account IAM.
Static entries are generated for the configured installed activity; dynamic and
pinned entries use the actual launcher activity. Removed entries are disabled,
including pinned copies. Receipt reads do not consume them; acknowledge after
resolution or rejection. At most 32 receipts are retained for seven days. User
pinning remains a system confirmation; submission is not proof of pinning. No
shortcut opens an arbitrary executable URL or performs a resource mutation.

Non-OTP `navigator.credentials.get` requests now preserve the supported native
container across platforms. Autofill participation is enabled on interactive WebViews and
can be disabled explicitly with `webview.autofill_enabled = false`.
`webview.webauthn_enabled` opts into AndroidX WebKit 1.14.0 Web Authentication and
Credential Manager 1.6.0 dependencies. Unsupported WebViews report unavailability.
Headless WebViews always disable both Autofill participation and native Web
Authentication UI. A legitimate relying-party authority and app/domain association are still
required; the packaged origin cannot impersonate arbitrary private-IP servers.
Use public fixture accounts for provider qualification and retain the trusted
system-browser authentication path where the authority does not support this
association. No personal password-manager database is read by these APIs.

Unattended push still requires an explicitly selected native transport and
the deployment's authenticated registration/revocation protocol. Existing downstream
infrastructure can host that logic, including private IP deployments; an extra
hosted backend is not inherently required. Network reachability and OS wake-up
transport remain separate requirements. Capability reporting continues to state
that push and arbitrary background JavaScript are unavailable.

## Recovery and window sizing

Renderer termination detaches the affected WebView, invalidates its callbacks
and displays native recovery. Reload requires an explicit user action; there
are no automatic renderer retries. Native startup recovery uses a 30-second
elapsed-time watchdog. A blocked UI thread, ANR or native crash requires
separate operating-system diagnostics.

Root Back uses page history before backgrounding the primary task. Secondary
windows close independently. Android dimensions that fill or exceed their
container use MATCH_PARENT so they follow rotation, insets, keyboard resizing
and multi-window bounds. Smaller explicit dimensions remain fixed. Layout
updates run on the UI thread and ignore detached WebViews.

## Application integration

To keep native startup recovery visible until the application has mounted a
usable screen and loaded its essential styles:

```toml
[webview]
require_ready = true
```

```js
import application from 'oro:application'

// After rendering the usable application surface and loading required styles:
const window = await application.getCurrentWindow()
await window.markReady()
```

Only a top-level application window can acknowledge its own readiness. Hidden runtime service windows use runtime initialization rather than a visible-screen acknowledgement. On other
desktop platforms the acknowledgement succeeds without an Android loading
surface. When the option is absent, Android uses runtime initialization as its
compatibility acknowledgement. Readiness does not authorize private operations.
Once native recovery is shown, a late acknowledgement does not dismiss it;
recovery requires an explicit reload. Reload loads the window's initial
navigation URL, loses unsaved input, and does not resubmit a WebView POST or
replay native IPC calls. Apps must reconcile any uncertain server mutations
before allowing another submission.

Automatic IPC sequences have a per-document namespace so a delayed reply from an
obsolete document cannot resolve a replacement document's request. Callers that
supply an explicit sequence must maintain their own uniqueness. `ipc.parseSeq()`
continues to expose the numeric counter; correlate responses using the full
sequence, including its document/worker suffix.

For notification recovery, restore the session first, then read pending receipts:

```js
import { getResponses, acknowledgeResponse } from 'oro:notification'

const receipts = await getResponses()
for (const receipt of receipts) {
  // Resolve receipt.id and receipt.action against current authorized app state.
  // Handle valid destinations or discard obsolete/unauthorized identities.
  // Acknowledge only after that reconciliation has completed.
  await acknowledgeResponse(receipt.receiptId)
}
```

Each receipt contains `receiptId`, notification `id`, action identity, and
`receivedAt` (Unix milliseconds). Reads do not consume receipts; repeated
acknowledgements are allowed. Other platforms reject these two APIs with
`ENOSYS`. Runtime notification capability is required on Android.

The ledger retains at most 64 responses and 256 outstanding intent tokens for
seven days; capacity eviction can remove older identities. Notification IDs and
action IDs must be opaque strings of at most 256 characters. Native requests cap titles at 4,096 characters, bodies at 16,384, channel/tag IDs at 256, icon/image URIs at 4,096, and vibration at 64 entries of at most 10 seconds each. Malformed action payloads are rejected. Notification
bodies, titles, and arbitrary `data` are not persisted in this ledger. Live
events retain their existing `data` payload, so applications must handle that
payload being absent during catch-up. Live events and a subsequent ledger read
can describe the same receipt: deduplicate using `receiptId` and acknowledge it
after reconciliation. The ledger never establishes an account, origin trust,
IAM authorization, or user consent to a resource mutation.

Old notifications posted by a prior runtime lack receipt tokens and are not
accepted by this routing path. On upgrade, apps must cancel/reconcile old
notifications before upgrading, using the old runtime or app-owned Android
notification manager, and repost current authorized state afterward. Android
notification tags now include the full opaque ID and application tag; cancellation
through the new runtime does not target legacy tags. Backup exclusions do not
remove historical backups or erase existing localStorage credentials. App-owned
resource overrides must preserve the extraction-rule contract if they replace
the generated XML.

## New API adoption and compatibility

Android applications must rebuild the CLI, native runtime, Kotlin sources, and
packaged JS together. Old package trees have no asset-manifest binding and enter
native recovery. The installed CLI version is not evidence that its native
archives were rebuilt. The packaged `oro/internal/runtime-contract.json` and
native Android bridge must both declare protocol 5. Protocol 4 introduced document-owned
native authentication, launcher and deferred-read operations to the presentation,
provider-query, native cleanup and capability methods. CLI, native archives,
Kotlin and packaged JS must be rebuilt together. Protocol 5 adds native password
selection, shared notification presentation and compiled extension dispatch;
older protocol artifacts cannot qualify these contracts. The compatibility gate is
local package integrity, not server authorization or a signing certificate.
Custom Gradle asset tasks must preserve the generated inventory/AAPT filter.
A missing or extra application asset fails closed. The two exact AGP-generated
ART paths, `dexopt/baseline.prof` and `dexopt/baseline.profm`, may be added after
the inventory is bound; each is capped at 16 MiB. APK signing covers those
profiles, and the release verifier records their hashes separately. No other
unlisted asset path is accepted. The asset check runs once per process on
a navigation worker; the native 30-second watchdog still covers the delay.

The default Android manifest now requires explicit sensitive capabilities, e.g.:

```toml
[permissions]
allow_notifications = true
allow_local_network = true
# Enable camera/microphone, location, media or USB only for supported workflows.
```

WebView camera/microphone/geolocation requests require a live visible owner,
a trusted requesting origin, explicit runtime capability opt-in and an existing
Android OS grant. Applications request OS permission from their contextual
workflow before media capture; WebView requests do not silently broaden grants.
Protected media identifiers require explicit `webview.allow_protected_media`.

The Android WebView exceptions are `webview.allow_automatic_windows`,
`allow_third_party_cookies`, `allow_content_access`, `allow_file_access`, and
`allow_mixed_content`, and `allow_protected_media`. Each defaults to false and must be explicitly true.
`webview.bridge_origins` accepts whitespace-separated exact HTTPS origins;
wildcards and credentials are rejected. Packaged origin is trusted automatically;
a configured development origin is trusted only in a debug build. Workers need
a browser-set trusted Origin for IPC XHR. Apps using custom workers/frames must
qualify that contract before distributing an update. App-owned native interfaces
are not secured by the runtime listener automatically.

`oro:network-status` exports `getStatus()` and `onChange(callback)`.
`oro:document-access` exports `list()`, `persist(uri, modes)`, and
`release(uri, modes)`. Persistence never grants access to an arbitrary URI.
`ApplicationWindow.share({ files })` and `navigator.share({ files })` support
Android File objects within the documented bounds. `navigator.canShare()` checks
platform and payload support before opening native UI.

`oro:notification.getDeliveryStatus(channel)` reads channel policy;
`openChannelSettings(channel)` opens system controls after a user action.
Status explicitly reports `pushAvailable: false` and a submission-only delivery
boundary. Runtime channels do not establish server/account authorization.

`ApplicationWindow.setSystemAppearance({ theme, color, insets })` requires Android 11+ and all
three fields. `theme` controls system-bar icon contrast; `color` is `#rrggbb`; the primary surface is saved for the next native activity frame;
`insets` is `native` or `web`. Earlier Android versions reject this managed animation/ownership API with `ENOSYS`; core screen sizing uses a guarded AndroidX/DisplayMetrics path on APIs 26–29. Existing app-owned native inset listeners must
stop before adopting runtime ownership. `oro:window/appearance` exports
`onInsetsChange()` and `onBackGesture()` with unsubscribe callbacks. Back events
observe the runtime native callback; custom activities must forward them and
retain their own correct dismissal priority. IME `end` also covers interruptions;
consume current inset values rather than treating it as proof of full animation.

`oro:haptics.perform(kind, { enabled: savedPreference })` accepts `selection`,
`confirmation`, or `rejection`. The default preference is false.
`oro:diagnostics/runtime.getBuildIdentity()` returns build identity and bounded,
redacted lifecycle timing records. Android-only APIs reject with `ENOSYS` on
other platforms; observations alone do not announce hardware availability.

## Fullscreen, protection, document trees and native cleanup

Fullscreen, content protection, scoped document access and native cleanup are
independent capabilities. Query support and enable only the features used by
the application.

```js
import application from 'oro:application'
import documents from 'oro:document-access'
import background from 'oro:background'
import { getCapabilities } from 'oro:diagnostics/runtime'
import { getStatus as getStorageStatus } from 'oro:secure-storage'

const capabilities = await getCapabilities()
const window = await application.getCurrentWindow()
await window.setSystemAppearance({
  theme: 'system',
  color: '#161616',
  insets: 'native',
})
if (capabilities.nativeFullscreen) await window.setFullscreen(true)
// Call from a user action; observe windowfullscreenchange for native exits.
await window.setFullscreen(false)
await window.setContentProtection(true)

// These URIs must come from the user's document/tree picker result.
const metadata = await documents.stat(selectedUri)
const page = await documents.readDirectory(selectedTreeUri, {
  offset: 0,
  limit: 128,
})
// Use each returned entry.uri directly; never append a filename to a tree URI.
const storage = await getStorageStatus()

// Requires explicit [background] enabled = true and generated manifest entries.
if (capabilities.nativeBackgroundCleanup) {
  await background.register('share-prune')
  await background.schedule({
    kind: 'share-prune',
    delayMs: 86400001,
    requiresBatteryNotLow: true,
  })
}
```

Fullscreen is bound to the live Java Window identity, including queued native
requests. A hidden, replaced or destroyed WebView cannot transfer ownership to a
replacement window with the same index. Native entry requires Android 11+, a
focused foreground activity and explicit managed inset ownership. A native Exit
fullscreen button stays available; Back hides the IME before exiting fullscreen,
and a later Back retains ordinary application/history behavior. Suspension,
hiding, navigation and renderer destruction exit instead of automatically
re-entering. The owner fills its content container during fullscreen. Resize requests update
the frame to restore, and exit restores that requested frame and the previous
system-bar visibility/behavior. This API does
not capture mouse input or emulate document.fullscreenElement. Custom activities
must preserve runtime Back and lifecycle forwarding and coordinate their own
inset/flag owners.

On Android, `window.focus()` shows the requested interactive window, raises its
view and requests WebView focus after the fragment transaction commits. Back,
insets, fullscreen and credential delivery then use the same foreground window
order. Queued focus is discarded if the window is replaced or closed, its
WebView is detached, the activity loses focus, or fragment state is already saved.
Headless windows cannot take interactive focus. Focusing does not open the IME.

For protection before activity views are created, opt in with:

```toml
[window]
content_protection = true
```

This activity-wide baseline is retained by the window API; a window cannot
remove a host/configuration protection flag. Content protection uses FLAG_SECURE for the entire activity while any visible
runtime window requests protection, retaining a flag already set by the host.
It does not protect clipboard contents, shared files, copies, or private values
already exported by the application. Detaching a window releases its runtime
request. Native flag owners outside this API must coordinate updates.

Document queries have two worker slots, no queued provider work, a five-second
result deadline and a 1 MiB response cap. Cancellation is cooperative: a provider
that ignores it can continue occupying a slot after the caller receives timeout;
new work then returns EBUSY instead of creating more threads. Provider cancellation
uses at most two additional calls with no queue, so a blocked cancellation cannot
stall the deadline thread or other callers' timeout responses. Tree pages contain
at most 128 entries and offsets up to 10,000. Cursor order is provider-defined;
restart and reconcile by URI after edits or process loss. Unknown size and
modified-time values remain null. Revoked grants return EACCES. No query grants
new access. Generic `fs.opendir(contentUri)` remains ENOTSUP.

Native background support is intentionally typed: `share-prune` removes only
expired files in the runtime's dedicated share cache. It does not run application
JS, restore credentials, contact servers, continue transfers, or deliver push.
Enabling it adds RECEIVE_BOOT_COMPLETED and a BIND_JOB_SERVICE-protected service.
It uses persisted JobScheduler state, one unique job, one worker, charging/battery
constraints, and at most three native cleanup attempts with Android exponential
backoff starting at 30 seconds. API 34+ uses an isolated scheduler namespace;
earlier versions reserve job ID `0x4f524f01`. Timings are not exact, Android can
delay work, and force-stop prevents execution until relaunch. Scheduling success
is not execution success. Status exposes interrupted state if Android retains no
job; it does not silently recreate an uncertain task. Cancellation can finish
one in-progress expired-file deletion. App-owned/custom manifests must preserve
the explicit service and permission when adopting this feature.

Share filenames require valid Unicode, reject the reserved marker name and
respect Android's 255 UTF-8 byte limit before copying bytes. Share expiry now
uses an immutable marker rather than directory modification time. Partial pruning cannot renew the remaining files' grant lifetime. Existing
unmarked shares from older versions are unavailable through the new provider
and are removed by pruning. Recipients can retain copies or already-open file
descriptors; neither expiry nor cancellation recalls those copies.

Keystore status reads only existing key policy. It never creates a key, decrypts
stored values or includes an alias/key list in diagnostics. A present key does
not prove that every stored value remains readable. Android 11+ exit summaries
contain at most 16 numeric reason/status/time/importance/memory records; raw
process names, descriptions, state summaries and traces are omitted. Each exit
explicitly reports that its artifact identity is unknown; OS history can predate
the currently running build. A reviewed
diagnostics export remains application-owned. Native capability reporting keeps
push, arbitrary background JS, queued writes and pointer capture explicitly false.
Biometric, launcher, native deferred-read and Web Authentication support are now
reported separately using explicit configuration and platform capability checks.

Platform contracts:
[KeyInfo protection metadata](https://developer.android.com/reference/android/security/keystore/KeyInfo),
[historical process exits](<https://developer.android.com/reference/android/app/ActivityManager#getHistoricalProcessExitReasons(java.lang.String,int,int)>),
and [JobScheduler constraints and persistence](https://developer.android.com/reference/android/app/job/JobScheduler).

## Release artifact verifier

`bin/verify-android-release.py` performs read-only checks of an APK against a
trusted signing certificate SHA-256 supplied independently of that APK:

```sh
python3 bin/verify-android-release.py app-release.apk \
  --sdk /path/to/android-sdk \
  --certificate-sha256 TRUSTED_RELEASE_CERTIFICATE_SHA256 \
  --output /tmp/android-release-evidence.json
```

It verifies the APK signature and rejects debug signing, checks non-debuggable,
backup-disabled and cleartext-disabled manifest policy, validates ZIP alignment
with `zipalign -P 16`, inspects every ELF64 PT_LOAD segment for 16 KiB alignment,
checks ELF machine/ABI agreement, recomputes packaged asset hashes, and checks
that every runtime ABI binds the matching manifest digest. It returns a nonzero
status for a failed gate. The report includes public build inputs and certificate
digests, not device credentials. It does not claim TLS pinning, runtime behavior,
accessibility, server authorization, or measured performance.

`applicationInputSha256` separately identifies staged Android native inputs:
Kotlin/Java and native sources/headers, application resources, the main manifest
and manifest-library inputs, Gradle/NDK build files, normalized configuration,
SDK source/API contract, native archives and declared toolchain versions. Build,
application and SDK directory prefixes are normalized before hashing generated
build files and settings. Configuration values are represented by one combined
digest, never returned by diagnostics. The release verifier recomputes the
inventory fingerprint and rejects missing, altered or inconsistent evidence.
Diagnostics return `null` for older or unbound evidence.

The inventory covers staged inputs used to generate the merged manifest; it does
not claim to hash final merged manifests, compiled DEX/ELF outputs, signing keys,
or external compiler inputs introduced by custom build scripts. The APK SHA-256
and trusted signing certificate identify the exact compiled and signed artifact.
An input fingerprint alone does not establish reproducible output or device
behavior.

## Runtime qualification still required

Before device checks, record the emulator engine, image revision, Android API,
ABI, WebView, graphics backend and acceleration mode. Require completed Android
boot and usable system services; an ADB connection alone does not establish
readiness. System-server restarts or System UI failures before installing the
qualification app invalidate that environment's app results. Keep those failures
separate from runtime failures, and do not use software-emulator timings as
physical-device performance evidence.

- Execute supported Android API, WebView, vendor and ABI coverage, including
  activity recreation, repeated renderer failures, low memory and native crashes.
- Measure startup, frame time, memory, battery and long-running resource bounds.
- Qualify provider cancellation, revoked grants, cloud documents, large files,
  reboot, Doze and OS job interruption with isolated public fixtures.
- Exercise biometric enrollment changes, key invalidation, lockout and actual
  device-credential fallback; qualify legitimate Credential Manager and Autofill
  provider workflows with public test accounts.
- Check accessibility, increased text size, reduced motion, both navigation
  modes, multi-window, tablets, foldables and right-to-left presentation.
- Verify trusted production signing, pinned reproducibility, historical upgrades
  and behavior of the intended signed release artifact. Build and structural
  checks alone do not qualify device execution.

## Distribution consistency

`oroc --android-runtime-contract` validates the installed Android source catalog
and required API modules, checks their bridge protocol, and prints their SHA-256
digests along with hashes of the compiled Gradle and manifest templates. Android
project generation uses that same compiled catalog and rejects missing or empty
inputs. The application asset evidence includes this distribution contract and
the native archive inventory; its digest remains bound into the native init shim.

The source installer embeds a fingerprint of CLI-owned sources and headers and
recompiles CLI objects when that fingerprint changes. Package validation compares
it with both the intended source snapshot and the staged CLI sources. A changed
CLI body is detected even when its version, source catalog and template strings
are unchanged. Manually compiled CLIs without this fingerprint reject Android
distribution validation.

Package staging runs `bin/check-android-distribution.js` against the intended
source snapshot. It rejects stale compiled source catalogs, stale compiled
templates, changed packaged Kotlin/API inputs and missing runtime archives. The
resulting `android-distribution.json` also identifies the CLI binary and native
archives. These checks establish the recorded build composition. Trusted signing,
reproducibility and execution of the intended artifact require separate checks.

Run `npm run release:verify-npm -- <version> <commit> <platform.tgz> <node.tgz>
<runtime.tgz>` against the actual packed artifacts. The verifier installs all
three packages into separate local and global temporary prefixes, checks the
installed distribution inventory and CLI contract, and builds a consumer app
through each installed command. It uses its own temporary npm cache and excludes
source-checkout overrides from the child environment. Passing a source-tree CLI
check alone does not establish that npm installed the intended artifacts.

Generated Android native compilation maps project paths to `.` using
`-ffile-prefix-map`. This keeps debug paths from changing the ELF build ID when
the same unsigned application is built in different project directories. Verify
reproducibility with independent project outputs and compare complete APK bytes,
then compare ZIP entry contents if they differ. Record the SDK inventory, native
archives, JDK, Android/NDK, Gradle and dependency inputs for both runs. Matching
builds on one provisioned host do not qualify clean-host dependency resolution,
other ABIs or historical signed upgrades.

`oro:diagnostics/runtime.getBuildIdentity()` includes at most four
`startupTimings`: native library loading, native allocation, configured extensions
and process lifecycle observation. Durations use a monotonic clock and fixed phase
names, with matching Android trace sections. They exclude OS process launch,
class verification before the phase, renderer startup and application mounting;
they must not be treated as complete cold-start latency. Empty extension
configurations skip extension lifecycle callback registration.

## Explicit native password selection

Framework password selection is separate from WebView Autofill and WebAuthn.
Enable it explicitly:

```toml
[credentials]
enabled = true
```

`oro:credentials.getStatus()` queries support without reading credentials.
Provider results can arrive before the requesting Activity resumes. A selected
result stays in native memory for at most five seconds while that same Activity
resumes. Delivery rechecks the requesting document token, window, origin,
attachment and foreground ownership. Cancellation, navigation, activity stop
and the original request deadline still discard the result. No chooser is
reopened automatically, and no selected value is logged or delivered in the
background.
`selectPassword({ username, signal })` opens the Android 14+ chooser after a user
action. Automatic selection is disabled. An optional username restricts the
result to that exact identity. The result belongs only in the requesting private
interactive form; it is never saved, submitted or logged by this API. The provider
determines the app association; this API does not assert arbitrary remote origins.
Cancellation, navigation, renderer loss, backgrounding and the native deadline
release the request. Cancellation rejects `ABORT_ERR`, no available credential
rejects `ENOENT`, and unsupported platforms/configurations reject `ENOSYS`.
Provider fill/save/update and legitimate app/domain association need their own
acceptance using public test accounts.

## Native notification presentation

`oro:notification.configureChannels()` registers at most 32 versioned channel
identities with immutable name, importance, sound, vibration and badge defaults.
Reusing an ID with changed defaults rejects `EEXIST`. Current user channel and
DND settings remain authoritative. Sound defaults support `default`, `none` or
a packaged `raw/name` resource.
The `default` identity selects the built-in application channel and cannot be
registered as a custom channel.

`oro:notification.submit()` takes an opaque ID, title and existing channel plus
optional body, tag, actions, group key, explicit summary, group alert behavior,
packaged drawable small icon and redacted public lock-screen replacement. Apply
the same group alert policy to its members. The runtime does not generate or
reconcile group summaries automatically. Notifications retain private visibility;
the caller must supply public-safe replacement text. Submission returns
`{ id, submitted: true, presentationObserved: false }` and does not prove display,
sound or user attention.

`oro:notification.cancel(id, { tag })` cancels the exact submitted identity.
Cancellation does not create a dismissal receipt or acknowledge an application
event. Native receivers can use `NativeNotifications.cancel(Context, id, tag)`
without an Activity or WebView.

Compiled receivers and deferred executors can call
`NativeNotifications.configureChannels(Context, JSONArray)` and
`NativeNotifications.submit(Context, JSONObject)` without starting a WebView.
These are the same implementation used by legacy notification submission and
the new JavaScript API. They retain the existing opaque intent tokens and
`getResponses()` / `acknowledgeResponse()` ledger. Applications still authenticate
destinations and deduplicate foreground, catch-up and transport events. Transport
receipt is separate from presentation and application acknowledgement.

## Compiled native extensions

Extensions implement `oro.runtime.app.NativeExtension`. Declare their source
files and classes, manifest fragments and fixed Maven dependencies explicitly:

```toml
[android]
runtime_protocol = "5"
sources = "android/ExampleExtension.kt android/ExampleReceiver.kt"
resources = "android/res"
extensions = "org.example.ExampleExtension"
manifests = "android/extension-manifest.xml"
application_theme = "@style/Theme.Example.Startup"
usb_notification_icon = "@drawable/example_usb_notification"
# Optional fixed group:artifact:version coordinates; no dynamic or snapshot versions.
# dependencies = "org.example:feature:1.0.0"
```

Lists accept whitespace or commas. At most 32 extensions/manifests and 64 unique
dependency coordinates are accepted. Snapshot version labels, including mixed-case
spellings, are rejected because the same coordinate can resolve to changed bytes.
Runtime-owned WebKit/Credential Manager
dependencies cannot be overridden; WebKit has a strict version requirement.
Each manifest fragment becomes a manifest-only Android library merged by AGP,
so receivers, services and package visibility queries need no generated-XML edits.
Use fully qualified component names and explicit export/permission boundaries.
The generated application manifest retains higher merge priority, including its
backup policy. Qualify the final merged release manifest.

The resources directory is relative to the application project, including when
the CLI changes directory during generation. Nested resource folders and Android
qualifiers are copied; nested symbolic links and non-regular files are rejected.

`application_theme` selects a packaged style for the main application, including
the first Activity frame. Omit it to retain `Theme.AppCompat.DayNight`. Custom
styles must inherit an AppCompat theme when using the standard Activity. Put the
style in `[android] resources`; a separate manifest library cannot resolve styles
that exist only in the final application. The setting is rendered directly in
the main manifest and does not require generated-output edits. An Activity with
its own explicit theme continues to use that theme.

`usb_notification_icon` selects a packaged drawable for the USB service's small
notification icon. It is compiled as application metadata and loaded by the
service through PackageManager. Omit it to retain the system information icon.
Both references are bounded and validated; missing resources fail Android
resource linking. These settings do not grant permissions or start USB services.

Compiled receivers and executors can call
`DeferredSync.request(Context, action, JSONObject)` for `support`, `enqueue`,
`list`, `status`, `resume`, `cancel` and `release`, without an Activity or WebView.
This uses the same validation, journal, capacity and generation checks as
`oro:background/deferred`. `support` and `list` return a JSONObject and JSONArray,
respectively; the other operations return a task JSONObject. Use the existing
opaque reference fields for enqueue; credentials and request bodies are rejected.
Register the native executor from Application startup so it is available in a
cold service process. Authorization is checked by that executor at execution,
including native-originated enqueue. Native access does not add periodic policy,
queued writes or generic background JavaScript.

The CLI generates direct constructor references to configured extension classes,
so missing classes and interface/constructor mismatches fail compilation. Startup
runs from `Application.onCreate`, including cold service/receiver processes.
Activity creation/resume/stop/destruction, launch-intent and legacy activity-result
hooks are forwarded without a custom Activity subclass. Register modern Activity
Result launchers during the activity-created hook. Intent hooks receive untrusted
Android inputs and must validate their action, sender and payload independently.

`oro:native.request(name, action, payload, { signal })` uses the existing trusted
main-frame/document-token route, capacity/deadline/result bounds and cancellation
on navigation, renderer loss and activity stop. Extensions must implement their
own feature and server authorization. `NativeExtensionCall.onCancel()` installs
one cleanup callback; late registration runs immediately after completion.
Cleanup also runs after success, and callback exceptions do not crash the runtime
or prevent completion. Work must release owned resources and cooperate with
cancellation. Extension exceptions expose fixed error codes rather than private
exception text. No executable path, class name or JavaScript closure is loaded
from a request.

Cancellation identities include the originating document token, so a retained
cancel-before-start record cannot cancel a later document's request with the same
ID. Completed requests remove their scheduled deadline entries immediately.
Password and biometric operations use the same late-safe cleanup registration;
cancelling a system prompt releases its busy state even if cancellation throws.

Opt-in IPC debug traces report command or sequence metadata. They omit request
queries, payloads, buffers, event contents and native results, including passwords
and secure-storage values. Application code must apply the same restriction to
its own logging and diagnostic exports.

Extension startup failures stop application initialization with a fixed error
message and no chained private exception. Request and activity-hook failures do
not expose exception text. Configured extensions remain trusted compiled host
code; they are responsible for their own authorization and resource ownership.

## Native HTTPS and durable approved pins

Compiled receivers and deferred executors can call
`oro.runtime.app.NativeHttps.request(context, url, method, headers, body,
cancellation, timeoutMs, maximumResponseBytes)` from their worker thread. It
returns a status and bounded byte array, or `NativeHttpsFailure` with a fixed code.
The transport does not perform application authorization or persist credentials.

Android `oro:tls.setWebViewTlsPins` and `NativeTlsPolicy.replace(context, value)`
use one authenticated policy in `noBackupFilesDir`, protected by an Android
Keystore HMAC key. A cold native service reads that policy without an Activity or
WebView. Corrupt records and unavailable keys fail with `ETRUST`. Replacing a
policy closes active native connections; requests also recheck policy before
sending and before returning results. Explicit `NativeTlsPolicy.revoke(context,
authority)` records a deny entry for the exact authority, including public-CA
certificates. Replacing the policy is an explicit approval decision; clearing all
pins restores Android's default CA and hostname validation.

Pin tokens hash the complete leaf certificate DER, matching WebView behavior.
An explicit host:port entry takes precedence over a host entry. Existing host
entries apply to all ports; use explicit authorities for server approval. An
approved matching leaf identifies that authority, including self-signed or
name-mismatched certificates. No process-wide TLS factory or hostname verifier is
changed. Native requests reject credentials in URLs, fragments, cleartext,
redirects and header injection. Trust selection, certificate approval, grant
scope, server IAM and rotation decisions belong to the application.

Each request allows 1–60,000 ms, at most 1 MiB of request/response data, 64 headers
and eight concurrent connections. An absolute timer disconnects the connection;
connect/read timeouts also apply. The caller supplies a dedicated
`CancellationSignal`, which closes the connection. Failures expose `EINVAL`,
`ENOSPC`, `E2BIG`, `ETIMEDOUT`, `ECANCELLED`, `ETRUST`, `EREDIRECT` or `EIO`, without
provider exception text, URLs, tokens or response bodies. Native task records
continue to contain opaque identities only. TLS policy is shared with WebView;
socket TLS retains its separate existing pin API.

## Linux native IPC

The core archive and WebProcess helper retain the same bridge layout, including
the opaque WebKit context pointer. Native replies execute through the configured
bridge dispatcher. Streamed events read their queued body from the runtime that
created it, including delivery to workers; a missing response cannot consume an
entry from the other transport.
Queue consumption removes an entry atomically before delivering its reply.
Enqueue, consumption and diagnostic snapshots use the same context lock.

Filesystem watchers deliver the first change after startup without waiting for
the debounce interval. The interval applies after the first delivered event.

Desktop native extensions compile generated objects into the project's platform
output, with separate objects for each extension and source. The SDK and source
directories can be read-only. Each build recompiles against its current
configuration, headers and compiler flags, including the embedded configuration
shim. Supplied object files and static archives remain inputs and are retained
after production builds.
