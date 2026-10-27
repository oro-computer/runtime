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
export function getSupport(): Promise<WorkflowSupport>;
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
export function approveAuthority(options: WorkflowAuthorityOptions, request?: WorkflowRequestOptions): Promise<WorkflowAuthority>;
/**
 * Disable this locally approved destination and cancel its active calls. Server
 * grants, transport registrations and certificate trust remain separate.
 * @param {WorkflowAuthorityId} options
 * @returns {Promise<WorkflowRevokeResult>}
 */
export function revokeAuthority(options: WorkflowAuthorityId): Promise<WorkflowRevokeResult>;
/**
 * Release a revoked destination after releasing all its bindings and credential
 * copies. Original sign-in storage and remote registrations are unchanged.
 * @param {WorkflowAuthorityId} options
 * @returns {Promise<WorkflowReleaseResult>}
 */
export function releaseAuthority(options: WorkflowAuthorityId): Promise<WorkflowReleaseResult>;
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
export function discover(options: WorkflowDiscoveryOptions): Promise<WorkflowDiscovery>;
/**
 * Imports a bearer credential from the definition's fixed secure-storage key in
 * this application's origin. Sign-in and setItem happen in the application's
 * foreground authentication flow; this API accepts no secret value or arbitrary
 * key/alias selector. Restoring an opaque handle does not prove server IAM.
 * @param {WorkflowCredentialOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<WorkflowCredential>}
 */
export function importCredential(options: WorkflowCredentialOptions, request?: WorkflowRequestOptions): Promise<WorkflowCredential>;
/**
 * Create an explicitly authorized installation binding. Scope must exactly match
 * its native credential. No plaintext credential or response is returned.
 * @param {WorkflowBindingOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<WorkflowStatus>}
 */
export function bind(options: WorkflowBindingOptions, request?: WorkflowRequestOptions): Promise<WorkflowStatus>;
/**
 * Start one visible, user-requested read using the shared finite foreground
 * controller. Admission returns its durable ID/generation, not read completion.
 * @param {WorkflowRunOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<import('./foreground.js').ForegroundStatus>}
 */
export function run(options: WorkflowRunOptions, request?: WorkflowRequestOptions): Promise<import("./foreground.js").ForegroundStatus>;
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
export function monitor(options: WorkflowMonitorOptions, request?: WorkflowRequestOptions): Promise<import("./foreground/continuous.js").ContinuousStatus>;
/**
 * Schedule read-only catch-up using the shared OS scheduler. No custom native
 * executor is needed. The platform's periodic interval/permission limits apply.
 * @param {WorkflowScheduleOptions} options
 * @param {WorkflowRequestOptions} [request]
 * @returns {Promise<import('./background/deferred.js').DeferredReadState>}
 */
export function schedule(options: WorkflowScheduleOptions, request?: WorkflowRequestOptions): Promise<import("./background/deferred.js").DeferredReadState>;
/**
 * Return references for oro:push.register. JavaScript performs authenticated
 * server enrollment and acknowledges its exact endpoint generation through
 * oro:push; receipt of a push only triggers the fixed approved read.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowPushOptions>}
 */
export function getPushOptions(options: WorkflowId): Promise<WorkflowPushOptions>;
/**
 * Read bounded local outcome/count metadata. Credentials, cursors and server
 * payloads are excluded. Process loss retains unknown attempts without replay.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowStatus>}
 */
export function getStatus(options: WorkflowId): Promise<WorkflowStatus>;
/**
 * Persist local disablement, cancel active work and remove scheduled reads.
 * Check drained before claiming cleanup finished. Remote enrollment/revocation
 * and existing notification cancellation remain separate explicit operations.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowStatus>}
 */
export function stop(options: WorkflowId): Promise<WorkflowStatus>;
/**
 * Disable a native credential and cancel its active workflow calls. This does
 * not revoke the server token or erase historical encrypted originals.
 * @param {WorkflowRevokeOptions} options
 * @returns {Promise<WorkflowRevokeResult>}
 */
export function revokeCredential(options: WorkflowRevokeOptions): Promise<WorkflowRevokeResult>;
/**
 * Read this authorized binding's opaque notification responses. Recheck current
 * account/navigation policy, then acknowledge through oro:notification. Private
 * server event payloads and credential values are never returned.
 * @param {WorkflowId} options
 * @returns {Promise<WorkflowResponse[]>}
 */
