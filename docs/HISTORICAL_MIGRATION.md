# Historical Android migration

`oro:migration` imports allowlisted AES-GCM envelopes and preserves published
Android receiver, service, job and Activity alias names. Applications configure
data in the project root containing `oro.toml`; common migration mechanics do
not require application Kotlin, C++, JNI or receiver implementations.

This is an Android backend. Probe `getSupport()` before offering migration.
The ready versions are `protectedImport: "protected-import-v1"` and
`retainedComponents: "retained-components-v1"`. A configured backend reports
`nativeIntegrationRequired: false`. Other platforms and older runtimes report
unavailable; operations reject `ENOSYS`. A JavaScript module alone cannot add
these implementations to an older packaged runtime.

The JavaScript compatibility probe requires
`authentication.status.historicalMigrationProtocol === 1` before sending any
migration operation. An older Android backend therefore returns unavailable
without receiving an unknown migration request.

Support results include `platform`, `implementation`, `backend` and `reason`.
On iOS/iPadOS, macOS, Linux and Windows the reason is
`migration-backend-unimplemented`; older Android runtimes report
`migration-protocol-unavailable`. Operational rejection uses `ENOSYS` with
`feature: "migration"`, platform and reason metadata. Disable the migration
action and explain that the installed runtime lacks this backend; do not fall
back to plaintext storage or an unowned background JavaScript task. A configured
Android backend with disabled/invalid definitions returns its configuration
error in the support result. Permission, missing keys (`EKEYREVOKED`), corrupt
data (`EBADMSG`), stale versions (`ESTALE`) and cancellation (`ABORT_ERR`) remain
distinct from a missing backend.

## Bundled policy

```toml
[migration]
enabled = true
definitions = "migration.json"

[permissions]
allow_biometrics = true
```

The CLI validates the definition, generates Kotlin subclasses of the SDK's
retained adapters and adds their manifest entries. It copies the immutable
definition into `oro-migration.json` and binds it through normal native/asset
build evidence. Do not also declare the same classes in application sources or
duplicate their manifest entries. Existing published names must remain exact.

```json
{
  "schema": 1,
  "protected": [
    {
      "id": "saved-sign-in",
      "preferences": "application.history",
      "key": "protected-sign-in",
      "alias": "application.historical.sign-in",
      "aad": "{package}:saved-sign-in",
      "encoding": "base64",
      "ciphertextField": "ciphertext",
      "authentication": "biometric-or-device",
      "policy": "biometric-or-device"
    }
  ],
  "components": []
}
```

Definitions contain at most 16 protected sources and 16 retained components in
64 KiB. Selectors come exclusively from that bundled policy. JavaScript supplies
an ID and a bounded opaque `sourceRef`, never an alias, filesystem path, codec,
AAD string or component class. `preferences` selects a fixed private
SharedPreferences file. `key`, `alias` and `aad` support only `{package}` and
`{sourceRef}` substitution. Source references accept letters, digits, `_` and
`-`, at most 128 characters. Current reserved `oro.` aliases are excluded.

The supported envelope is JSON containing `iv` and the configured ciphertext
field. `encoding` is standard Base64 or URL-safe Base64; the IV is 12 bytes,
GCM tag is 128 bits, and plaintext is at most 64 KiB. Source envelopes are
bounded to 100,000 UTF-8 bytes and JSON nesting to 16. Schemas and the meaning
of decrypted application fields remain application policy.

## Protected import and private use

```js
import migration from 'oro:migration'

const source = { profile: 'saved-sign-in', sourceRef: 'public-account-reference' }
const { sourceSha256 } = await migration.inspectProtected(source)
const imported = await migration.importProtected({
  ...source,
  sourceSha256,
  authorityRef,
  accountRef,
  targetRef,
  userInitiated: true
}, { signal })
```

`inspectProtected` returns a source version: the exact ciphertext digest for
AES-GCM sources, or a keyed opaque version for native/plain metadata sources.
Plaintext hashes are not exposed for dictionary guessing. Import opens the
original key read-only; a missing or invalidated key is never replaced.
Authentication-bound sources require per-operation key metadata and a system
CryptoObject prompt for the original decrypt. The new encrypt has its own
CryptoObject prompt under the configured runtime protection policy. These are
two distinct operations, not a cached authentication boolean. Source
`authentication: "none"` is allowed for historical background envelopes only;
it rejects a key that actually requires authentication, and the new protected
copy still requires its own system prompt.

