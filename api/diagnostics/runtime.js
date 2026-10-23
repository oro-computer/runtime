import ipc from '../ipc.js'

/**
 * @typedef {object} AndroidStartupTiming
 * @property {'native-library'|'native-allocation'|'native-extensions'|'process-lifecycle'} phase Fixed native startup phase.
 * @property {number} durationMs Monotonic duration; excludes process launch and renderer startup.
 */

/**
 * @typedef {object} AndroidLifecycleEvent
 * @property {number} index Runtime window index.
 * @property {'loading'|'ready'|'failed'|'renderer-lost'|'reload'} phase
 * @property {number} elapsedMs Monotonic time since the first retained process event.
 * @property {number} durationMs Startup elapsed time for ready/failure; zero otherwise.
 */

/**
 * @typedef {object} AndroidBuildIdentity
 * @property {string} nativeVersion Version compiled into the native runtime.
 * @property {string} nativeRevision Source revision compiled into the native runtime.
 * @property {string} cliVersion CLI that generated this application's init shim.
 * @property {string} assetManifestSha256 SHA-256 of asset and build evidence bound into the native init shim; not the APK digest.
 * @property {string|null} applicationInputSha256 Staged Android native files, normalized settings, SDK inventory and toolchain input fingerprint; null for older or unbound evidence. APK digest and signing certificate identify the exact compiled artifact.
 * @property {number} androidBridgeProtocol Native bridge contract version.
 * @property {string} abi Compiled architecture.
 * @property {boolean} debug
 * @property {number} sdk Device SDK.
 * @property {number} targetSdk Application target SDK.
 * @property {string|null} webViewVersion Installed WebView package version.
 * @property {boolean} assetsVerified Whether native startup verified all packaged assets.
 * @property {AndroidProcessExits} processExits Redacted, OS-retained exit summaries.
 * @property {AndroidLifecycleEvent[]} lifecycleEvents At most 64 process-local timings; no URLs or application payloads.
 * @property {AndroidStartupTiming[]} startupTimings At most four native startup phases; no application payloads.
 */

/**
 * @typedef {object} AndroidProcessExit
 * @property {boolean} buildIdentityKnown False; OS history may predate the running artifact.
 * @property {number} reason Android ApplicationExitInfo reason code.
 * @property {number} status Exit status code.
 * @property {number} timestamp Unix milliseconds.
 * @property {number} importance Android process importance.
 * @property {number} pssKiB Last recorded proportional memory size.
 * @property {number} rssKiB Last recorded resident memory size.
 */

/**
 * @typedef {object} AndroidProcessExits
 * @property {boolean} available API 30+ system query succeeded.
 * @property {AndroidProcessExit[]} entries At most 16 summaries; no descriptions, traces, or process names.
 */

/**
 * @typedef {object} RuntimeCapabilities
 * @property {boolean} nativeFullscreen Requires Android 11+ and a live foreground window.
 * @property {boolean} fullscreenRequiresManagedInsets
 * @property {boolean} screenProtection Android FLAG_SECURE, not control of exported content.
 * @property {boolean} documentMetadata
 * @property {boolean} documentTreeQueries Existing user-selected tree grants only.
 * @property {number} documentQueryWorkers
 * @property {number} documentQueryDeadlineMs
 * @property {boolean} nativeBackgroundCleanup Explicitly enabled native share-prune scheduling.
 * @property {boolean} backgroundJavaScript False; arbitrary durable JS execution is unavailable.
 * @property {boolean} push Configured optional UnifiedPush transport. Query oro:push for selection and registered handlers; this does not prove server enrollment or delivery.
 * @property {boolean} applicationBackHandling Document-owned Back handler with native fallback.
 * @property {boolean} periodicDeferredReads Configured periodic native read scheduling.
 * @property {boolean} nativeDeferredMaintenance Registered retry-safe cleanup/revocation scheduling; general queued writes remain unavailable.
 * @property {boolean} biometricSecretUnlock
 * @property {boolean} namedLauncherDestinations
 * @property {boolean} nativeDeferredReads Configured native read/reconciliation scheduler; query registered executors separately.
 * @property {boolean} deferredWrites False; queued mutation replay is not supported.
 * @property {boolean} webAuthentication Explicit WebAuthn app integration and WebView feature support. This does not establish relying-party/domain association or enrollment.
 * @property {boolean} nativePasswordSelection Explicit Android 14+ framework password chooser; separate from Autofill and passkeys.
 * @property {boolean} nativeNotificationPresentation Channel/group/public-version submission with shared response receipts.
 * @property {boolean} systemTextScaling Android interactive WebViews apply absolute system text zoom before navigation and on configuration changes; webview.text_zoom can select fixed or manual ownership. Linear WebView zoom is not Android nonlinear native text scaling.
 * @property {boolean} nativeExtensions Configured compiled native extension lifecycle and document-owned operations.
 * @property {boolean} pointerCapture
 * @property {boolean} historicalProcessExits
 */

