import { test } from 'oro:test'
import path from 'oro:path'
import os from 'oro:os'
import fs from 'oro:fs/promises'
import { Buffer } from 'oro:buffer'
import { open, OPEN_READONLY } from 'oro:sqlite'

function uniquePath (prefix = 'sqlite-test') {
  const stamp = Date.now().toString(36) + Math.random().toString(36).slice(2)
  return path.join(os.tmpdir(), `${prefix}-${stamp}.db`)
}

test('sqlite :memory: basics', (t) => {
  const db = open(':memory:')

  t.equal(db.closed, false, 'database opens successfully')

  const create = db.exec(
    'CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, body TEXT)'
  )
  t.equal(create.changes, 0, 'create table produces no row changes')

  const insert = db.exec("INSERT INTO notes (body) VALUES ('hello')")
  t.equal(insert.changes, 1, 'insert modifies a row')
  t.ok(insert.lastInsertRowid >= 1n, 'lastInsertRowid is bigint >= 1')

  const select = db.exec('SELECT id, body FROM notes ORDER BY id ASC')
  t.equal(select.rows.length, 1, 'select returns one row')
  t.same(select.columns, ['id', 'body'], 'column names are returned')
  t.same(
    select.columnsMeta.map((meta) => meta.type),
    ['integer', 'text'],
    'column metadata includes runtime types'
  )
  t.equal(select.rows[0].id, 1, 'row id is numeric')
  t.equal(select.rows[0].body, 'hello', 'row body matches value')

  const arraySelect = db.exec('SELECT id, body FROM notes', { mode: 'array' })
  t.equal(arraySelect.mode, 'array', 'array mode is reflected in result')
  t.equal(
    Array.isArray(arraySelect.rows[0]),
    true,
    'rows are arrays in array mode'
  )
  t.equal(arraySelect.rows[0][0], 1, 'array mode preserves numeric ids')
  t.equal(arraySelect.rows[0][1], 'hello', 'array mode preserves data order')

  db.close()
  t.equal(db.closed, true, 'database closed flag set')
  t.throws(
    () => db.exec('SELECT 1'),
    /Database connection is closed/,
    'exec after close throws'
  )
})

test('sqlite file database lifecycle', async (t) => {
  const dbPath = uniquePath('socket-db')
  const db = open(dbPath)

  db.exec('CREATE TABLE meta (name TEXT PRIMARY KEY, value TEXT)')
  db.exec("INSERT INTO meta (name, value) VALUES ('version', '1')")
  db.close()

  const stats = await fs.stat(dbPath)
  t.ok(stats.isFile(), 'database file exists on disk')

  const reopened = open(dbPath)
  const read = reopened.exec("SELECT value FROM meta WHERE name = 'version'")
  t.equal(read.rows[0].value, '1', 'persisted row readable after reopen')
  reopened.close()

  await fs.unlink(dbPath)
  t.pass('database file cleaned up')
})

test('sqlite blob columns return buffers', (t) => {
  const db = open(':memory:')
  db.exec('CREATE TABLE files (id INTEGER PRIMARY KEY, data BLOB)')

  const payload = Buffer.from('hello-socket', 'utf8')
  const hex = payload.toString('hex')
  db.exec(`INSERT INTO files (data) VALUES (X'${hex}')`)

  const result = db.exec('SELECT data FROM files LIMIT 1')
  t.equal(
    Buffer.isBuffer(result.rows[0].data),
    true,
    'blob column is decoded into Buffer'
  )
  t.equal(
    result.rows[0].data.toString('utf8'),
    'hello-socket',
    'buffer decodes to original value'
  )

  db.close()
})

test('sqlite OPEN_READONLY does not create new files', async (t) => {
  const missingPath = uniquePath('ro-missing')
  t.throws(
    () => open(missingPath, { flags: OPEN_READONLY }),
    /NotFound/i,
    'read-only open rejects missing file'
  )

  const exists = await fs.access(missingPath).then(
    () => true,
    () => false
  )
  t.equal(exists, false, 'read-only open left filesystem untouched')

  const dbPath = uniquePath('ro-existing')
  const writable = open(dbPath)
  writable.exec('CREATE TABLE t (id INTEGER PRIMARY KEY, value TEXT)')
  writable.close()

  const readonly = open(dbPath, { flags: OPEN_READONLY })
  t.ok(
    readonly.flags & OPEN_READONLY,
    'read-only flag preserved on database object'
  )
  t.throws(
    () => readonly.exec("INSERT INTO t (value) VALUES ('fail')"),
    /readonly/i,
    'writes fail in read-only mode'
  )
  const check = readonly.exec('SELECT COUNT(*) AS total FROM t')
  t.equal(check.rows[0].total, 1, 'existing row remains accessible')
  readonly.close()

  await fs.unlink(dbPath)
})

