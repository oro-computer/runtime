# Native Android qualification fixtures

The native integration guide is [Android native integration](../../../docs/ANDROID_NATIVE_INTEGRATION.md).
`deferred/Periodic.kt` adds recurring-read and restricted maintenance cases to
the deterministic deferred fixture. The host Back and RFC 8291 interoperability
regressions are `test/unit/android-back-handling.test.js` and
`test/unit/android-push-crypto.test.js`; they compile the production algorithms
with public fixtures when a Kotlin/JVM toolchain and JSON library are provided.
These regressions do not qualify OS periodic timing, real distributor delivery,
Keystore recovery, cold receiver startup or main-app navigation. Those require
an isolated Android app and the intended device/provider matrix. Automated
suites remain subject to the workspace's execution restrictions.

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