/**
 * Reports implemented Android native contracts and explicit unavailable features.
 * A supported contract still needs permission, active ownership, and application
 * authorization. Other platforms reject with ENOSYS.
 * @returns {Promise<RuntimeCapabilities>}
 */
export async function getCapabilities () {
  const { data, err } = await ipc.request('diagnostics.capabilities')
  if (err) throw err
  return data
}

/**
 * Reads Android build identity without URLs, account data, input values, or logs.
 * Other platforms reject with `ENOSYS`. This is not proof of release signing.
 * @returns {Promise<AndroidBuildIdentity>}
 */
export async function getBuildIdentity () {
  const { data, err } = await ipc.request('diagnostics.buildIdentity')
  if (err) throw err
  return data
}

/**
 * A base container class for diagnostic information.
 */
export class Diagnostic {
  /**
   * A container for handles related to the diagnostics
   */
  static Handles = class Handles {
    /**
     * The nunmber of handles in this diagnostics.
     * @type {number}
     */
    count = 0

    /**
     * A set of known handle IDs
     * @type {string[]}
     */
    ids = []

    /**
     * `Diagnostic.Handles` class constructor.
     * @private
     */
    constructor () {
      Object.seal(this)
    }
  }

  /**
   * Known handles for this diagnostics.
   * @type {Diagnostic.Handles}
   */
  handles = new Diagnostic.Handles()
}

/**
 * A container for libuv diagnostics
 */
export class UVDiagnostic extends Diagnostic {
  /**
   * A container for libuv metrics.
   */
  static Metrics = class Metrics {
    /**
     * The number of event loop iterations.
     * @type {number}
     */
    loopCount = 0

    /**
     * Number of events that have been processed by the event handler.
     * @type {number}
     */
    events = 0

    /**
     * Number of events that were waiting to be processed when the
     * event provider was called.
     * @type {number}
     */
    eventsWaiting = 0
  }

  /**
   * Known libuv metrics for this diagnostic.
   * @type {UVDiagnostic.Metrics}
   */
  metrics = new UVDiagnostic.Metrics()

  /**
   * The current idle time of the libuv loop
   * @type {number}
   */
  idleTime = 0

  /**
   * The number of active requests in the libuv loop
   * @type {number}
   */
  activeRequests = 0
}

/**
 * A container for Core Post diagnostics.
 */
export class PostsDiagnostic extends Diagnostic {}

/**
 * A container for child process diagnostics.
 */
export class ChildProcessDiagnostic extends Diagnostic {}

/**
 * A container for AI diagnostics.
 */
export class AIDiagnostic extends Diagnostic {
  /**
   * A container for AI LLM diagnostics.
   */
  static LLMDiagnostic = class LLMDiagnostic extends Diagnostic {}

