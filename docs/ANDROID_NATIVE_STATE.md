# Native state and foreground execution

Use [declarative workflows](DECLARATIVE_WORKFLOWS.md) for common background
reads, push hints, checkpoints and notifications without application native
sources. The lower-level facilities below support custom executors and explicit
legacy interoperability. They do not establish server authentication, enrollment,
remote transfer completion, or permission to retry an uncertain external
operation. Register a custom compiled executor from `NativeExtension.onCreate`
only when its behavior falls outside the supported runtime workflow; do not load
executable paths or JavaScript into it.
Rebuild and distribute the paired CLI, native archives, Kotlin sources and APIs.
For an existing development-linked API package, `node bin/link-runtime-api.js
api <linked-api-package>` adds missing module/type links and refuses conflicting
files. Copied/release packages require normal packaging rather than link refresh.

For the C++20 process-scoped Linux backend, see
[Linux native integration](LINUX_NATIVE_INTEGRATION.md). Android service and
JobScheduler guarantees do not transfer to an ordinary desktop process.


## Project root and build configuration

Use the directory containing `oro.toml` as the project root. For an optional
custom native extension, keep its Kotlin under `android/` and web assets under
`public/`, relative to that root:

```text
project/
  oro.toml
  android/
    Monitoring.kt
  public/
    index.html
    main.js
```

From the project root, run `oroc build --platform android --config ./oro.toml`.
From another directory, pass `--config /absolute/project/oro.toml`; the CLI uses
its parent directory as the project root. No parent-directory search is needed.
Prefer this explicit selector in automation and monorepos. Paths in
`android.sources`, `android.manifests`, `build.copy` and `build.output` are relative
to that project root. Native Kotlin is compiled into the APK by the normal
Android Gradle toolchain. It is not a runtime-loaded script. Do not add generated
service declarations to an application manifest when the configuration below
already generates them.

In a read-only SDK or constrained build account, keep Gradle, Android debug
signing and Kotlin daemon state writable outside the SDK. Set `GRADLE_USER_HOME`
and `ANDROID_USER_HOME` to private writable directories. If the JDK's default
user home is read-only, scope `JAVA_TOOL_OPTIONS=-Duser.home=/writable/build-home`
to the build invocation. Preserve the SDK manager's own Java runtime; do not
set a global `-Djava.home` override. See [build environment](./BUILD_ENVIRONMENT.md)
for first-run tool configuration and read-only installations.

## Encrypted native records

`NativeRecords.open(context, namespace, NativeRecordIdentity(...))` returns a
blocking, native-only `NativeRecordStore`. Run its I/O on a worker. Namespaces
match `[a-z][a-z0-9_.-]{0,63}`; `oro.` namespaces belong to the runtime. Applications
resolve and authorize the exact authority/account/identity references before
opening a store. Changing an identity reference selects a different encrypted
scope; this is isolation, not a server authorization decision.

```kotlin
val records = NativeRecords.open(context, "product.credentials",
  NativeRecordIdentity(authorityRef, accountRef, identityEpoch))
val before = records.read()
val after = records.apply(before.revision,
  mapOf("sealed-state" to publicOrApplicationBytes), commitId = operationId)
```

Each scope has a separate non-exportable Android Keystore AES-256-GCM key. Scope,
identity, key generation, revision and commit ID are authenticated. The encrypted
SQLite database and rollback journal live in `noBackupFilesDir`, separate from
`oro:secure-storage`. There is no JavaScript record-store route. Other compiled
native code is trusted; these contracts do not sandbox a malicious extension or
prevent application filesystem APIs from deleting ciphertext.

`read()` returns caller-owned byte copies, a monotonic revision and the latest
commit ID. It never creates a key. `apply(expectedRevision, changes, commitId)`
atomically checks the revision and replaces/removes the specified records.
Null removes a record. An empty update still advances the revision. Empty scopes
retain their revision, preventing stale compare-and-swap after clear/reset.
`ESTALE` means the expected revision no longer matches; re-read and reconsider
the intended change. `EBUSY` permits bounded retry of local contention.

