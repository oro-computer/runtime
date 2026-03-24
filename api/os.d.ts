/**
 * Returns the operating system CPU architecture for which Socket was compiled.
 * @returns {string} - 'arm64', 'ia32', 'x64', or 'unknown'
 */
export function arch(): string;
/**
 * Returns an array of objects containing information about each CPU/core.
 * @returns {Array<object>} cpus - An array of objects containing information about each CPU/core.
 * The properties of the objects are:
 * - model `<string>` - CPU model name.
 * - speed `<number>` - CPU clock speed (in MHz).
 * - times `<object>` - An object containing the fields user, nice, sys, idle, irq representing the number of milliseconds the CPU has spent in each mode.
 *   - user `<number>` - Time spent by this CPU or core in user mode.
 *   - nice `<number>` - Time spent by this CPU or core in user mode with low priority (nice).
 *   - sys `<number>` - Time spent by this CPU or core in system mode.
 *   - idle `<number>` - Time spent by this CPU or core in idle mode.
 *   - irq `<number>` - Time spent by this CPU or core in IRQ mode.
 * @see {@link https://nodejs.org/api/os.html#os_os_cpus}
 */
export function cpus(): Array<object>;
/**
 * Returns an object containing network interfaces that have been assigned a network address.
 * @returns {object}  - An object containing network interfaces that have been assigned a network address.
 * Each key on the returned object identifies a network interface. The associated value is an array of objects that each describe an assigned network address.
 * The properties available on the assigned network address object include:
 * - address `<string>` - The assigned IPv4 or IPv6 address.
 * - netmask `<string>` - The IPv4 or IPv6 network mask.
 * - family `<string>` - The address family ('IPv4' or 'IPv6').
 * - mac `<string>` - The MAC address of the network interface.
 * - internal `<boolean>` - Indicates whether the network interface is a loopback interface.
 * - scopeid `<number>` - The numeric scope ID (only specified when family is 'IPv6').
 * - cidr `<string>` - The CIDR notation of the interface.
 * @see {@link https://nodejs.org/api/os.html#os_os_networkinterfaces}
 */
export function networkInterfaces(): object;
/**
 * Returns the operating system platform.
 * @returns {string} - 'android', 'cygwin', 'freebsd', 'linux', 'darwin', 'ios', 'openbsd', 'win32', or 'unknown'
 * @see {@link https://nodejs.org/api/os.html#os_os_platform}
 * The returned value is equivalent to `process.platform`.
 */
export function platform(): string;
/**
 * Returns the operating system name.
 * @returns {string} - 'CYGWIN_NT', 'Mac', 'Darwin', 'FreeBSD', 'Linux', 'OpenBSD', 'Windows_NT', 'Win32', or 'Unknown'
 * @see {@link https://nodejs.org/api/os.html#os_os_type}
 */
export function type(): string;
/**
 * @returns {boolean} - `true` if the operating system is Windows.
 */
export function isWindows(): boolean;
/**
 * @returns {string} - The operating system's default directory for temporary files.
 */
export function tmpdir(): string;
/**
 * Get resource usage.
 */
export function rusage(): any;
/**
 * Returns the system uptime in seconds.
 * @returns {number} - The system uptime in seconds.
 */
export function uptime(): number;
/**
 * Returns the operating system name.
 * @returns {string} - The operating system name.
 */
export function uname(): string;
/**
 * It's implemented in process.hrtime.bigint()
 * @ignore
 */
export function hrtime(): any;
/**
 * Node.js doesn't have this method.
 * @ignore
 */
export function availableMemory(): any;
/**
 * The host operating system. This value can be one of:
 * - android
 * - android-emulator
 * - iphoneos
 * - iphone-simulator
 * - linux
 * - macosx
 * - unix
 * - unknown
 * - win32
 * @ignore
 * @return {'android'|'android-emulator'|'iphoneos'|iphone-simulator'|'linux'|'macosx'|unix'|unknown'|win32'}
 */
export function host(): "android" | "android-emulator" | "iphoneos" | iphone;
/**
 * Returns the home directory of the current user.
 * @return {string}
 */
export function homedir(): string;
export { constants };
/**
 * @type {string}
 * The operating system's end-of-line marker. `'\r\n'` on Windows and `'\n'` on POSIX.
 */
export const EOL: string;
export default exports;
import constants from './os/constants.js';
import * as exports from './os.js';
