/**
 * Reads policy/enrollment availability without prompting or creating keys.
 * Other platforms reject with ENOSYS.
 * @param {AuthenticationOptions} [options]
 * @returns {Promise<AuthenticationStatus>}
 */
export function getStatus(options?: AuthenticationOptions): Promise<AuthenticationStatus>;
/**
 * Encrypts at most 64 KiB after a native cryptographic authentication prompt.
 * At most 64 protected keys are retained. Existing keys are never silently
 * replaced after enrollment changes, removal or invalidation. Keep the returned
 * sealed record and handle EKEYREVOKED/ENOENT through explicit app recovery.
 * @param {string|Uint8Array|ArrayBuffer} value
 * @param {ProtectedKeyOptions} options
 * @returns {Promise<ProtectedSecret>}
 */
export function seal(value: string | Uint8Array | ArrayBuffer, options: ProtectedKeyOptions): Promise<ProtectedSecret>;
/**
 * Decrypts only through a user-authenticated native Cipher operation. Returns
 * bytes; decoding or restoring an application session remains app-owned.
 * @param {ProtectedSecret} sealed
 * @param {ProtectedKeyOptions} options
 * @returns {Promise<Uint8Array>}
 */
export function open(sealed: ProtectedSecret, options: ProtectedKeyOptions): Promise<Uint8Array>;
/**
 * Creates or reuses a non-exportable P-256 key and signs an enrollment challenge
 * after cryptographic authentication. This does not enroll the key on a server.
 * @param {ChallengeOptions} options
 * @returns {Promise<ChallengeSignature>}
 */
export function createSigningKey(options: ChallengeOptions): Promise<ChallengeSignature>;
/**
 * Signs a fresh authority/account-bound server challenge using an existing
 * authentication-gated key. No success boolean establishes server authority.
 * @param {ChallengeOptions} options
 * @returns {Promise<ChallengeSignature>}
 */
export function signChallenge(options: ChallengeOptions): Promise<ChallengeSignature>;
/**
 * Explicitly removes this app's protected secret or signing key. Ciphertext and
 * server enrollment become unusable; revoke enrolled keys on the server too.
 * @param {ProtectedKeyRemovalOptions} options
 * @returns {Promise<void>}
 */
export function removeKey(options: ProtectedKeyRemovalOptions): Promise<void>;
declare namespace _default {
    export { getStatus };
    export { seal };
    export { open };
    export { createSigningKey };
    export { signChallenge };
    export { removeKey };
}
export default _default;
export type AuthenticationPolicy = "biometric" | "biometric-or-device";
export type AuthenticationOptions = {
    /**
     * Strong biometric, optionally device credential fallback. The fallback policy does not invalidate on biometric enrollment changes.
     */
    policy?: AuthenticationPolicy;
    /**
     * Aborting, navigation, renderer loss or backgrounding cancels the prompt.
     */
    signal?: AbortSignal;
};
export type ProtectedKeyOptions = AuthenticationOptions & {
    id: string;
};
export type ProtectedKeyRemovalOptions = ProtectedKeyOptions & {
    kind?: "secret" | "signing";
};
export type ProtectedSecret = {
    schema: number;
    id: string;
    policy: AuthenticationPolicy;
    /**
     * Base64 AES-GCM IV.
     */
    iv: string;
    /**
     * Base64 ciphertext and authentication tag. Store this sealed record, not plaintext.
     */
    ciphertext: string;
};
export type AuthenticationStatus = {
    /**
     * Android API 30+.
     */
    supported: boolean;
    /**
     * Explicit capability and manifest permission.
     */
    enabled: boolean;
    /**
     * Current enrollment/hardware/policy result, not authenticated identity.
     */
    canAuthenticate: boolean;
    /**
     * Native BiometricManager result or -1 when unavailable.
     */
    statusCode: number;
    policy: AuthenticationPolicy;
    cryptographicOperationsOnly: boolean;
    serverVerificationRequired: boolean;
    /**
     * At most one native prompt per process.
     */
    busy: boolean;
};
/**
 * `authority` is an exact HTTPS origin, including a self-hosted IP origin when
 * properly secured. `accountRef` is an opaque account reference. `challenge` is
 * canonical Base64 of 16..1024 server-generated random bytes; `expiresAt` is a
 * server challenge expiry in Unix milliseconds, at most five minutes ahead.
 */
export type ChallengeOptions = ProtectedKeyOptions & {
    authority: string;
    accountRef: string;
    challenge: string;
    expiresAt: number;
};
export type ChallengeSignature = {
    algorithm: "ECDSA_P256_SHA256";
    /**
     * Base64 SubjectPublicKeyInfo. Enrollment must be authenticated by the authority.
     */
    publicKey: string;
    /**
     * Base64 exact UTF-8 JSON bytes signed, binding application, authority, account, key, nonce and expiry.
     */
    payload: string;
    /**
     * Base64 ASN.1 DER ECDSA signature. The server must verify it, nonce single use, current account/key authorization and expiry.
     */
    signature: string;
};
