# Declarative background workflows

Applications can use bundled data to connect a bounded HTTPS read to native
checkpointing and notifications. Application Kotlin, C++, JNI, receiver classes
and service implementations are not required for this path. Custom protocols
outside runtime concerns may still register compiled executors. A missing
standard backend or reusable workflow capability remains runtime work; it must
not become a requirement for application native glue.

The initial backend is Android. Other backends currently report unavailable;
their implementation belongs in the runtime, rather than in product adapters.
Implementation and device/release qualification are separate. Query
oro:workflows.getSupport before offering controls.

## Project configuration

Paths are relative to the project root containing oro.toml. Copy the immutable
definition into the package using the ordinary copy map:

~~~toml
[build]
copy_map = "copy-map.toml"

[workflows]
enabled = true
definitions = "workflows.json"

[foreground]
enabled = true

[background]
deferred_enabled = true

[push]
enabled = true
provider = "unifiedpush"

[permissions]
allow_notifications = true
~~~

A copy map can contain:

~~~toml
"index.html" = "index.html"
"workflows.json" = "workflows.json"
~~~

The runtime reads definitions from the APK asset manager, not a URL, service
worker, mutable page or downloaded policy. No application native callbacks flag
or source list is needed. Definitions have schema1, at most16 workflows and a
64KiB input bound. The executable public example is
test/fixtures/workflows/read-notify; it contains HTML, JSON, TOML and no Kotlin or
C++ source.

## Supported policy

Each workflow declares one fixed HTTPS GET URL or a fixed path with a separately
approved HTTPS origin, timeout1–60000ms and response
limit1–1048576bytes. Redirects, request bodies, downloaded code, eval, arbitrary
expressions and queued mutations are excluded. An optional named cursor query
parameter is the only runtime addition to the fixed URL. The existing approved
WebView/native TLS policy applies, including explicit certificate pins for
approved self-hosted authorities. Use the supported oro:tls APIs to manage that
trust; accepting a workflow does not approve a new server certificate.

For a self-hosted IP address or hostname chosen after installation, bundle
`"authority": "approved", "path": "/api/alerts"` instead of `url`. Call
`workflows.approveAuthority({ workflow, authorityRef, accountRef, targetRef,
origin, userInitiated: true })` in the foreground. A native dialog shows the
actual origin. Pass its returned `authorityHandle` to credential import and
binding. Approval is scoped to that definition revision and exact identity; no
background method accepts an arbitrary destination. Revoking an authority denies
new presentation and cancels active work. Certificate trust, server IAM and
remote revocation remain separate. Fixed-URL definitions do not accept a handle.

For a gateway mounted beneath a prefix, its bundled request must opt in with
`"allowApprovedBasePath": true`. Pass `basePath: "/gateway/team"` to
`approveAuthority`. The native dialog displays the full fixed GET URL. Prefix
segments accept ASCII letters, digits, dot, underscore, tilde and hyphen;
empty segments, traversal, percent escapes, query/fragment delimiters and
backslashes are rejected. The prefix is immutable in the approval and scoped
to the exact workflow revision, origin, authority, account and target. The
bundled read suffix remains fixed. Existing definitions retain root-only
behavior. Check `getSupport().approvedBasePaths` before offering this option.

### Read retry and outcome scope

`getSupport().readRetryPolicy === 'bounded-read-backoff-v1'` identifies the
Android safe-read implementation. Each read cycle permits five GET attempts
within 120 seconds, with delays of 1, 2, 4 and 8 seconds. Each request also obeys
the definition timeout and the owning foreground/deferred controller deadline.
Transport I/O failures, request timeouts and HTTP 408/429/500/502/503/504 may
retry. TLS/trust rejection, redirect, authorization failure, invalid JSON,
oversized responses, invalid scope and local storage failures do not retry.
Every attempt reacquires local authority, credential validity and TLS policy.

A successful retry lets a continuous monitor continue polling. After exhausting
transient retries, an explicitly running monitor waits at least 60 seconds (or
its longer configured poll interval) before a new bounded read cycle. Push hints
cannot bypass this cooldown. Authorization, TLS and permanent response failures
end the monitor. A finite run reports a known failed read; scheduled work uses
the shared scheduler's separate five-attempt admission budget, then suspends for
explicit reconciliation/resume. This bounds request rate without terminating a
user-started monitor on an extended network outage. Stop cancels the current HTTPS connection or wakes the owned backoff wait.
Inspect `drained` before releasing handles. `readAttempts` and `retryAt` expose
bounded progress metadata without exporting credentials or response content.
No service or monitor restarts automatically after process loss.

