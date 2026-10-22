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
 * @property {boolean} push False until a native provider and authenticated backend are configured.
 * @property {boolean} biometricSecretUnlock
 * @property {boolean} namedLauncherDestinations
 * @property {boolean} nativeDeferredReads Configured native read/reconciliation scheduler; query registered executors separately.
 * @property {boolean} deferredWrites False; queued mutation replay is not supported.
 * @property {boolean} webAuthentication Explicit WebAuthn app integration and WebView feature support. This does not establish relying-party/domain association or enrollment.
 * @property {boolean} nativePasswordSelection Explicit Android 14+ framework password chooser; separate from Autofill and passkeys.
 * @property {boolean} nativeNotificationPresentation Channel/group/public-version submission with shared response receipts.
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
export function getCapabilities(): Promise<RuntimeCapabilities>;
/**
 * Reads Android build identity without URLs, account data, input values, or logs.
 * Other platforms reject with `ENOSYS`. This is not proof of release signing.
 * @returns {Promise<AndroidBuildIdentity>}
 */
export function getBuildIdentity(): Promise<AndroidBuildIdentity>;
/**
 * Queries runtime diagnostics.
 * @return {Promise<QueryDiagnostic>}
 */
export function query(type: any): Promise<QueryDiagnostic>;
/**
 * A base container class for diagnostic information.
 */
export class Diagnostic {
    /**
     * A container for handles related to the diagnostics
     */
    static Handles: {
        new (): {
            /**
             * The nunmber of handles in this diagnostics.
             * @type {number}
             */
            count: number;
            /**
             * A set of known handle IDs
             * @type {string[]}
             */
            ids: string[];
        };
    };
    /**
     * Known handles for this diagnostics.
     * @type {Diagnostic.Handles}
     */
    handles: {
        new (): {
            /**
             * The nunmber of handles in this diagnostics.
             * @type {number}
             */
            count: number;
            /**
             * A set of known handle IDs
             * @type {string[]}
             */
            ids: string[];
        };
    };
}
/**
 * A container for libuv diagnostics
 */
export class UVDiagnostic extends Diagnostic {
    /**
     * A container for libuv metrics.
     */
    static Metrics: {
        new (): {
            /**
             * The number of event loop iterations.
             * @type {number}
             */
            loopCount: number;
            /**
             * Number of events that have been processed by the event handler.
             * @type {number}
             */
            events: number;
            /**
             * Number of events that were waiting to be processed when the
             * event provider was called.
             * @type {number}
             */
            eventsWaiting: number;
        };
    };
    /**
     * Known libuv metrics for this diagnostic.
     * @type {UVDiagnostic.Metrics}
     */
    metrics: {
        new (): {
            /**
             * The number of event loop iterations.
             * @type {number}
             */
            loopCount: number;
            /**
             * Number of events that have been processed by the event handler.
             * @type {number}
             */
            events: number;
            /**
             * Number of events that were waiting to be processed when the
             * event provider was called.
             * @type {number}
             */
            eventsWaiting: number;
        };
    };
    /**
     * The current idle time of the libuv loop
     * @type {number}
     */
    idleTime: number;
    /**
     * The number of active requests in the libuv loop
     * @type {number}
     */
    activeRequests: number;
}
/**
 * A container for Core Post diagnostics.
 */
export class PostsDiagnostic extends Diagnostic {
}
/**
 * A container for child process diagnostics.
 */
export class ChildProcessDiagnostic extends Diagnostic {
}
/**
 * A container for AI diagnostics.
 */
export class AIDiagnostic extends Diagnostic {
    /**
     * A container for AI LLM diagnostics.
     */
    static LLMDiagnostic: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * Known AI LLM diagnostics.
     * @type {AIDiagnostic.LLMDiagnostic}
     */
    llm: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
}
/**
 * A container for various filesystem diagnostics.
 */
export class FSDiagnostic extends Diagnostic {
    /**
     * A container for filesystem watcher diagnostics.
     */
    static WatchersDiagnostic: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * A container for filesystem descriptors diagnostics.
     */
    static DescriptorsDiagnostic: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * Known FS watcher diagnostics.
     * @type {FSDiagnostic.WatchersDiagnostic}
     */
    watchers: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * @type {FSDiagnostic.DescriptorsDiagnostic}
     */
    descriptors: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
}
/**
 * A container for various timers diagnostics.
 */
export class TimersDiagnostic extends Diagnostic {
    /**
     * A container for core timeout timer diagnostics.
     */
    static TimeoutDiagnostic: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * A container for core interval timer diagnostics.
     */
    static IntervalDiagnostic: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * A container for core immediate timer diagnostics.
     */
    static ImmediateDiagnostic: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * @type {TimersDiagnostic.TimeoutDiagnostic}
     */
    timeout: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * @type {TimersDiagnostic.IntervalDiagnostic}
     */
    interval: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
    /**
     * @type {TimersDiagnostic.ImmediateDiagnostic}
     */
    immediate: {
        new (): {
            /**
             * Known handles for this diagnostics.
             * @type {Diagnostic.Handles}
             */
            handles: {
                new (): {
                    /**
                     * The nunmber of handles in this diagnostics.
                     * @type {number}
                     */
                    count: number;
                    /**
                     * A set of known handle IDs
                     * @type {string[]}
                     */
                    ids: string[];
                };
            };
        };
        /**
         * A container for handles related to the diagnostics
         */
        Handles: {
            new (): {
                /**
                 * The nunmber of handles in this diagnostics.
                 * @type {number}
                 */
                count: number;
                /**
                 * A set of known handle IDs
                 * @type {string[]}
                 */
                ids: string[];
            };
        };
    };
}
/**
 * A container for UDP diagnostics.
 */
