/**
 * @typedef {object} DeferredReadOptions
 * @property {string} id Stable task identity, 1..128 letters/digits/underscores/hyphens.
 * @property {string} kind Name registered by compiled native app code.
 * @property {string} authorityRef Opaque configured server identity, never a URL or credential.
 * @property {string} accountRef Opaque account identity; authorization is rechecked at execution.
 * @property {string} targetRef Opaque resource identity.
 * @property {string} operationRef Opaque operation identity for read-only status/reconciliation.
 * @property {'any'|'unmetered'} [network='unmetered'] OS path constraint, not server availability.
 * @property {boolean} [requiresCharging=false]
 * @property {boolean} [requiresBatteryNotLow=true]
 * @property {number} [delayMs=0] Minimum delay, at most seven days. OS/process availability controls timing.
 * @property {number} [intervalMs=0] Read recurrence: 0 for one-shot, or 15 minutes through seven days. Requires delayMs=0.
 * @property {number} [flexMs] Periodic execution window: at least five minutes or 5% of intervalMs, whichever is larger. Defaults to that minimum; at most intervalMs. Omit for one-shot reads.
 */
/**
 * @typedef {DeferredReadOptions & { schema: number, generation: string, jobId: number, mode?: 'read'|'maintenance', phase: string, attempts: number, updatedAt: number, lastOutcome?: string, completedRuns?: number }} DeferredReadState
 */
/**
 * @typedef {object} DeferredReadSupport
 * @property {string} [platform] Present on an unavailable cross-platform support result.
 * @property {'missing'|'restricted'} [implementation] Missing runtime backend or platform API restriction; not a permission denial.
 * @property {string} [reason] Stable unavailable reason.
 * @property {boolean} available
 * @property {string[]} registeredKinds
 * @property {number} maximumTasks 32, including terminal records until explicitly released.
 * @property {number} maximumAttempts 5 with exponential backoff starting at 30 seconds.
 * @property {boolean} readOnly True for enqueue. Maintenance uses a separate explicitly registered contract.
 * @property {boolean} queuedWrites False.
 * @property {boolean} backgroundJavaScript False.
 * @property {boolean} survivesForceStop False. The app must relaunch, reconcile interrupted state and explicitly resume supported work.
 * @property {boolean} periodicReads True. Successful cycles reset their attempt budget; terminal failures suspend recurrence.
 * @property {number} minimumIntervalMs 900000.
 * @property {number} minimumFlexMs 300000.
 * @property {string[]} registeredMaintenanceKinds Explicitly registered native maintenance executors.
 * @property {Object<string, string[]>} maintenanceOperations Allowed cleanup/revoke operations per maintenance kind.
 * @property {'application-process'} [executionScope] Linux scheduling scope.
 * @property {boolean} [survivesProcessExit] False on Linux; durable records require explicit interruption reconciliation.
 */
/** @returns {Promise<DeferredReadSupport>} */
export function getSupport(): Promise<DeferredReadSupport>;
/**
 * Schedules an authorized native read/reconciliation. Reusing the same task ID
 * with identical options returns its current state, without duplicate delivery.
 * Changing an existing task returns EEXIST. Each attempt has a 60-second result
 * deadline; a blocked native executor can still occupy the single worker.
 * @param {DeferredReadOptions} options
 * @returns {Promise<DeferredReadState>}
 */
export function enqueue(options: DeferredReadOptions): Promise<DeferredReadState>;
/**
 * @typedef {Omit<DeferredReadOptions, 'operationRef'|'intervalMs'|'flexMs'> & { operationRef: 'cleanup'|'revoke' }} DeferredMaintenanceOptions
 */
/**
 * Schedules one registered cleanup/revocation task. Registration must explicitly
 * assert retry safety in native code. The executor reauthorizes every attempt
 * and owns idempotency, prerequisites and uncertain-outcome reconciliation.
 * General mutations, credentials, request bodies and periodic maintenance are
 * rejected. Uses the same journal, five-attempt budget and cancellation rules.
 * @param {DeferredMaintenanceOptions} options
 * @returns {Promise<DeferredReadState>}
 */