export function getResponses(options: WorkflowId): Promise<WorkflowResponse[]>;
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
export function getEvents(options: WorkflowId): Promise<WorkflowEventOutcome[]>;
/**
 * Explicitly consume selected dedup/outcome records only after application
 * reconciliation. This permits the same server IDs to become eligible again;
 * it does not acknowledge or revoke server state. Running bindings reject EBUSY.
 * No automatic pruning removes this protection.
 * @param {WorkflowReleaseEventsOptions} options
 * @returns {Promise<WorkflowStatus>}
 */
export function releaseEvents(options: WorkflowReleaseEventsOptions): Promise<WorkflowStatus>;
/**
 * Release a disabled, drained binding after consuming its outcomes and handling
 * transport/server cleanup through oro:push and the authenticated server.
 * The fixed source secure-storage key and its originals are untouched.
 * @param {WorkflowReleaseOptions} options
 * @returns {Promise<WorkflowReleaseResult>}
 */
export function release(options: WorkflowReleaseOptions): Promise<WorkflowReleaseResult>;
/**
 * Release a locally revoked credential after all referencing bindings have
 * stopped and drained. This does not delete the source secure-storage item.
 * @param {WorkflowRevokeOptions} options
 * @returns {Promise<WorkflowReleaseResult>}
 */