Only the GET and response validation are inside the retry loop. Notification
attempt records precede OS submission, and an interrupted submission remains
unknown without automatic replay. A successful read/submission still does not
establish human attention, server acknowledgement or remote mutation success.
The capability names the implementation; exact device, outage, Doze/reboot,
minified product, historical upgrade and deployment acceptance retain their own
qualification scope.

### Executor composition and correlation

`getSupport().executorComposition === 'workflow-namespace-v1'` reserves space
for sixteen bundled `workflow.*` registrations in each finite, continuous,
deferred and push registry, separately from sixteen application registrations.
The active-binding limit remains eight. Custom handlers should use their own
application prefix; they must not reuse a workflow kind or credential source
key belonging to a different identity. Duplicate registration remains invalid.

`getSupport().correlationVersion === 'workflow-events-v1'` identifies the public
event mapping. An event handle is lowercase hexadecimal SHA-256 of the UTF-8
bytes of the declared server event-ID string, without trimming or Unicode
normalization. Deduplication is scoped to the binding. Both `getEvents` and
`getResponses` include `notificationId`, the exact identifier accepted by
`oro:notification` cancellation. Prefer that returned identifier over building
one in application code. These fields contain no event title, body, credential
or source payload. Releasing an event outcome permits a future submission of
that event; cancellation alone does not release its deduplication record.

Responses are bounded JSON objects with a declared items pointer, event-ID
pointer and optional cursor pointer. Each read accepts at most32 events and
JSON nesting is capped at32. JSON pointers support ordinary object keys, array
indices and the standard escaped slash/tilde forms. There is no expression
language. Declared identity checks compare response strings against the exact
bound authority/account/target references before a notification plan is returned.
A bearer workflow must check its account reference.

Notification title/body pointers are bounded to160/512characters. Public
lock-screen content is a literal packaged publicVersion, never selected from
server data. Channels and group policy are application configuration and use
the existing shared notification implementation. Submission does not prove
display, sound, user attention or alarm acknowledgement.

## Credentials and application authority

The application first performs its own foreground sign-in/server authorization.
It can store its approved read-only bearer using the existing
oro:secure-storage.setItem API. The workflow definition fixes the source key:

~~~json
"authentication": {
  "type": "bearer",
  "key": "approved-read-only-bearer"
}
~~~

Then JavaScript calls workflows.importCredential with the definition ID, opaque
authority/account/target references and an explicit user action. The runtime
reads that fixed key from the invoking document's secure-storage origin and
returns an opaque credentialHandle. Workflow methods accept no token, password,
private alias selector or arbitrary source-key parameter. Keep secret-bearing
sign-in/storage operations outside assistant tool arguments.

The native copy is encrypted, excluded from JavaScript storage enumeration and
bound to the definition revision, invoking origin and exact identity references.
A definition change, scope mismatch, expiry or local revocation denies reuse.
An application update cannot silently send a cached credential to a changed
workflow destination. Original source storage remains untouched.

This binding does not establish account IAM. The application still owns server
authentication, token issuance/refresh, consent and correct schema/identity
mapping. The runtime rechecks its local binding, approved TLS state and current
credential before presentation; HTTP401/403 is not treated as a successful read.

## JavaScript lifecycle

~~~js
import workflows from 'oro:workflows'
import foreground from 'oro:foreground'

const state = await workflows.bind({
  workflow: 'account-alerts',
  authorityRef,
  accountRef,
  targetRef,
  credentialHandle,
  userInitiated: true
})

const admitted = await workflows.run({
  id: state.id,
  userInitiated: true
})
// Admission is not completion. Inspect the shared finite controller:
const outcome = await foreground.getStatus(admitted.id)
~~~

Import and bind are foreground operations owned by the current trusted document.
Background execution thereafter belongs to the runtime and does not keep a
WebView alive. Schedule read-only catch-up with workflows.schedule; OS scheduling
and periodic minimums remain those of oro:background/deferred. The runtime
registers its standard executors at Application startup, including cold jobs.

For explicitly user-started ongoing monitoring, bundle `pollIntervalMs`
(5000–3600000) and opt into `[foreground] continuous_enabled = true` with a
truthful `continuous_subtype`. Call `workflows.monitor({ id, userInitiated: true })`.
The shared continuous controller owns the visible Stop action and service
lifetime. A push received during monitoring durably queues a read hint and wakes
the existing worker. Monitoring does not automatically restart after boot,
force-stop or process loss. Query `oro:foreground/continuous` for admission and
outcomes; scheduling and push remain independently available.