No plaintext leaves the runtime during import. Plain byte buffers are cleared
after rewrap. The returned opaque handle refers to an encrypted native record,
bound to the invoking document origin, application identity references, bundled
policy revision and exact original digest. Source changes before commit reject
`ESTALE`. Original SharedPreferences values and aliases remain untouched on
success, cancellation, corruption or interruption. At most 16 imported records
and the existing 64 authentication-key limit are supported; exhaustion rejects
`ENOSPC` rather than overwriting history.
Ciphertext bodies use bounded native chunks committed atomically with their
metadata, so the full 64 KiB plaintext limit fits the underlying record limit.
All migration state shares a 256 KiB aggregate decoded-byte budget; individual
capacity maxima do not override that budget. Policy and request fingerprints
use sorted JSON keys, so property ordering does not change their identity.

When an imported runtime key is lost, explicit recovery can provide a new opaque
`recoveryRef` nonce to `importProtected`. The original historical source must
still be readable and its current version must match. The nonce selects a new
protected copy; retries with the same nonce return that copy. Earlier imports
remain retained. This does not recreate a lost historical key or restore server
authorization.

Retrying an already committed import returns its existing handle. Use
`discoverProtected({profile, sourceRef, authorityRef, accountRef, targetRef})`
after a lost reply. That does not unlock or export a value. To use it in the
owning private foreground form, call
`openProtected({handle, userInitiated: true}, {signal})`. It always runs a fresh
per-operation prompt and returns Base64 bytes only to the original document.
Never log those bytes or pass them to assistant tools. Application JavaScript
can decode its historical schema and perform its authenticated account flow.

Navigation, Activity stop, renderer replacement, abort and the shared 90-second
deadline invalidate pending prompts/replies. Invalidation/missing keys require
explicit recovery; bad GCM authentication is corruption. Neither condition
authorizes deleting originals, creating a replacement historical key or
reauthorizing a server account. Retain the relevant policy entries while their
imported records are still used; changing a policy revision fails closed.

## Existing native records and shortcut metadata

For an application already using the SDK's Kotlin v1 record store, select its
exact existing namespace and identity without application JNI:

```json
{
  "id": "old-registry",
  "format": "native-records-v1",
  "records": {
    "namespace": "application.history",
    "identity": {
      "authorityRef": "application.history",
      "accountRef": "{package}",
      "identityRef": {
        "preferences": "application.registry",
        "key": "identity",
        "default": "installation-v1"
      }
    }
  },
  "key": "registration.{sourceRef}",
  "authentication": "none",
  "policy": "biometric-or-device"
}
```

Identity selectors are fixed literals, `{package}`, or fixed private preference
lookups with explicit defaults. They cannot select `oro.` scopes or receive an
arbitrary namespace from JavaScript. Reading never resets a missing/corrupt key.
The native row, revision, commit and wrapping key are preserved. New runtime
scopes can change the shared SQLite file; this does not imply that the original
application row was rewritten. Opaque source versions use an app-private key
and include the source wrapping-key epoch.

Unencrypted historical JSON metadata can use `format: "preferences-json"`,
fixed `preferences` and `key` selectors, `authentication: "none"` and an explicit
target protection `policy`. Import can rewrap it without exposing a value.
For existing foreground schema/routing integration, an unprotected source may
also declare `exposure: "private-document"`. Only then can
`readUnprotected({profile, sourceRef, userInitiated: true}, {signal})` return
Base64 bytes after native foreground consent. Authentication-bound sources
always reject that path. It is not a general record-store route or a permission
to expose values through assistant tooling.

## Retained receivers and jobs

A noninteractive historical registry source uses the same source format with
`authentication: "none"`. Its fixed alias and AAD read the old encrypted record
without exporting tokens or endpoint values to JavaScript. A retained job entry
can then declare:

```json
{
  "id": "old-refresh",
  "className": "example.application.LegacyRefresh",
  "type": "job",
  "source": "old-registry",
  "referenceExtra": "clientId",
  "referencePointer": "/clientId",
  "generationPointer": "/generation",
  "minimumJobId": 23000,
  "maximumJobId": 23007,
  "identityFields": {
    "authorityRef": { "pointer": "/authority", "encoding": "sha256" },
    "accountRef": { "pointer": "/account", "encoding": "sha256" },
    "targetRef": { "pointer": "/clientId", "encoding": "text" }
  }
}
```