SQLite transactions use full synchronization. A failed/uncertain commit can
return `EUNKNOWN`: do not assume that the change was rolled back. Read the latest
commit ID, or use the coordinator's retained receipt when later local changes
could hide that ID. Local atomicity does not extend across a remote request.
Hardware/OS/filesystem durability still requires release/device qualification.

Missing keys return `EKEYREVOKED`; corrupt ciphertext returns `EBADMSG`. These
errors preserve the database. `status()` reads the revision and key presence
without decrypting or generating a replacement key; readability is not probed.
After explicit application recovery/consent, `reset(expectedRevision)` installs
an empty scope with a new key and a higher revision. It affects only that
identity. It does not recover lost plaintext or reauthorize an account.

`migrate(id, sourceSha256, expectedRevision, records)` atomically imports records
and retains a migration marker. The same marker/digest does not overwrite data;
a changed digest or conflicting existing record returns `EEXIST`. The caller
must validate the legacy format and exact identity before import. No legacy
source is deleted, even on success. Retire its reader only after migration and
interruption acceptance passes.

Limits are 256 identity scopes per app database, 128 user records per scope,
64 KiB per user value, and 256 KiB total decoded bytes plus key names per scope.
There are at most 16 coordination slots and 32 migration markers per scope.
Internal metadata counts toward the total. Keystore creation caps at 512 keys;
reset retirement metadata also caps at 512. `NativeRecords.collectRetiredKeys`
compares live references under the SQLite write lock and removes at most 512
unreferenced/retired aliases per call, including orphan keys after failed commits.
Run it on a worker. It never deletes a referenced key. Failed collection retains
journaled retirement entries for retry. These limits return `ENOSPC` rather than
allowing unbounded growth.

## Conditional native coordination

`NativeCoordinator(records)` shares the record store's transaction and identity
boundary. It keeps intents, outcomes and optional receipts encrypted alongside
record changes. This coordinates local subsystems; application code owns external
protocols, authorization, idempotency keys and receipt interpretation.
Participating metadata must use the same identity-bound record store. Existing
push/deferred journals and legacy application stores are separate boundaries;
this API does not make independent stores or a server transaction atomic.

```kotlin
val coordinator = NativeCoordinator(records)
val prepared = coordinator.prepare("enrollment", "activate", records.read().revision)
val executing = coordinator.begin(prepared.ticket)
// Invoke the authorized external protocol only after begin is durably stored.
val committed = coordinator.commit(executing.ticket,
  mapOf("enrollment" to confirmedRecord), confirmedReceipt)
```

Tickets bind the store identity, slot, random generation and whole-scope revision.
Any intervening local write invalidates the ticket. Inspection obtains a fresh
snapshot; refreshing a ticket is an explicit app decision to reconcile current
state, never permission to replay an external write.

The transitions are:

- `prepare`: absent slot → `prepared`; existing outcomes require explicit release.
- `begin`: `prepared` → `executing`, before an external side effect is attempted.
- `abort`: only `prepared` → `aborted`.
- `commit`: `executing` → `committed`, atomically with record changes and receipt.
- `unknown` / `recover`: `executing` → `unknown`; no remote rollback is asserted.
- `resolve`: application-reconciled `unknown` → `committed` or `aborted`.
- `release`: remove a terminal outcome only after its receipt is consumed/reconciled.

`get(slot)` is read-only. After process loss, it preserves `executing` and reports
`ownerProcessMatches: false`. There is no automatic retry, abort or liveness
inference. The application explicitly fences/reconciles the abandoned operation.
A cancellation after `begin`, a lost network reply, or a crash before commit
requires external reconciliation. A stale ticket cannot settle a replacement
slot. Receipts remain until explicit release and are limited to 64 KiB.

## User-started foreground executor

### Public notification artwork

