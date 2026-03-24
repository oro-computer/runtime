/**
 * Class for handling encryption and key management.
 */
export class Encryption {
    /**
     * Creates a shared key based on the provided seed or generates a random one.
     * @param {Uint8Array|string} seed - Seed for key generation.
     * @returns {Promise<Uint8Array>} - Shared key.
     */
    static createSharedKey(seed: Uint8Array | string): Promise<Uint8Array>;
    /**
     * Creates a key pair for signing and verification.
     * @param {Uint8Array|string} seed - Seed for key generation.
     * @returns {Promise<{ publicKey: Uint8Array, privateKey: Uint8Array }>} - Key pair.
     */
    static createKeyPair(seed: Uint8Array | string): Promise<{
        publicKey: Uint8Array;
        privateKey: Uint8Array;
    }>;
    /**
     * Creates an ID using SHA-256 hash.
     * @param {string} str - String to hash.
     * @returns {Promise<Uint8Array>} - SHA-256 hash.
     */
    static createId(str: string): Promise<Uint8Array>;
    /**
     * Creates a cluster ID using SHA-256 hash with specified output size.
     * @param {string} str - String to hash.
     * @returns {Promise<Uint8Array>} - SHA-256 hash with specified output size.
     */
    static createClusterId(str: string): Promise<Uint8Array>;
    /**
     * Signs a message using the given secret key.
     * @param {Buffer} b - The message to sign.
     * @param {Uint8Array} sk - The secret key to use.
     * @returns {Uint8Array} - Signature.
     */
    static sign(b: Buffer, sk: Uint8Array): Uint8Array;
    /**
     * Verifies the signature of a message using the given public key.
     * @param {Buffer} b - The message to verify.
     * @param {Uint8Array} sig - The signature to check.
     * @param {Uint8Array} pk - The public key to use.
     * @returns {number} - Returns non-zero if the buffer could not be verified.
     */
    static verify(b: Buffer, sig: Uint8Array, pk: Uint8Array): number;
    /**
     * Mapping of public keys to key objects.
     * @type {Object.<string, { publicKey: Uint8Array, privateKey: Uint8Array, ts: number }>}
     */
    keys: {
        [x: string]: {
            publicKey: Uint8Array;
            privateKey: Uint8Array;
            ts: number;
        };
    };
    /**
     * Adds a key pair to the keys mapping.
     * @param {Uint8Array|string} publicKey - Public key.
     * @param {Uint8Array} privateKey - Private key.
     */
    add(publicKey: Uint8Array | string, privateKey: Uint8Array): void;
    /**
     * Removes a key from the keys mapping.
     * @param {Uint8Array|string} publicKey - Public key.
     */
    remove(publicKey: Uint8Array | string): void;
    /**
     * Checks if a key is in the keys mapping.
     * @param {Uint8Array|string} to - Public key or Uint8Array.
     * @returns {boolean} - True if the key is present, false otherwise.
     */
    has(to: Uint8Array | string): boolean;
    /**
     * Opens a sealed message using the specified key.
     * @param {Buffer} message - The sealed message.
     * @param {Object|string} v - Key object or public key.
     * @returns {Buffer} - Decrypted message.
     * @throws {Error} - Throws ENOKEY if the key is not found.
     */
    openUnsigned(message: Buffer, v: any | string): Buffer;
    sealUnsigned(message: any, v: any): any;
    /**
     * Decrypts a sealed and signed message for a specific receiver.
     * @param {Buffer} message - The sealed message.
     * @param {Object|string} v - Key object or public key.
     * @returns {Buffer} - Decrypted message.
     * @throws {Error} - Throws ENOKEY if the key is not found, EMALFORMED if the message is malformed, ENOTVERIFIED if the message cannot be verified.
     */
    open(message: Buffer, v: any | string): Buffer;
    /**
     * Seals and signs a message for a specific receiver using their public key.
     *
     * `Seal(message, receiver)` performs an _encrypt-sign-encrypt_ (ESE) on
     * a plaintext `message` for a `receiver` identity. This prevents repudiation
     * attacks and doesn't rely on packet chain guarantees.
     *
     * let ct = Seal(sender | pt, receiver)
     * let sig = Sign(ct, sk)
     * let out = Seal(sig | ct)
     *
     * In an setup between Alice & Bob, this means:
     * - Only Bob sees the plaintext
     * - Alice wrote the plaintext and the ciphertext
     * - Only Bob can see that Alice wrote the plaintext and ciphertext
     * - Bob cannot forward the message without invalidating Alice's signature.
     * - The outer encryption serves to prevent an attacker from replacing Alice's
     *   signature. As with _sign-encrypt-sign (SES), ESE is a variant of
     *   including the recipient's name inside the plaintext, which is then signed
     *   and encrypted Alice signs her plaintext along with her ciphertext, so as
     *   to protect herself from a laintext-substitution attack. At the same time,
     *   Alice's signed plaintext gives Bob non-repudiation.
     *
     * @see https://theworld.com/~dtd/sign_encrypt/sign_encrypt7.html
     *
     * @param {Buffer} message - The message to seal.
     * @param {Object|string} v - Key object or public key.
     * @returns {Buffer} - Sealed message.
     * @throws {Error} - Throws ENOKEY if the key is not found.
     */
    seal(message: Buffer, v: any | string): Buffer;
}
import Buffer from '../buffer.js';
