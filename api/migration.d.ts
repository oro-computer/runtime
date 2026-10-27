/**
 * Probe without prompting, creating keys or reading legacy values. Other platforms
 * and older runtimes report unavailable; operational calls still reject ENOSYS.
 * Android implements this backend. iOS/iPadOS, macOS, Linux and Windows return
 * reason migration-backend-unimplemented; older Android SDKs return
 * migration-protocol-unavailable. A configured backend can be unavailable due
 * to disabled/malformed bundled definitions; inspect error before enabling UI.
 * @returns {Promise<MigrationSupport>}
 */
export function getSupport(): Promise<MigrationSupport>;
/**
 * @param {ProtectedSource} options
 * @returns {Promise<ProtectedSourceStatus>}
 */
export function inspectProtected(options: ProtectedSource): Promise<ProtectedSourceStatus>;
/**
 * Opens the bundled historical envelope with its original key, then rewraps to
 * an authentication-bound runtime key. Each protected cryptographic operation
 * uses its own system prompt. No plaintext is returned. The original source
 * digest is rechecked before commit; retry/discovery recovers an already committed import.
 * After explicit user recovery, a fresh recoveryRef rewraps the still-readable
 * original into a new key/handle while preserving earlier imports. Reuse that
 * recoveryRef to reconcile a lost reply. Missing original keys are never replaced.
 * @param {ProtectedImportOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<ProtectedImport>}
 */
export function importProtected(options: ProtectedImportOptions, settings?: MigrationRequestOptions): Promise<ProtectedImport>;
/**
 * Unlock a migrated value for the owning private interactive form. This always
 * uses the new key's per-operation prompt, never a cached authentication boolean.
 * @param {ProtectedOpenOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<ProtectedValue>}
 */
export function openProtected(options: ProtectedOpenOptions, settings?: MigrationRequestOptions): Promise<ProtectedValue>;
/**
 * @param {ProtectedIdentity} options
 * @returns {Promise<ProtectedImport[]>}
 */
export function discoverProtected(options: ProtectedIdentity): Promise<ProtectedImport[]>;
/**
 * Read only a bundled noninteractive source explicitly marked exposure:
 * private-document. Requires native foreground consent and returns Base64 to
 * that private document. Authentication-bound sources always reject this path.
 * Never expose the result through logs, assistant tools or background handlers.
 * @param {UnprotectedReadOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<UnprotectedValue>}
 */
export function readUnprotected(options: UnprotectedReadOptions, settings?: MigrationRequestOptions): Promise<UnprotectedValue>;
/**
 * Read bounded opaque references captured from an allowlisted historical
 * Activity action. Primary window only. Every entry is untrusted: authorize
 * its destination in the current account before navigating or performing work.
 * @param {HistoricalEntryOptions} options
 * @returns {Promise<HistoricalEntry[]>}
 */
export function getHistoricalEntries(options: HistoricalEntryOptions): Promise<HistoricalEntry[]>;
/**
 * Acknowledge an owned routing entry after application validation. This does
 * not acknowledge a source alarm, remote operation or notification delivery.
 * @param {HistoricalEntryId} options
 * @returns {Promise<HistoricalEntryAcknowledgement>}
 */
export function acknowledgeHistoricalEntry(options: HistoricalEntryId): Promise<HistoricalEntryAcknowledgement>;
/**
 * @param {HistoricalComponentSource} options
 * @returns {Promise<HistoricalSourceStatus>}
 */
export function inspectHandover(options: HistoricalComponentSource): Promise<HistoricalSourceStatus>;
/**
 * Prepare an exact source/replacement binding after native consent. Pass an
 * empty expectedGeneration only for an absent mapping. Bundled identity fields
 * must match the already-authorized workflow binding; no server authority is inferred.
 * @param {HandoverOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<HandoverStatus>}
 */
export function prepareHandover(options: HandoverOptions, settings?: MigrationRequestOptions): Promise<HandoverStatus>;
/**
 * Rediscover exact owned generations after an interrupted reply, including Stop.
 * An existing historical reference cannot be rebound to another account/authority.
 * @param {HandoverIdentity} options
 * @returns {Promise<DiscoveredHandover[]>}
 */