Use a compiled monochrome drawable for both finite and continuous Stop
notifications without replacing their public controls or channel policy:

```toml
[foreground]
notification_icon = "@drawable/runtime_status"
```

Package `runtime_status.xml` or an equivalent drawable through the project's
ordinary Android resources. Only `@drawable/name` references are accepted.
Generated manifest metadata retains the resource through shrinking and lets
Android's resource compiler reject a missing drawable. The empty default uses
the runtime status icon. Qualify its actual small-icon appearance and Stop on
the installed APK; successful resource compilation does not prove appearance.

### Blocking extension calls

`NativeExtensionCall.runOnWorker` provides four workers and sixteen queued slots
for compiled blocking code. Call it once from `NativeExtension.request` instead
of installing an independent `onCancel` callback:

```kotlin
override fun request(call: NativeExtensionCall, action: String, options: JSONObject) {
  if (action != "readPublicStatus") { call.failure("EINVAL"); return }
  call.runOnWorker { cancellation ->
    cancellation.throwIfCanceled()
    // Resolve and authorize exact application references here.
    val result = JSONObject().put("public", true)
    cancellation.throwIfCanceled()
    result
  }
}
```

The helper preserves document ownership, the common 90-second request deadline,
512 KiB result bound and foreground revalidation. Saturation returns `EBUSY`.
Cancellation interrupts the task and signals its Android `CancellationSignal`;
native I/O must observe it and close its own resources. A cancelled worker and
its cancellation listeners retain admission until both finish. Cleanup runs off
the main thread. Exceptions return fixed error codes without exception text.
Do not call `success` yourself inside the operation or retain an Activity/view
on the worker. The helper returns only the final value; it does not authorize
accounts, migrate stores or prove a remote transfer completed.

Enable the generated non-exported Android `dataSync` service explicitly:

```toml
[foreground]
enabled = true

[permissions]
allow_notifications = true
```

The service runs only in the default app process. Android admits or rejects its
start, applies aggregate foreground-service time limits, and can stop the app.
The runtime does not start it at boot or restart/replay it after process loss.
This service supports bounded data-sync work; it does not provide permanent
monitoring, general background JavaScript or other foreground-service types.
See Android's [foreground service timeout contract](https://developer.android.com/develop/background-work/services/fgs/timeout).

```kotlin
ForegroundTasks.registerExecutor("product.transfer", ForegroundExecutor { context, task, signal ->
  // Resolve opaque refs and recheck current authorization. Return null if denied.
  transfers.authorize(context, task, signal)
})
```

The application's `transfers.authorize` returns an `AuthorizedForegroundExecution`.
Its `execute(CancellationSignal)` returns COMPLETED, UNAUTHORIZED or UNKNOWN, and
`close()` releases connections and credentials. COMPLETED is the executor's
assertion, not independent proof of remote durability. Cancellation callbacks
must return promptly, release blocking resources and allow execute/close to end.
An uncooperative native worker cannot be safely preempted in-process; the runtime
quarantines its occupied slot and prevents another executor from overlapping it.

```js
import foreground from 'oro:foreground'

const support = await foreground.getSupport()
if (support.canStart) {
  // Run from an explicit user action in the visible, focused app.
  const admitted = await foreground.start({
    id: 'transfer-1', kind: 'product.transfer',
    authorityRef: 'server-1', accountRef: 'account-1',
    targetRef: 'destination-1', operationRef: 'upload',
    userInitiated: true, durationMs: 15 * 60 * 1000
  })
  const status = await foreground.getStatus(admitted.id)
  // Persist/reconcile status.generation; use it for stop/release.
}
```

`userInitiated: true` is an application assertion, not a cryptographic user-gesture
proof. Native UI can use `ForegroundTasks.start(activity, task, durationMs, true,
completion)`. Both paths revalidate a live resumed/focused Activity immediately
before OS launch. Notification permission, enabled notifications and the existing
channel must permit presentation. The channel is low importance and keeps OS/user
policy. Its fixed public text contains no account or destination identifiers.

