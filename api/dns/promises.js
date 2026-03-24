/**
 * @module dns.promises
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
 * import { lookup } from 'oro:dns/promises'
 * ```
 */

import diagnostics from '../diagnostics.js'
import { rand64 } from '../crypto.js'
import ipc from '../ipc.js'
import { normalizeLookupOptions, createLookupError } from './utils.js'
import { ADDRCONFIG, ALL, V4MAPPED, constants } from './constants.js'

import * as exports from './promises.js'

const dc = diagnostics.channels.group('dns', [
  'lookup.start',
  'lookup.end',
  'lookup'
])

/**
 * @async
 * @typedef {import('./index.js').LookupAddress} LookupAddress
 * @typedef {import('./index.js').LookupOptions} LookupOptions
 *
 * @async
 * @see {@link https://nodejs.org/api/dns.html#dnspromiseslookuphostname-options}
 * @param {string} hostname - The host name to resolve.
 * @param {(LookupOptions|number|string)=} [opts] - Lookup options or family.
 * @returns {Promise<LookupAddress|LookupAddress[]>}
 */
export async function lookup (hostname, opts) {
  if (typeof hostname !== 'string') {
    const err = new TypeError(
      `The "hostname" argument must be of type string. Received type ${typeof hostname} (${hostname})`
    )
    err.code = 'ERR_INVALID_ARG_TYPE'
    throw err
  }

  const options = normalizeLookupOptions(opts)
  const payload = {
    ...options,
    id: rand64(),
    hostname
  }

  dc.channel('lookup.start').publish({
    hostname,
    family: options.family,
    all: options.all
  })

  const { err, data } = await ipc.send('dns.lookup', payload)

  if (err) {
    throw createLookupError(hostname, err)
  }

  dc.channel('lookup.end').publish({
    hostname,
    family: options.family,
    all: options.all
  })

  dc.channel('lookup').publish({
    hostname,
    family: options.family,
    all: options.all
  })

  if (options.all) {
    return Array.isArray(data?.addresses) ? data.addresses : []
  }

  return data
}

export default exports

export { ADDRCONFIG, ALL, V4MAPPED, constants }
