/**
 * @module dns
 *
 * This module enables name resolution. For example, use it to look up IP
 * addresses of host names. Although named for the Domain Name System (DNS),
 * it does not always use the DNS protocol for lookups. dns.lookup() uses the
 * operating system facilities to perform name resolution. It may not need to
 * perform any network communication. To perform name resolution the way other
 * applications on the same system do, use dns.lookup().
 *
 * Example usage:
 * ```js
 * import { lookup } from 'oro:dns'
 * ```
 */

import { isFunction } from '../util.js'
import * as promises from './promises.js'
import diagnostics from '../diagnostics.js'
import { rand64 } from '../crypto.js'
import ipc from '../ipc.js'
import { normalizeLookupOptions, createLookupError } from './utils.js'
import { ADDRCONFIG, ALL, V4MAPPED, constants } from './constants.js'

import * as exports from './index.js'

const dc = diagnostics.channels.group('dns', [
  'lookup.start',
  'lookup.end',
  'lookup'
])

/**
 * @typedef {object} LookupAddress
 * @property {string} address - Resolved IP address.
 * @property {4|6} family - Address family of the result.
 *
 * @typedef {object} LookupOptions
 * @property {0|4|6|'IPv4'|'IPv6'} [family=0] - Address family preference.
 * @property {number} [hints=0] - Optional getaddrinfo flags (combine dns.ADDRCONFIG, dns.V4MAPPED, dns.ALL).
 * @property {boolean} [all=false] - Return all resolved addresses if true.
 * @property {boolean} [verbatim=false] - Preserve the original address ordering.
 */

/**
 * Resolves a host name (e.g. `example.org`) into the first found A (IPv4) or
 * AAAA (IPv6) record. All option properties are optional. If options is an
 * integer, then it must be 4 or 6 – if options is 0 or not provided, then IPv4
 * and IPv6 addresses are both returned if found.
 *
 * From the node.js website...
 *
 * > With the all option set to true, the arguments for callback change to (err,
 * addresses), with addresses being an array of objects with the properties
 * address and family.
 *
 * > On error, err is an Error object, where err.code is the error code. Keep in
 * mind that err.code will be set to 'ENOTFOUND' not only when the host name does
 * not exist but also when the lookup fails in other ways such as no available
 * file descriptors. dns.lookup() does not necessarily have anything to do with
 * the DNS protocol. The implementation uses an operating system facility that
 * can associate names with addresses and vice versa. This implementation can
 * have subtle but important consequences on the behavior of any Node.js program.
 * Please take some time to consult the Implementation considerations section
 * before using dns.lookup().
 *
 * @see {@link https://nodejs.org/api/dns.html#dns_dns_lookup_hostname_options_callback}
 * @param {string} hostname - The host name to resolve.
 * @param {(LookupOptions|number|string)=} [options] - Lookup options or the record family.
 * @param {function(Error, string|LookupAddress[], 4|6=):void} cb - Invoked when the lookup completes.
 * @returns {void}
 */
export function lookup (hostname, options = {}, cb) {
  if (typeof hostname !== 'string') {
    const err = new TypeError(
      `The "hostname" argument must be of type string. Received type ${typeof hostname} (${hostname})`
    )
    err.code = 'ERR_INVALID_ARG_TYPE'
    throw err
  }

  if (typeof options === 'function') {
    cb = options
    options = {}
  }

  if (!isFunction(cb)) {
    const err = new TypeError(
      `The "callback" argument must be of type function. Received type ${typeof cb} undefined`
    )
    err.code = 'ERR_INVALID_ARG_TYPE'
    throw err
  }

  const normalized = normalizeLookupOptions(options)
  const payload = {
    ...normalized,
    id: rand64(),
    hostname
  }

  dc.channel('lookup.start').publish({
    hostname,
    family: normalized.family,
    all: normalized.all,
    sync: false
  })

  ipc
    .send('dns.lookup', payload)
    .then((result) => {
      const { err, data } = result || {}

      if (err) {
        const error = createLookupError(hostname, err)
        if (normalized.all) {
          cb(error)
        } else {
          cb(error, undefined, undefined)
        }
        return
      }

      dc.channel('lookup.end').publish({
        hostname,
        family: normalized.family,
        all: normalized.all,
        sync: false
      })

      dc.channel('lookup').publish({
        hostname,
        family: normalized.family,
        all: normalized.all,
        sync: false
      })

      if (normalized.all) {
        cb(null, Array.isArray(data?.addresses) ? data.addresses : [])
        return
      }

      cb(null, data?.address ?? null, data?.family ?? normalized.family ?? 0)
    })
    .catch((error) => {
      const err = createLookupError(hostname, error)
      if (normalized.all) {
        cb(err)
      } else {
        cb(err, undefined, undefined)
      }
    })
}

export { promises }

export { ADDRCONFIG, ALL, V4MAPPED, constants }

export default exports

for (const key in exports) {
  const value = exports[key]
  if (key in promises && isFunction(value) && isFunction(promises[key])) {
    value[Symbol.for('nodejs.util.promisify.custom')] = promises[key]
    value[Symbol.for('oro.runtime.util.promisify.custom')] = promises[key]
  }
}
