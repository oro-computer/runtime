# Native platform parity

Oro uses shared APIs with explicit platform backends. A module existing
in the SDK does not establish that its operations run on every target. This
document covers the newer native interfaces, rather than every Oro
API. Existing notifications, windows, network events and secure storage have
their own platform implementations and qualification requirements.

## Discover support before offering an operation

```js
import platform from 'oro:platform'
import authentication from 'oro:authentication'

const implementation = await platform.getSupport('authentication')
if (implementation.implementation === 'implemented') {
  const status = await authentication.getStatus({ policy: 'biometric' })
  // Use the documented status fields before offering authentication.
}
```

`platform.getCapabilities()` and `platform.getSupport(feature)` query the compiled
backend inventory without opening a prompt or reading saved values. The result
has schema `1`, a platform, and an implementation of `implemented`, `missing` or
`restricted`. Each descriptor identifies its backend or a reason for absence.
`requiresRuntimeCheck` means the feature's own support/status probe is required.
This inventory is source support, not proof of device hardware, OS permission,
configuration, successful execution, qualification, or a matching packaged SDK.

Read-only probes for authentication, password selection, launcher publication,
deferred work, push and foreground execution return an explicit unavailable
status for missing/restricted backends. Operational calls continue to reject
with `ENOSYS`. Permission, storage, busy, protocol and cancellation errors are
preserved. An implemented backend returning `ENOSYS` is an error, not silently
normalized into an unavailable feature. Unknown feature names reject.

These results require a CLI/native runtime rebuilt from the same source as the
JavaScript SDK. Older binaries without `runtime.platform.support` still reject;
the new JavaScript cannot add a native backend to an old package.

`oro:platform` is the JavaScript entry point. JavaScript documentation is
available in `man 3 oro-platform`; compile-time C platform macros are documented
separately in `man 3 oro-platform-c`.

## Implementation inventory

| Feature | Android | iOS / iPadOS | macOS | Linux | Windows |
| --- | --- | --- | --- | --- | --- |
| Per-operation authentication / authenticated keys | Android Keystore and system prompt | Backend missing | Backend missing | TPM2 / hardware PKCS#11; explicit provider-PIN policy | Backend missing |
| Explicit password-provider selection | Credential Manager | Backend missing | Backend missing | Secret Service native chooser | Backend missing |
| Declarative read / push-hint / checkpoint / notification workflows | Bundled runtime backend; device/release scope must be qualified | Backend missing | Backend missing | Backend missing | Backend missing |
| Historical protected imports / retained components | Bundled allowlisted Android migration and generated adapters; historical acceptance is scoped separately | Backend missing | Backend missing | Backend missing | Backend missing |
| Finite foreground native jobs | Data-sync service | Backend missing | Backend missing | Native executor; optional separate session host | Backend missing |
| Continuous foreground native work | Opt-in special-use service | Restricted: no unbounded background execution | Backend missing | Native executor; optional separate session host | Backend missing |
| Deferred registered native work | JobScheduler | Backend missing | Backend missing | Durable native scheduler; optional session host | Backend missing |
| Push registration / native delivery protocol | UnifiedPush | Backend missing | Backend missing | UnifiedPush D-Bus connector; optional cold session host | Backend missing |
| Launcher publication | Android shortcuts | Backend missing | Backend missing | Desktop actions and receipts; no pin flow | Backend missing |
| Compiled extension routing | Android implementation | Backend missing | Backend missing | Isolated top-level WebKit bridge | Backend missing |
| Native Back handling | Android implementation | Backend missing | Backend missing | GTK Back keys, bounded WebKit handler | Backend missing |
| System gesture exclusion rectangles | Android implementation | Android-specific API | Android-specific API | Android-specific API | Android-specific API |
| Notification channel/group submission API | Android implementation | Backend missing | Backend missing | Freedesktop transport and app policies; no Android grouping | Backend missing |
| Native encrypted records / coordination | Existing Kotlin v1; C++ v2 also compiled | Shared C++ v2, qualification pending | Shared C++ v2, qualification pending | Shared C++ v2 | Shared C++ v2, qualification pending |
| OS secret-store backing | Android Keystore wrapping | Keychain | Keychain | Secret Service | Credential Manager |

"Backend missing” identifies implementation work, not a defect inferred from an
untested scenario. The notification row describes the newer channel/group
submission API; it does not deny existing desktop notification APIs.
Likewise, the launcher and Back rows describe Android-shaped native interfaces,
not the absence of ordinary windows, focus, keyboard events or app links.

