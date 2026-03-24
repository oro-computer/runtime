/**
 * Selects a suitable update for the given options.
 * @param {UpdateManifest} manifest
 * @param {UpdateSelectionOptions} [options]
 * @returns {UpdateSelectionResult|null}
 */
export function selectUpdate(manifest: UpdateManifest, options?: UpdateSelectionOptions): UpdateSelectionResult | null;
/**
 * Verifies an artifact payload against the hash declared in the target.
 * @param {Uint8Array|ArrayBuffer} payload
 * @param {UpdateTarget} target
 * @returns {Promise<void>}
 */
export function verifyArtifact(payload: Uint8Array | ArrayBuffer, target: UpdateTarget): Promise<void>;
/**
 * Opens a verified artifact as a tar archive using the native tar service.
 * This is a convenience helper that wraps the artifact bytes in a TarArchive
 * so callers can inspect and extract entries using the `oro:tar` API surface.
 *
 * @param {Uint8Array|ArrayBuffer|import('../buffer.js').Buffer} artifact
 * @returns {Promise<import('../tar.js').TarArchive>}
 */
export function openArtifactArchive(artifact: Uint8Array | ArrayBuffer | import("../buffer.js").Buffer): Promise<import("../tar.js").TarArchive>;
/**
 * Downloads an artifact and verifies its hash.
 * Prefers the native update service and falls back to the JS fetch-based
 * implementation when the service is not available in this build.
 * @param {UpdateTarget} target
 * @param {DownloadOptions} [options]
 * @returns {Promise<Uint8Array>}
 */
export function downloadUpdate(target: UpdateTarget, options?: DownloadOptions): Promise<Uint8Array>;
/**
 * Fetches and verifies a manifest + signature pair.
 * @param {ManifestFetchOptions} options
 * @returns {Promise<{ manifest: UpdateManifest, raw: Uint8Array, signature: ManifestSignature }>}
 */
export function fetchManifest(options: ManifestFetchOptions): Promise<{
    manifest: UpdateManifest;
    raw: Uint8Array;
    signature: ManifestSignature;
}>;
/**
 * High-level helper: fetches & verifies the manifest, selects an update,
 * and optionally downloads the artifact. Prefers the native update(service)
 * when available and falls back to the JS implementation otherwise.
 * @param {UpdateCheckOptions} options
 * @returns {Promise<UpdateCheckResult>}
 */
