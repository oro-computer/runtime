# Linux native integration

Common workflows must not require application native adapters. The
[declarative workflow API](DECLARATIVE_WORKFLOWS.md) currently has an Android
backend; its Linux backend is runtime implementation work still outstanding.
The compiled integration below remains optional for genuinely custom protocols
and behavior outside supported runtime features.

Linux applications can use encrypted records, compiled extension calls, finite
and continuous work, deferred reads/maintenance, freedesktop notifications,
UnifiedPush and launcher destinations. Rebuild the CLI, SDK sources, desktop
objects, native archive and JavaScript package together. New APIs cannot add a
native backend to an older binary.

## Project configuration and discovery

Keep `oro.toml` at the project root. Run the CLI there or pass
`--config /absolute/project/oro.toml`; paths are relative to that root. Enable
only facilities the application adopts, augmenting its ordinary configuration:

```toml
[foreground]
enabled = true
continuous_enabled = true

[background]
deferred_enabled = true

[push]
enabled = true
provider = "unifiedpush"
# Optional explicit service; otherwise select a discovered distributor in the UI.
distributor = "org.unifiedpush.Distributor.Example"

[launcher]
enabled = true

[meta]
application_protocol = "example-app"
```

This does not select a service-worker mode or start background JavaScript.
Inspect `oro:platform` capabilities and each feature's support/status before
offering operations. `implemented` identifies compiled code, not permission,
an unlocked secret store, a notification server or a working distributor.
Protected keys use trusted hardware PKCS#11 providers, including TPM2, as
described below. Ordinary Secret Service storage is not an authentication-bound
private-key provider. Password selection uses its separate explicit chooser.

## Native service hosting

The default remains application-process execution. For work independent of a
renderer or GUI process, opt into a compiled native session service:

```toml
[linux.native]
service_enabled = true
sources = "native/startup.cc"
initializers = "initialize_product_native"
```

Paths are relative to the directory containing `oro.toml`. At most 16 distinct
project-contained C++ files and 16 C initializer names are accepted. Absolute
paths, escapes through symlinks, missing files and malformed names fail the
build. Register handlers in the initializer, without launching automatic writes:

```cpp
#include "runtime/native/linux_platform.hh"

extern "C" void initialize_product_native(oro::runtime::Runtime* runtime) {
  const auto backend = oro::runtime::native::LinuxPlatform::fromRuntime(*runtime);
  backend->registerExecutor(true, "product.monitor", ownedMonitoringExecutor);
  backend->registerPushHandler("product.alert", ownedPushHandler);
  backend->registerDeferredRead("product.reconcile", ownedReadExecutor);
}
```

The CLI compiles these files into the application and generates its startup
entry. A loaded WebView extension alone cannot initialize a cold service.
The same installed application binary handles `--oro-native-service` before GTK
initialization. It starts no window, WebView, JavaScript or arbitrary executable.
Interactive password selection and Back remain in the foreground GUI.