For push, get workflow references using workflows.getPushOptions and pass them
to oro:push.register with the application's server key. Foreground JavaScript
uses the existing push enrollment APIs to authenticate server enrollment and
acknowledge its exact endpoint generation. Background pushes are hints to fetch
the fixed approved URL; their payload is not arbitrary code or notification
content. Endpoint renewal requiring new server enrollment stays pending until
the application confirms it. Transport registration is not trusted delivery.

## Stop, outcomes and retention

Stop persists local disablement and changes the binding generation before
requesting cancellation. A worker still cleaning up reports drained:false and
retains admission. Credential revocation disables local use and cancels associated
calls; it does not revoke a server grant. Existing notifications and transport
registrations require their existing explicit cancellation/unregister APIs.

Before each OS submission, the runtime durably records an attempt. After a
successful OS call it records submitted. A lost reply, interrupted process or
failed commit retains unknown and does not authorize automatic resubmission.
The cursor and event ledger are encrypted. Public status contains counts and
opaque handles, not credentials, cursors or server response bodies. A new
independent read does not settle an older uncertain notification.

At most8 bindings,16 credential copies,16 approved authorities and128 retained
event outcomes per binding
are allowed. Saturation returns ENOSPC; no automatic pruning drops deduplication
protection. getEvents returns opaque outcomes. releaseEvents explicitly consumes
selected records after application reconciliation and can make those IDs eligible
again; it is not a server acknowledgement. Disabled/drained bindings with consumed outcomes and locally
revoked unused credential copies have explicit release operations. Release all
referencing bindings/credentials before releasing a revoked authority. The
foreground `discover` API recovers handles for an exact application-resolved
identity and the invoking document origin after UI registry loss; it does not
return tokens, cursors or payloads, or establish server authorization. Consumed finite
run receipts should be released through oro:foreground. Preserve unknown outcomes
until the application has reconciled them.

A notification response is an opaque event hint. Restore current account and
navigation authorization before opening it, then acknowledge the retained receipt.
Private native correlation is never included in generic notification diagnostics.

## Verification boundary

Source files are workflow_policy.kt and native_workflows.kt under
src/runtime/app, plus api/workflows.js. Host policy checks cover scope validation,
bounded input, malformed UTF-8, duplicate IDs, revision changes and code exclusion.
OS scheduling, real network cancellation, cold push, notification presentation,
credential revocation, process loss, signed upgrades and each supported backend
must be qualified against exact SDK/APK artifacts. An unexecuted scenario is not
a confirmed defect.

## Repeatable public checks

Run the host policy/startup and owned-cancellation checks after rebuilding the
artifacts and supplying the Kotlin/JVM fixture dependencies:

~~~sh
node --test test/unit/workflow-policy.test.js test/unit/android-startup-timings.test.js \
  test/unit/native-children.test.js test/unit/native-unicode.test.js
~~~

The isolated Android driver is `test/scripts/test-declarative-workflows-android.js`.
It requires the matching example APK already installed on an explicitly selected
device and an explicitly selected loopback public TLS fixture port. The example's
placeholder fixed URLs must be replaced in a **copy** before packaging, and its
`[webview] tls_pins` must contain that test server's certificate SHA-256. Start the
public-only server in `test/fixtures/workflows/public-server.cjs` with
`ORO_PUBLIC_WORKFLOW_STATE` pointing to a private writable directory containing
`public-key.pem` and `public-cert.pem`; it writes `public-port.txt`. Forward that
single port using `adb -s SERIAL reverse tcp:PORT tcp:PORT`. Never package the
private test key. The product HTTPS code must pass the packaged pin; only the
host driver's separate loopback control requests bypass CA validation.

~~~sh
ORO_TEST_ANDROID_SERIAL=SERIAL ORO_TEST_WORKFLOW_PORT=PORT \
  node test/scripts/test-declarative-workflows-android.js
~~~

The driver uses only `oro.runtime.qualification.workflows`, public fixture data
and the supplied server. It checks real pinned reads, deduplication, private
metadata redaction, periodic admission, continuous background reads, Stop/drain,
opaque credentials, scope denial, revocation and interrupted-process outcomes.
It deliberately closes that public application to check process loss. Keep the
test app focused except for its automated Home check. The app's UI storage is
only a hint: restore a saved ID through `getStatus`, or use exact-identity
`discover` and an explicit selection. Do not infer authoritative binding state
from a stale localStorage value.

Cold authenticated push, reboot/Doze, notification actions and historical signed
upgrades remain separate acceptance checks. R8 builds using debug native archives
qualify shrinker compatibility for that artifact, not a production native build
or production signing policy. Preserve APK hashes, SDK manifest, compiler
fingerprint, test output and unexecuted checks with each result.