Linux scopes, native registration, cancellation, notification privacy and
optional persistent hosting are documented in
[Linux native integration](LINUX_NATIVE_INTEGRATION.md). Linux authentication-bound keys use trusted TPM2/hardware PKCS#11 providers with explicit provider-PIN policy; biometric-only remains restricted. Secret Service
storage does not establish that security property. Native hosting and password
selection have their own scoped acceptance, separate from deployment qualification.

Equivalent user outcomes require platform-specific designs. For example, finite
user-initiated work, scheduled refresh and push on Apple platforms have separate
OS admission policies. A foreground process on desktop does not itself provide
boot startup, a daemon, an authenticated server, reconnection correctness or a
completed transfer. An authentication confirmation boolean must not substitute
for a cryptographically bound key operation.

## Native encrypted records and operation coordination

`src/runtime/native/records.hh` exposes blocking C++20 `RecordStore` and
`RecordCoordinator`. These APIs are for trusted compiled native integrations;
there is no JavaScript route for reading their records or supplying their keys.
Obtain the production provider from
`runtime.services.secureStorage.nativeRecordKeys()` after starting that service.
Use a private application data directory, a stable namespace and exact opaque
authority/account/identity references:

```cpp
using namespace oro::runtime::native;
auto keys = runtime.services.secureStorage.nativeRecordKeys();
RecordStore store(privateDatabasePath, keys, "session-state",
  {authorityRef, accountRef, identityRef});
auto snapshot = store.read();
auto saved = store.apply(snapshot.revision, changes, operationRef);
```

Run these operations on a native worker, away from UI/event-loop callbacks.
SQLite writer admission has a two-second busy timeout. OS secret-store calls
can prompt or block independently; they do not have a cancellation or bounded
shutdown guarantee. Drain workers before stopping/destroying their runtime.
Retained providers fail with `EACCES` after service stop/destruction.

The v2 core provides:

- SQLite transactions, monotonic compare-and-swap revisions, encrypted
  XChaCha20-Poly1305 envelopes and authenticated scope/revision/key-slot/commit
  metadata. A persistent random database identity separates different databases.
- Scope binding to the namespace and authority/account/identity references.
  This is local separation, not server authentication or authorization.
- OS-backed keys in a private namespace rejected by public secure-storage
  routes. No plaintext or file-key production fallback. Keychain, Secret Service
  and Credential Manager do not imply universal hardware-backed protection.
- `ESTALE` for stale revisions; `EBUSY` for writer contention; `EBADMSG` for
  malformed/decryption failures; `EKEYREVOKED` for a missing record key;
  `EACCES` for an unavailable key provider. Failed commits report `EUNKNOWN`;
  inspect durable state rather than assuming the external operation failed.
- Explicit reset/recovery and idempotent migration markers. Missing keys never
  trigger silent record replacement. Migration rejects conflicting values or
  source digests and never removes the source records.
- Two key slots per scope. A reservation survives interruption during first key
  creation, allowing retry without continually allocating aliases. Reset uses
  the alternate slot; call `collectRetiredKey()` to retire the inactive key.

Limits per database are 256 scopes, two key slots per scope, 128 user records per
scope, 64 KiB per value, 256 KiB decoded aggregate, 32 migration markers and 16
coordination slots. Internal metadata counts toward aggregate/value bounds.
File deletion or database cloning outside this API can leave orphaned OS keys;
the bounds do not constrain arbitrary same-user filesystem tampering. This is
not protection against whole-database rollback or a hostile native extension.

The coordinator records `prepared`, `executing`, `unknown`, `aborted` and
`committed` outcomes, fenced by scope, generation and revision. Only prepared
work can be aborted without reconciliation. Interrupted execution becomes
unknown, never a successful transfer or a request to replay the operation.
Committed local record updates and a local receipt share one transaction.
Applications must query the remote authority and explicitly resolve unknown
outcomes. No local receipt proves remote authentication, push trust, protocol
reconnection or transfer completion.

The Android Kotlin store's v1 format is unchanged. C++ v2 is a separate format
and namespace, not an automatic migration or a replacement for already qualified
Android integrations. Migration requires a trusted native adapter, public-data
qualification and application acceptance before retiring an existing store.

## Repeatable verification

`.github/workflows/native-parity.yml` compiles and executes the real shared core
on Linux, macOS and Windows runners. It tests encrypted CAS, scope separation,
migration conflicts, concurrent writers, missing keys, reset, interrupted key
creation, process loss and lost-reply reconciliation. Its key fixture contains
only public test bytes and is not a production key provider. The key-store test
also exercises Linux error propagation and provider lifetime through host
facades; those checks do not qualify the operating system's key service.

Run the Linux workflow locally with `act` in a copied workspace:

