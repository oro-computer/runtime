/**
 * @typedef {object} PlatformRequestOptions
 * @property {AbortSignal} [signal] Abort native work and discard a late result.
 */
/**
 * Invokes a narrowly named native operation with a JSON object payload. Requests
 * require a foreground interactive document, expire after 90 seconds and share
 * native capacity limits. Results are limited to 512 KiB. The extension must
 * implement its own feature authorization; trusted origin is not server IAM.
 * Unregistered extensions and unimplemented platforms reject with ENOSYS.
 * @param {string} extension Configured extension name.
 * @param {string} action Operation name.
 * @param {object} [payload]
 * @param {PlatformRequestOptions} [options]
 * @returns {Promise<any>}
 */
export function request(extension: string, action: string, payload?: object, options?: PlatformRequestOptions): Promise<any>;
/**
 * @typedef {'authentication'|'credentials'|'foreground'|'foreground.continuous'|'deferred'|'push'|'launcher'|'extension'|'back'|'gestures'|'notifications'|'records'|'coordination'|'secure-storage'|'background'|'workflows'} PlatformFeature
 */
/**
 * @typedef {object} PlatformFeatureSupport
 * @property {number} schema Descriptor schema version, currently 1.
 * @property {'android'|'ios'|'macos'|'linux'|'windows'|'unknown'} platform iPadOS uses the ios backend.
 * @property {PlatformFeature} feature
 * @property {'implemented'|'missing'|'restricted'} implementation Missing is unimplemented runtime work; restricted is a platform API limitation.
 * @property {string} backend Native implementation family, empty when absent.
 * @property {string} reason Stable unavailable reason, empty for an implemented backend.
 * @property {boolean} requiresRuntimeCheck An implemented backend still needs the feature's runtime support, permission, hardware and configuration checks.
 */
/**
 * @typedef {object} PlatformCapabilities
 * @property {number} schema Currently 1.
 * @property {string} platform
 * @property {Object<string, PlatformFeatureSupport>} features Inventory of the new native operation interfaces. This does not describe every existing Oro API.
 */
/**
 * Reads the compiled backend inventory on every platform without prompting,
 * loading extensions or accessing saved data. An implemented backend is not
 * proof of hardware, OS admission, current permission or device qualification.
 * @returns {Promise<PlatformCapabilities>}
 */
export function getCapabilities(): Promise<PlatformCapabilities>;
/**
 * Reads one compiled native feature descriptor without prompting. Query the
 * feature's own support/status API before starting an operation. Missing and
 * OS-restricted features are separate results; operational calls still reject.
 * @param {PlatformFeature} feature
 * @returns {Promise<PlatformFeatureSupport>}
 */
export function getSupport(feature: PlatformFeature): Promise<PlatformFeatureSupport>;
declare namespace _default {
    export { request };
    export { getCapabilities };
    export { getSupport };
}
export default _default;
export type PlatformRequestOptions = {
    /**
     * Abort native work and discard a late result.
     */
    signal?: AbortSignal;
};
export type PlatformFeature = "authentication" | "credentials" | "foreground" | "foreground.continuous" | "deferred" | "push" | "launcher" | "extension" | "back" | "gestures" | "notifications" | "records" | "coordination" | "secure-storage" | "background" | "workflows";
export type PlatformFeatureSupport = {
    /**
     * Descriptor schema version, currently 1.
     */
    schema: number;
    /**
     * iPadOS uses the ios backend.
     */
    platform: "android" | "ios" | "macos" | "linux" | "windows" | "unknown";
    feature: PlatformFeature;
    /**
     * Missing is unimplemented runtime work; restricted is a platform API limitation.
     */
    implementation: "implemented" | "missing" | "restricted";
    /**
     * Native implementation family, empty when absent.
     */
    backend: string;
    /**
     * Stable unavailable reason, empty for an implemented backend.
     */
    reason: string;
    /**
     * An implemented backend still needs the feature's runtime support, permission, hardware and configuration checks.
     */
    requiresRuntimeCheck: boolean;
};
export type PlatformCapabilities = {
    /**
     * Currently 1.
     */
    schema: number;
    platform: string;
    /**
     * Inventory of the new native operation interfaces. This does not describe every existing Oro API.
     */
    features: {
        [x: string]: PlatformFeatureSupport;
    };
};
