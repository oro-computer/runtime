# Native service bindings

Prefer JavaScript APIs and the bundled declarative workflow path for ordinary
application work. Neither a language rewrite nor a larger C++ adapter layer is
runtime gap closure. The following bindings support genuinely custom native
consumers and explicit legacy interoperability without application-owned JNI
class lookup, method signatures, DTO conversion or cancellation watcher threads.

## Android facade

The public header runtime/native/android_platform.hh supplies:

- https(NativeHttpsRequest, cancellation): fixed request/response types, shared
  native TLS policy, canonical binary encoding, eight-connection admission,
  1MiB bounds, rejected redirects and caller-owned cleanup.
- submitNotification/configureNotificationChannels/cancelNotification: the same
  validated presentation/channel rules as oro:notification.
- notificationStatus/activeNotifications/notificationResponses and acknowledgement:
  supported OS metadata and native-only private target correlation. No title/body
  introspection is required. Historical non-runtime OS entries are marked legacy;
  only application policy can map those old identities.
- openV1Records: explicit access to the existing Kotlin v1 NativeRecords and
  NativeCoordinator, retaining exact revision/commit/ticket/receipt semantics.

Record and HTTPS types are in runtime/native/android_services.hh. Notification
JSON uses the documented public notification option schema. No arbitrary Java
method or class name is a service argument. The SDK owns the private transport,
UTF-8/UTF-16 conversion, JNI local/global references, fixed error mapping and
Application context. U+0000 and supplementary characters are preserved.

Every service call requires an owned invocation or child thread. Keeping a
handle does not grant a later unrelated thread access. Access remains available
during owned cleanup, and is denied after backend drain. A JavaScript extension
result is still discarded if its original document has been cancelled/replaced.

## Owned child execution

Create a group inside an authorized callback using
platform.children(parentSignal, limit). All groups in that registry share eight
reserved slots. Each group has a limit1–8 and monotonically increasing opaque
handles. start returns a handle; cancel(handle) requests stop, and join(handle)
waits for actual cleanup before returning its status and releasing capacity.
close cancels and joins all remaining children.

A finished but unjoined child retains its slot. An uncooperative child keeps
close/drain waiting. Parent shutdown and deadlines propagate cancellation.
Unrelated connections have independent child signals. A child cannot wait on
its own group or drain its owning registry: EDEADLK is returned.

ExecutionCancellation.onCancel installs a removable event listener, including
on a signal already cancelled. Deadlines and delivery use a shared SDK event
dispatcher, not one polling thread per application operation. Copies of a
signal share its stop/deadline. Up to32 listeners per signal and256 observed
signals per process are supported. Explicit deadline values are0 (none) or
1–86400000ms. Callbacks should request resource cancellation and return;
registration destruction waits for an executing listener. It does not prove
that the resource itself has stopped.

An HTTPS call also inherits its owning callback/child cancellation, even if the
caller supplies an independent additional signal. Java CancellationSignal
creation, attachment, listener delivery and release belong to the SDK. The
owned HTTPS variant returns only after its connection/listener cleanup;
cancellation never frees its child admission early. SDK-managed child threads
receive fresh scoped Application/service access, never retained JNIEnv values.

## Explicit v1 interoperability

openV1Records(name, identity) consumes the existing v1 store directly. The name
follows NativeRecords.open rules and is distinct from the mandatory product.
prefix on C++ v2 openRecords. No source key or encrypted record is silently
replaced or converted.

Returned snapshots use binary maps, exact int64 revisions and commit IDs.
Coordinator prepare/begin/unknown/abort/commit/resolve/recover/release call the
existing v1 implementation. Revisions are encoded without JavaScript-number
precision loss. Migration markers still bind an explicit source digest.
Missing keys, stale writes, uncertain commits and absent outcomes retain their
existing fixed errors.

Application code still owns its legacy record codec, namespace/identity mapping,
old aliases, consent, account authorization and remote reconciliation. This
interop path does not claim signed historical migration or safe downgrade.
Keep old readers/originals until exact upgrade and interruption acceptance passes.

## Retirement and scope

Generic JNI conversion/watchers/service introspection can be removed only after
equivalent supported calls pass the application's own debug/minified-release,
UTF/NUL, both-ABI, cancellation and lifecycle acceptance. For common
read/checkpoint/notify work, replace the compiled handler with the declarative
workflow rather than rebuilding it around these C++ bindings.

Protected historical-key migration and retained component/wire/job compatibility
use the JavaScript/configuration APIs in [historical migration](HISTORICAL_MIGRATION.md).
The bundled aliases/codecs/AAD and published component names must match the
supported history. These service bindings do not make an arbitrary
legacy alias, schema, queued job or endpoint automatically compatible. Preserve
those sources until their explicitly scoped migration checks pass.
