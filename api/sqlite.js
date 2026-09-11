/**
 * @module sqlite
 *
 * Lightweight SQLite convenience layer backed by the runtime's native
 * sqlite3 integration. When ORO_HOME configures an extension directory on
 * desktop, Android, or iOS, the runtime will attempt to auto-load the cr-sqlite
 * extension for every database connection so that CRR (conflict-free
 * replicated relations) features are available transparently. You can use
 * {@link hasCRSQLite} to detect whether cr-sqlite is currently available.
 * Without ORO_HOME, ordinary SQLite connections remain available.
 *
 * Example:
 *
 * ```js
 * import { open } from 'oro:sqlite'
 *
 * const db = open('app.db')
 * db.exec('CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, body TEXT)')
 * db.exec('INSERT INTO notes (body) VALUES (?)', { params: ['hello'] })
 * const { rows } = db.exec('SELECT * FROM notes')
 * ```
 */

import ipc from './ipc.js'
import gc from './gc.js'
import { rand64 } from './crypto.js'
import { Buffer } from './buffer.js'

const SQLITE_FLAGS = {
  OPEN_READONLY: 0x00000001,
  OPEN_READWRITE: 0x00000002,
  OPEN_CREATE: 0x00000004,
  OPEN_URI: 0x00000040,
  OPEN_MEMORY: 0x00000080,
  OPEN_NOMUTEX: 0x00008000,
  OPEN_FULLMUTEX: 0x00010000,
  OPEN_SHAREDCACHE: 0x00020000,
  OPEN_PRIVATECACHE: 0x00040000
}

export const OPEN_READONLY = SQLITE_FLAGS.OPEN_READONLY
export const OPEN_READWRITE = SQLITE_FLAGS.OPEN_READWRITE
export const OPEN_CREATE = SQLITE_FLAGS.OPEN_CREATE
export const OPEN_URI = SQLITE_FLAGS.OPEN_URI
export const OPEN_MEMORY = SQLITE_FLAGS.OPEN_MEMORY
export const OPEN_NOMUTEX = SQLITE_FLAGS.OPEN_NOMUTEX
export const OPEN_FULLMUTEX = SQLITE_FLAGS.OPEN_FULLMUTEX
export const OPEN_SHAREDCACHE = SQLITE_FLAGS.OPEN_SHAREDCACHE
export const OPEN_PRIVATECACHE = SQLITE_FLAGS.OPEN_PRIVATECACHE
export const OPEN_DEFAULT = OPEN_READWRITE | OPEN_CREATE | OPEN_FULLMUTEX

const ROW_MODE_OBJECT = 'object'
const ROW_MODE_ARRAY = 'array'

const kFinalize = gc.finalizer
const MAX_SAFE_BIGINT = BigInt(Number.MAX_SAFE_INTEGER)
const MIN_SAFE_BIGINT = BigInt(Number.MIN_SAFE_INTEGER)

function ensureResult (result, source) {
  if (result?.err) {
    throw result.err
  }

  if (result?.data !== undefined) {
    return result.data
  }

  if (result?.source === source) {
    return null
  }

  return result
}

function decodeResultValue (value) {
  if (value && typeof value === 'object' && !Array.isArray(value)) {
    if (value.encoding === 'base64') {
      const source = typeof value.data === 'string' ? value.data : ''
      try {
        return Buffer.from(source, 'base64')
      } catch {
        return Buffer.alloc(0)
      }
    }

    if (value.type === 'integer') {
      const raw = value.value
      let text

      if (typeof raw === 'string') {
        text = raw
      } else if (typeof raw === 'number' && Number.isFinite(raw)) {
        text = String(Math.trunc(raw))
      } else if (typeof raw === 'bigint') {
        return raw
      } else {
        text = String(raw ?? 0)
      }

      try {
        const big = BigInt(text)
        if (big <= MAX_SAFE_BIGINT && big >= MIN_SAFE_BIGINT) {
          return Number(big)
        }
        return big
      } catch {
        const numeric = Number(text)
        return Number.isNaN(numeric) ? text : numeric
      }
    }
  }

  return value
}

