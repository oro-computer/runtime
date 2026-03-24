/**
 * Converts an `signal` code to its corresponding string message.
 * @param {import('./os/constants.js').signal} {code}
 * @return {string}
 */
export function toString(code: any): string;
/**
 * Gets the code for a given 'signal' name.
 * @param {string|number} name
 * @return {signal}
 */
export function getCode(name: string | number): signal;
/**
 * Gets the name for a given 'signal' code
 * @return {string}
 * @param {string|number} code
 */
export function getName(code: string | number): string;
/**
 * Gets the message for a 'signal' code.
 * @param {number|string} code
 * @return {string}
 */
export function getMessage(code: number | string): string;
/**
 * Add a signal event listener.
 * @param {string|number} signal
 * @param {function(SignalEvent)} callback
 * @param {{ once?: boolean }=} [options]
 */
export function addEventListener(signalName: any, callback: (arg0: SignalEvent) => any, options?: {
    once?: boolean;
} | undefined): void;
/**
 * Remove a signal event listener.
 * @param {string|number} signal
 * @param {function(SignalEvent)} callback
 * @param {{ once?: boolean }=} [options]
 */
export function removeEventListener(signalName: any, callback: (arg0: SignalEvent) => any, options?: {
    once?: boolean;
} | undefined): void;
export { constants };
export const channel: BroadcastChannel;
export const SIGHUP: any;
export const SIGINT: any;
export const SIGQUIT: any;
export const SIGILL: any;
export const SIGTRAP: any;
export const SIGABRT: any;
export const SIGIOT: any;
export const SIGBUS: any;
export const SIGFPE: any;
export const SIGKILL: any;
export const SIGUSR1: any;
export const SIGSEGV: any;
export const SIGUSR2: any;
export const SIGPIPE: any;
export const SIGALRM: any;
export const SIGTERM: any;
export const SIGCHLD: any;
export const SIGCONT: any;
export const SIGSTOP: any;
export const SIGTSTP: any;
export const SIGTTIN: any;
export const SIGTTOU: any;
export const SIGURG: any;
export const SIGXCPU: any;
export const SIGXFSZ: any;
export const SIGVTALRM: any;
export const SIGPROF: any;
export const SIGWINCH: any;
export const SIGIO: any;
export const SIGINFO: any;
export const SIGSYS: any;
export const strings: {
    [SIGHUP]: string;
    [SIGINT]: string;
    [SIGQUIT]: string;
    [SIGILL]: string;
    [SIGTRAP]: string;
    [SIGABRT]: string;
    [SIGIOT]: string;
    [SIGBUS]: string;
    [SIGFPE]: string;
    [SIGKILL]: string;
    [SIGUSR1]: string;
    [SIGSEGV]: string;
    [SIGUSR2]: string;
    [SIGPIPE]: string;
    [SIGALRM]: string;
    [SIGTERM]: string;
    [SIGCHLD]: string;
    [SIGCONT]: string;
    [SIGSTOP]: string;
    [SIGTSTP]: string;
    [SIGTTIN]: string;
    [SIGTTOU]: string;
    [SIGURG]: string;
    [SIGXCPU]: string;
    [SIGXFSZ]: string;
    [SIGVTALRM]: string;
    [SIGPROF]: string;
    [SIGWINCH]: string;
    [SIGIO]: string;
    [SIGINFO]: string;
    [SIGSYS]: string;
};
declare namespace _default {
    export { addEventListener };
    export { removeEventListener };
    export { constants };
    export { channel };
    export { strings };
    export { toString };
    export { getName };
    export { getCode };
    export { getMessage };
    export { SIGHUP };
    export { SIGINT };
    export { SIGQUIT };
    export { SIGILL };
    export { SIGTRAP };
    export { SIGABRT };
    export { SIGIOT };
    export { SIGBUS };
    export { SIGFPE };
    export { SIGKILL };
    export { SIGUSR1 };
    export { SIGSEGV };
    export { SIGUSR2 };
    export { SIGPIPE };
    export { SIGALRM };
    export { SIGTERM };
    export { SIGCHLD };
    export { SIGCONT };
    export { SIGSTOP };
    export { SIGTSTP };
    export { SIGTTIN };
    export { SIGTTOU };
    export { SIGURG };
    export { SIGXCPU };
    export { SIGXFSZ };
    export { SIGVTALRM };
    export { SIGPROF };
    export { SIGWINCH };
    export { SIGIO };
    export { SIGINFO };
    export { SIGSYS };
}
export default _default;
export type signal = any;
import { SignalEvent } from '../internal/events.js';
import { signal as constants } from '../os/constants.js';
