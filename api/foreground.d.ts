/**
 * Reports configuration, registered executors and current notification policy.
 * Missing or platform-restricted backends return unavailable status; operational calls still reject with ENOSYS.
 * @returns {Promise<ForegroundSupport>}
 */
export function getSupport(): Promise<ForegroundSupport>;
/**
 * Starts from the current foreground document after an explicit user action.
 * Linux additionally requires an action-capable freedesktop notification server
 * for its public Stop control. It does not install a daemon or restart work.
 * Returns durable admission, not completion. Navigation or an aborted/lost reply
 * can race admission: inspect the ID before another attempt. The admitted native
 * operation survives Home/navigation and has its own Stop notification/deadline.
 * Task removal, OS timeout and stop record uncertainty; no work auto-restarts.
 * @param {ForegroundOptions} options
 * @param {ForegroundRequestOptions} [requestOptions]
 * @returns {Promise<ForegroundStatus>}
 */
export function start(options: ForegroundOptions, requestOptions?: ForegroundRequestOptions): Promise<ForegroundStatus>;
/**
 * Reads the retained status without restarting work.
 * @param {string} id
 * @returns {Promise<ForegroundStatus>}
 */
export function getStatus(id: string): Promise<ForegroundStatus>;
/**
 * Lists at most sixteen retained, redacted operation statuses.
 * @returns {Promise<ForegroundStatus[]>}
 */
export function list(): Promise<ForegroundStatus[]>;
/**
 * Requests stop for one exact generation. An uncertain remote outcome stays
 * unknown. Cancellation does not undo an already completed remote operation.
 * @param {string} id
 * @param {string} expectedGeneration
 * @returns {Promise<ForegroundStatus>}
 */
export function stop(id: string, expectedGeneration: string): Promise<ForegroundStatus>;
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
export type ForegroundOptions = {
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
    /**
     * 1,000..1,800,000 milliseconds, subject to Android limits.
     */
    durationMs?: number;
};
export type ForegroundStatus = {
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
    durationMs: number;
};
export type ForegroundSupport = {
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
    maximumDurationMs: number;
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
     * Linux executes only while the application process lives.
     */
    executionScope?: "application-process";
    /**
     * False on Linux; a desktop process is not a boot service.
     */
    survivesProcessExit?: boolean;
};
export type ForegroundRequestOptions = {
    /**
     * Cancels the request, not an already admitted operation; query its durable ID after an uncertain reply.
     */
    signal?: AbortSignal;
};