```sh
XDG_CACHE_HOME="$PWD/.cache/act" act workflow_dispatch \
  -W .github/workflows/native-parity.yml -j unix-core \
  --matrix os:ubuntu-24.04 \
  -P ubuntu-24.04=ghcr.io/catthehacker/ubuntu:act-24.04 \
  --container-architecture linux/amd64 \
  --secret-file /dev/null --env-file /dev/null --var-file /dev/null
```

Do not use `--bind` against a shared tree with checkout/cleanup steps. The
default `act` checkout copies local source into its container; inspect its logs
to confirm that local uncommitted source was included.
Use a Docker container with a read-only source mount and writable temporary
build directory if `act` cannot run. Preserve the command, image digest, logs,
test counts, failures and skipped coverage. Required CI prerequisites fail the
job instead of turning a missing compiler/library into a passing skipped test.

These jobs qualify the shared core only. Native Keychain/Credential Manager
access, mobile build/linking, permissions, locked-session behavior, lifecycle,
UI and installed-package matching require separate platform acceptance.
Linux `act`/Docker cannot execute macOS/iOS/iPadOS or Windows OS services.

On Linux, after rebuilding the full native archive, run the production adapter
against a separate public test keyring:

```sh
node test/scripts/test-native-keyring.js
```

This requires the rebuilt native dependency archives, `clang++`,
`dbus-run-session`, `gnome-keyring-daemon`, and the Secret Service client library.
`ORO_TEST_NATIVE_RUNTIME_ARCHIVE` can select an isolated rebuilt runtime archive;
its headers must match the source tree. The runner creates private XDG data,
configuration, runtime and state directories and starts its own D-Bus session.
It checks encrypted writes, reset, retired-key cleanup, service lifetime,
separate-process reopen and a disconnected-bus failure. It does not inspect the
desktop session's credential database. Locked-keyring prompts and denied access
still need separate acceptance. Public artifacts remain in the printed
temporary directory for inspection.

## Qualification recorded on 2026-10-24

The working source is version 0.1.8 on base revision
`9e7e92e74d17fdf5eed358da8f09fee9c5bf1a14`, with uncommitted changes. The
following evidence describes this source candidate, not a released package:

| Check actually run | Result and scope |
| --- | --- |
| Linux shared-core workflow through `act` | Eight tests passed, no skips, using Ubuntu 24.04 / Node 24 |
| Docker fallback | Eight tests passed, no skips, using the same public Ubuntu runner image |
| Full Linux runtime archive build | Passed, including native records and the capability IPC route |
| Production Linux Secret Service adapter | Passed encrypted read/write/reset, retired-key cleanup, stop/destruction, process reopen and disconnected-bus rejection in an isolated public test keyring |
| Targeted Android host regressions | Ten passed; one generated-manifest check skipped because its paired CLI/SDK was not selected |
| macOS / Windows workflow jobs | Added; not executed on native hosts during this qualification |
| iOS / iPadOS build, UI and OS-service checks | Not executed |
| Newly packaged CLI + SDK and installed application | Not qualified for these changes |

The Linux container image used was
`ghcr.io/catthehacker/ubuntu:act-24.04`, local image ID
`sha256:cb041d0df9a749a73358ded823dd44f7c24111ef3efd152a3e6f3f4e3846f153`.
Locked-keyring prompts, denied-service access, desktop session changes and
non-Linux key-store behavior remain unverified.

## Remaining platform acceptance

| Work | Exact next check | Owner |
| --- | --- | --- |
| Shared core packaging | Rebuild CLI and SDK together; confirm the support route, core symbols and API source in the installed package | Runtime packaging |
| Apple records and keys | Build on Apple SDKs; round-trip/reset/reopen with Keychain, then test locked-device and denied-access errors | Runtime Apple |
| Windows records and keys | Run native core job, then round-trip/reset/reopen through Credential Manager and test access failures | Runtime Windows |
| Linux keys | Round-trip/reset/reopen against an isolated unlocked Secret Service; separately test locked/unavailable service | Runtime Linux |
| Authentication | Implement each platform's per-operation key binding, invalidation and recovery, then qualify actual system prompts | Runtime platform maintainers |
| Password selection | Implement provider selection with cancellation/rotation/process-loss handling and public test entries | Runtime platform maintainers |
| Background and push | Implement OS-specific execution/delivery backends; qualify reboot, suspend, cancellation and routing without assuming deadlines | Runtime platform maintainers |
| Application adoption | Integrate supported primitives, authoritative authorization and remote reconciliation; retire workarounds only after installed-app acceptance | Application/backend maintainers |

Historical Android qualification remains scoped to the recorded APKs and
devices. New shared source and new capability probes do not retroactively
qualify a different packaged runtime or close the platform work listed above.