export function discoverHandovers(options: HandoverIdentity): Promise<DiscoveredHandover[]>;
/**
 * Activate only the prepared generation after rechecking source and replacement.
 * The original component names, job IDs/schedules and encrypted receipts remain.
 * This neither unregisters old endpoints nor claims remote migration completion.
 * @param {HandoverId} options
 * @returns {Promise<HandoverStatus>}
 */
export function activateHandover(options: HandoverId): Promise<HandoverStatus>;
/**
 * @param {HandoverId} options
 * @returns {Promise<HandoverStatus>}
 */
export function getHandoverStatus(options: HandoverId): Promise<HandoverStatus>;
/**
 * Fence this generation and cancel its admitted reads. Poll drained before
 * treating owned cleanup as complete. Already-submitted effects stay uncertain.
 * @param {HandoverId} options
 * @returns {Promise<HandoverStatus>}
 */
export function stopHandover(options: HandoverId): Promise<HandoverStatus>;
/**
 * @param {HandoverId} options
 * @returns {Promise<RetainedReceipt[]>}
 */
export function getRetainedReceipts(options: HandoverId): Promise<RetainedReceipt[]>;
/**
 * Record an application-verified result without claiming runtime verification of
 * its server. Repeated identical resolutions are idempotent; changed ones conflict.
 * @param {ReceiptResolution} options
 * @returns {Promise<ReceiptResolutionResult>}
 */
export function resolveRetainedReceipt(options: ReceiptResolution): Promise<ReceiptResolutionResult>;
/**
 * Release only an explicitly resolved receipt. Unknown receipts never expire or
 * disappear automatically; at capacity new admission returns ENOSPC.
 * @param {RetainedReceiptId} options
 * @returns {Promise<ReceiptReleaseResult>}
 */