Admission returns `starting`, not a completed operation. Abort/navigation may
race admission; after an uncertain reply, inspect the same ID before retrying.
Once admitted, Home/navigation does not cancel the native operation. The ongoing
notification opens the app and provides a generation-bound Stop action. A stale
Stop cannot affect a replacement. Stop, task removal, deadline or OS timeout
cancel the signal and stop the service. Work that may have started stays UNKNOWN,
not a fabricated completed or rolled-back transfer. Terminal outcomes remain in
the encrypted journal until explicit `release(id, generation)`.

There is one concurrent executor, including cancellation/resource cleanup, at
most 16 retained task records, at most 16 registered kinds, and a requested
duration of 1 second through 30 minutes. The default is 15 minutes. The deadline
uses elapsed realtime and is checked while the process runs; sleep/suspension
and OS admission can affect observation time. Android's own timeout callback
stops the service immediately. Losing notification permission/channel admission
also stops the operation. No retry occurs automatically. App or OS process loss
becomes UNKNOWN on the next explicit inspection, without reopening a WebView or
replaying work.


## Continuous user-started monitoring

`oro:foreground/continuous` and the compiled Kotlin `ContinuousTasks` controller
provide continuous `specialUse` execution. They share the finite controller's
admission, encrypted journal and cancellation implementation but have separate
executors, channels, notification IDs and journal scopes. One continuous executor
can run alongside one finite `ForegroundTasks` executor. Each mode retains its
own limit of 16 records and 16 registered kinds. The continuous executor has no
runtime duration cap. Android can stop it or suspend network/CPU access; no wake
lock or exemption from Doze is acquired.

