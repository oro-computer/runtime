/**
 * @module migration
 * Bundled historical protected imports and retained Android component handover.
 * Aliases, codecs, AAD and component identities come from immutable project data.
 * Original records/keys and uncertain receipts are preserved. Account authority,
 * historical schema and authenticated server reconciliation remain application policy.
 *
 * Platform support: Android only, with `[migration] enabled = true`, bundled
 * definitions and an SDK reporting historicalMigrationProtocol 1. Protected
 * imports require Android 11+ authentication-bound keys. iOS/iPadOS, macOS,
 * Linux and Windows historical migration backends are not implemented.
 * `getSupport()` reports unavailable with a platform/reason; operational calls
 * reject ENOSYS. There is no fallback to plaintext storage or background JS.
 * Source implementation and a support probe do not establish historical signed
 * upgrade, production signing, OEM authentication or server reconciliation acceptance.
 * Fixed `prefer-native-records-v1` source selection uses legacy ciphertext only
 * while the native scope is absent. A present scope is authoritative; missing
 * rows, lost keys and provenance mismatches never fall back to legacy data.
 * `[migration] generate_adapters = false` bundles identical policy for a bridge
 * build retaining existing app components. Generated adapters do not continue
 * old alarm presentation before an explicitly approved replacement handover.
 */
import platformRequest from './internal/platform-request.js'
import { platform } from './os.js'

/**
 * @typedef {object} MigrationRequestOptions
 * @property {AbortSignal} [signal] Cancels owned prompts/work and discards late document replies.
 */
/**
 * @typedef {object} MigrationSupport
 * @property {boolean} available
 * @property {string} platform Current runtime platform.
 * @property {'implemented'|'missing'} implementation Backend inventory, not device qualification.
 * @property {string} backend Native backend name, empty when missing.
 * @property {string} reason Fixed unavailable reason, empty when available.
 * @property {string} [protectedImport] protected-import-v1 on the configured Android backend.
 * @property {string} [retainedComponents] retained-components-v1 on the configured Android backend.
 * @property {string} [sourceSelection] prefer-native-records-v1 when bounded source selection is implemented (Android only).
 * @property {string} [legacyProvenance] android-json-pairs-sha256-v1 when aggregate ciphertext validation is implemented (Android only).
 * @property {boolean} [generatedAdapters] False for a bridge build retaining app-owned component classes and manifest entries.
 * @property {boolean} nativeIntegrationRequired Always false.
 * @property {string[]} profiles Bundled protected source IDs; no aliases or saved values.
 * @property {string[]} components Bundled retained component IDs.
 * @property {number} [maximumImports] 16; encrypted bodies are committed in bounded native chunks.
 * @property {number} [maximumReceipts] 48, including unresolved historical input.
 * @property {number} [maximumEntries] 15 untrusted routing entries.
 * @property {number} [maximumStoredBytes] 262144 aggregate decoded native-state bytes.
 * @property {string} [error] Fixed unavailable/configuration code.
 */
/**
 * @typedef {object} ProtectedSource
 * @property {string} profile Bundled allowlist entry.
 * @property {string} sourceRef Bounded historical record reference, not a key or alias selector.
 */
/**
 * @typedef {object} ProtectedSourceStatus
 * @property {string} profile
 * @property {string} sourceSha256 Original ciphertext digest, or an opaque keyed source version for native records.
 * @property {boolean} originalsPreserved Always true.
 */
/**
 * @typedef {ProtectedSource & { authorityRef: string, accountRef: string, targetRef: string }} ProtectedIdentity
 */
/**
 * @typedef {ProtectedIdentity & { sourceSha256: string, userInitiated: boolean, recoveryRef?: string }} ProtectedImportOptions
 */
/**
 * @typedef {object} ProtectedImport
 * @property {string} handle Opaque document-origin and identity-bound rewrapped record.
 * @property {string} profile
 * @property {string} sourceSha256
 * @property {boolean} valueExported Always false for import/discovery.
 * @property {boolean} originalsPreserved Always true; there is no automatic deletion of old keys/ciphertext.
 */
/**
 * @typedef {object} ProtectedOpenOptions
 * @property {string} handle
 * @property {boolean} userInitiated Must follow explicit private interactive user action.
 */
/**
 * @typedef {object} ProtectedValue
 * @property {string} value Base64 bytes for the original requesting private form only. Never log or expose through assistant tooling.
 */
/**
 * @typedef {object} HistoricalComponentSource
 * @property {string} component Bundled retained component ID.
 * @property {string} sourceRef Historical reference in that component's fixed source family.
 */
/**
 * @typedef {object} HistoricalSourceStatus
 * @property {string} sourceSha256
 * @property {string} sourceGeneration Bundled generation field, or envelope digest when no old generation field exists.
 */