export class UDPDiagnostic extends Diagnostic {
}
/**
 * A container for various queried runtime diagnostics.
 */
export class QueryDiagnostic {
    posts: PostsDiagnostic;
    childProcess: ChildProcessDiagnostic;
    ai: AIDiagnostic;
    fs: FSDiagnostic;
    timers: TimersDiagnostic;
    udp: UDPDiagnostic;
    uv: UVDiagnostic;
}
declare namespace _default {
    export { getCapabilities };
    export { getBuildIdentity };
    export { query };
}
export default _default;
export type AndroidStartupTiming = {
    /**
     * Fixed native startup phase.
     */
    phase: "native-library" | "native-allocation" | "native-extensions" | "process-lifecycle";
    /**
     * Monotonic duration; excludes process launch and renderer startup.
     */
    durationMs: number;
};
export type AndroidLifecycleEvent = {
    /**
     * Runtime window index.
     */
    index: number;
    phase: "loading" | "ready" | "failed" | "renderer-lost" | "reload";
    /**
     * Monotonic time since the first retained process event.
     */
    elapsedMs: number;
    /**
     * Startup elapsed time for ready/failure; zero otherwise.
     */
    durationMs: number;
};
export type AndroidBuildIdentity = {
    /**
     * Version compiled into the native runtime.
     */
    nativeVersion: string;
    /**
     * Source revision compiled into the native runtime.
     */
    nativeRevision: string;
    /**
     * CLI that generated this application's init shim.
     */
    cliVersion: string;
    /**
     * SHA-256 of asset and build evidence bound into the native init shim; not the APK digest.
     */
    assetManifestSha256: string;
    /**
     * Staged Android native files, normalized settings, SDK inventory and toolchain input fingerprint; null for older or unbound evidence. APK digest and signing certificate identify the exact compiled artifact.
     */
    applicationInputSha256: string | null;
    /**
     * Native bridge contract version.
     */
    androidBridgeProtocol: number;
    /**
     * Compiled architecture.
     */
    abi: string;
    debug: boolean;
    /**
     * Device SDK.
     */
    sdk: number;
    /**
     * Application target SDK.
     */
    targetSdk: number;
    /**
     * Installed WebView package version.
     */
    webViewVersion: string | null;
    /**
     * Whether native startup verified all packaged assets.
     */
    assetsVerified: boolean;
    /**
     * Redacted, OS-retained exit summaries.
     */
    processExits: AndroidProcessExits;
    /**
     * At most 64 process-local timings; no URLs or application payloads.
     */
    lifecycleEvents: AndroidLifecycleEvent[];
    /**
     * At most four native startup phases; no application payloads.
     */
    startupTimings: AndroidStartupTiming[];
};
export type AndroidProcessExit = {
    /**
     * False; OS history may predate the running artifact.
     */
    buildIdentityKnown: boolean;
    /**
     * Android ApplicationExitInfo reason code.
     */
    reason: number;
    /**
     * Exit status code.
     */
    status: number;
    /**
     * Unix milliseconds.
     */
    timestamp: number;
    /**
     * Android process importance.
     */
    importance: number;
    /**
     * Last recorded proportional memory size.
     */
    pssKiB: number;
    /**
     * Last recorded resident memory size.
     */
    rssKiB: number;
};
export type AndroidProcessExits = {
    /**
     * API 30+ system query succeeded.
     */
    available: boolean;
    /**
     * At most 16 summaries; no descriptions, traces, or process names.
     */
    entries: AndroidProcessExit[];
};
export type RuntimeCapabilities = {
    /**
     * Requires Android 11+ and a live foreground window.
     */
    nativeFullscreen: boolean;
    fullscreenRequiresManagedInsets: boolean;
    /**
     * Android FLAG_SECURE, not control of exported content.
     */
    screenProtection: boolean;
    documentMetadata: boolean;
    /**
     * Existing user-selected tree grants only.
     */
    documentTreeQueries: boolean;
    documentQueryWorkers: number;
    documentQueryDeadlineMs: number;
    /**
     * Explicitly enabled native share-prune scheduling.
     */
    nativeBackgroundCleanup: boolean;
    /**
     * False; arbitrary durable JS execution is unavailable.
     */
    backgroundJavaScript: boolean;
    /**
     * False until a native provider and authenticated backend are configured.
     */
    push: boolean;
    biometricSecretUnlock: boolean;
    namedLauncherDestinations: boolean;
    /**
     * Configured native read/reconciliation scheduler; query registered executors separately.
     */
    nativeDeferredReads: boolean;
    /**
     * False; queued mutation replay is not supported.
     */
    deferredWrites: boolean;
    /**
     * Explicit WebAuthn app integration and WebView feature support. This does not establish relying-party/domain association or enrollment.
     */
    webAuthentication: boolean;
    /**
     * Explicit Android 14+ framework password chooser; separate from Autofill and passkeys.
     */
    nativePasswordSelection: boolean;
    /**
     * Channel/group/public-version submission with shared response receipts.
     */
    nativeNotificationPresentation: boolean;
    /**
     * Configured compiled native extension lifecycle and document-owned operations.
     */
    nativeExtensions: boolean;
    pointerCapture: boolean;
    historicalProcessExits: boolean;
};