export function releaseCredential(options: WorkflowRevokeOptions): Promise<WorkflowReleaseResult>;
declare namespace _default {
    export { getSupport };
    export { approveAuthority };
    export { revokeAuthority };
    export { releaseAuthority };
    export { discover };
    export { importCredential };
    export { bind };
    export { run };
    export { monitor };
    export { schedule };
    export { getPushOptions };
    export { getStatus };
    export { stop };
    export { revokeCredential };
    export { getResponses };
    export { getEvents };
    export { releaseEvents };
    export { release };
    export { releaseCredential };
}
export default _default;
export type WorkflowRequestOptions = {
    signal?: AbortSignal;
};
export type WorkflowSupport = {
    available: boolean;
    /**
     * Current platform on an unavailable backend result.
     */
    platform?: string;
    /**
     * Unavailable backend inventory.
     */
    implementation?: "missing" | "restricted";
    /**
     * Fixed unavailable reason, including workflow-backend-unimplemented.
     */
    reason?: string;
    /**
     * Native backend name when reported.
     */
    backend?: string;
    /**
     * Bundled definition IDs.
     */
    workflows: string[];
    /**
     * Always false for these workflows.
     */
    nativeIntegrationRequired: boolean;
    /**
     * Always false.
     */
    backgroundJavaScript: boolean;
    /**
     * bounded-read-backoff-v1 when supported.
     */
    readRetryPolicy?: string;
    /**
     * Five attempts including the initial GET.
     */
    maximumReadAttempts?: number;
    /**
     * Continuous monitors wait at least this long after exhausting transient read retries.
     */
    minimumReadCycleCooldownMs?: number;
    /**
     * Total read retry budget, excluding notification submission.
     */
    maximumReadBudgetMs?: number;
    /**
     * workflow-namespace-v1 reserves independent registry capacity.
     */
    executorComposition?: string;
    /**
     * Bundled definitions, independent of binding capacity.
     */
    maximumDefinitions?: number;
    /**
     * Application handlers coexist with bundled workflows.
     */
    maximumApplicationHandlersPerType?: number;
    /**
     * workflow-events-v1 identifies event/notification correlation.
     */
    correlationVersion?: string;
    /**
     * Bundled allowApprovedBasePath permits a native-approved prefix.
     */
    approvedBasePaths?: boolean;
    maximumBindings: number;
    maximumRetainedEventsPerBinding: number;
    /**
     * Fixed configuration error, empty when valid.
     */
    error?: string;
};
export type WorkflowIdentity = {
    /**
     * Bundled definition ID.
     */
    workflow: string;
    /**
     * Application-resolved opaque authority.
     */
    authorityRef: string;
    /**
     * Application-resolved opaque account.
     */
    accountRef: string;
    /**
     * Application-resolved opaque target.
     */
    targetRef: string;
    /**
     * Required for a definition using an approved destination.
     */
    authorityHandle?: string;
    /**
     * Must be true after the application's explicit user action.
     */
    userInitiated: boolean;
};
export type WorkflowCredentialOptions = WorkflowIdentity & {
    expiresAt?: number;
};
export type WorkflowBindingOptions = WorkflowIdentity & {
    credentialHandle?: string;
};
export type WorkflowId = {
    /**
     * Opaque installation binding ID.
     */
    id: string;
};
export type WorkflowCredential = {
    /**
     * Opaque native-only credential reference.
     */
    credentialHandle: string;
    /**
     * Always false.
     */
    valueExported: boolean;
};
export type WorkflowStatus = {
    id: string;
    workflow: string;
    generation: string;
    enabled: boolean;
    state: "idle" | "running" | "unknown";
    /**
     * A completed read/submission is not presentation or server acknowledgement.
     */
    lastOutcome: string;
    running: boolean;
    /**
     * False while an owned worker still holds resources.
     */
    drained: boolean;
    /**
     * Retained unknown outcome after process loss.
     */
    interruptedOutcome: string;
    /**
     * Attempts in the current or most recent read cycle.
     */
    readAttempts?: number;
    /**
     * Next wall-clock retry time, zero when no backoff is pending.
     */
    retryAt?: number;
    /**
     * A durable push hint awaits a read; this does not establish delivery.
     */
    pendingRead: boolean;
    retainedEvents: number;
    /**
     * Held without automatic resubmission.
     */
    uncertainEvents: number;
};
export type WorkflowRunOptions = WorkflowId & {
    userInitiated: boolean;
    durationMs?: number;
};
export type WorkflowScheduleOptions = WorkflowId & {
    intervalMs?: number;
    network?: "any" | "unmetered";
    requiresCharging?: boolean;
    requiresBatteryNotLow?: boolean;
};
export type WorkflowPushOptions = {
    id: string;
    kind: string;
    authorityRef: string;
    accountRef: string;
    /**
     * Native workflow binding reference.
     */
    targetRef: string;
};
export type WorkflowResponse = {
    receiptId: string;
    /**
     * Notification identifier under correlationVersion workflow-events-v1.
     */
    notificationId?: string;
    /**
     * Opaque correlation; contains no server payload.
     */
    eventHandle: string;
    action: string;
    receivedAt: number;
};
export type WorkflowRevokeOptions = {
    credentialHandle: string;
};
export type WorkflowRevokeResult = {
    revokedLocally: boolean;
    /**
     * Always false.
     */
    serverRevocationConfirmed: boolean;
};
export type WorkflowAuthorityOptions = WorkflowIdentity & {
    origin: string;
    basePath?: string;
};
export type WorkflowAuthority = {
    authorityHandle: string;
    /**
     * Native-approved immutable prefix, empty for root-mounted gateways.
     */
    basePath?: string;
    /**
     * Approved HTTPS origin; this does not approve its certificate or server IAM.
     */
    origin: string;
};
export type WorkflowAuthorityId = {
    authorityHandle: string;
};
export type WorkflowDiscoveryOptions = {
    workflow: string;
    authorityRef: string;
    accountRef: string;
    targetRef: string;
};
export type WorkflowDiscovery = {
    bindings: WorkflowStatus[];
    credentials: (WorkflowCredential & {
        enabled: boolean;
        authorityHandle: string;
    })[];
    authorities: (WorkflowAuthority & {
        enabled: boolean;
    })[];
};
export type WorkflowMonitorOptions = WorkflowId & {
    userInitiated: boolean;
};
export type WorkflowReleaseOptions = WorkflowId & {
    generation: string;
};
export type WorkflowReleaseEventsOptions = WorkflowReleaseOptions & {
    eventHandles: string[];
};
export type WorkflowEventOutcome = {
    eventHandle: string;
    /**
     * Scoped notification ID; no source event payload is included.
     */
    notificationId?: string;
    /**
     * Submission is never proof of human attention.
     */
    outcome: "attempting" | "submitted" | "unknown";
};
export type WorkflowReleaseResult = {
    released: boolean;
    /**
     * Always false.
     */
    serverRevocationConfirmed: boolean;
};