function normalizeRows (rows, mode) {
  if (!Array.isArray(rows)) {
    return []
  }

  if (mode === ROW_MODE_ARRAY) {
    return rows.map((row) => {
      if (!Array.isArray(row)) {
        return row
      }
      return row.map(decodeResultValue)
    })
  }

  return rows.map((row) => {
    if (!row || typeof row !== 'object' || Array.isArray(row)) {
      return row
    }

    const next = {}
    for (const [key, value] of Object.entries(row)) {
      next[key] = decodeResultValue(value)
    }
    return next
  })
}

function normalizeColumns (input) {
  if (!Array.isArray(input)) {
    return []
  }
  return input.map((column) => String(column))
}

function normalizeColumnsMeta (input) {
  if (!Array.isArray(input)) {
    return []
  }

  return input.map((meta) => {
    if (!meta || typeof meta !== 'object') {
      return {
        name: '',
        declType: '',
        type: 'null'
      }
    }

    return {
      name: typeof meta.name === 'string' ? meta.name : '',
      declType: typeof meta.declType === 'string' ? meta.declType : '',
      type: typeof meta.type === 'string' ? meta.type : 'null'
    }
  })
}

function parseChanges (value) {
  if (typeof value === 'number' && Number.isFinite(value)) {
    return value
  }

  const parsed = Number.parseInt(value ?? 0, 10)
  return Number.isNaN(parsed) ? 0 : parsed
}

function parseLastInsertRowid (value) {
  if (typeof value === 'bigint') {
    return value
  }

  try {
    return BigInt(value ?? 0)
  } catch {
    return 0n
  }
}

function normalizeResultEnvelope (data = {}, mode = ROW_MODE_OBJECT) {
  const normalizedMode =
    mode === ROW_MODE_ARRAY ? ROW_MODE_ARRAY : ROW_MODE_OBJECT
  const columns = normalizeColumns(data.columns)
  const columnsMeta = normalizeColumnsMeta(data.columnsMeta)

  return {
    rows: normalizeRows(data.rows, normalizedMode),
    columns,
    columnsMeta,
    changes: parseChanges(data.changes),
    lastInsertRowid: parseLastInsertRowid(data.lastInsertRowid),
    mode: normalizedMode
  }
}

function normalizeChunkEnvelope (data = {}, mode = ROW_MODE_OBJECT) {
  const normalized = normalizeResultEnvelope(data, mode)
  return {
    ...normalized,
    done: Boolean(data.done)
  }
}

function encodeParamValue (value) {
  if (value === undefined || value === null) {
    return null
  }

  if (typeof value === 'bigint') {
    return {
      type: 'integer',
      value: value.toString()
    }
  }

  if (Buffer.isBuffer(value)) {
    return {
      encoding: 'base64',
      data: value.toString('base64')
    }
  }

  if (ArrayBuffer.isView(value)) {
    return {
      encoding: 'base64',
      data: Buffer.from(
        value.buffer,
        value.byteOffset,
        value.byteLength
      ).toString('base64')
    }
  }

  if (value instanceof ArrayBuffer) {
    return {
      encoding: 'base64',
      data: Buffer.from(new Uint8Array(value)).toString('base64')
    }
  }

  if (typeof value === 'number') {
    if (!Number.isFinite(value)) {
      throw new RangeError('statement parameters must be finite numbers')
    }
    return value
  }

  if (typeof value === 'boolean' || typeof value === 'string') {
    return value
  }

  if (
    value &&
    typeof value === 'object' &&
    typeof value.toJSON === 'function'
  ) {
    return value.toJSON()
  }

  return JSON.parse(JSON.stringify(value))
}

function encodeParams (params) {
  if (params === undefined) {
    return []
  }

  if (!Array.isArray(params)) {
    return [encodeParamValue(params)]
  }

  return params.map(encodeParamValue)
}

let cachedCRSQLiteSupport