/**
 * @typedef {HistoricalComponentSource & HistoricalSourceStatus & { expectedGeneration: string, bindingId: string, bindingGeneration: string, authorityRef: string, accountRef: string, targetRef: string, userInitiated: boolean }} HandoverOptions
 */
/**
 * @typedef {object} HandoverId
 * @property {string} id
 * @property {string} generation Exact native handover generation.
 */
/**
 * @typedef {HandoverId & HistoricalSourceStatus & { phase: 'prepared'|'active'|'stopped', originalsPreserved: boolean, remoteRevocationConfirmed: boolean, drained: boolean }} HandoverStatus
 */
/**
 * @typedef {object} HandoverIdentity
 * @property {string} authorityRef
 * @property {string} accountRef
 * @property {string} targetRef
 */
/**
 * @typedef {HandoverStatus & HistoricalComponentSource} DiscoveredHandover
 */
/**
 * @typedef {object} RetainedReceipt
 * @property {string} receiptId
 * @property {string} sourceGeneration
 * @property {'receiver'|'job'} kind
 * @property {'unknown'|'read-completed'} outcome A local read does not confirm the old delivery, mutation or server acknowledgement.
 * @property {number} receivedAt
 */
/**
 * @typedef {HandoverId & { receiptId: string }} RetainedReceiptId
 */
/**
 * @typedef {RetainedReceiptId & { outcome: 'applied'|'not-applied', receiptSha256: string, userInitiated: boolean }} ReceiptResolution
 */
/**
 * @typedef {object} ReceiptResolutionResult
 * @property {boolean} resolvedLocally
 * @property {boolean} remoteOutcomeVerifiedByRuntime Always false. The application must verify its authenticated server receipt.
 */
/**
 * @typedef {object} ReceiptReleaseResult
 * @property {boolean} released
 */
/**
 * @typedef {ProtectedSource & { userInitiated: boolean }} UnprotectedReadOptions
 */
/**
 * @typedef {ProtectedValue & { originalsPreserved: boolean }} UnprotectedValue
 */
/**
 * @typedef {object} HistoricalEntryOptions
 * @property {string} component Bundled Activity alias profile.
 */
/**
 * @typedef {HistoricalEntryOptions & { entryId: string }} HistoricalEntryId
 */
/**
 * @typedef {HistoricalEntryId & { action: string, references: Object<string,string>, receivedAt: number, untrusted: boolean }} HistoricalEntry
 */
/**
 * @typedef {object} HistoricalEntryAcknowledgement
 * @property {boolean} acknowledged
 */

function unavailable (currentPlatform) {
  return {
    available: false,
    platform: currentPlatform,
    implementation: 'missing',
    backend: '',
    reason: currentPlatform === 'android' ? 'migration-protocol-unavailable' : 'migration-backend-unimplemented',
    nativeIntegrationRequired: false,
    profiles: [],
    components: [],
    error: 'ENOSYS'
  }
}

async function request (action, options, request = {}) {
  const currentPlatform = platform()
  const status = currentPlatform === 'android' ? await platformRequest('authentication', 'status', {}, request.signal) : null
  if (status?.historicalMigrationProtocol !== 1) {
    const support = unavailable(currentPlatform)
    const error = new Error(`Historical migration is unavailable on ${currentPlatform}: ${support.reason}. Use migration.getSupport() before offering migration.`)
    error.code = 'ENOSYS'
    error.feature = 'migration'
    error.platform = currentPlatform
    error.implementation = support.implementation
    error.reason = support.reason
    throw error
  }
  return platformRequest('authentication', 'migration.' + action, options, request.signal)
}

/**
 * Probe without prompting, creating keys or reading legacy values. Other platforms
 * and older runtimes report unavailable; operational calls still reject ENOSYS.
 * Android implements this backend. iOS/iPadOS, macOS, Linux and Windows return
 * reason migration-backend-unimplemented; older Android SDKs return
 * migration-protocol-unavailable. A configured backend can be unavailable due
 * to disabled/malformed bundled definitions; inspect error before enabling UI.
 * @returns {Promise<MigrationSupport>}
 */
export async function getSupport () {
  if (platform() !== 'android') return unavailable(platform())
  try {
    const support = await request('support', {})
    return { platform: platform(), implementation: 'implemented', backend: 'android-historical-migration', reason: support.available ? '' : support.error || 'migration-unavailable', ...support }
  } catch (error) {
    if (error?.code !== 'ENOSYS') throw error
    return unavailable(platform())
  }
}

/**
 * @param {ProtectedSource} options
 * @returns {Promise<ProtectedSourceStatus>}
 */
export async function inspectProtected (options) { return request('inspectProtected', options) }

/**
 * Opens the bundled historical envelope with its original key, then rewraps to
 * an authentication-bound runtime key. Each protected cryptographic operation
 * uses its own system prompt. No plaintext is returned. The original source
 * digest is rechecked before commit; retry/discovery recovers an already committed import.
 * After explicit user recovery, a fresh recoveryRef rewraps the still-readable
 * original into a new key/handle while preserving earlier imports. Reuse that
 * recoveryRef to reconcile a lost reply. Missing original keys are never replaced.
 * @param {ProtectedImportOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<ProtectedImport>}
 */