Native host policy can recover a lost application-side push registry through
the scoped, redacted discovery and revision-fenced unregister methods described
in [Native push recovery](ANDROID_NATIVE_INTEGRATION.md#scoped-push-recovery-from-compiled-policy).
These methods require product authorization and consent; they are not exposed
as renderer enumeration or implicit cleanup.

The package includes D-Bus activation files for `<bundle>.NativeRuntime` and
`<bundle>.NativePush`, plus `<bundle>-native.service` under systemd user units.
Install the package so the activation paths resolve to its installed binary.
For an explicitly enabled login host, run:

```sh
systemctl --user daemon-reload
systemctl --user enable --now org.example.Product-native.service
```

Use the actual configured bundle identifier. Packaging does not enable a unit
or turn on user lingering. Login hosting needs a session bus and an unlocked
Secret Service; boot before user login is not promised. systemd can restart the
host after failure, and cold UnifiedPush delivery can activate it through D-Bus.
Neither mechanism restarts interrupted foreground work or reconciles unknown
writes. Deferred interruption still requires explicit reconciliation/resume.
Distributors must support activation and acknowledgement/retry; retained server
ciphertext is subject to that server's retention policy.

Native GUI operations delegate to the service through HMAC-authenticated
requests. The secret stays in an OS-key-encrypted native record; public D-Bus
access and bus-name ownership grant no operation authority. The host checks UID,
the executable inode, a fresh host generation, request age and a bounded replay
set. Responses bind the request nonce and signature. Stale installed binaries,
missing keys and unavailable services fail explicitly; there is no local journal
fallback. Cancellation uses a separate signed method, outside the work queue.
These checks protect the native transport, not product IAM or compromised
trusted native code. Product handlers still reauthorize every exact identity.

Notification actions commit their bounded receipt before launching the GUI.
Only the receipt UUID crosses the activation URI; resolve retained receipts and
reauthorize after login. Dismissal and native Stop controls do not launch the GUI.
The user unit stops only the host process so a separately opened GUI survives
host shutdown. Linux desktop attention, actual cold product routing, package
upgrades, login/reboot policy and release deployment need OS qualification.

## Explicit saved-password selection

`oro:credentials.selectPassword()` shows a native modal with at most 64 Secret
Service items whose `application` attribute exactly matches the bundle and
whose `username` matches any requested filter. The search loads metadata only;
the selected item is then unlocked and read. Selection never lists other app
items, installs WebKit Autofill, saves a password or authenticates a server.
Lookup attributes are provider metadata, not attested application identity.

Enable `[credentials] enabled = true`. Cancel, navigation, external focus loss,
hidden/destroyed windows and the original deadline discard results. The runtime
recognizes its own modal while it owns focus, then allows up to five seconds for
the parent to resume. Locked-provider prompts and additional Secret Service
implementations need separate qualification. Automatic browser autofill and
Android password-provider interoperability are separate features.

## Desktop Back

`oro:window/back` handles Alt+Left and dedicated Back keys. Native fullscreen
exit takes priority; otherwise the current document has 750 ms to consume the
request. An unhandled request uses WebKit history, closes a secondary window or
minimizes the root. Escape and ordinary editing keys retain native GTK/IME
behavior. Navigation and renderer loss invalidate the handler; focus loss
cancels the pending request without removing its registration.

The older `oro:background` entry point cleans Android share-provider files only.
Linux has no corresponding Android FileProvider cache, so that operation is
reported as platform-restricted. Use registered Linux deferred work for actual
application reads and explicitly retry-safe cleanup. Android gesture exclusion,
pinning, animated insets and channel/group rules likewise remain OS-specific;
they are not silently emulated as Linux guarantees.

## Native registration and lifetime

The SDK ships `src/runtime/native/linux_platform.hh`. A trusted C++20 integration
with the application's `Runtime&` registers callbacks during native startup:

```cpp
using namespace oro::runtime::native;
const auto backend = runtime.services.platform.nativeLinux();
backend->registerExecutor(false, "product.read", ownedFiniteExecutor);
backend->registerExecutor(true, "product.monitor", ownedMonitoringExecutor);
backend->registerExtension("product.metadata", ownedMetadataHandler);
backend->registerDeferredRead("product.reconcile", ownedReadExecutor);
backend->registerDeferredMaintenance("product.cleanup", {"cleanup"}, true,
  ownedRetrySafeMaintenanceExecutor);
backend->registerPushHandler("product.alert", ownedPushHandler);
backend->initializeNativeServices();
```

An ordinary compiled C++ extension can include
`"runtime/native/linux_platform.hh"` and obtain the same backend with
`LinuxPlatform::fromExtension(context)` inside its initializer. Load it through
`oro:extension` with `allow: ['context_get_router']`. The helper avoids including
the runtime's internal dependency graph. It pins already-loaded extension code
until process exit, so document replacement or extension unloading cannot unmap
registered callback targets. Deinitializers must still cancel/drain work before
disposing captured application resources. Changing native callback code requires
restarting the process. At most 32 modules can use this facade per runtime.

`backend->openRecords("product.credentials", identity)` opens the native-only
application record database using the runtime's OS key provider. Run this on a
worker, after resolving exact authorized identity references. Product namespaces
must start with `product.`. These C++ v2 records do not automatically migrate
Android Kotlin v1 or legacy application storage.

These callback names represent application-owned compiled functions, not
executable paths, JS closures or configuration keys. Callbacks must own captured
resources, reauthorize exact opaque authority/account/target references and
observe `ExecutionCancellation` during bounded I/O. Keep runtime-dependent
resources alive until workers drain. Do not synchronously wait for another
operation on the same request queue or invoke shutdown inside its callbacks.

`LinuxPlatform::shutdown()` rejects new requests, cancels work and drains workers
before their runtime resources are destroyed. Standalone `ExecutionController`
and `DesktopPush` integrations call `shutdown()` then `drain()` themselves.
OS secret-store calls and uncooperative callbacks can block shutdown; there is
no general thread-termination guarantee. Cancelled workers retain their slots
and exclusive journal leases until they exit. See
[native records](NATIVE_PLATFORM_PARITY.md#native-encrypted-records-and-operation-coordination)
for encrypted CAS, key-loss recovery and migration.

There are at most 32 admitted platform requests and 512 KiB per public result.
Compiled extension requests have a 90-second cooperative deadline. Interactive
calls require a visible, focused, non-headless top-level `oro://<bundle>` document.
An isolated WebKit world supplies its private proof. Navigation, focus loss and
renderer termination cancel pending calls; late results are discarded.
Same-origin code controlling the top-level application is already trusted.
This is not a sandbox for compromised app code or hostile native extensions.

## Execution and durable outcomes

`oro:foreground` and `oro:foreground/continuous` each have one native executor
slot and 16 retained task records. Finite duration is 1 second through 30 minutes;
continuous work has no duration cap. Admission requires explicit user initiation,
notification permission and an action-capable freedesktop notification server.
Fixed public notification text includes Stop. Display and attention remain OS
policy. Stop signals the exact generation directly, even when the normal request
queue is full, then records its outcome.
Notification-server disappearance, session-bus loss or removal of the running
control signals cancellation too. Page notification calls cannot replace or
cancel reserved `oro.runtime.*` controls. These checks cannot detect every
desktop policy that silently suppresses presentation.

Admission is not completion. Executors assert `completed`, `unauthorized` or
`unknown`; applications own protocol verification. Stop, deadline, lost replies
and process loss can leave uncertainty. Inspect retained IDs before retrying,
reconcile external state and explicitly release terminal generations. Work never
restarts automatically.

`oro:background/deferred` retains at most 32 tasks and five attempts per cycle,
with exponential retry beginning at 30 seconds. Attempts have a 60-second
cooperative deadline. Periodic reads use 15-minute through seven-day intervals
and bounded flex windows. Maintenance permits only separately registered,
explicitly retry-safe cleanup/revocation. Arbitrary queued writes are rejected.
Every attempt reauthorizes. Interrupted tasks require reconciliation and explicit
`resume`; reopening does not silently replay them.

Constraints use `GNetworkMonitor` availability/metering and sysfs battery state,
with 15% as the low-battery threshold. A host without a battery is treated as
mains powered; malformed present battery data fails the constraint. These are
local admission hints, not server reachability or guaranteed execution times.
Suspend, clock changes, keyring locks and OS scheduling affect execution.

**Execution belongs to the configured native process.** By default this is the
GUI application. With native service hosting enabled it is the separate session
host. Closing the GUI then leaves hosted work intact. Stopping the host cancels
work; restarting it preserves uncertainty and does not silently replay jobs.
Login/reboot hosting requires installing and explicitly enabling the user unit.
There is no JavaScript keepalive or universal pre-login/background guarantee.

## Notifications and routing

The named exports of `oro:notification` support `configureChannels`, `submit`,
`cancel`, `getDeliveryStatus`, `getResponses` and `acknowledgeResponse`. Linux
channel IDs are immutable application policies, separate from OS channels.
The desktop-entry hint and default themed icon use `build.name`, matching the
packaged desktop entry and icon names.
They cannot override DND, audio routing or desktop preferences. Sound supports
`default`/`none`; vibration and Android raw resources reject `ENOSYS`. Badges
are not guaranteed.

IDs/tags replace exact owned notifications. Silent notifications and replacements
without `renotify` request sound suppression. The shared API rejects Android
group summaries and non-`all` group alert policies because freedesktop has no
equivalent guarantee. Applications must design Linux grouping explicitly.
Submission proves neither display, audible attention nor alarm acknowledgement.

Linux cannot enforce Android lock-screen visibility. Supply only public content.
If `publicVersion` is supplied, **its title/body replace private content on every
desktop surface**, including when unlocked. Private title/body never reach the
notification server in that case. The returned `visibilityPolicy` states this
behavior.

Only known actions from the current notification server become receipts. At most
64 receipts are retained for seven days, without notification text or arbitrary
payloads. Saved receipts survive restart; receiving new actions requires the live
notification owner. A configured native host retains that ownership while the
GUI is closed and launches the GUI after an action receipt is committed. A killed
host cannot promise notification-server action replay. Restore authorization and
resolve deleted/revoked targets before opening; acknowledge after reconciliation.

## UnifiedPush

The connector implements the
[UnifiedPush D-Bus API](https://unifiedpush.org/developers/spec/dbus/) through a
private connection and `<bundle>.NativePush` service. Discover installed or
activatable `org.unifiedpush.Distributor.*` services with
`push.getDistributors()`, then call `push.selectDistributor({ distributor })`.
Configuration or `UNIFIEDPUSH_DISTRIBUTOR` can select explicitly. Multiple
unselected providers reject `EAGAIN`; unregister existing subscriptions before
changing provider.

Private P-256 keys, tokens, subscriptions and the encrypted inbox remain native
and use OS-backed encrypted records. Incoming peers must own the selected
distributor name and present its exact token. The connector decrypts authenticated
single-record `aes128gcm` Web Push using
[RFC 8291](https://www.rfc-editor.org/rfc/rfc8291.html). Malformed records and
authentication failures never reach the handler.

Limits are 16 registrations, 64 pending messages, 4096 encrypted bytes per message,
seven-day retention, five handling/enrollment attempts and a bounded 256-entry
duplicate ledger. Shared record-store byte limits also apply; capacity errors
never acknowledge unretained input. Do not log enrollment material.

Enroll the endpoint/keys with the exact authenticated server, then acknowledge
its generation. Rotation sets `enrollmentPending` and emits `pushchange`; stale
acknowledgements reject. Handlers authorize every delivery and tolerate duplicates
across interruption. Local unregister does not prove remote revocation. Server
IAM, trust, replay policy, severity and routing remain application responsibilities.

`initializeNativeServices()` restores transport registrations and drains retained
input while the process runs. A packaged native host supplies the connector's
D-Bus activation entry for cold transport startup. The support descriptor keeps
`coldActivationConfigured: false` until deployment is independently qualified;
configuration alone does not establish an installed activation path.
Login/reboot, suspend and production enrollment need separate acceptance.

For local Linux development, run the following in separate terminals:

```sh
oroc push server
oroc push distributor --platform=linux --server=http://127.0.0.1:8787 --allow-loopback-http
```

Add `[push] development_allow_loopback_http = true` only in the development app
configuration. Literal loopback HTTP is an explicit exception; LAN HTTP and
DNS aliases remain rejected. Android distributors continue to require HTTPS.
Select `org.unifiedpush.Distributor.OroDev` through `oro:push` or set
`[push] distributor` to that name. The default command still builds the Android
APK; Linux mode needs no Gradle or Android SDK.

The Linux distributor runs in the foreground until interrupted. It polls ntfy
every two seconds, backs off to 30 seconds on failure, and encrypts registrations
and pending ciphertext with an OS Secret Service key. It retains at most 32
registrations, 32 pending messages of at most 4096 bytes each, 1024 IDs for the
server's replay boundary second and seven days of pending messages. The native
journal also has a 256 KiB aggregate limit. Saturating the deduplication window
pauses intake instead of evicting IDs needed for progress. It rejects redirects and oversized server
frames; full storage pauses intake before advancing the cursor. Only a matching
connector acknowledgement removes a pending message. Offline applications do
not block delivery to other registrations. Server retention bounds recovery from
distributor outages. Anonymous development topics do not establish production
IAM, VAPID deployment policy or authenticated enrollment.

HTTPS uses system trust unless `--ca-cert` supplies an explicit PEM CA database.
The override is local to this distributor and replaces its system database;
hostname verification remains enabled. It does not change OS trust settings.

## Desktop integration

`oro:launcher.replace` publishes a separate owned desktop entry with at most 16
actions under `$XDG_DATA_HOME/applications`, preserving the installed app entry.
Actions contain opaque IDs/generations; account/target bindings and receipts are
encrypted. Changed publications invalidate old generations, including copied
entries. Identical publication retains its generation. Private temporary files,
synchronization and atomic rename protect updates. A failed file publication
after journal commit returns `EUNKNOWN`; repeat identical publication to reconcile.
Other files and symlinks are not overwritten.

The executable accepts untrusted routing hints into a 32-entry, seven-day receipt
ledger. It never authorizes a target. Read/acknowledge after sign-in and current
target resolution. `pinSupported` is false; `pin()` rejects `ENOSYS`. Desktop
environments decide whether to expose
[desktop actions](https://specifications.freedesktop.org/desktop-entry/latest/extra-actions.html).
Publication/intake tests do not establish visible menus, pin confirmation or
the full installed cold/warm routing path.

`oro:network-status` exposes connectivity, metering, captive-portal state and
changes from `GNetworkMonitor`. Transports are empty with
`transportDetailsAvailable: false`; `serverReachabilityKnown` is false. An online
event proves neither SSH/RDP/SPICE reconnection, fresh IAM nor completed transfers.

WebKitGTK already incorporates GTK font DPI into page scaling. Oro adds no second
multiplier. Public X11 checks cover initial 150% DPI, live change to 100%, reload
and renderer replacement without accumulated scale. They do not qualify every
compositor, monitor, assistive input or earliest painted pixel. Android's
`webview.text_zoom` policy remains separate.

WebKit process termination cancels pending document operations and shows a native
recovery panel offering explicit reload for an owned app URL. It does not reload
automatically or claim remote rollback. After reload, restore authorized state
and reconcile unfinished operations. Full process loss follows app startup.

## Verification

After rebuilding the native archive and dependencies:

```sh
node test/scripts/test-native-linux.js
node test/scripts/test-native-linux-ui.js
node test/scripts/test-native-service.js
node test/scripts/test-native-credentials.js
ORO_TEST_NATIVE_CLI=/absolute/paired-sdk/bin/oroc \
  ORO_TEST_NTFY_BIN=/absolute/server-enabled/ntfy \
  node test/scripts/test-native-distributor.js
```

`ORO_TEST_NATIVE_RUNTIME_ARCHIVE` selects an isolated rebuilt archive. Checks use
current source APIs, a public keyring, isolated buses, mock notification/distributor
servers and real GTK/WebKit under Xvfb. They exercise encryption, owner/token
rejection, retries, rotation, durable state and explicit renderer recovery. They
do not qualify physical attention, production deployment or a matching packaged
CLI/SDK without separate artifact acceptance.

`.github/workflows/native-parity.yml` runs portable records/execution and Linux
D-Bus/Web Push regressions, real isolated password selection and Linux native
source/unit generation. Run through `act` in a copied workspace as described
in [native platform parity](NATIVE_PLATFORM_PARITY.md). Native Apple, Windows and
mobile OS qualification remains separate.

## Protected keys: TPM2 and hardware PKCS#11

Enable `[permissions] allow_biometrics = true`. The default `biometric` policy
remains unsupported on Linux: a PIN does not attest biometric authentication.
Use explicit `{ policy: 'biometric-or-device' }` only when the product accepts
a key-provider PIN or the provider's protected authentication path. This is not
the desktop login PIN, password or a server authentication result.

```toml
[authentication.linux]
provider = "auto"
rsa_bits = "2048"
# Optional trusted absolute module path; otherwise discover installed p11-kit modules.
# module = "/usr/lib/x86_64-linux-gnu/pkcs11/libtpm2_pkcs11.so"
# token_serial = "exact-serial"
```

`auto` prefers a unique eligible TPM2 provider, then a unique hardware PKCS#11
token. `tpm2` and `pkcs11` restrict selection to that family. Multiple eligible
tokens return `EAGAIN`; select a trusted module and/or token serial in native
project configuration. Install and initialize the provider outside the app: for
TPM2 this is tpm2-pkcs11 with its TPM access prerequisites; for another device,
use its vendor module registered with p11-kit or an explicit module path. Never
put PINs in configuration, environment variables or JavaScript. The native
masked prompt collects a fresh PIN per operation; protected-path providers use
the device controls. An incorrect PIN is not retried automatically.

The trusted provider must advertise a hardware slot, P-256 signing and/or
RSA-OAEP-SHA256 wrapping, with token-resident, local, sensitive, non-exportable,
immutable, noncopyable private keys and `CKA_ALWAYS_AUTHENTICATE`. The runtime
checks these attributes at every use and requires context-specific login after
initializing each signature or unwrap. Unsupported attributes/mechanisms fail
explicitly; the runtime does not weaken the template or fall back to software.
Hardware flags are provider assertions, not hardware attestation. Use only
trusted modules. Some devices expose only one supported operation family; inspect
`signingAvailable` and `sealingAvailable` and still handle operation errors.

Long-lived private keys stay in the provider. Linux schema 2 secret records
contain an ephemeral AES-256-GCM key wrapped with RSA-OAEP-SHA256, a provider/key
binding, IV and ciphertext. The native worker verifies a user-authenticated
unwrap even when sealing, then erases transient key buffers. AES keys briefly
exist in native memory; this differs from Android's authenticated Keystore
Cipher. Preserve every field of the returned sealed record. Records are not
portable across platforms or replacement hardware. P-256 challenge signatures
use the same exact application/authority/account/key/challenge/expiry payload
as Android and are locally verified before returning. Server enrollment, nonce
single use, account authorization and recovery remain application/backend work.

Key object IDs hash the Linux user ID, full application bundle ID, opaque
application key ID and key kind. Different bundles remain distinct even on the
same provider. Two installations with the same bundle intentionally share an
application identity. The shorter `Oro:` label is descriptive; lookup uses the
full key object ID. This namespacing does not create an OS sandbox between
programs running as the same Linux user.

At most 64 key identities are retained in a separately encrypted native index.
Saved keys are pinned to module identity, token serial hash and public-key
fingerprint, and never silently switch providers or regenerate. Missing or
changed keys fail with `EKEYREVOKED`, missing hardware with `ENODEV`. Interrupted
creation/removal leaves an explicit unfinished phase (`EUNKNOWN` on reuse);
`removeKey` can complete retirement before the application deliberately creates
a replacement and re-enrolls it. Removing a local key does not revoke server
enrollment. Cancellation releases the request promptly, but an uncooperative
provider call remains quarantined and keeps the backend busy until it returns.
Shutdown drains it before releasing OS storage resources; there is no forced
thread termination guarantee.

Biometric enrollment invalidation, hardware attestation, automatic desktop login
recovery and platform-independent export of private keys are not provided by
this Linux backend. Real TPMs, vendor tokens, PIN lockout and protected-path UI
require native qualification; software TPM tests establish interoperability only.

## Unreadable push transport state

The native host exposes `inspectPushJournalRecovery()` and
`quarantineUnreadablePushJournal(consent)`. They require explicit installation-wide
consent and preserve original encrypted records and Secret Service keys. Read
[the shared recovery procedure](ANDROID_NATIVE_INTEGRATION.md#unreadable-push-journal-recovery)
for revision fencing, retry IDs, bounded archives and remote-revocation limits.
Fresh Linux push state uses a separate SQLite file; unrelated native state is not
reset. Whole shared-database corruption, recovery-selector loss, unavailable
Secret Service and authentication/provider errors fail closed. GUI clients of a
remote session host receive `ENOTSUP`; run the policy in the compiled host.
