/**
 * @module workflows
 * Bundled declarative background reads and notifications. No application native
 * executor or background JavaScript is required on implemented platforms.
 * Definitions are immutable packaged assets. IAM, server enrollment, schemas,
 * consent and routing remain application policy.
 *
 * Platform support: the declarative executor is implemented on Android only.
 * Linux, macOS, iOS/iPadOS and Windows workflow backends are not implemented,
 * even where their lower-level scheduling, push or native execution APIs exist.
 * `getSupport()` returns available false and reason workflow-backend-unimplemented
 * on those platforms; operations reject ENOSYS. Probe before enabling workflows;
 * there is no arbitrary background JavaScript or automatic native-extension fallback.
 * Android requires bundled definitions and OS admission. A successful read or
 * local receipt does not establish trusted push, server authorization, protocol
 * reconnection, completed transfers or production/device qualification.
 */
import platformRequest from './internal/platform-request.js'

/**
 * @typedef {object} WorkflowRequestOptions
 * @property {AbortSignal} [signal]
 */
/**
 * @typedef {object} WorkflowSupport
 * @property {boolean} available
 * @property {string} [platform] Current platform on an unavailable backend result.
 * @property {'missing'|'restricted'} [implementation] Unavailable backend inventory.
 * @property {string} [reason] Fixed unavailable reason, including workflow-backend-unimplemented.
 * @property {string} [backend] Native backend name when reported.
 * @property {string[]} workflows Bundled definition IDs.
 * @property {boolean} nativeIntegrationRequired Always false for these workflows.
 * @property {boolean} backgroundJavaScript Always false.
 * @property {string} [readRetryPolicy] bounded-read-backoff-v1 when supported.
 * @property {number} [maximumReadAttempts] Five attempts including the initial GET.
 * @property {number} [minimumReadCycleCooldownMs] Continuous monitors wait at least this long after exhausting transient read retries.
 * @property {number} [maximumReadBudgetMs] Total read retry budget, excluding notification submission.
 * @property {string} [executorComposition] workflow-namespace-v1 reserves independent registry capacity.
 * @property {number} [maximumDefinitions] Bundled definitions, independent of binding capacity.
 * @property {number} [maximumApplicationHandlersPerType] Application handlers coexist with bundled workflows.
 * @property {string} [correlationVersion] workflow-events-v1 identifies event/notification correlation.
 * @property {boolean} [approvedBasePaths] Bundled allowApprovedBasePath permits a native-approved prefix.
 * @property {number} maximumBindings
 * @property {number} maximumRetainedEventsPerBinding
 * @property {string} [error] Fixed configuration error, empty when valid.
 */
/**
 * @typedef {object} WorkflowIdentity
 * @property {string} workflow Bundled definition ID.
 * @property {string} authorityRef Application-resolved opaque authority.
 * @property {string} accountRef Application-resolved opaque account.
 * @property {string} targetRef Application-resolved opaque target.
 * @property {string} [authorityHandle] Required for a definition using an approved destination.
 * @property {boolean} userInitiated Must be true after the application's explicit user action.
 */
/**
 * @typedef {WorkflowIdentity & { expiresAt?: number }} WorkflowCredentialOptions
 */
/**
 * @typedef {WorkflowIdentity & { credentialHandle?: string }} WorkflowBindingOptions
 */
/**
 * @typedef {object} WorkflowId
 * @property {string} id Opaque installation binding ID.
 */
/**
 * @typedef {object} WorkflowCredential
 * @property {string} credentialHandle Opaque native-only credential reference.
 * @property {boolean} valueExported Always false.
 */
/**
 * @typedef {object} WorkflowStatus
 * @property {string} id
 * @property {string} workflow
 * @property {string} generation
 * @property {boolean} enabled
 * @property {'idle'|'running'|'unknown'} state
 * @property {string} lastOutcome A completed read/submission is not presentation or server acknowledgement.
 * @property {boolean} running
 * @property {boolean} drained False while an owned worker still holds resources.
 * @property {string} interruptedOutcome Retained unknown outcome after process loss.
 * @property {number} [readAttempts] Attempts in the current or most recent read cycle.
 * @property {number} [retryAt] Next wall-clock retry time, zero when no backoff is pending.
 * @property {boolean} pendingRead A durable push hint awaits a read; this does not establish delivery.
 * @property {number} retainedEvents
 * @property {number} uncertainEvents Held without automatic resubmission.
 */
/**
 * @typedef {WorkflowId & { userInitiated: boolean, durationMs?: number }} WorkflowRunOptions
 */
/**
 * @typedef {WorkflowId & { intervalMs?: number, network?: 'any'|'unmetered', requiresCharging?: boolean, requiresBatteryNotLow?: boolean }} WorkflowScheduleOptions
 */
