/**
 * Reads policy/enrollment availability without prompting or creating keys.
 * Missing or platform-restricted backends return unavailable status; operational calls still reject with ENOSYS.
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
 * Decrypts through an Android authenticated Cipher or Linux authenticated hardware
 * unwrap followed by native AES-GCM. Linux ephemeral AES keys exist briefly in
 * native memory and are erased after use; long-lived private keys stay in the
 * provider. Returns bytes; application session recovery remains app-owned.
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
     * Strong biometric; Android optionally allows device credentials. Linux requires explicit biometric-or-device and a key-provider PIN/protected path. That policy does not invalidate on biometric enrollment changes.
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
    /**
     * Linux schema 2: RSA-OAEP-SHA256 wrapped ephemeral AES key. Preserve the whole record.
     */
    wrappedKey?: string;
    /**
     * Linux schema 2: application, key and provider binding.
     */
    binding?: string;
    /**
     * Linux schema 2: hardware RSA key size. Records are platform-specific.
     */
    wrappingKeyBits?: number;
};
export type AuthenticationStatus = {
    /**
     * Present on an unavailable cross-platform support result.
     */
    platform?: string;
    /**
     * Missing runtime backend or platform restriction; not a permission denial.
     */
    implementation?: "missing" | "restricted";
    /**
     * Stable unavailable reason.
     */
    reason?: string;
    /**
     * Platform implements this policy; Android requires API 30+.
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
    /**
     * 1 when the Android historical migration endpoints are compiled in; query oro:migration.getSupport for project configuration.
     */
    historicalMigrationProtocol?: number;
    serverVerificationRequired: boolean;
    /**
     * At most one native prompt per process.
     */
    busy: boolean;
    /**
     * Linux reports false; a provider PIN is not biometric attestation.
     */
    biometricOnlySupported?: boolean;
    /**
     * Linux reports key-provider-pin, including providers with a protected authentication path.
     */
    credentialType?: string;
    /**
     * Available Linux provider families: tpm2 or pkcs11. Discovery does not authenticate.
     */
    providers?: string[];
    /**
     * Linux auto preference is tpm2-then-hardware-pkcs11. Saved keys never switch providers automatically.
     */
    providerPreference?: string;
    /**
     * Linux reports false; trusted modules declare hardware slot support.
     */
    hardwareAttestationProvided?: boolean;
    /**
     * Provider advertises supported signing mechanisms; actual key policy is checked at use.
     */
    signingAvailable?: boolean;
    /**
     * Provider advertises supported wrapping mechanisms; actual key policy is checked at use.
     */
    sealingAvailable?: boolean;
    /**
     * Configured RSA size for newly created Linux secret keys.
     */
    newWrappingKeyBits?: number;
    /**
     * Stable native discovery error when no eligible Linux provider is available.
     */
    providerStatusCode?: string;
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