test('sqlite shared memory uri works across connections', (t) => {
  const uri = 'file:memdb1?mode=memory&cache=shared'
  const db1 = open(uri)
  db1.exec('CREATE TABLE shared (id INTEGER PRIMARY KEY, value TEXT)')
  db1.exec("INSERT INTO shared (value) VALUES ('shared')")

  const db2 = open(uri)
  const result = db2.exec('SELECT value FROM shared')
  t.equal(
    result.rows[0].value,
    'shared',
    'second connection observes shared memory rows'
  )

  db1.close()
  db2.close()
})

test('sqlite exec returns last result set for multiple selects', (t) => {
  const db = open(':memory:')
  db.exec('CREATE TABLE multi (id INTEGER PRIMARY KEY, name TEXT)')
  db.exec("INSERT INTO multi (name) VALUES ('a'), ('b')")

  const result = db.exec(
    'SELECT id FROM multi ORDER BY id; SELECT name FROM multi ORDER BY id'
  )
  t.same(result.columns, ['name'], 'columns reflect last select statement')
  t.equal(result.rows.length, 2, 'rows from last select are returned')
  t.equal(result.rows[0].name, 'a', 'row data aligned with last select result')

  db.close()
})

test('sqlite prepared statements support parameters and blobs', (t) => {
  const db = open(':memory:')
  db.exec(
    'CREATE TABLE items (id INTEGER PRIMARY KEY, label TEXT, payload BLOB)'
  )
  db.exec('CREATE TABLE nums (id INTEGER PRIMARY KEY, value INTEGER)')

  const insert = db.prepare('INSERT INTO items (label, payload) VALUES (?, ?)')
  const first = Buffer.from('first', 'utf8')
  insert.run(['one', first])
  insert.run(['two', Buffer.from('second', 'utf8')])
  insert.finalize()

  const numbers = db.prepare('INSERT INTO nums (value) VALUES (?)')
  numbers.run([1n])
  numbers.run([42])
  numbers.finalize()

  const select = db.prepare(
    'SELECT id, label, payload FROM items ORDER BY id ASC'
  )
  const result = select.run()
  t.equal(result.rows.length, 2, 'prepared select returns all rows')
  t.same(
    result.rows.map((row) => row.label),
    ['one', 'two'],
    'labels preserved'
  )
  t.equal(result.rows[0].id, 1, 'ids returned as numbers')
  t.equal(
    Buffer.isBuffer(result.rows[0].payload),
    true,
    'payload returned as Buffer'
  )
  t.equal(
    result.rows[0].payload.toString('utf8'),
    'first',
    'payload contents preserved'
  )
  select.finalize()

  const nums = db.exec('SELECT value FROM nums ORDER BY id ASC')
  t.same(
    nums.rows,
    [{ value: 1 }, { value: 42 }],
    'integer parameters persist as numbers'
  )

  db.close()
})

test('sqlite returns big integers as bigint', (t) => {
  const db = open(':memory:')
  db.exec('CREATE TABLE bigs (id INTEGER PRIMARY KEY, value INTEGER)')

  const value = 2n ** 54n
  db.exec(`INSERT INTO bigs (value) VALUES (${value.toString()})`)

  const result = db.exec('SELECT value FROM bigs LIMIT 1')
  t.equal(
    typeof result.rows[0].value,
    'bigint',
    'large integers decode to BigInt'
  )
  t.equal(result.rows[0].value, value, 'BigInt value preserved exactly')

  const arrayResult = db.exec('SELECT value FROM bigs LIMIT 1', {
    mode: 'array'
  })
  t.equal(
    typeof arrayResult.rows[0][0],
    'bigint',
    'array mode also yields BigInt'
  )
  t.equal(arrayResult.rows[0][0], value, 'array mode BigInt preserved')

  db.close()
})

test('sqlite statement iterate yields rows in batches', async (t) => {
  const db = open(':memory:')
  db.exec('CREATE TABLE letters (id INTEGER PRIMARY KEY, symbol TEXT)')

  const insert = db.prepare('INSERT INTO letters (symbol) VALUES (?)')
  insert.run(['a'])
  insert.run(['b'])
  insert.run(['c'])
  insert.finalize()

  const select = db.prepare('SELECT symbol FROM letters ORDER BY id ASC')
  const symbols = []
  for await (const row of select.iterate([], { batchSize: 1 })) {
    symbols.push(row.symbol)
  }
  t.same(symbols, ['a', 'b', 'c'], 'iterate yields all symbols sequentially')
  select.finalize()

  db.close()
})