/**
 * @typedef {object} WorkflowPushOptions
 * @property {string} id
 * @property {string} kind
 * @property {string} authorityRef
 * @property {string} accountRef
 * @property {string} targetRef Native workflow binding reference.
 */
/**
 * @typedef {object} WorkflowResponse
 * @property {string} receiptId
 * @property {string} [notificationId] Notification identifier under correlationVersion workflow-events-v1.
 * @property {string} eventHandle Opaque correlation; contains no server payload.
 * @property {string} action
 * @property {number} receivedAt
 */
/**
 * @typedef {object} WorkflowRevokeOptions
 * @property {string} credentialHandle
 */
/**
 * @typedef {object} WorkflowRevokeResult
 * @property {boolean} revokedLocally
 * @property {boolean} serverRevocationConfirmed Always false.
 */

/**
 * Probe configuration and implementation without reading saved credentials.
 * An unavailable platform is runtime implementation work, not a requirement for
 * product Kotlin/C++ adapters.
 * @returns {Promise<WorkflowSupport>}
 */
export async function getSupport () {
  return platformRequest('workflows', 'support', {})
}

/**
 * @typedef {WorkflowIdentity & { origin: string, basePath?: string }} WorkflowAuthorityOptions
 */
/**
 * @typedef {object} WorkflowAuthority
 * @property {string} authorityHandle
 * @property {string} [basePath] Native-approved immutable prefix, empty for root-mounted gateways.
 * @property {string} origin Approved HTTPS origin; this does not approve its certificate or server IAM.
 */
/**
 * @typedef {object} WorkflowAuthorityId
 * @property {string} authorityHandle
 */

/**
 * Request native foreground confirmation for a self-hosted HTTPS origin. Only
 * definitions declaring authority: "approved" accept it. The path and operation
 * remain fixed by the bundled definition. A basePath requires the bundled request
 * to opt into allowApprovedBasePath; consent shows the full fixed read URL. Cancellation leaves no new approval.
 * @param {WorkflowAuthorityOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<WorkflowAuthority>}
 */
export async function approveAuthority (options, request = {}) {
  return platformRequest('workflows', 'approveAuthority', options, request.signal)
}

/**
 * Disable this locally approved destination and cancel its active calls. Server
 * grants, transport registrations and certificate trust remain separate.
 * @param {WorkflowAuthorityId} options
 * @returns {Promise<WorkflowRevokeResult>}
 */
export async function revokeAuthority (options) {
  return platformRequest('workflows', 'revokeAuthority', options)
}

/**
 * Release a revoked destination after releasing all its bindings and credential
 * copies. Original sign-in storage and remote registrations are unchanged.
 * @param {WorkflowAuthorityId} options
 * @returns {Promise<WorkflowReleaseResult>}
 */
export async function releaseAuthority (options) {
  return platformRequest('workflows', 'releaseAuthority', options)
}

/**
 * @typedef {object} WorkflowDiscoveryOptions
 * @property {string} workflow
 * @property {string} authorityRef
 * @property {string} accountRef
 * @property {string} targetRef
 */
/**
 * @typedef {object} WorkflowDiscovery
 * @property {WorkflowStatus[]} bindings
 * @property {(WorkflowCredential & { enabled: boolean, authorityHandle: string })[]} credentials
 * @property {(WorkflowAuthority & { enabled: boolean })[]} authorities
 */

/**
 * Recover bounded local handles for an exact application-resolved identity and
 * the invoking document origin. No values, cursors or server payloads are
 * exported. Rediscovery does not establish server authorization.
 * @param {WorkflowDiscoveryOptions} options
 * @returns {Promise<WorkflowDiscovery>}
 */
export async function discover (options) {
  return platformRequest('workflows', 'discover', options)
}

/**
 * Imports a bearer credential from the definition's fixed secure-storage key in
 * this application's origin. Sign-in and setItem happen in the application's
 * foreground authentication flow; this API accepts no secret value or arbitrary
 * key/alias selector. Restoring an opaque handle does not prove server IAM.
 * @param {WorkflowCredentialOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<WorkflowCredential>}
 */
export async function importCredential (options, request = {}) {
  return platformRequest('workflows', 'importCredential', options, request.signal)
}

/**
 * Create an explicitly authorized installation binding. Scope must exactly match
 * its native credential. No plaintext credential or response is returned.
 * @param {WorkflowBindingOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<WorkflowStatus>}
 */
export async function bind (options, request = {}) {
  return platformRequest('workflows', 'bind', options, request.signal)
}

/**
 * Start one visible, user-requested read using the shared finite foreground
 * controller. Admission returns its durable ID/generation, not read completion.
 * @param {WorkflowRunOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<import('./foreground.js').ForegroundStatus>}
 */
export async function run (options, request = {}) {
  return platformRequest('workflows', 'run', options, request.signal)
}