export async function importProtected (options, settings = {}) { return request('importProtected', options, settings) }

/**
 * Unlock a migrated value for the owning private interactive form. This always
 * uses the new key's per-operation prompt, never a cached authentication boolean.
 * @param {ProtectedOpenOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<ProtectedValue>}
 */
export async function openProtected (options, settings = {}) { return request('openProtected', options, settings) }

/**
 * @param {ProtectedIdentity} options
 * @returns {Promise<ProtectedImport[]>}
 */
export async function discoverProtected (options) { return request('discoverProtected', options) }

/**
 * Read only a bundled noninteractive source explicitly marked exposure:
 * private-document. Requires native foreground consent and returns Base64 to
 * that private document. Authentication-bound sources always reject this path.
 * Never expose the result through logs, assistant tools or background handlers.
 * @param {UnprotectedReadOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<UnprotectedValue>}
 */
export async function readUnprotected (options, settings = {}) { return request('readUnprotected', options, settings) }

/**
 * Read bounded opaque references captured from an allowlisted historical
 * Activity action. Primary window only. Every entry is untrusted: authorize
 * its destination in the current account before navigating or performing work.
 * @param {HistoricalEntryOptions} options
 * @returns {Promise<HistoricalEntry[]>}
 */
export async function getHistoricalEntries (options) { return request('entries', options) }

/**
 * Acknowledge an owned routing entry after application validation. This does
 * not acknowledge a source alarm, remote operation or notification delivery.
 * @param {HistoricalEntryId} options
 * @returns {Promise<HistoricalEntryAcknowledgement>}
 */
export async function acknowledgeHistoricalEntry (options) { return request('acknowledgeEntry', options) }

/**
 * @param {HistoricalComponentSource} options
 * @returns {Promise<HistoricalSourceStatus>}
 */
export async function inspectHandover (options) { return request('inspectHandover', options) }

/**
 * Prepare an exact source/replacement binding after native consent. Pass an
 * empty expectedGeneration only for an absent mapping. Bundled identity fields
 * must match the already-authorized workflow binding; no server authority is inferred.
 * @param {HandoverOptions} options
 * @param {MigrationRequestOptions} [settings]
 * @returns {Promise<HandoverStatus>}
 */
export async function prepareHandover (options, settings = {}) { return request('prepareHandover', options, settings) }

/**
 * Rediscover exact owned generations after an interrupted reply, including Stop.
 * An existing historical reference cannot be rebound to another account/authority.
 * @param {HandoverIdentity} options
 * @returns {Promise<DiscoveredHandover[]>}
 */
export async function discoverHandovers (options) { return request('discoverHandovers', options) }

/**
 * Activate only the prepared generation after rechecking source and replacement.
 * The original component names, job IDs/schedules and encrypted receipts remain.
 * This neither unregisters old endpoints nor claims remote migration completion.
 * @param {HandoverId} options
 * @returns {Promise<HandoverStatus>}
 */
export async function activateHandover (options) { return request('activateHandover', options) }

/**
 * @param {HandoverId} options
 * @returns {Promise<HandoverStatus>}
 */
export async function getHandoverStatus (options) { return request('statusHandover', options) }

/**
 * Fence this generation and cancel its admitted reads. Poll drained before
 * treating owned cleanup as complete. Already-submitted effects stay uncertain.
 * @param {HandoverId} options
 * @returns {Promise<HandoverStatus>}
 */
export async function stopHandover (options) { return request('stopHandover', options) }

/**
 * @param {HandoverId} options
 * @returns {Promise<RetainedReceipt[]>}
 */
export async function getRetainedReceipts (options) { return request('receipts', options) }

/**
 * Record an application-verified result without claiming runtime verification of
 * its server. Repeated identical resolutions are idempotent; changed ones conflict.
 * @param {ReceiptResolution} options
 * @returns {Promise<ReceiptResolutionResult>}
 */
export async function resolveRetainedReceipt (options) { return request('resolveReceipt', options) }

/**
 * Release only an explicitly resolved receipt. Unknown receipts never expire or
 * disappear automatically; at capacity new admission returns ENOSPC.
 * @param {RetainedReceiptId} options
 * @returns {Promise<ReceiptReleaseResult>}
 */
export async function releaseRetainedReceipt (options) { return request('releaseReceipt', options) }

export default { getSupport, inspectProtected, importProtected, openProtected, discoverProtected, readUnprotected, getHistoricalEntries, acknowledgeHistoricalEntry, inspectHandover, prepareHandover, discoverHandovers, activateHandover, getHandoverStatus, stopHandover, getRetainedReceipts, resolveRetainedReceipt, releaseRetainedReceipt }