export function checkForUpdates(options: UpdateCheckOptions): Promise<UpdateCheckResult>;
export default api;
export type UpdateTarget = {
    /**
     * - Target platform identifier (for example, `darwin`, `win32`, `linux`).
     */
    platform: string;
    /**
     * - Target CPU architecture (for example, `x64`, `arm64`).
     */
    arch: string;
    /**
     * - Absolute or relative URL for the update payload.
     */
    artifactUrl: string;
    /**
     * - Expected payload length in bytes.
     */
    length?: number;
    /**
     * - Hash algorithm identifier (for example, `sha256`).
     */
    hashAlgorithm: string;
    /**
     * - Hex or base64url encoded hash of the payload.
     */
    hash: string;
    /**
     * - Optional artifact signature algorithm (for example, `ed25519`).
     */
    signatureAlgorithm?: string;
    /**
     * - Optional encoded signature over the artifact bytes.
     */
    artifactSignature?: string;
    /**
     * - Optional OS version range constraint.
     */
    osVersionRange?: string;
};
export type UpdateDescriptor = {
    /**
     * - Update identifier, unique within the manifest.
     */
    id: string;
    /**
     * - Application version string (semantic version recommended).
     */
    version: string;
    /**
     * - Distribution channel (for example, `stable`, `beta`).
     */
    channel?: string;
    /**
     * - Minimum Oro runtime version required.
     */
    minRuntimeVersion?: string;
    /**
     * - Whether this update is considered critical.
     */
    critical?: boolean;
    /**
     * - Optional URL to human-readable release notes.
     */
    notesUrl?: string;
    /**
     * - Platform-specific artifacts for this update.
     */
    targets: UpdateTarget[];
};
export type UpdateManifest = {
    /**
     * - Manifest schema version.
     */
    schemaVersion: number;
    /**
     * - Application identifier (for example, reverse DNS).
     */
    appId: string;
    /**
     * - ISO8601 timestamp when the manifest was generated.
     */
    generatedAt?: string;
    /**
     * - Optional list of known channels.
     */
    channels?: string[];
    /**
     * - List of available updates.
     */
    updates: UpdateDescriptor[];
};
export type ManifestSignature = {
    /**
     * - Signature schema version.
     */
    schemaVersion: number;
    /**
     * - Signature algorithm (for example, `ed25519`).
     */
    algorithm: string;
    /**
     * - Optional key identifier for bookkeeping.
     */
    keyId?: string;
    /**
     * - Raw signature bytes.
     */
    signature: Uint8Array;
    /**
     * - Original textual encoding (`hex`, `base64`, or `base64url`).
     */
    encoding?: string;
};
export type KeyLike = Uint8Array | ArrayBuffer | import("../buffer.js").Buffer | string;
export type ManifestFetchOptions = {
    /**
     * - URL of the manifest JSON document.
     */
    manifestUrl: string;
    /**
     * - URL of the manifest signature JSON; defaults to `manifestUrl + '.sig'`.
     */
    signatureUrl?: string;
    /**
     * - Public key used to verify the manifest signature.
     */
    publicKey?: KeyLike;
    /**
     * - Optional list of public keys; the manifest is accepted if any key verifies.
     */
    publicKeys?: KeyLike[];
    /**
     * - Optional expected appId; if provided, the manifest's appId must match.
     */
    expectedAppId?: string;
    /**
     * - Optional custom fetch implementation.
     */
    fetch?: typeof globalThis.fetch;
    /**
     * - Optional abort signal for network requests.
     */
    signal?: AbortSignal;
    /**
     * - Optional additional HTTP headers for manifest/signature requests.
     */
    headers?: Record<string, string>;
    /**
     * - Optional maximum manifest size in bytes; manifests larger than this are rejected.
     */
    maxManifestBytes?: number;
};
export type UpdateSelectionOptions = {
    /**
     * - Desired update channel; defaults to `"stable"`.
     */
    channel?: string;
    /**
     * - Current application version.
     */
    currentVersion?: string;
    /**
     * - Target platform identifier; defaults to the runtime platform when available.
     */
    platform?: string;
    /**
     * - Target architecture identifier; defaults to the runtime architecture when available.
     */
    arch?: string;
    /**
     * - Current Oro runtime version; defaults to `process.versions.oro` when available.
     */
    runtimeVersion?: string;
};
export type UpdateSelectionResult = {
    /**
     * - The validated manifest.
     */
    manifest: UpdateManifest;
    /**
     * - The chosen update descriptor.
     */
    update: UpdateDescriptor;
    /**
     * - The chosen platform-specific target.
     */
    target: UpdateTarget;
};
export type DownloadOptions = {
    /**
     * - Optional custom fetch implementation.
     */
    fetch?: typeof globalThis.fetch;
    /**
     * - Optional abort signal for the download request.
     */
    signal?: AbortSignal;
    /**
     * - Optional maximum artifact size in bytes; artifacts larger than this are rejected.
     */
    maxArtifactBytes?: number;
};
export type UpdateCheckOptions = ManifestFetchOptions & UpdateSelectionOptions & DownloadOptions & {
    download?: boolean;
};
export type UpdateCheckResult = {
    /**
     * - Indicates whether an update is available.
     */
    updateAvailable: boolean;
    /**
     * - The validated manifest.
     */
    manifest: UpdateManifest;
    /**
     * - The validated manifest signature.
     */
    signature: ManifestSignature;
    /**
     * - The chosen update descriptor, when `updateAvailable` is `true`.
     */
    update?: UpdateDescriptor;
    /**
     * - The chosen platform-specific target, when `updateAvailable` is `true`.
     */
    target?: UpdateTarget;
    /**
     * - The downloaded and verified artifact bytes when `download` is `true`.
     */
    artifact?: Uint8Array;
};
export type UpdateModule = {
    selectUpdate: typeof selectUpdate;
    verifyArtifact: typeof verifyArtifact;
    downloadUpdate: typeof downloadUpdate;
    fetchManifest: typeof fetchManifest;
    checkForUpdates: typeof checkForUpdates;
};
/**
 * @typedef {object} UpdateModule
 * @property {typeof selectUpdate} selectUpdate
 * @property {typeof verifyArtifact} verifyArtifact
 * @property {typeof downloadUpdate} downloadUpdate
 * @property {typeof fetchManifest} fetchManifest
 * @property {typeof checkForUpdates} checkForUpdates
 */
/** @type {UpdateModule} */
declare const api: UpdateModule;