Identity mappings support fixed object pointers and either literal text or
lowercase hexadecimal SHA256 of its UTF-8 value. All three identity fields must
match the replacement's already-approved workflow binding. If old jobs carry a
generation extra, declare `generationExtra` and stale callbacks are rejected.
If the old record has no generation field, omit `generationPointer`: its exact
envelope digest is the source generation. The runtime does not invent a
historical generation that the old writer never recorded.
An optional `checks` array declares up to eight scalar predicates, for example
`{"pointer":"/revoked","equals":false,"default":false}`. Missing values reject
unless the bundled policy explicitly declares their default. Checks and identity
mapping are revalidated for each admitted input. With an explicit logical
generation, unrelated checkpoint changes need not invalidate an active mapping;
the application must advance that generation when authorization changes.
Source policy, resolved native scope and original wrapping-key epoch remain
independently pinned, so key/scope replacement always requires fresh handover.

Receiver entries use `type: "receiver"`, a bounded `actions` allowlist,
`tokenExtra`, `tokenPointer`, and either `senderPointer` or fixed
`senderPreferences`/`senderKey`. Both the source token and sending distributor
UID must match. Older Android versions without sender UID require a matching
PendingIntent creator proof. Source-key families are bounded to 32 candidates.
The receiver never trusts old payload text as a notification or a grant. It
retains bounded selected input fields encrypted and may request a fresh read
from the new fixed authorized workflow. It does not acknowledge an old transport
message or register a changed endpoint automatically.

`type: "service"` retains an importance binder with a fixed action allowlist;
it exposes no methods or account authority. `type: "activity"` retains an
Activity alias targeting exactly the configured main Activity. Application
routing must still interpret and authorize historical destinations; preserving
a class name is not proof that every old navigation target works.
For aliases that need old intent references, declare `actions` and
`referenceExtras` (at most eight each). `getHistoricalEntries({component})`
returns at most 15 primary-window entries containing only bounded opaque
reference strings, action, timestamp and `untrusted: true`. No arbitrary extras,
labels, URLs or payloads are forwarded. Authorize the current destination and
then call `acknowledgeHistoricalEntry({component, entryId})`. Old private metadata
can be read through the separately consented source above. Repeated Activity
start callbacks for the same intent object do not produce duplicate entries.

```js
const source = await migration.inspectHandover({
  component: 'old-refresh', sourceRef: legacyClientId
})
const prepared = await migration.prepareHandover({
  component: 'old-refresh', sourceRef: legacyClientId,
  ...source,
  expectedGeneration: '',
  bindingId: binding.id, bindingGeneration: binding.generation,
  authorityRef, accountRef, targetRef,
  userInitiated: true
}, { signal })
const active = await migration.activateHandover({
  id: prepared.id, generation: prepared.generation
})
```

Preparation requires native foreground consent. Activation rechecks the exact
source digest/generation, policy revision and replacement generation. Retry is
idempotent within the same prepared generation. `discoverHandovers` resolves
lost replies for an exact application identity. An existing historical reference
cannot be rebound to a different authority/account/target. A changed generation
returns `ESTALE`; nothing falls back to arbitrary payload-based routing.

The adapters retain OS component names, queued job IDs and schedules. Activation
does not cancel a historical job, unregister an endpoint or delete old registry
metadata. Job callbacks can run only a fixed safe GET on the approved replacement.
OS scheduling, process lifetime and current runtime authorization still apply.
Jobs have a 25-second owned deadline; receivers finish their OS callback within
eight seconds. Two workers and eight queued entries bound adapter admission.
No receiver opens a WebView or automatically starts foreground monitoring.
The CLI retains the boot permission required by persisted legacy jobs even when
the ordinary deferred-work backend is disabled.

`stopHandover` fences its generation and requests cancellation of admitted job
reads. Poll the returned new generation until `drained` before treating cleanup
as complete. Stopping handover does not revoke the replacement workflow itself;
stop/revoke that binding separately when ending its independently authorized work.