/**
 * @typedef {WorkflowId & { userInitiated: boolean }} WorkflowMonitorOptions
 */

/**
 * Start an explicitly user-requested continuous read session when the bundled
 * definition declares pollIntervalMs and the project opts into continuous
 * foreground admission. There is no boot/force-stop restart. Stop remains
 * available through the shared public notification/controller.
 * @param {WorkflowMonitorOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<import('./foreground/continuous.js').ContinuousStatus>}
 */
export async function monitor (options, request = {}) {
  return platformRequest('workflows', 'monitor', options, request.signal)
}

/**
 * Schedule read-only catch-up using the shared OS scheduler. No custom native
 * executor is needed. The platform's periodic interval/permission limits apply.
 * @param {WorkflowScheduleOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<import('./background/deferred.js').DeferredReadState>}
 */
export async function schedule (options, request = {}) {
  return platformRequest('workflows', 'schedule', options, request.signal)
}

/**
 * Return references for oro:push.register. JavaScript performs authenticated
 * server enrollment and acknowledges its exact endpoint generation through
 * oro:push; receipt of a push only triggers the fixed approved read.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowPushOptions>}
 */
export async function getPushOptions (options) {
  return platformRequest('workflows', 'pushOptions', options)
}

/**
 * Read bounded local outcome/count metadata. Credentials, cursors and server
 * payloads are excluded. Process loss retains unknown attempts without replay.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowStatus>}
 */
export async function getStatus (options) {
  return platformRequest('workflows', 'status', options)
}

/**
 * Persist local disablement, cancel active work and remove scheduled reads.
 * Check drained before claiming cleanup finished. Remote enrollment/revocation
 * and existing notification cancellation remain separate explicit operations.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowStatus>}
 */
export async function stop (options) {
  return platformRequest('workflows', 'stop', options)
}

/**
 * Disable a native credential and cancel its active workflow calls. This does
 * not revoke the server token or erase historical encrypted originals.
 * @param {WorkflowRevokeOptions} options
 * @returns {Promise<WorkflowRevokeResult>}
 */
export async function revokeCredential (options) {
  return platformRequest('workflows', 'revokeCredential', options)
}

/**
 * Read this authorized binding's opaque notification responses. Recheck current
 * account/navigation policy, then acknowledge through oro:notification. Private
 * server event payloads and credential values are never returned.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowResponse[]>}
 */
export async function getResponses (options) {
  return platformRequest('workflows', 'responses', options)
}

/**
 * @typedef {WorkflowId & { generation: string }} WorkflowReleaseOptions
 */
/**
 * @typedef {WorkflowReleaseOptions & { eventHandles: string[] }} WorkflowReleaseEventsOptions
 */
/**
 * @typedef {object} WorkflowEventOutcome
 * @property {string} eventHandle
 * @property {string} [notificationId] Scoped notification ID; no source event payload is included.
 * @property {'attempting'|'submitted'|'unknown'} outcome Submission is never proof of human attention.
 */
/**
 * @typedef {object} WorkflowReleaseResult
 * @property {boolean} released
 * @property {boolean} serverRevocationConfirmed Always false.
 */

/**
 * Read opaque local event outcomes, including uncertainty retained after a lost
 * submission reply. Response bodies and source server IDs are not exported.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowEventOutcome[]>}
 */
export async function getEvents (options) {
  return platformRequest('workflows', 'events', options)
}

/**
 * Explicitly consume selected dedup/outcome records only after application
 * reconciliation. This permits the same server IDs to become eligible again;
 * it does not acknowledge or revoke server state. Running bindings reject EBUSY.
 * No automatic pruning removes this protection.
 * @param {WorkflowReleaseEventsOptions} options
 * @returns {Promise<WorkflowStatus>}
 */
export async function releaseEvents (options) {
  return platformRequest('workflows', 'releaseEvents', options)
}

/**
 * Release a disabled, drained binding after consuming its outcomes and handling
 * transport/server cleanup through oro:push and the authenticated server.
 * The fixed source secure-storage key and its originals are untouched.
 * @param {WorkflowReleaseOptions} options
 * @returns {Promise<WorkflowReleaseResult>}
 */
export async function release (options) {
  return platformRequest('workflows', 'release', options)
}

/**
 * Release a locally revoked credential after all referencing bindings have
 * stopped and drained. This does not delete the source secure-storage item.
 * @param {WorkflowRevokeOptions} options
 * @returns {Promise<WorkflowReleaseResult>}
 */
export async function releaseCredential (options) {
  return platformRequest('workflows', 'releaseCredential', options)
}

export default { getSupport, approveAuthority, revokeAuthority, releaseAuthority, discover, importCredential, bind, run, monitor, schedule, getPushOptions, getStatus, stop, revokeCredential, getResponses, getEvents, releaseEvents, release, releaseCredential }
