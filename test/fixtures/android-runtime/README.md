# Native Android qualification fixtures

The native integration guide is [Android native integration](../../../docs/ANDROID_NATIVE_INTEGRATION.md).

The separate Kotlin UnifiedPush connector fixture is
[`android-push-distributor`](../android-push-distributor). Build it with
`node test/scripts/build-push-distributor-fixture.js --output=/path/to/public-check.apk`
after exporting the normal `ANDROID_HOME`, `JAVA_HOME` and `GRADLE_HOME` settings.
It compiles the runtime's unchanged `PushCryptography` with public test data.
No private key is returned in its host-readable result file.

`node test/scripts/test-push-distributor.js` qualifies an explicitly selected
`ANDROID_SERIAL` using `ORO_TEST_DISTRIBUTOR_APK`, `ORO_TEST_PUSH_CONNECTOR_APK`,
`ORO_TEST_PUSH_CA`, `ORO_TEST_PUSH_ORIGIN` (default `https://127.0.0.1:18881`) and
optional `ORO_TEST_PUSH_REPORT`. Start the local CLI server with that certificate
and build Oro Push Dev with the matching `--server` and `--ca-cert`. The runner
installs only `dev.oro.push` and `dev.oro.push.qualification`, enables the public
distributor settings with signed test instrumentation, and removes its own adb
reverse mapping when finished. `ORO_TEST_PUSH_DEBUG_SETUP=1` provides a narrower
transport-only mode without activity transitions. That mode explicitly leaves
settings interaction, link selection and uninstrumented service startup unverified.
Use an isolated test device/emulator: changing the distributor's configured
server resets its development registrations. The receiving fixture's APK and
results remain available for review. This check covers real selection identity,
two registrations, RFC 8291 delivery, ACK rejection, explicit reopen after
force-stop and unregistration. It does not qualify reboot, Doze, vendor battery
policies, a production native handler or authenticated backend enrollment.

`deferred/Periodic.kt` adds recurring-read and restricted maintenance cases to
the deterministic deferred fixture. The host Back and RFC 8291 interoperability
regressions are `test/unit/android-back-handling.test.js` and
`test/unit/android-push-crypto.test.js`; they compile the production algorithms
with public fixtures when a Kotlin/JVM toolchain and JSON library are provided.
These regressions do not qualify OS periodic timing, real distributor delivery,
Keystore recovery, cold receiver startup or main-app navigation. Those require
an isolated Android app and the intended device/provider matrix. Automated
suites remain subject to the workspace's execution restrictions.

`extensions/WindowQualification.kt` observes only an isolated public package's
native window sizes, text zoom, focus, lifecycle and Android `FrameMetrics`.
Configure `WindowQualificationExtension` and its DUMP-protected receiver only
in a package beginning `oro.runtime.qualification.`. The fixed `WINDOWS.SNAPSHOT`,
`WINDOWS.RESET_METRICS` and `WINDOWS.RECREATE` action suffixes use the
`oro.runtime.qualification.` prefix. Draw observations are bounded to 128 and
frame samples to 2,048. They contain no page text, input values or URLs.
`WINDOWS.PORTRAIT`, `WINDOWS.LANDSCAPE` and `WINDOWS.RELEASE_ORIENTATION` change
only this fixture Activity's requested orientation; release it after the check.
Snapshots include native client location and registered gesture-exclusion
rectangles. Those rectangles prove view registration, not OS gesture acceptance.
The first observed draw after instrumentation attaches does not prove the
earliest displayed pixel. Native Window metrics include loading/recovery UI;
measure warmed idle and interaction phases separately. This fixture does not
establish application performance budgets or production leak/soak acceptance.

`DocumentsProvider.kt` supplies fixed public test content for an isolated
qualification APK. It is excluded from the production CLI source catalog and
must never be declared in a production manifest. No account, server credentials,
or user-selected files are involved.

