import ipc from '../ipc.js'
import { randomBytes } from '../crypto.js'
import { AbortError } from '../errors.js'

// Only support probes can resolve to an explicit unavailable result. Status
// reads for a specific operation, account or registration never use this path.
function supportFeature (feature, action) {
  if (feature === 'foreground' && action === 'continuous.support') return 'foreground.continuous'
  if (['authentication', 'credentials', 'launcher'].includes(feature) && action === 'status') return feature
  if (['foreground', 'deferred', 'push', 'workflows'].includes(feature) && action === 'support') return feature
  return null
}

function unsupportedStatus (feature, descriptor, options) {
  const metadata = {
    platform: descriptor.platform,
    implementation: descriptor.implementation,
    reason: descriptor.reason,
    backend: descriptor.backend,
    supported: false,
    available: false,
    enabled: false,
    busy: false
  }
  switch (feature) {
    case 'workflows': return {
      ...metadata,
      workflows: [],
      nativeIntegrationRequired: false,
      backgroundJavaScript: false,
      maximumBindings: 0,
      maximumRetainedEventsPerBinding: 0
    }
    case 'authentication': return {
      ...metadata,
      canAuthenticate: false,
      statusCode: -1,
      policy: options.policy ?? 'biometric',
      cryptographicOperationsOnly: false,
      serverVerificationRequired: true
    }
    case 'credentials': return metadata
    case 'launcher': return {
      ...metadata, pinSupported: false, maximumPerActivity: 0, rateLimited: false, authorizationBoundary: 'resolve-after-authentication'
    }
    case 'push': return {
      ...metadata,
      provider: null,
      selectedDistributor: '',
      registeredKinds: [],
      maximumRegistrations: 0,
      maximumPendingMessages: 0,
      maximumMessageBytes: 0,
      backgroundJavaScript: false,
      survivesForceStop: false
    }
    case 'deferred': return {
      ...metadata,
      registeredKinds: [],
      registeredMaintenanceKinds: [],
      maintenanceOperations: {},
      maximumTasks: 0,
      maximumAttempts: 0,
      readOnly: true,
      queuedWrites: false,
      backgroundJavaScript: false,
      survivesForceStop: false,
      periodicReads: false,
      minimumIntervalMs: 0,
      minimumFlexMs: 0
    }
    default: return {
      ...metadata,
      canStart: false,
      registeredKinds: [],
      maximumDurationMs: feature === 'foreground.continuous' ? null : 0,
      maximumTasks: 0,
      maximumConcurrentExecutors: 0,
      automaticRestart: false,
      backgroundJavaScript: false
    }
  }
}

/**
 * Runs a bounded document-owned native operation and cancels its native work
 * when the caller aborts. Native deadlines also release callers that disappear.
 * @param {string} feature
 * @param {string} action
 * @param {object} options
 * @param {AbortSignal} [signal]
 * @returns {Promise<any>}
 */
export async function platformRequest (feature, action, options, signal) {
  if (signal?.aborted) throw new AbortError()
  const id = randomBytes(16).toString('hex')
  const documentToken = Reflect.get(globalThis, '__oroNativeDocumentToken') ?? ''
  const abort = () => {
    ipc.request('runtime.platform', {
      feature: 'cancel', action: 'cancel', id: randomBytes(16).toString('hex'), documentToken, options: JSON.stringify({ id })
    }).catch(() => {})
  }
  signal?.addEventListener('abort', abort, { once: true })
  try {
    const { data, err } = await ipc.request('runtime.platform', {
      feature, action, id, documentToken, options: JSON.stringify(options)
    }, { signal, timeout: 95000 })
    if (signal?.aborted) throw new AbortError()
    if (err) throw err
    return data
  } catch (error) {
    const queriedFeature = supportFeature(feature, action)
    if (!queriedFeature || error?.code !== 'ENOSYS') throw error
    const response = await ipc.request('runtime.platform.support', { feature: queriedFeature }, { signal, timeout: 5000 })
    if (signal?.aborted) throw new AbortError()
    if (response.err) throw response.err
    const descriptor = response.data
    if (descriptor?.schema !== 1 || descriptor.feature !== queriedFeature ||
        !['missing', 'restricted'].includes(descriptor.implementation)) throw error
    return unsupportedStatus(queriedFeature, descriptor, options)
  } finally {
    signal?.removeEventListener('abort', abort)
  }
}

export default platformRequest