Status, Stop and retained-receipt inspection/resolution remain available after
the source key or bundled source policy changes. Those recovery operations do
not authorize new input admission. Activation and input admission still require
the pinned source, identity, policy and wrapping-key epoch to validate.

## Uncertain outcomes and historical qualification

Inputs are recorded before read admission. Interrupted, unassigned or unconfirmed
inputs remain `unknown`. A completed fixed GET is `read-completed`, not proof of
the old notification's delivery, server acknowledgement, a remote mutation or
human attention. An unresolved interrupted job blocks automatic redispatch of
that source generation. No uncertain legacy mutation or OS notification is
blindly replayed.

`getRetainedReceipts` exposes IDs, source generation, kind, timestamp and outcome,
not private payloads. The application must authenticate and verify its own server
reconciliation, then explicitly call `resolveRetainedReceipt` with an outcome
and receipt digest. The result states `remoteOutcomeVerifiedByRuntime: false`.
Only explicitly resolved receipts can be released. Unknown receipts never expire
automatically; the 48-receipt limit returns `ENOSPC`. Preserve the original source
and any still-needed receipts until signed upgrade/rollback acceptance passes.

The executable public fixture is `test/fixtures/migration/public`: only HTML,
JSON and TOML are application inputs. `SeedInstrumentation.kt` belongs in a
separate test APK and creates public test aliases/envelopes and an old job. It
must never ship in the application. Seeded-history tests do not qualify private
historical releases, production signing, old endpoint handover, all OEM prompt
policies or remote IAM. Those require their exact historical artifacts and
application/backend owners.

In a source checkout, repeatable fixture preparation, the separate seeder APK, required prompts and
the four boundary phases are documented in
[`test/fixtures/migration/README.md`](../test/fixtures/migration/README.md).
Cancellation phases must not receive user approval. The key-recovery phase
intentionally removes only the isolated fixture's historical test key and runs
last. Passing reports retain their exact device, SDK and APK scope; they do not
authorize retiring a product's historical compatibility path.

## Selecting between two installed histories

A single retained component still names one `source`. A bundled
`prefer-native-records-v1` profile can select between one fixed NativeRecords
scope and one fixed, noninteractive AES-GCM preference family. It is a read-only
selector, not an automatic migration or recovery operation. Probe
`sourceSelection === "prefer-native-records-v1"` and
`legacyProvenance === "android-json-pairs-sha256-v1"`; protocol 1 alone does not
establish support for these additional formats. Older SDKs reject the definition.

```json
{
  "schema": 1,
  "protected": [
    {
      "id": "old-registry",
      "format": "aes-gcm",
      "preferences": "application.background",
      "key": "registration.{sourceRef}",
      "alias": "application.background.v1",
      "aad": "registration.{sourceRef}",
      "encoding": "base64url",
      "ciphertextField": "body",
      "authentication": "none",
      "policy": "biometric-or-device"
    },
    {
      "id": "copied-registry",
      "format": "native-records-v1",
      "records": {
        "namespace": "application.alarms",
        "identity": {
          "authorityRef": "application.alarms",
          "accountRef": "{package}",
          "identityRef": {
            "preferences": "application.alarm.registry",
            "key": "identity",
            "default": "installation-registry-v1"
          }
        }
      },
      "key": "registration.{sourceRef}",
      "authentication": "none",
      "policy": "biometric-or-device",
      "provenance": {
        "legacy": "old-registry",
        "markerKey": "registry.v1",
        "format": 1,
        "formatPointer": "/format",
        "digestPointer": "/sourceSha256",
        "digestFormat": "android-json-pairs-sha256-v1"
      }
    },
    {
      "id": "installed-registry",
      "format": "prefer-native-records-v1",
      "primary": "copied-registry",
      "fallback": "old-registry",
      "authentication": "none",
      "policy": "biometric-or-device"
    }
  ],
  "components": []
}
```

Use `source: "installed-registry"` in the existing receiver/job definition;
keep its published class, sender/token checks, reference/generation mapping and
identity fields unchanged. Do not declare two components with the same class.
The primary must declare provenance against the selected fallback. Both leaf
sources must have the same target authentication policy as the selector and
exactly one final `{sourceRef}` placeholder after a nonempty key prefix. Selection
cannot nest or run page-supplied code. Only an explicit `exposure` on the selector
permits private-document reads; child exposure does not grant that permission.

