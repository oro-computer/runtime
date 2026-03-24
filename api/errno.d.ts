/**
 * Converts an `errno` code to its corresponding string message.
 * @param {import('./os/constants.js').errno} {code}
 * @return {string}
 */
export function toString(code: any): string;
/**
 * Gets the code for a given 'errno' name.
 * @param {string|number} name
 * @return {errno}
 */
export function getCode(name: string | number): errno;
/**
 * Gets the name for a given 'errno' code
 * @return {string}
 * @param {string|number} code
 */
export function getName(code: string | number): string;
/**
 * Gets the message for a 'errno' code.
 * @param {number|string} code
 * @return {string}
 */
export function getMessage(code: number | string): string;
/**
 * @typedef {import('./os/constants.js').errno} errno
 */
export const E2BIG: any;
export const EACCES: any;
export const EADDRINUSE: any;
export const EADDRNOTAVAIL: any;
export const EAFNOSUPPORT: any;
export const EAGAIN: any;
export const EALREADY: any;
export const EBADF: any;
export const EBADMSG: any;
export const EBUSY: any;
export const ECANCELED: any;
export const ECHILD: any;
export const ECONNABORTED: any;
export const ECONNREFUSED: any;
export const ECONNRESET: any;
export const EDEADLK: any;
export const EDESTADDRREQ: any;
export const EDOM: any;
export const EDQUOT: any;
export const EEXIST: any;
export const EFAULT: any;
export const EFBIG: any;
export const EHOSTUNREACH: any;
export const EIDRM: any;
export const EILSEQ: any;
export const EINPROGRESS: any;
export const EINTR: any;
export const EINVAL: any;
export const EIO: any;
export const EISCONN: any;
export const EISDIR: any;
export const ELOOP: any;
export const EMFILE: any;
export const EMLINK: any;
export const EMSGSIZE: any;
export const EMULTIHOP: any;
export const ENAMETOOLONG: any;
export const ENETDOWN: any;
export const ENETRESET: any;
export const ENETUNREACH: any;
export const ENFILE: any;
export const ENOBUFS: any;
export const ENODATA: any;
export const ENODEV: any;
export const ENOENT: any;
export const ENOEXEC: any;
export const ENOLCK: any;
export const ENOLINK: any;
export const ENOMEM: any;
export const ENOMSG: any;
export const ENOPROTOOPT: any;
export const ENOSPC: any;
export const ENOSR: any;
export const ENOSTR: any;
export const ENOSYS: any;
export const ENOTCONN: any;
export const ENOTDIR: any;
export const ENOTEMPTY: any;
export const ENOTSOCK: any;
export const ENOTSUP: any;
export const ENOTTY: any;
export const ENXIO: any;
export const EOPNOTSUPP: any;
export const EOVERFLOW: any;
export const EPERM: any;
export const EPIPE: any;
export const EPROTO: any;
export const EPROTONOSUPPORT: any;
export const EPROTOTYPE: any;
export const ERANGE: any;
export const EROFS: any;
export const ESPIPE: any;
export const ESRCH: any;
export const ESTALE: any;
export const ETIME: any;
export const ETIMEDOUT: any;
export const ETXTBSY: any;
export const EWOULDBLOCK: any;
export const EXDEV: any;
export const strings: any;
export { constants };
declare namespace _default {
    export { constants };
    export { strings };
    export { toString };
    export { getCode };
    export { getMessage };
}
export default _default;
export type errno = import("./os/constants.js").errno;
import { errno as constants } from './os/constants.js';
