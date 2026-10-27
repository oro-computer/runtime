/**
 * Reports configuration, registered executors and current notification policy.
 * Missing or platform-restricted backends return unavailable status; operational calls still reject with ENOSYS.
 * @returns {Promise<ContinuousSupport>}
 */
export function getSupport(): Promise<ContinuousSupport>;
/**
 * Starts from the current foreground document after an explicit user action.
 * Returns durable admission, not completion. Navigation or an aborted/lost reply
 * can race admission: inspect the ID before another attempt. The admitted native
 * operation survives Home/navigation and has its own public Stop notification.
 * Removing the Android app task keeps monitoring active. Linux uses a live application process and an
 * action-capable desktop notification server; closing that process ends work.
 * Stop or process loss records uncertainty. Reopen/reboot never restarts work;
 * a new explicit start is required.
 * @param {ContinuousOptions} options
 * @param {ContinuousRequestOptions} [requestOptions]
 * @returns {Promise<ContinuousStatus>}
 */
export function start(options: ContinuousOptions, requestOptions?: ContinuousRequestOptions): Promise<ContinuousStatus>;
/**
 * Reads the retained status without restarting work.
 * @param {string} id
 * @returns {Promise<ContinuousStatus>}
 */
export function getStatus(id: string): Promise<ContinuousStatus>;
/**
 * Lists at most sixteen retained, redacted monitoring statuses.
 * @returns {Promise<ContinuousStatus[]>}
 */
export function list(): Promise<ContinuousStatus[]>;
/**
 * Requests stop for one exact generation. An uncertain remote outcome stays
 * unknown. Cancellation does not undo an already completed remote operation.
 * @param {string} id
 * @param {string} expectedGeneration
 * @returns {Promise<ContinuousStatus>}
 */
export function stop(id: string, expectedGeneration: string): Promise<ContinuousStatus>;
/**
 * Releases a terminal journal entry after app reconciliation. Resource cleanup
 * must finish before the operation ID can be released or a new executor started.
 * @param {string} id
 * @param {string} expectedGeneration
 * @returns {Promise<void>}
 */
export function release(id: string, expectedGeneration: string): Promise<void>;
declare namespace _default {
    export { getSupport };
    export { start };
    export { getStatus };
    export { list };
    export { stop };
    export { release };
}
export default _default;
export type ContinuousOptions = {
    /**
     * Opaque operation ID; retained until explicitly released.
     */
    id: string;
    /**
     * Registered compiled native executor.
     */
    kind: string;
    /**
     * Opaque saved authority reference.
     */
    authorityRef: string;
    /**
     * Opaque saved account reference.
     */
    accountRef: string;
    /**
     * Opaque saved target reference.
     */
    targetRef: string;
    /**
     * Opaque compiled operation reference.
     */
    operationRef: string;
    /**
     * Assert that this call follows an explicit user action.
     */
    userInitiated: true;
};
export type ContinuousStatus = {
    id: string;
    kind: string;
    /**
     * Exact generation required for stop/release.
     */
    generation: string;
    phase: "starting" | "running" | "completed" | "unauthorized" | "unknown" | "failed" | "rejected";
    /**
     * Executor assertion, not independent server verification.
     */
    outcome: "unknown" | "completed" | "unauthorized" | "failed" | "not-started";
    /**
     * Unix milliseconds.
     */
    updatedAt: number;
};
export type ContinuousSupport = {
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
    /**
     * Configured native backend; OS admission remains separate.
     */
    available: boolean;
    /**
     * Notification permission/channel policy permits an attempt; OS admission can still reject.
     */
    canStart: boolean;
    registeredKinds: string[];
    /**
     * No runtime duration cap; the OS can stop the process.
     */
    maximumDurationMs: null;
    /**
     * Retained journal entries, including terminal outcomes.
     */
    maximumTasks: number;
    /**
     * One, including a cancelled executor awaiting resource cleanup.
     */
    maximumConcurrentExecutors: number;
    automaticRestart: false;
    backgroundJavaScript: false;
    /**
     * Linux work ends with the application process.
     */
    executionScope?: "application-process";
    /**
     * False on Linux.
     */
    survivesProcessExit?: boolean;
};
export type ContinuousRequestOptions = {
    /**
     * Cancels the request, not an already admitted operation; query its durable ID after an uncertain reply.
     */
    signal?: AbortSignal;
};
