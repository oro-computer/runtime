/**
 * @typedef {object} PasswordSelectionOptions
 * @property {string} [username] Restrict results to this exact saved username.
 * @property {AbortSignal} [signal] Cancel the document-owned native prompt.
 */
/**
 * @typedef {object} SelectedPassword
 * @property {string} username Provider-selected username.
 * @property {string} password Private value for the interactive form only.
 */
/**
 * @typedef {object} PasswordSelectionStatus
 * @property {string} [platform] Present on an unavailable cross-platform support result.
 * @property {'missing'|'restricted'} [implementation] Missing runtime backend or platform API restriction; not a permission denial.
 * @property {string} [reason] Stable unavailable reason.
 * @property {boolean} supported Platform implements native password selection.
 * @property {boolean} enabled Application opted into selection.
 * @property {boolean} busy A selection request is active.
 * @property {'secret-service'} [provider] Linux provider backend.
 * @property {'application-attribute'} [association] Linux searches exact application and optional username attributes.
 * @property {boolean} [autofill] False on Linux; selection does not install WebKit Autofill.
 */
/**
 * Queries support without opening a provider or reading a credential.
 * Missing or platform-restricted backends return unavailable status; operational calls still reject with ENOSYS.
 * @returns {Promise<PasswordSelectionStatus>}
 */
export function getStatus(): Promise<PasswordSelectionStatus>;
/**
 * Opens the native provider chooser after an explicit user action. Results bind
 * to the requesting foreground document and are never persisted or logged by
 * this API. No remote origin is asserted; providers determine app association.
 * It does not submit a form, save/update a password or authenticate a server.
 * Cancellation rejects ABORT_ERR; no available credential rejects ENOENT.
 * A provider result waits at most five seconds for the requesting Activity to
 * resume, then revalidates document ownership and the foreground window.
 * Cancellation, navigation, activity stop and the original deadline still apply.
 * Linux shows at most 64 matching Secret Service accounts in a native modal. Items
 * must carry `application=<bundle identifier>` and `username` attributes. Only
 * the chosen item is unlocked/read. These lookup attributes are not an attested
 * app association. Secret Service providers and locked-keyring prompts require
 * desktop qualification; no browser-origin association or automatic fill is added.
 * @param {PasswordSelectionOptions} [options]
 * @returns {Promise<SelectedPassword>}
 */
export function selectPassword(options?: PasswordSelectionOptions): Promise<SelectedPassword>;
declare namespace _default {
    export { getStatus };
    export { selectPassword };
}
export default _default;
export type PasswordSelectionOptions = {
    /**
     * Restrict results to this exact saved username.
     */
    username?: string;
    /**
     * Cancel the document-owned native prompt.
     */
    signal?: AbortSignal;
};
export type SelectedPassword = {
    /**
     * Provider-selected username.
     */
    username: string;
    /**
     * Private value for the interactive form only.
     */
    password: string;
};
export type PasswordSelectionStatus = {
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
     * Platform implements native password selection.
     */
    supported: boolean;
    /**
     * Application opted into selection.
     */
    enabled: boolean;
    /**
     * A selection request is active.
     */
    busy: boolean;
    /**
     * Linux provider backend.
     */
    provider?: "secret-service";
    /**
     * Linux searches exact application and optional username attributes.
     */
    association?: "application-attribute";
    /**
     * False on Linux; selection does not install WebKit Autofill.
     */
    autofill?: boolean;
};