/**
 * Returns true when the runtime is able to open a SQLite database with the
 * cr-sqlite extension loaded and ready for use.
 *
 * This reflects the *current* process configuration. When ORO_HOME is set,
 * desktop, Android, and iOS builds load cr-sqlite during database open.
 * This predicate checks that the extension's SQL functions are available.
 * It returns false if the extension is unconfigured or cannot be loaded.
 *
 * The result is cached for the lifetime of the process.
 *
 * @returns {boolean}
 */
export function hasCRSQLite () {
  if (typeof cachedCRSQLiteSupport === 'boolean') {
    return cachedCRSQLiteSupport
  }

  try {
    const db = new Database(':memory:', { flags: OPEN_DEFAULT | OPEN_MEMORY })
    try {
      db.exec('SELECT crsql_version()')
    } finally {
      db.close()
    }
    cachedCRSQLiteSupport = true
  } catch {
    cachedCRSQLiteSupport = false
  }

  return cachedCRSQLiteSupport
}

export class Statement {
  #database
  #id = ''
  #closed = false
  #columns = []
  #columnsMeta = []
  #finalizerState = null

  constructor (database, descriptor = {}) {
    if (!database || typeof descriptor?.id === 'undefined') {
      throw new TypeError('Invalid statement descriptor')
    }

    this.#database = database
    this.#id = String(descriptor.id)
    this.#columns = normalizeColumns(descriptor.columns)
    this.#columnsMeta = normalizeColumnsMeta(descriptor.columnsMeta)
    this.#finalizerState = {
      id: this.#id,
      closed: false,
      databaseRef: database ? new WeakRef(database) : null
    }

