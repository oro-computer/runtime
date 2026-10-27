import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import { spawnSync } from 'node:child_process'

const source = fs.readFileSync(new URL('../../api/internal/database.js', import.meta.url), 'utf8')
  .replace("import gc from '../gc.js'", 'const gc = { ref () {}, unref () {}, finalizer: Symbol() }')
const moduleURL = `data:text/javascript;base64,${Buffer.from(source + '\n//# sourceURL=oro-database-fixture.js').toString('base64')}`

function probe (body) {
  const result = spawnSync(process.execPath, ['--input-type=module'], {
    encoding: 'utf8',
    timeout: 10000,
    input: `
      import assert from 'node:assert/strict'
      import { setImmediate as nextTurn } from 'node:timers/promises'
      const unhandled = []
      const reported = []
      process.on('unhandledRejection', error => unhandled.push(error))
      globalThis.reportError = error => reported.push(error)
      globalThis.ErrorEvent = class extends Event {
        constructor (type, options) { super(type); Object.assign(this, options) }
      }
      class Request extends EventTarget {
        result = null
        error = null
        transaction = null
        succeed (result) { this.result = result; this.dispatchEvent(new Event('success')) }
        fail (error) { this.error = error; this.dispatchEvent(new Event('error')) }
      }
      globalThis.IDBTransaction = class extends EventTarget {
        error = null
        commits = 0
        commitFailure = null
        store = null
        objectStore () { return this.store }
        commit () { this.commits++; if (this.commitFailure) throw this.commitFailure }
        fail (error) { this.error = error; this.dispatchEvent(new Event('error')) }
        abort () { this.dispatchEvent(new Event('abort')) }
      }
      const { Database, DatabaseRequestQueue } = await import(${JSON.stringify(moduleURL)})
      async function openDatabase (behavior) {
        const transaction = new IDBTransaction()
        const calls = []
        transaction.store = Object.fromEntries(['get', 'getAll', 'put', 'add', 'delete', 'openCursor'].map(method => [method, () => {
          calls.push(method)
          const request = new Request()
          request.transaction = transaction
          queueMicrotask(() => behavior(request, transaction, method))
          return request
        }]))
        globalThis.indexedDB = { open () {
          const request = new Request()
          queueMicrotask(() => request.succeed({ transaction: () => transaction }))
          return request
        } }
        const database = new Database('fixture')
        await database.open()
        return { database, transaction, calls }
      }
      ${body}
      await nextTurn()
      await nextTurn()
      assert.deepEqual(unhandled.map(error => error.message), [], 'caught database errors must not create unhandled rejections')
      assert.deepEqual(reported.map(error => error.message), [], 'caught database errors must not be globally reported')
      console.log('database probe passed')
    `
  })
  assert.equal(result.error, undefined)
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.equal(result.stdout, 'database probe passed\n')
}

test('queue rejection preserves caller failure and emits one queue error', () => {
  probe(`
    const queue = new DatabaseRequestQueue()
    const request = new Request()
    const failure = new Error('request failed')
    const events = []
    queue.addEventListener('error', event => events.push(event))
    const pending = queue.push(request)
    const rejected = assert.rejects(pending, error => error.cause === failure)
    request.fail(failure)
    await rejected
    assert.equal(events.length, 1)
    assert.equal(events[0].error.cause, failure)
    assert.equal(events[0].request, request)
    assert.equal(queue.length, 0)
  `)
})

test('request failures stay handled when their transaction rejects first', () => {
  probe(`
    for (const operation of ['get', 'put', 'insert', 'delete']) {
      const failure = new Error(operation + ' failed')
      const { database, transaction } = await openDatabase((request, transaction) => {
        transaction.fail(failure)
        queueMicrotask(() => request.fail(failure))
      })
      await assert.rejects(database[operation]('key', operation === 'put' || operation === 'insert' ? { value: 1 } : undefined), error => error.cause === failure)
      assert.equal(transaction.commits, operation === 'get' ? 0 : 1)
    }
  `)
})

test('queued write failures stay handled when commit throws synchronously', () => {
  probe(`
    for (const operation of ['put', 'insert', 'delete']) {
      const failure = new Error('transaction aborted')
      const commitFailure = new Error('commit is inactive')
      const { database, transaction } = await openDatabase(request => request.fail(failure))
      transaction.commitFailure = commitFailure
      await assert.rejects(database[operation]('key', operation === 'delete' ? undefined : { value: 1 }), error => error === commitFailure)
      assert.equal(transaction.commits, 1)
    }
  `)
})

test('a successful request does not hide a later transaction abort', () => {
  probe(`
    const { database } = await openDatabase((request, transaction) => {
      request.succeed({ key: 'key', value: 'value' })
      queueMicrotask(() => transaction.abort())
    })
    await assert.rejects(database.get('key'), /The request was aborted/)
  `)
})

test('update rejects a failed lookup without reporting globally or writing a replacement', () => {
  probe(`
    const failure = new Error('lookup failed')
    const { database, transaction, calls } = await openDatabase(request => request.fail(failure))
    await assert.rejects(database.update('key', { value: 1 }), error => error.cause === failure)
    assert.deepEqual(calls, ['get'])
    assert.equal(transaction.commits, 0)
  `)
})

test('upgrade and blocked callback failures reject the queued opening request', () => {
  probe(`
    for (const eventType of ['upgradeneeded', 'blocked']) {
      for (const asynchronous of [false, true]) {
        const queue = new DatabaseRequestQueue()
        const request = new Request()
        const failure = new Error(eventType + ' failed')
        const callback = asynchronous ? async () => { await nextTurn(); throw failure } : () => { throw failure }
        const rejected = assert.rejects(queue.push(request, callback), error => error === failure)
        request.dispatchEvent(new Event(eventType))
        await rejected
        assert.equal(queue.length, 0)
      }
    }
  `)
})

test('wait observes the queued promises and propagates their failure', () => {
  probe(`
    const queue = new DatabaseRequestQueue()
    const first = new Request()
    const second = new Request()
    queue.push(first)
    queue.push(second)
    const waited = queue.wait()
    first.succeed('one')
    second.succeed('two')
    assert.deepEqual(await waited, ['one', 'two'])
    const failed = new Request()
    const failure = new Error('waiting failed')
    queue.push(failed)
    const rejected = assert.rejects(queue.wait(), error => error.cause === failure)
    failed.fail(failure)
    await rejected
    assert.equal(queue.length, 0)
  `)
})
