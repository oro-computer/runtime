/**
 * @typedef {object} LauncherDestination
 * @property {string} id Stable opaque reference, 1..128 letters/digits/underscores/hyphens.
 * @property {string} label User-visible label, 1..80 characters, no control characters.
 * @property {string} accountRef Opaque account identity; never credentials or tokens.
 * @property {string} targetRef Opaque saved target identity; never an executable URL or mutation.
 */
/**
 * @typedef {object} LauncherReceipt
 * @property {string} receiptId
 * @property {string} id
 * @property {string|null} accountRef Null for a compile-time static entry, to be resolved after sign-in.
 * @property {string} targetRef
 * @property {boolean} static
 * @property {number} receivedAt Unix milliseconds. At most 32 receipts, retained for seven days.
 */
/**
 * @typedef {object} LauncherStatus
 * @property {string} [platform] Present on an unavailable cross-platform support result.
 * @property {'missing'|'restricted'} [implementation] Missing runtime backend or platform contract restriction; not a permission denial.
 * @property {string} [reason] Stable unavailable reason.
 * @property {boolean} available
 * @property {boolean} pinSupported
 * @property {number} maximumPerActivity Android's quota, shared with app-owned shortcuts.
 * @property {boolean} rateLimited
 * @property {'resolve-after-authentication'} authorizationBoundary
 */
/**
 * @typedef {object} LauncherPublicationResult
 * @property {number} published
 * @property {boolean} authorizationEstablished False; apps must authenticate and resolve targets.
 */
/**
 * @typedef {object} LauncherPinResult
 * @property {boolean} requested
 * @property {boolean} pinConfirmed False; submission is not user confirmation.
 */
/**
 * Reads native launcher support without publishing destinations.
 * @returns {Promise<LauncherStatus>}
 */
export function getStatus(): Promise<LauncherStatus>;
/**
 * Replaces this runtime's dynamic destinations, preserving unrelated app
 * shortcuts. Linux publishes a separate owned desktop entry containing actions;
 * it does not overwrite the installed app entry or request pinning. Changed
 * publications invalidate old routing generations, including copied entries.
 * Removed Android targets are disabled, including pinned copies. At most
 * 16 entries and Android's per-activity quota. Must run from a visible window.
 * @param {LauncherDestination[]} destinations
 * @returns {Promise<LauncherPublicationResult>}
 */
export function replace(destinations: LauncherDestination[]): Promise<LauncherPublicationResult>;
/**
 * Requests Android's user-controlled pin flow for an existing destination. Linux rejects ENOSYS.
 * Submission is not confirmation that the user pinned the shortcut.
 * @param {string} id
 * @returns {Promise<LauncherPinResult>}
 */
export function pin(id: string): Promise<LauncherPinResult>;
/**
 * Reads pending cold/warm launch identities after restoring authorization.
 * Reads do not consume receipts. Validate current IAM and handle removed
 * accounts/targets; opening a shortcut must not replay a mutation.
 * @returns {Promise<LauncherReceipt[]>}
 */
export function getResponses(): Promise<LauncherReceipt[]>;
/**
 * Acknowledges a reconciled or rejected routing receipt. Repeat acknowledgements
 * are allowed. Call only after authenticated target resolution has completed.
 * @param {string} receiptId
 * @returns {Promise<void>}
 */
export function acknowledgeResponse(receiptId: string): Promise<void>;
declare namespace _default {
    export { getStatus };
    export { replace };
    export { pin };
    export { getResponses };
    export { acknowledgeResponse };
}
export default _default;
export type LauncherDestination = {
    /**
     * Stable opaque reference, 1..128 letters/digits/underscores/hyphens.
     */
    id: string;
    /**
     * User-visible label, 1..80 characters, no control characters.
     */
    label: string;
    /**
     * Opaque account identity; never credentials or tokens.
     */
    accountRef: string;
    /**
     * Opaque saved target identity; never an executable URL or mutation.
     */
    targetRef: string;
};
export type LauncherReceipt = {
    receiptId: string;
    id: string;
    /**
     * Null for a compile-time static entry, to be resolved after sign-in.
     */
    accountRef: string | null;
    targetRef: string;
    static: boolean;
    /**
     * Unix milliseconds. At most 32 receipts, retained for seven days.
     */
    receivedAt: number;
};
export type LauncherStatus = {
    /**
     * Present on an unavailable cross-platform support result.
     */
    platform?: string;
    /**
     * Missing runtime backend or platform contract restriction; not a permission denial.
     */
    implementation?: "missing" | "restricted";
    /**
     * Stable unavailable reason.
     */
    reason?: string;
    available: boolean;
    pinSupported: boolean;
    /**
     * Android's quota, shared with app-owned shortcuts.
     */
    maximumPerActivity: number;
    rateLimited: boolean;
    authorizationBoundary: "resolve-after-authentication";
};
export type LauncherPublicationResult = {
    published: number;
    /**
     * False; apps must authenticate and resolve targets.
     */
    authorizationEstablished: boolean;
};
export type LauncherPinResult = {
    requested: boolean;
    /**
     * False; submission is not user confirmation.
     */
    pinConfirmed: boolean;
};