    gc.ref(this)
  }

  get id () {
    return this.#id
  }

  get closed () {
    return this.#closed
  }

  get columns () {
    return this.#columns.slice()
  }

  get columnsMeta () {
    return this.#columnsMeta.map((entry) => ({ ...entry }))
  }

  #assertOpen () {
    if (this.#closed) {
      throw new Error('Statement has been finalized')
    }
  }

  bind (params = []) {
    this.#assertOpen()

    const payload = {
      id: this.#id,
      params: JSON.stringify(encodeParams(params))
    }

    const result = ipc.sendSync('sqlite.statement.bind', payload)
    ensureResult(result, 'sqlite.statement.bind')
    return this
  }

  step (options = {}) {
    this.#assertOpen()

    const mode =
      options?.mode === ROW_MODE_ARRAY ? ROW_MODE_ARRAY : ROW_MODE_OBJECT
    const payload = { id: this.#id }

    if (
      typeof options?.limit === 'number' &&
      Number.isFinite(options.limit) &&
      options.limit > 0
    ) {
      payload.limit = String(Math.trunc(options.limit))
    }

    if (mode === ROW_MODE_ARRAY) {
      payload.mode = mode
    }

    const result = ipc.sendSync('sqlite.statement.step', payload)
    const data = ensureResult(result, 'sqlite.statement.step')
    const chunk = normalizeChunkEnvelope(data, mode)

    this.#columns = chunk.columns
    this.#columnsMeta = chunk.columnsMeta

    return chunk
  }

  reset () {
    this.#assertOpen()
    const result = ipc.sendSync('sqlite.statement.reset', { id: this.#id })
    ensureResult(result, 'sqlite.statement.reset')
    this.#columnsMeta = this.#columnsMeta.map((entry) => ({
      ...entry,
      type: 'null'
    }))
    return this
  }

  run (params = [], options = {}) {
    const mode =
      options?.mode === ROW_MODE_ARRAY ? ROW_MODE_ARRAY : ROW_MODE_OBJECT
    const batchSize =
      typeof options?.batchSize === 'number' && options.batchSize > 0
        ? Math.trunc(options.batchSize)
        : undefined
    const resetAfter = options?.reset !== false

    if (params !== undefined) {
      this.bind(params)
    }

    const rows = []
    let done = false
    let lastChunk = null

    do {
      const chunk = this.step({ limit: batchSize, mode })
      lastChunk = chunk
      done = chunk.done
      if (chunk.rows.length > 0) {
        rows.push(...chunk.rows)
      }
    } while (!done)

    if (resetAfter) {
      try {
        this.reset()
      } catch {}
    }

    if (!lastChunk) {
      return {
        rows,
        columns: this.#columns.slice(),
        columnsMeta: this.#columnsMeta.map((entry) => ({ ...entry })),
        changes: 0,
        lastInsertRowid: 0n,
        mode
      }
    }

    return {
      rows,
      columns: lastChunk.columns,
      columnsMeta: lastChunk.columnsMeta,
      changes: lastChunk.changes,
      lastInsertRowid: lastChunk.lastInsertRowid,
      mode: lastChunk.mode
    }
  }

  all (params = [], options = {}) {
    const result = this.run(params, options)
    return result.rows
  }

  iterate (params = [], options = {}) {
    const statement = this
    const mode =
      options?.mode === ROW_MODE_ARRAY ? ROW_MODE_ARRAY : ROW_MODE_OBJECT
    const batchSize =
      typeof options?.batchSize === 'number' && options.batchSize > 0
        ? Math.trunc(options.batchSize)
        : undefined
    const resetAfter = options?.reset !== false

    return (async function * () {
      if (params !== undefined) {
        statement.bind(params)
      }

      let done = false
      try {
        while (!done) {
          const chunk = statement.step({ limit: batchSize, mode })
          done = chunk.done
          for (const row of chunk.rows) {
            // allow scheduling between chunks
            // eslint-disable-next-line no-void
            await Promise.resolve()
            yield row
          }
        }
      } finally {
        if (resetAfter) {
          try {
            statement.reset()
          } catch {}
        }
      }
    })()
  }

  finalize () {
    this.#finalizeInternal({ quiet: false })
  }

  _finalize (options = {}) {
    this.#finalizeInternal({ quiet: true, release: options?.release !== false })
  }

  #finalizeInternal ({ quiet = false, release = true } = {}) {
    if (this.#closed) {
      return
    }

    try {
      ipc.sendSync('sqlite.statement.finalize', { id: this.#id })
    } catch (err) {
      if (!quiet) {
        throw err
      }
    } finally {
      this.#closed = true
      if (this.#finalizerState) {
        this.#finalizerState.closed = true
        this.#finalizerState.databaseRef = null
      }
      if (release) {
        this.#database?._releaseStatementById(this.#id)
      }
      this.#database = null
    }
  }

  [kFinalize] () {
    const state = this.#finalizerState
    return {
      args: [state],
      handle (held) {
        if (!held || held.closed) {
          return
        }

        try {
          ipc.sendSync('sqlite.statement.finalize', { id: held.id })
        } catch {}

        held.closed = true

        const database = held.databaseRef?.deref?.()
        if (database && typeof database._releaseStatementById === 'function') {
          database._releaseStatementById(held.id)
        }
      }
    }
  }
}

export class Database {
  #state = { id: null, closed: true }
  #path = ''
  #flags = OPEN_DEFAULT
  #statements = new Map()

  constructor (path, options = {}) {
    if (typeof path !== 'string' || path.length === 0) {
      throw new TypeError('path must be a non-empty string')
    }

    const id = options?.id !== undefined ? options.id : rand64()
    this.#state.id = String(id)
    this.#path = path
    this.#flags =
      typeof options?.flags === 'number' ? options.flags : OPEN_DEFAULT

    this.#open()
    gc.ref(this)
  }

  get id () {
    return this.#state.id
  }

  get path () {
    return this.#path
  }

  get flags () {
    return this.#flags
  }

  get closed () {
    return this.#state.closed
  }