| Stored history | Result |
| --- | --- |
| Native scope is absent (revision zero) | Read the AES original, without creating that scope or key. |
| Native scope exists, marker matches current legacy family | Read the native row. Native progress is authoritative even if old ciphertext contains different progress. |
| Native scope exists but requested row was deleted | `ENOENT`; never resurrect the old row. |
| Missing/malformed marker or marker format/digest | `EBADMSG`; preserve originals. |
| Well-formed marker records a different legacy family digest | `EEXIST`; require explicit application reconciliation. |
| Guarded legacy digest or selected format changes during the checked read | `ESTALE`; obtain a fresh version. |
| Native wrapping key was removed or invalidated | `EKEYREVOKED`; no fallback or automatic reset. |
| Legacy family for native provenance validation exceeds 32 entries, 100000 bytes per value or 262144 aggregate name/value UTF-8 bytes | `ENOSPC`; no truncation. |

The digest includes **every** preference key beginning with the expanded legacy
prefix, including keys that are not valid `sourceRef` identifiers. Values must be
strings. Sort keys by unsigned UTF-8 bytes, form Android `org.json.JSONArray`
`[[key, ciphertextString], ...]`, serialize using its `toString()`, then SHA-256
the exact UTF-8 bytes. The empty family hashes `[]`. This matches that specific
historical format; it does not accept a generic JSON canonicalization or
plaintext digest. A missing key among otherwise retained ciphertext changes the
digest, as does an old writer changing the envelope. Nonstring values reject
`EBADMSG`.

The runtime checks legacy digest around the native snapshot and checks a
legacy-to-native selection transition around legacy reads. SharedPreferences
and NativeRecords do not share a transaction. Applications must quiesce old
writers for handover; these checks cannot exclude a write immediately after the
last check or a change-and-revert between snapshots. Loss or restoration of the
entire native database can also make a previously present scope appear absent;
applications must reconcile restored installation state before authorizing a
replacement. The digest is provenance,
not server authentication, authorization or proof of endpoint revocation.

Changing from AES selection to native selection invalidates the retained
handover policy epoch. Prepare and approve a new replacement; an old active
mapping cannot silently authorize the new source. Static profile dependencies
also bind protected imports. Opening a successfully imported copy does not
require the old source/key to remain available. Existing single-source policies
without provenance retain their prior policy identity.

## Bridge release before replacing published components

Generated adapters admit a read only after a foreground-approved replacement
handover. Valid historical input before activation is recorded as unknown; the
SDK does **not** render the previous application's alarms. Retaining a class name
alone does not preserve historical endpoint, schedule or presentation semantics.
If the application needs uninterrupted historical behavior before first launch,
use a bridge release:

```toml
[migration]
enabled = true
definitions = "migration.json"
generate_adapters = false
```

This option validates and bundles the same immutable policy, emitting no Kotlin
adapters or manifest entries. Keep the already published application component
classes, manifest declarations and old delivery implementation in that build.
No new application JNI or reader is required for source selection. The output
must not contain previously generated migration adapters; use a separate output
path for the bridge instead of changing modes in an existing generated tree.
Disabling generation without retaining the old components loses their OS entry
points and is not a safe bridge.

In the bridge's private foreground UI, inspect the selected history and use the
normal workflow and migration APIs to prepare the replacement. Reconcile old
endpoint ownership, outstanding jobs and unknown outcomes with the backend,
quiesce the old writers, and obtain the required user approval. Persist the exact
workflow binding and handover generation. The application decides the release
boundary and must verify that the old implementation does not duplicate the
new workflow while both are available.

A later signed upgrade enables generated adapters (the default) and removes the
old application implementations and their duplicate manifest entries. The
bundled JSON bytes are identical between modes. Preserve bundle ID, signer,
document origin, policy, scope identity, source generation and key epoch, plus
the approved replacement workflow. A support probe reports `generatedAdapters`
for the build; changing generation mode alone does not alter the policy digest.

Acceptance requires actual old APK → bridge → generated APK upgrades without
clearing data, for both stored histories, including an old-writer digest change,
a deleted native row, key loss, pre-handover delivery, post-handover receiver/job
execution, notification routing and exact-generation Stop. Host tests and a new
public APK do not establish those production upgrade results. Broader Android
OEMs, historical signers and server reconciliation remain separate checks.