export function enqueueMaintenance(options: DeferredMaintenanceOptions): Promise<DeferredReadState>;
/** @returns {Promise<DeferredReadState[]>} */
export function list(): Promise<DeferredReadState[]>;
/**
 * Reads retained OS scheduling state and the durable last outcome. Interrupted
 * state is explicit and is never silently rescheduled.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export function status(id: string): Promise<DeferredReadState>;
/**
 * Explicitly resumes an interrupted task after app reconciliation. Preserves
 * the five-attempt budget and replaces its generation, so obsolete callbacks
 * cannot complete the resumed task. The native executor reauthorizes it again.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export function resume(id: string): Promise<DeferredReadState>;
/**
 * Cancels scheduling and signals a running native executor. An already-running
 * read can finish; its result cannot complete a cancelled or replaced task.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export function cancel(id: string): Promise<DeferredReadState>;
/**
 * Cancels and releases a retained task record. Preserve any required application
 * receipt before releasing. A later enqueue of this ID starts a new generation.
 * @param {string} id
 * @returns {Promise<DeferredReadState>}
 */
export function release(id: string): Promise<DeferredReadState>;
declare namespace _default {
    export { getSupport };
    export { enqueue };
    export { enqueueMaintenance };
    export { list };
    export { status };
    export { resume };
    export { cancel };
    export { release };
}
export default _default;
export type DeferredReadOptions = {
    /**
     * Stable task identity, 1..128 letters/digits/underscores/hyphens.
     */
    id: string;
    /**
     * Name registered by compiled native app code.
     */
    kind: string;
    /**
     * Opaque configured server identity, never a URL or credential.
     */
    authorityRef: string;
    /**
     * Opaque account identity; authorization is rechecked at execution.
     */
    accountRef: string;
    /**
     * Opaque resource identity.
     */
    targetRef: string;
    /**
     * Opaque operation identity for read-only status/reconciliation.
     */
    operationRef: string;
    /**
     * OS path constraint, not server availability.
     */
    network?: "any" | "unmetered";
    requiresCharging?: boolean;
    requiresBatteryNotLow?: boolean;
    /**
     * Minimum delay, at most seven days. OS/process availability controls timing.
     */
    delayMs?: number;
    /**
     * Read recurrence: 0 for one-shot, or 15 minutes through seven days. Requires delayMs=0.
     */
    intervalMs?: number;
    /**
     * Periodic execution window: at least five minutes or 5% of intervalMs, whichever is larger. Defaults to that minimum; at most intervalMs. Omit for one-shot reads.
     */
    flexMs?: number;
};
export type DeferredReadState = DeferredReadOptions & {
    schema: number;
    generation: string;
    jobId: number;
    mode?: "read" | "maintenance";
    phase: string;
    attempts: number;
    updatedAt: number;
    lastOutcome?: string;
    completedRuns?: number;
};
export type DeferredReadSupport = {
    /**
     * Present on an unavailable cross-platform support result.
     */
    platform?: string;
    /**
     * Missing runtime backend or platform API restriction; not a permission denial.
     */
    implementation?: "missing" | "restricted";
    /**
     * Stable unavailable reason.
     */
    reason?: string;
    available: boolean;
    registeredKinds: string[];
    /**
     * 32, including terminal records until explicitly released.
     */
    maximumTasks: number;
    /**
     * 5 with exponential backoff starting at 30 seconds.
     */
    maximumAttempts: number;
    /**
     * True for enqueue. Maintenance uses a separate explicitly registered contract.
     */
    readOnly: boolean;
    /**
     * False.
     */
    queuedWrites: boolean;
    /**
     * False.
     */
    backgroundJavaScript: boolean;
    /**
     * False. The app must relaunch, reconcile interrupted state and explicitly resume supported work.
     */
    survivesForceStop: boolean;
    /**
     * True. Successful cycles reset their attempt budget; terminal failures suspend recurrence.
     */
    periodicReads: boolean;
    /**
     * 900000.
     */
    minimumIntervalMs: number;
    /**
     * 300000.
     */
    minimumFlexMs: number;
    /**
     * Explicitly registered native maintenance executors.
     */
    registeredMaintenanceKinds: string[];
    /**
     * Allowed cleanup/revoke operations per maintenance kind.
     */
    maintenanceOperations: {
        [x: string]: string[];
    };
    /**
     * Linux scheduling scope.
     */
    executionScope?: "application-process";
    /**
     * False on Linux; durable records require explicit interruption reconciliation.
     */
    survivesProcessExit?: boolean;
};
export type DeferredMaintenanceOptions = Omit<DeferredReadOptions, "operationRef" | "intervalMs" | "flexMs"> & {
    operationRef: "cleanup" | "revoke";
};
