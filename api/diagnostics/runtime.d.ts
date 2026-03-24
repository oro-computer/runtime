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
    export { query };
}
export default _default;