Add the following to the project's **`oro.toml`**. The public purpose must describe
the application's actual eligible use case; it is compiled into the manifest.
The runtime does not establish store approval or specialUse eligibility. See the
[Android service-type requirements](https://developer.android.com/develop/background-work/services/fgs/service-types#special-use).

```toml
[build]
name = "native_monitor"
copy = "public"
output = "build"

[meta]
bundle_identifier = "org.example.nativemonitor"
version = "1.0.0"

[android]
sources = "android/Monitoring.kt"
extensions = "org.example.monitor.MonitoringExtension"

[permissions]
allow_notifications = true

[foreground]
continuous_enabled = true
continuous_subtype = "User-enabled monitoring of explicitly authorized connections"
# Independently enable finite dataSync operations when the app needs them:
# enabled = true
```

`continuous_enabled` defaults to false. The CLI rejects a missing/blank purpose,
more than 512 UTF-8 bytes or control characters, and XML-escapes its value.
Enabling it generates `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_SPECIAL_USE` and
a non-exported `RuntimeContinuousService` in the default process, with a
`PROPERTY_SPECIAL_USE_FGS_SUBTYPE` property. On Android 14 and later the service
enters the specialUse type. Earlier supported Android versions use their
untyped foreground-service entry. It adds no boot receiver or scheduler.

Register a compiled executor from `NativeExtension.onCreate`, using
`ContinuousTasks.registerExecutor(kind, ForegroundExecutor { ... })`. The executor
contract is the same `AuthorizedForegroundExecution` used for finite work.
Native UI can call `ContinuousTasks.start(activity, task, true, completion)`.
An executor registered only with `ForegroundTasks` is unavailable to
`ContinuousTasks`, and vice versa. This prevents a page from promoting a finite
executor to continuous work. No account secrets, URLs, payloads or executable
code are accepted by the start request; only bounded opaque references cross IPC.

The application must resolve those references against its current saved identity
and grants on the worker before opening a connection. It must recheck changing
authorization and exact endpoint/account generations while monitoring, including
before every reconnect or remote mutation. Its compiled executor may manage
bounded per-connection workers, but must cancel and join them in `close()`.
The runtime owns service lifecycle and cancellation; product protocols, retry
policy, event severity, payload authorization and server receipts remain compiled
application responsibilities.

From an explicit user action in the visible, focused application:

```js
import monitoring from 'oro:foreground/continuous'
import Notification from 'oro:notification'

await Notification.requestPermission()
const support = await monitoring.getSupport()
if (support.canStart) {
  const admission = await monitoring.start({
    id: 'monitor-1', kind: 'product.monitor',
    authorityRef: 'authority-1', accountRef: 'account-1',
    targetRef: 'connections-1', operationRef: 'watch', userInitiated: true
  })
  // Retain this ID/generation for inspection, explicit stop and reconciliation.
  const current = await monitoring.getStatus(admission.id)
}
```

`getSupport()` returns `maximumDurationMs: null`, `automaticRestart: false` and
`backgroundJavaScript: false`. `start()` rejects a `durationMs` option. A `canStart`
result is a configuration/notification-policy snapshot, not proof of OS admission
or a user-gesture token. Native entry rechecks resumed/focused ownership before
OS launch. Success returns durable `starting` admission; it does not promise that
native authorization or a remote operation succeeded. Abort/navigation/lost IPC
can race admission. Inspect the same ID before any new attempt.

Request permission through `Notification.requestPermission()` as part of the
application's explicit user flow. Configuration only declares the permission;
`getSupport()` and `start()` do not show a permission dialog or change channel
settings.

The notification uses the fixed public text "User-started monitoring” and a
visible **Stop** action without account or target details. The historical
`oro.runtime.continuous.v1` channel retains the user's existing channel policy.
Permission/channel denial prevents admission. Revocation while running requests
cancellation on the next controller check (one second while the main loop runs),
and stops the service. The OS controls when callbacks run during suspension.
A blocked channel is not replaced with a new channel to bypass user policy.

| Trigger | Continuous controller behavior |
| --- | --- |
| Home, navigation or Activity recreation | Admitted native work continues; no WebView is kept alive for it |
| Remove app task from Recents | Monitoring continues with its public notification; finite dataSync work retains its stop-on-task-removal behavior |
| Notification Stop or `stop(id, generation)` | Only the exact generation is cancelled; an absent/stale notification generation cannot stop a replacement |
| Executor returns | Records its COMPLETED/UNAUTHORIZED/UNKNOWN assertion, closes resources and stops the service |
| OS timeout, service destruction or notification/permission loss | Requests cancellation, stops the service and preserves uncertainty |
| OS process loss, Android user Stop, force-stop or reboot | No restart/replay. The retained in-progress record becomes UNKNOWN on the next explicit inspection |
| Open app again | Inspection is permitted; monitoring does not resume automatically |
| Explicit new start after reconciliation | Requires a visible/focused user action and a registered authorized executor; release the old terminal ID/generation or choose a new ID |

`stop` does not roll back server effects. `release(id, generation)` removes a
terminal journal entry only after worker, cancellation callback and service
cleanup finish. An uncooperative executor remains quarantined; another continuous
executor cannot overlap it. The independent finite slot is unaffected. `status`,
`list`, `stop` and `release` remain native query/control functions even if the
feature is subsequently disabled; the page-facing route requires the feature to
be configured. Use native worker inspection for disabled-feature recovery.

Before retiring a custom service, register the product executor, bind it to exact
saved identities, and qualify an in-place signed upgrade on seeded historical
state. Preserve the old keys/readers and active task/endpoint generation until
handover is confirmed. Do not start both old and new monitors for one enrollment.
Qualify public notification display and actual Stop, stale Stop, permission/channel
loss, Home/Recents, process loss/reopen, network interruption, Doze/reboot and
product authorization/reconciliation. Host facades validate controller algorithms;
they do not establish OS admission, delivery, battery behavior or completed
transfers. The public native fixture is
`test/fixtures/android-runtime/extensions/StateQualification.kt`; the controller
regressions are under `test/fixtures/android-runtime/state/foreground/`.