  #assertOpen () {
    if (this.closed) {
      throw new Error('Database connection is closed')
    }
  }

  #open () {
    const params = {
      id: this.id,
      path: this.#path,
      flags: this.#flags
    }

    const result = ipc.sendSync('sqlite.open', params)
    ensureResult(result, 'sqlite.open')
    this.#state.closed = false
  }

  close () {
    if (this.closed) {
      return
    }

    this.#finalizeStatements({ quiet: true })

    const result = ipc.sendSync('sqlite.close', { id: this.id })
    ensureResult(result, 'sqlite.close')
    this.#state.closed = true
    this.#state.id = null
  }

  exec (sql, options = {}) {
    this.#assertOpen()

    if (typeof sql !== 'string' || sql.length === 0) {
      throw new TypeError('sql must be a non-empty string')
    }

    const mode =
      options?.mode === ROW_MODE_ARRAY ? ROW_MODE_ARRAY : ROW_MODE_OBJECT
    const params = { id: this.id, sql }

    if (mode === ROW_MODE_ARRAY) {
      params.mode = mode
    }

    if (Object.prototype.hasOwnProperty.call(options, 'params')) {
      params.params = JSON.stringify(encodeParams(options.params))
    }

    const result = ipc.sendSync('sqlite.exec', params)
    const data = ensureResult(result, 'sqlite.exec')
    return normalizeResultEnvelope(data, mode)
  }

  async execAsync (sql, options = {}) {
    this.#assertOpen()

    if (typeof sql !== 'string' || sql.length === 0) {
      throw new TypeError('sql must be a non-empty string')
    }

    const mode =
      options?.mode === ROW_MODE_ARRAY ? ROW_MODE_ARRAY : ROW_MODE_OBJECT
    const params = { id: this.id, sql }

    if (mode === ROW_MODE_ARRAY) {
      params.mode = mode
    }

    if (Object.prototype.hasOwnProperty.call(options, 'params')) {
      params.params = JSON.stringify(encodeParams(options.params))
    }

    const result = await ipc.send('sqlite.exec', params)
    const data = ensureResult(result, 'sqlite.exec')
    return normalizeResultEnvelope(data, mode)
  }

  query (sql, options = {}) {
    const { rows } = this.exec(sql, options)
    return rows
  }

  async queryAsync (sql, options = {}) {
    const { rows } = await this.execAsync(sql, options)
    return rows
  }

  prepare (sql) {
    this.#assertOpen()

    if (typeof sql !== 'string' || sql.length === 0) {
      throw new TypeError('sql must be a non-empty string')
    }

    const result = ipc.sendSync('sqlite.prepare', { id: this.id, sql })
    const data = ensureResult(result, 'sqlite.prepare') ?? {}
    data.columns = normalizeColumns(data.columns)
    data.columnsMeta = normalizeColumnsMeta(data.columnsMeta)

    const statement = new Statement(this, data)
    this._trackStatement(statement)
    return statement
  }

  _trackStatement (statement) {
    this.#statements.set(statement.id, new WeakRef(statement))
  }

  _releaseStatement (statement) {
    if (!statement) return
    this._releaseStatementById(statement.id)
  }

  _releaseStatementById (id) {
    if (typeof id === 'undefined') return
    this.#statements.delete(String(id))
  }

  #finalizeStatements ({ quiet = false } = {}) {
    for (const [, ref] of this.#statements) {
      const statement = ref?.deref?.()
      if (!statement) {
        continue
      }

      try {
        statement._finalize({ quiet: true, release: false })
      } catch (err) {
        if (!quiet) {
          throw err
        }
      }
    }
    this.#statements.clear()
  }

  [kFinalize] () {
    const state = this.#state
    return {
      args: [state],
      handle (held) {
        if (!held || held.closed || !held.id) {
          return
        }

        try {
          ipc.sendSync('sqlite.close', { id: held.id })
        } catch {}

        held.closed = true
        held.id = null
      }
    }
  }
}

export function open (path, options) {
  return new Database(path, options)
}

export default Object.freeze({
  Database,
  Statement,
  open,
  hasCRSQLite,
  OPEN_DEFAULT,
  OPEN_READONLY,
  OPEN_READWRITE,
  OPEN_CREATE,
  OPEN_URI,
  OPEN_MEMORY,
  OPEN_NOMUTEX,
  OPEN_FULLMUTEX,
  OPEN_SHAREDCACHE,
  OPEN_PRIVATECACHE
})