The `oro.runtime.qualification.Documents` provider exposes a tree root `root`
and documents `full`, `unknown`, `locked`, and `unicode`. Metadata uses real
DocumentsContract cursors, unknown sizes remain null, the locked document
rejects writes, and the Unicode display name contains a supplementary character.
The `oro.runtime.qualification.Slices` provider exposes `section` (the bounded
four-byte body inside a larger file) and `pipe` (an unknown-length six-byte
stream). Only the qualification app can access the slice provider.

Querying `blocked-cancel` takes eight seconds and its cancellation callback
blocks for seven seconds. Use it only in the isolated app to verify that the
five-second runtime result deadline remains independent of provider cancellation,
that occupied query slots return EBUSY, and that workers become usable again.

Manual acceptance with an isolated application package:

1. Declare the document provider with MANAGE_DOCUMENTS, temporary URI grants and
   the DOCUMENTS_PROVIDER intent filter; advertise FLAG_SUPPORTS_IS_CHILD on the
   root for tree selection. Declare the slice provider unexported.
2. Read tree pages through `oro:document-access`, then query each returned URI.
   Check pagination, Unicode names, unknown size and missing-document errors.
3. Exercise `oro:fs` read/write/truncate/append and rejected exclusive opens
   against `full`; verify `locked` reports access denial. Use public bytes only.
4. Read `section` with explicit offsets and `pipe` with a null position. Confirm
   bounded EOF and unknown-length sequential reads. Close every handle.
5. Qualify fullscreen entry, the native Exit control, Back with/without IME,
   suspension, secondary-window hiding, renderer destruction and rotation.
6. With explicit background enablement, inspect native share-prune scheduling,
   cancellation, actual execution and state. A submitted job is not completion.
7. Exercise FLAG_SECURE only in this app, then verify its flag lifecycle and
   accessible native recovery/exit controls. Restore device settings changed for
   the run. Use native screenshots; avoid CDP frame capture.

Record each qualification run against its APK digest, runtime identity and
device/WebView versions, with explicit outcomes and limits. Fixture availability
does not establish a passing device run. These fixtures do not qualify cloud
providers, reboot, historical upgrades, production signing, vendor behavior or
the supported Android/WebView matrix.

`NotificationReceiver.kt` exercises only notifications with one of two fixed
public fixture titles in this APK's own NotificationManager. Declare it only in
the qualification manifest, exported with sender permission `android.permission.DUMP`
so shell can invoke it. Operations `inspect`, `tap`, `action`, `dismiss`, and
`replay` use the actual OS PendingIntents; no arbitrary notification, receiver,
private payload or destination can be supplied. Replay checks one-shot refusal.
This qualifies native dispatch and receipt handling, not a physical shade tap.
The receiver must remain absent from production manifests and source catalogs.

`ProviderTools.kt` belongs in a separate provider qualification APK. Its manually
invoked instrumentation reads only fixed public picker labels and navigation
control bounds, without waiting for system-wide accessibility idle or returning
user filenames. Target the provider APK itself so inspection does not restart
the picker caller. The optional `click` argument accepts only the same fixed
public picker labels and navigation controls; it cannot select user filenames.
Existing accessibility services remain active. Its DUMP-protected receiver revokes only that provider's fixed
public tree from the fixed caller package `oro.runtime.qualification.client`.
Use that package ID for the isolated client APK. Neither helper belongs in production.

`extensions/QualificationExtension.kt` supplies two compiled extensions with fixed
public responses, lifecycle counters and delayed completion/cancellation checks.
Its receiver exercises cold native notification submission without a WebView.
Declare that receiver only in the isolated manifest, exported with the sender
permission `android.permission.DUMP`. The fixture reads only its own isolated
package's notification metadata. It contains no provider accounts or passwords.
Verify coexistence, unknown-extension rejection, abort/navigation cancellation,
immutable channels, grouped/private/public-version submission, blocked channels
and cold response receipts. An empty credential provider or chooser cancellation
does not qualify provider fill/save/update.

