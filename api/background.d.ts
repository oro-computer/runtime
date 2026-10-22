/**
 * Checks the supported native task kind. This does not register a JS handler or
 * authorize future application work. Other platforms reject with ENOSYS.
 * @param {'share-prune'} taskKind
 * @returns {Promise<BackgroundTaskSupport>}
 */
export function register(taskKind: "share-prune"): Promise<BackgroundTaskSupport>;
/**
 * Schedules one unique, persisted Android share-prune job. Scheduling replaces
 * the previous cleanup of that kind. Android decides timing and boot recovery;
 * force-stop prevents execution until relaunch. The task removes only expired
 * runtime-owned share files; it never executes JS, contacts servers, or retries
 * application mutations. Failed native cleanup has at most three attempts with
 * Android exponential backoff starting at 30 seconds. Requires background.enabled.
 * @param {BackgroundScheduleOptions} options
 * @returns {Promise<BackgroundTaskState>}
 */
export function schedule(options: BackgroundScheduleOptions): Promise<BackgroundTaskState>;
/**
 * Cancels the unique native cleanup. A running deletion may finish its current
 * file before observing cancellation. Already removed expired files stay removed.
 * @param {'share-prune'} [taskKind='share-prune']
 * @returns {Promise<BackgroundTaskState>}
 */
export function cancel(taskKind?: "share-prune"): Promise<BackgroundTaskState>;
/**
 * Reads actual native scheduling availability and durable state. An interrupted
 * record with no retained OS job is explicit; it is not silently re-enqueued.
 * Other platforms reject with ENOSYS.
 * @param {'share-prune'} [taskKind='share-prune']
 * @returns {Promise<BackgroundTaskStatus>}
 */
export function status(taskKind?: "share-prune"): Promise<BackgroundTaskStatus>;
/**
 * Whether this document requested the native Android cleanup capability.
 * Read status() for actual manifest and scheduler availability.
 * @type {boolean}
 */
export const available: boolean;
/**
 * @typedef {object} BackgroundModule
 * @property {typeof available} available
 * @property {typeof register} register
 * @property {typeof schedule} schedule
 * @property {typeof cancel} cancel
 * @property {typeof status} status
 */
/** @type {BackgroundModule} Native task entry points and the document's opt-in flag. */
export const background: BackgroundModule;
export default background;
export type BackgroundScheduleOptions = {
    /**
     * Native cleanup only; serialized payloads are rejected.
     */
    kind: "share-prune";
    /**
     * Earliest execution delay, from 0 through seven days.
     */
    delayMs?: number;
    requiresCharging?: boolean;
    requiresBatteryNotLow?: boolean;
};
export type BackgroundTaskState = {
    schema?: number;
    kind?: "share-prune";
    phase?: "scheduling" | "scheduled" | "running" | "stopped" | "interrupted" | "completed" | "cancelled" | "rejected" | "failed" | "idle";
    /**
     * Opaque scheduling identity, not an authorization token.
     */
    generation?: string;
    /**
     * At most three native cleanup attempts.
     */
    attempts?: number;
    /**
     * Earliest Unix execution time; never a deadline guarantee.
     */
    notBefore?: number;
    updatedAt?: number;
};
export type BackgroundTaskSupport = {
    kind: "share-prune";
    available: boolean;
    maximumAttempts: number;
    /**
     * False; Android controls execution time.
     */
    exactTiming: boolean;
    /**
     * False; the app must be launched again.
     */
    survivesForceStop: boolean;
};
export type BackgroundTaskStatus = {
    /**
     * Requires explicit background.enabled and generated manifest declarations.
     */
    available: boolean;
    kind: "share-prune";
    supportedKinds: string[];
    /**
     * Android currently retains the job.
     */
    pending: boolean;
    /**
     * Durable last scheduling record.
     */
    state: BackgroundTaskState;
    /**
     * False.
     */
    backgroundJavaScript: boolean;
    /**
     * False.
     */
    push: boolean;
};
export type BackgroundModule = {
    available: typeof available;
    register: typeof register;
    schedule: typeof schedule;
    cancel: typeof cancel;
    status: typeof status;
};
