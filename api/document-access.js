/**
 * @module document-access
 * Persistent Android Storage Access Framework grants for user-selected URIs.
 * Providers can revoke grants or refuse persistence; callers must handle errors.
 */
import ipc from './ipc.js'

/**
 * @typedef {object} DocumentGrant
 * @property {string} uri A scoped content URI, never a filesystem path.
 * @property {boolean} read
 * @property {boolean} write
 * @property {number} persistedAt Unix milliseconds.
 */

/**
 * @typedef {object} DocumentGrantOptions
 * @property {boolean} [read=true]
 * @property {boolean} [write=false]
 */

function parameters (uri, options) {
  if (typeof uri !== 'string' || uri.length > 4096 || Array.from(uri).some(character => character.charCodeAt(0) < 32 || character.charCodeAt(0) === 127) || !uri.startsWith('content://')) {
    throw new TypeError('A user-selected content URI of at most 4096 characters is required')
  }
  const read = options.read !== false
  const write = options.write === true
  if (!read && !write) throw new TypeError('At least one grant mode is required')
  return { uri, flags: (read ? 1 : 0) | (write ? 2 : 0) }
}

async function request (action, value = {}) {
  const { data, err } = await ipc.request('documentAccess', { action, ...value })
  if (err) throw err
  return data
}

/**
 * Lists persisted grants. This does not establish that the provider is reachable.
 * Other platforms reject with `ENOSYS`.
 * @returns {Promise<DocumentGrant[]>}
 */
export async function list () {
  return request('list')
}

/**
 * Retains an existing user-selected document grant across process death/reboot.
 * Android must have granted the requested persistable modes; this never opens
 * a permission prompt or obtains access to an arbitrary URI. At most 128 grants.
 * @param {string} uri
 * @param {DocumentGrantOptions} [options]
 * @returns {Promise<void>}
 */
export async function persist (uri, options = {}) {
  await request('persist', parameters(uri, options))
}

/**
 * Releases retained access. Close open files and stop using the URI first.
 * @param {string} uri
 * @param {DocumentGrantOptions} [options]
 * @returns {Promise<void>}
 */
export async function release (uri, options = {}) {
  await request('release', parameters(uri, options))
}

/**
 * @typedef {object} DocumentMetadata
 * @property {string} uri Provider URI; pass this URI directly to oro:fs.
 * @property {string|null} name Provider display name; never a filesystem path.
 * @property {string|null} mimeType A directory uses vnd.android.document/directory.
 * @property {number|null} size Bytes, or null when unknown.
 * @property {number|null} lastModified Unix milliseconds, or null when unknown.
 * @property {number|null} flags Android DocumentsContract provider flags, not an access grant.
 */

/**
 * @typedef {object} DocumentChildrenOptions
 * @property {number} [offset=0] Cursor offset from 0 through 10000.
 * @property {number} [limit=128] Maximum entries, from 1 through 128.
 */

/**
 * @typedef {object} DocumentChildren
 * @property {DocumentMetadata[]} entries
 * @property {boolean} truncated More entries exist beyond this page.
 * @property {number|null} nextOffset Null at EOF or the 10000 offset limit.
 */

async function query (action, uri, options = {}) {
  parameters(uri, { read: true })
  const { data, err } = await ipc.request('documentAccess.query', { action, uri, options: JSON.stringify(options) })
  if (err) throw err
  return data
}

/**
 * Queries a selected document or tree root without resolving a filesystem path.
 * Unknown provider sizes remain null. Queries have a five-second result deadline;
 * a timed-out provider may still occupy one of the two bounded worker slots.
 * Providers enforce existing grants. Other platforms reject with ENOSYS.
 * @param {string} uri
 * @returns {Promise<DocumentMetadata>}
 */
export async function stat (uri) {
  return query('stat', uri)
}

/**
 * Lists children within an existing selected tree grant. Entries contain proper
 * child document URIs. Pages follow provider cursor order, which is not a durable
 * snapshot: after edits or process loss, restart and reconcile by URI. At most
 * 128 entries and 1 MiB of metadata per response, with two concurrent queries.
 * This does not broaden grants or emulate ordinary filesystem path traversal.
 * Other platforms reject with ENOSYS.
 * @param {string} uri
 * @param {DocumentChildrenOptions} [options]
 * @returns {Promise<DocumentChildren>}
 */
export async function readDirectory (uri, options = {}) {
  const offset = options.offset ?? 0
  const limit = options.limit ?? 128
  if (!Number.isInteger(offset) || offset < 0 || offset > 10000 ||
      !Number.isInteger(limit) || limit < 1 || limit > 128) {
    throw new TypeError('Directory offset must be 0..10000 and limit must be 1..128')
  }
  return query('children', uri, { offset, limit })
}

export default { list, persist, release, stat, readDirectory }