The receiver's fixed `oro.runtime.qualification.PRESENTATION` action checks
immutable channels, grouping, explicit summaries, private/public versions,
packaged icons, blocked and missing channels, and one-shot dismissal. It then
finishes its broadcast. Invoke `oro.runtime.qualification.PRESENTATION_RECEIPT`
separately to verify and acknowledge the shared durable receipt and confirm
notification cancellation. A pending receipt returns `public-receipt-pending`;
poll from the host with a bounded deadline. Do not wait for a nested broadcast
inside the still-active posting broadcast, since Android can serialize delivery.
Android documents the effect on later broadcasts in
[`BroadcastReceiver.goAsync()`](<https://developer.android.com/reference/android/content/BroadcastReceiver#goAsync()>).

Each run uses distinct public IDs and a distinct group so an earlier receipt
cannot satisfy a later check. Posted run notifications are cancelled on failure
as well as success. Cold application startup and its broadcast deadline must be
measured separately from a warmed functional run; precompilation or warm-up does
not qualify first-delivery timing. Provide the `qualification_notification`
drawable in the isolated app resources. This uses an OS PendingIntent; it does
not establish a physical notification-shade tap.

`extensions/FailureQualification.kt` adds a separate DUMP-protected
`FailureReceiver` and a `FailureExtension` that registers a fixed public deferred
read executor. Compile both only in an isolated qualification package with
deferred reads and launcher destinations enabled. It directly exercises native
state machines without a WebView; it does not qualify JavaScript document
ownership or an interactive system prompt.

The fixed action prefix is `oro.runtime.qualification.FAILURE.`:

- `SEED`, followed by an application process stop and `RECEIPTS`, checks durable
  receipt recovery, duplicate/expired/malformed/foreign refusal and failed commits.
- `CANCELLATION` checks cleanup registered after completion, rejected duplicate
  cleanup, ignored late completion and concurrent settlement.
- `LAUNCHER` checks opaque routing and rejection after the route is revoked.
- `STORAGE` uses only a public fixture value. Failed commits preserve it; a
  deliberately corrupt envelope reports an error and remains intact. The original
  envelope is restored in `finally` and read again.
- `ENQUEUE` creates one public seven-day-delayed read. Use the host JobScheduler
  command for the `oro.runtime.deferred` namespace to run it, then observe
  `STATUS`. Stop only the isolated application while it is running, invoke
  `REVOKE`, then `RESUME` and verify an `unauthorized` terminal result. A separate
  enqueue/run/`CANCEL` checks cancellation, resource closure and a stable terminal
  result; `RELEASE` removes this fixture's task record.
- `PROFILE` returns the native startup durations and bound build identity.
  Collect cold process launches separately from repeated broadcasts within the
  same process.
- `ENQUEUE_AUTONOMOUS` uses the same fixed public task, with a 30-second minimum
  delay and an executor that completes a public in-memory read. Observe `STATUS`
  without forcing the job through shell commands, then `RELEASE` the terminal
  record. Network availability and ordinary OS quotas still apply; elapsed time
  alone does not establish completion. A completed record, one attempt and the
  matching running/closed markers establish that the OS dispatched this fixture.
  In a debuggable isolated APK, host reads through `run-as` can observe the two
  fixed preference files without launching the application. A `STATUS` broadcast
  can wake the process; record that distinction when checking cold dispatch.
  This does not establish backend authorization, alarm delivery or physical
  vendor behavior. Do not enqueue while another fixture task is retained.

For native-only foreground qualification, declare the fixture's
`oro.runtime.qualification.extensions.PublicNativeActivity` as an exported
Activity protected by `android.permission.DUMP`. It displays only fixed public
text, and creates no WebView. Start it once before an autonomous scheduling check
and leave it through Home. This establishes ordinary foreground use; do not
replace it with a standby-bucket override. A newly installed package in the
`never` bucket can legitimately retain a waiting job. Record the actual bucket
and constraints rather than treating a waiting job as a runtime failure.

All fixture operations use public data in their own package. `FailedCommitContext`
injects refusal before applying a preference write; this is not physical disk
exhaustion or Android's ambiguous durability after a failed disk commit. Process
force-stop checks do not qualify reboot, Doze, vendor killing or OS rescheduling.
Record the isolated package's standby bucket and job constraints. If host-driven
execution requires a temporary active bucket, restore its original bucket after
the run and report that override; this does not establish natural OS scheduling.

`extensions/PercentStorageQualification.kt` supplies a separate DUMP-protected
`PercentStorageReceiver`. Use only in an isolated package. The fixed prefix is
`oro.runtime.qualification.PERCENT.`. With the fixture Activity and window zero
ready, `WRITE` sends 41 actual native IPC writes using literal percent escapes,
empty strings, Unicode, NUL and binary data in UTF-8/base64/hex. Stop only the
fixture process, then invoke `READ` for 42 repeated cold native reads. `CLEAR`
removes the 14 public records. These direct native checks do not qualify the
JavaScript API or recover historical corrupted values.

`extensions/TlsQualification.kt` provides a DUMP-protected receiver for public
native HTTPS checks. Compile it only in an isolated qualification package. Supply
public test certificate DER assets `public-tls-certificate.der` and
`public-tls-rotated.der`, and expose a local TLS server through
`adb reverse tcp:18443 tcp:18443`. No production certificate or key is needed.
The server returns `public-tls-ok` at `/public`, redirects `/redirect` to a
separately counted receiver, returns over 64 bytes at `/oversized` and `/chunked`,
and delays `/slow`. The fixed action prefix is `oro.runtime.qualification.TLS.`.

`MATCH`, `MISMATCH`, `PORT`, `HOST`, `REVOKE`, `REDIRECT`, `SIZE`, `CHUNKED`,
`TIMEOUT`, `CANCEL`, `PRECANCEL`, `INVALID` and `CORRUPT` check real TLS, isolated
trust and bounded failures. `APPROVE`, followed by stopping only the fixture
process and invoking `COLD`, checks Keystore-authenticated approval after a cold
native start. Switch the server to the rotated certificate before `ROTATE`;
verify old approval fails and explicit new approval succeeds. `CLEAR` removes
this package's public approval. Verify the redirect receiver saw no request or
Authorization header. These checks qualify the runtime transport, not an app's
approval UI, grants, endpoint protocol, natural OS scheduling or main APK.

`extensions/TextScaleQualification.kt` supplies a DUMP-protected
`TextScaleReceiver` for an isolated package. Declare the receiver in its
qualification manifest and broadcast `oro.runtime.qualification.TEXT_SCALE`.
It runs six fixed public policy checks and reports the current Android font
scale and resolved system zoom. It does not create a WebView. This can check
the compiled policy even when an emulator's plain WebView control cannot start;
first-frame rendering, configuration callbacks, duplicate CSS scaling and
assistive behavior still require the interactive window matrix.

`extensions/StateQualification.kt` exercises native encrypted records and
coordination in an isolated `oro.runtime.qualification.` package. Its receiver
must be protected by `android.permission.DUMP`. The fixed
`oro.runtime.qualification.STATE.` prefix supports `RUN`, `SEED`, `RECOVER`,
`FOREGROUND` and `STOP`. `RUN` uses only this fixture's public scopes, injects a
failed SQLite write, temporarily corrupts and restores its own ciphertext,
deletes one public fixture key and explicitly resets only that identity. `SEED`,
followed by stopping only this package's process and invoking `RECOVER`, checks
durable executing/unknown outcomes without replay. `FOREGROUND` returns public
executor counters and redacted task statuses; `STOP` sends this app's actual
generation-bound notification PendingIntent. Enable `[foreground] enabled = true`
and notifications only in the isolated APK for the foreground checks. Neither
the extension nor its receiver belongs in a production source catalog/manifest.

`test/unit/android-native-state.test.js` compiles the production records,
coordinator and foreground controller with the host fixtures in `state/`.
Provide `kotlinc` and `java` on PATH, and set `ORO_KOTLIN_STDLIB`,
`ORO_TEST_JSON_JAR`, `ORO_TEST_SQLITE_JAR` (Xerial SQLite JDBC) and
`ORO_TEST_SLF4J_JAR` (SLF4J API) to existing local JARs. The tests never download
dependencies. They use real SQLite transactions and AES-GCM, with a host-only
fake Keystore and foreground-service OS facade. Separate JVMs test process
restart. Host tests do not establish Android admission, hardware key protection,
notification presentation, OS scheduling, reboot or device durability; qualify
those on Android against the exact APK and paired SDK inputs. The native
contracts and application adoption requirements are in
[native state and foreground execution](../../../docs/ANDROID_NATIVE_STATE.md).

`test/unit/android-gesture-exclusion.test.js` compiles production `gestures.kt`
with the host facades in `gestures/`. It checks bounds, mandatory-region clipping,
ownership loss, stale documents, rejected replacement and listener removal.
It requires Kotlin/JVM, `ORO_KOTLIN_STDLIB` and `ORO_TEST_JSON_JAR`. Real Android
edge arbitration needs an isolated app, the actual OS gesture-navigation mode
and public touch counters. Verify the handle receives an edge swipe while Back
still works outside it. Also check pause, hide, rotation, recreation and renderer
loss without returning stale exclusions. Do not use private screens for input.

### Continuous foreground fixture

Build `continuous/oro.toml` with a paired SDK that includes
`oro:foreground/continuous`. From the repository root:

```sh
oroc build --platform android --config ./test/fixtures/android-runtime/continuous/oro.toml
adb -s "$ANDROID_SERIAL" install -r ./test/fixtures/android-runtime/continuous/build/android/app/build/outputs/apk/dev/debug/app-dev-debug.apk
ANDROID_SERIAL="$ANDROID_SERIAL" \
ORO_TEST_CONTINUOUS_APK=./test/fixtures/android-runtime/continuous/build/android/app/build/outputs/apk/dev/debug/app-dev-debug.apk \
ORO_TEST_CONTINUOUS_REPORT=/tmp/oro-continuous-report.json \
node test/scripts/test-continuous-foreground.js
```

Use a dedicated, unlocked, explicitly selected Android test device. The runner
checks installed APK bytes before granting that public fixture notification
permission, starting its public executors, sending its protected receiver
commands, stopping its process and revoking/regranting its notification
permission. It attaches only to that package's WebView, removes only a task
verified to contain that package alone, and releases its own ADB forward. It
leaves the fixture stopped with notification permission granted. It does not
change screen timeout, lock credentials, global Doze policy or other applications.
Its fixed native test executor performs no network/account operation. The shared
`StateQualificationExtension` must never be declared by a production application.

The six qualification groups cover specialUse admission, finite/continuous
concurrency, Home, the notification's actual PendingIntent Stop, resource cleanup,
stale API generations, OS task removal, process-loss UNKNOWN without replay and
POST_NOTIFICATIONS revocation. These are programmatic controls; physical touches,
Doze/reboot, other Android versions/vendors and product authorization/receipts
require their own acceptance. See [the native execution lifecycle guide](../../../docs/ANDROID_NATIVE_STATE.md).

### Unreadable push journal fixture

Build `push-journal/oro.toml` with the paired recovery-protocol-1 SDK, then run
`ANDROID_SERIAL=<explicit-device> node test/scripts/test-native-push-journal-android.js <apk>`.
Use a fresh public `oro.runtime.qualification.pushjournal` installation: this is
a one-shot fault-injection fixture, and intentionally retained archives prevent
reusing its original IDs. Preserve earlier installations and evidence when
repeating with a new public bundle. The runner refuses other APK bundle IDs.

The fixture creates only public ciphertext and test aliases. It simulates a
missing runtime alias using ciphertext from a separate test alias; it deletes no
keys. It checks denied consent, stale snapshots, worker cancellation/drain,
malformed XML, exact retained original bytes, unchanged original key availability,
idempotent retry and process restart. The simulated local distributor counts
unexpected unregisters but has no remote endpoint. Thus zero unregisters does
not prove server revocation, delivery, Doze, reboot or historical migration.
The driver stops its own public app when finished.