  /**
   * Known AI LLM diagnostics.
   * @type {AIDiagnostic.LLMDiagnostic}
   */
  llm = new AIDiagnostic.LLMDiagnostic()
}

/**
 * A container for various filesystem diagnostics.
 */
export class FSDiagnostic extends Diagnostic {
  /**
   * A container for filesystem watcher diagnostics.
   */
  static WatchersDiagnostic = class WatchersDiagnostic extends Diagnostic {}

  /**
   * A container for filesystem descriptors diagnostics.
   */
  static DescriptorsDiagnostic = class DescriptorsDiagnostic extends Diagnostic {}

  /**
   * Known FS watcher diagnostics.
   * @type {FSDiagnostic.WatchersDiagnostic}
   */
  watchers = new FSDiagnostic.WatchersDiagnostic()

  /**
   * @type {FSDiagnostic.DescriptorsDiagnostic}
   */
  descriptors = new FSDiagnostic.DescriptorsDiagnostic()
}

/**
 * A container for various timers diagnostics.
 */
export class TimersDiagnostic extends Diagnostic {
  /**
   * A container for core timeout timer diagnostics.
   */
  static TimeoutDiagnostic = class TimeoutDiagnostic extends Diagnostic {}

  /**
   * A container for core interval timer diagnostics.
   */
  static IntervalDiagnostic = class IntervalDiagnostic extends Diagnostic {}

  /**
   * A container for core immediate timer diagnostics.
   */
  static ImmediateDiagnostic = class ImmediateDiagnostic extends Diagnostic {}

  /**
   * @type {TimersDiagnostic.TimeoutDiagnostic}
   */
  timeout = new TimersDiagnostic.TimeoutDiagnostic()

  /**
   * @type {TimersDiagnostic.IntervalDiagnostic}
   */
  interval = new TimersDiagnostic.IntervalDiagnostic()

  /**
   * @type {TimersDiagnostic.ImmediateDiagnostic}
   */
  immediate = new TimersDiagnostic.ImmediateDiagnostic()
}

/**
 * A container for UDP diagnostics.
 */
export class UDPDiagnostic extends Diagnostic {}

/**
 * A container for various queried runtime diagnostics.
 */
export class QueryDiagnostic {
  posts = new PostsDiagnostic()
  childProcess = new ChildProcessDiagnostic()
  ai = new AIDiagnostic()
  fs = new FSDiagnostic()
  timers = new TimersDiagnostic()
  udp = new UDPDiagnostic()
  uv = new UVDiagnostic()
}

/**
 * Queries runtime diagnostics.
 * @return {Promise<QueryDiagnostic>}
 */
export async function query (type) {
  const result = await ipc.request('diagnostics.query')

  if (result.err) {
    throw result.err
  }

  const query = Object.assign(new QueryDiagnostic(), result.data)

  if (typeof globalThis.__global_ipc_extension_handler === 'function') {
    const result = await ipc.request(
      'diagnostics.query',
      {},
      {
        useExtensionIPCIfAvailable: true
      }
    )

    if (result.data) {
      extend(query, Object.assign(new QueryDiagnostic(), result.data))
    }

    function extend (left, right) {
      for (const key in right) {
        if (Array.isArray(left[key]) && Array.isArray(right[key])) {
          left[key].push(...right[key])
        } else if (left[key] && typeof left[key] === 'object') {
          if (right[key] && typeof right[key] === 'object') {
            extend(left[key], right[key])
          }
        } else if (
          typeof left[key] === 'number' &&
          typeof right[key] === 'number'
        ) {
          left[key] += right[key]
        } else {
          left[key] = right[key]
        }
      }
    }
  }

  if (typeof type === 'string') {
    return type
      .trim()
      .split(/\.|\[|\]/g)
      .map((key) => key.trim())
      .filter((key) => key.length > 0)
      .reduce((q, k) => (q ? q[k] : null), query)
  }

  return query
}

export default {
  getCapabilities,
  getBuildIdentity,
  query
}