export function releaseRetainedReceipt(options: RetainedReceiptId): Promise<ReceiptReleaseResult>;
declare namespace _default {
    export { getSupport };
    export { inspectProtected };
    export { importProtected };
    export { openProtected };
    export { discoverProtected };
    export { readUnprotected };
    export { getHistoricalEntries };
    export { acknowledgeHistoricalEntry };
    export { inspectHandover };
    export { prepareHandover };
    export { discoverHandovers };
    export { activateHandover };
    export { getHandoverStatus };
    export { stopHandover };
    export { getRetainedReceipts };
    export { resolveRetainedReceipt };
    export { releaseRetainedReceipt };
}
export default _default;
export type MigrationRequestOptions = {
    /**
     * Cancels owned prompts/work and discards late document replies.
     */
    signal?: AbortSignal;
};
export type MigrationSupport = {
    available: boolean;
    /**
     * Current runtime platform.
     */
    platform: string;
    /**
     * Backend inventory, not device qualification.
     */
    implementation: "implemented" | "missing";
    /**
     * Native backend name, empty when missing.
     */
    backend: string;
    /**
     * Fixed unavailable reason, empty when available.
     */
    reason: string;
    /**
     * protected-import-v1 on the configured Android backend.
     */
    protectedImport?: string;
    /**
     * retained-components-v1 on the configured Android backend.
     */
    retainedComponents?: string;
    /**
     * prefer-native-records-v1 when bounded source selection is implemented (Android only).
     */
    sourceSelection?: string;
    /**
     * android-json-pairs-sha256-v1 when aggregate ciphertext validation is implemented (Android only).
     */
    legacyProvenance?: string;
    /**
     * False for a bridge build retaining app-owned component classes and manifest entries.
     */
    generatedAdapters?: boolean;
    /**
     * Always false.
     */
    nativeIntegrationRequired: boolean;
    /**
     * Bundled protected source IDs; no aliases or saved values.
     */
    profiles: string[];
    /**
     * Bundled retained component IDs.
     */
    components: string[];
    /**
     * 16; encrypted bodies are committed in bounded native chunks.
     */
    maximumImports?: number;
    /**
     * 48, including unresolved historical input.
     */
    maximumReceipts?: number;
    /**
     * 15 untrusted routing entries.
     */
    maximumEntries?: number;
    /**
     * 262144 aggregate decoded native-state bytes.
     */
    maximumStoredBytes?: number;
    /**
     * Fixed unavailable/configuration code.
     */
    error?: string;
};
export type ProtectedSource = {
    /**
     * Bundled allowlist entry.
     */
    profile: string;
    /**
     * Bounded historical record reference, not a key or alias selector.
     */
    sourceRef: string;
};
export type ProtectedSourceStatus = {
    profile: string;
    /**
     * Original ciphertext digest, or an opaque keyed source version for native records.
     */
    sourceSha256: string;
    /**
     * Always true.
     */
    originalsPreserved: boolean;
};
export type ProtectedIdentity = ProtectedSource & {
    authorityRef: string;
    accountRef: string;
    targetRef: string;
};
export type ProtectedImportOptions = ProtectedIdentity & {
    sourceSha256: string;
    userInitiated: boolean;
    recoveryRef?: string;
};
export type ProtectedImport = {
    /**
     * Opaque document-origin and identity-bound rewrapped record.
     */
    handle: string;
    profile: string;
    sourceSha256: string;
    /**
     * Always false for import/discovery.
     */
    valueExported: boolean;
    /**
     * Always true; there is no automatic deletion of old keys/ciphertext.
     */
    originalsPreserved: boolean;
};
export type ProtectedOpenOptions = {
    handle: string;
    /**
     * Must follow explicit private interactive user action.
     */
    userInitiated: boolean;
};
export type ProtectedValue = {
    /**
     * Base64 bytes for the original requesting private form only. Never log or expose through assistant tooling.
     */
    value: string;
};
export type HistoricalComponentSource = {
    /**
     * Bundled retained component ID.
     */
    component: string;
    /**
     * Historical reference in that component's fixed source family.
     */
    sourceRef: string;
};
export type HistoricalSourceStatus = {
    sourceSha256: string;
    /**
     * Bundled generation field, or envelope digest when no old generation field exists.
     */
    sourceGeneration: string;
};
export type HandoverOptions = HistoricalComponentSource & HistoricalSourceStatus & {
    expectedGeneration: string;
    bindingId: string;
    bindingGeneration: string;
    authorityRef: string;
    accountRef: string;
    targetRef: string;
    userInitiated: boolean;
};
export type HandoverId = {
    id: string;
    /**
     * Exact native handover generation.
     */
    generation: string;
};
export type HandoverStatus = HandoverId & HistoricalSourceStatus & {
    phase: "prepared" | "active" | "stopped";
    originalsPreserved: boolean;
    remoteRevocationConfirmed: boolean;
    drained: boolean;
};
export type HandoverIdentity = {
    authorityRef: string;
    accountRef: string;
    targetRef: string;
};
export type DiscoveredHandover = HandoverStatus & HistoricalComponentSource;
export type RetainedReceipt = {
    receiptId: string;
    sourceGeneration: string;
    kind: "receiver" | "job";
    /**
     * A local read does not confirm the old delivery, mutation or server acknowledgement.
     */
    outcome: "unknown" | "read-completed";
    receivedAt: number;
};
export type RetainedReceiptId = HandoverId & {
    receiptId: string;
};
export type ReceiptResolution = RetainedReceiptId & {
    outcome: "applied" | "not-applied";
    receiptSha256: string;
    userInitiated: boolean;
};
export type ReceiptResolutionResult = {
    resolvedLocally: boolean;
    /**
     * Always false. The application must verify its authenticated server receipt.
     */
    remoteOutcomeVerifiedByRuntime: boolean;
};
export type ReceiptReleaseResult = {
    released: boolean;
};
export type UnprotectedReadOptions = ProtectedSource & {
    userInitiated: boolean;
};
export type UnprotectedValue = ProtectedValue & {
    originalsPreserved: boolean;
};
export type HistoricalEntryOptions = {
    /**
     * Bundled Activity alias profile.
     */
    component: string;
};
export type HistoricalEntryId = HistoricalEntryOptions & {
    entryId: string;
};
export type HistoricalEntry = HistoricalEntryId & {
    action: string;
    references: {
        [x: string]: string;
    };
    receivedAt: number;
    untrusted: boolean;
};
export type HistoricalEntryAcknowledgement = {
    acknowledged: boolean;
};
