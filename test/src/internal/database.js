import test from 'oro:test'
import { Database } from 'oro:internal/database'

let sequence = 0

async function withDatabase (run) {
  const database = new Database(`oro.runtime.test.database.${Date.now()}.${++sequence}`)
  await database.open()
  try {
    await run(database)
  } finally {
    await database.drop()
  }
}

// Intercept only this test's connection. Other windows and loader caches keep
// using the original IndexedDB methods throughout the test.
async function interceptRequest (database, method, intercept, run) {
  const storage = database.storage
  const originalTransaction = storage.transaction
  storage.transaction = function (...args) {
    const transaction = originalTransaction.apply(this, args)
    const originalObjectStore = transaction.objectStore
    transaction.objectStore = function (...args) {
      const store = originalObjectStore.apply(this, args)
      const originalRequest = store[method]
      store[method] = function (...args) {
        return intercept(store, transaction, () => originalRequest.apply(this, args))
      }
      return store
    }
    return transaction
  }
  try {
    await run()
  } finally {
    delete storage.transaction
  }
}

async function observeRejections (t, run) {
  const rejections = []
  const onRejection = event => rejections.push(event.reason)
  globalThis.addEventListener('unhandledrejection', onRejection)
  try {
    await run()
    // Let the event loop report any promises left unobserved by the operation.
    await new Promise(resolve => setTimeout(resolve, 0))
    await new Promise(resolve => setTimeout(resolve, 0))
    t.equal(rejections.length, 0, 'caught database failures do not leak unhandled rejections')
  } finally {
    globalThis.removeEventListener('unhandledrejection', onRejection)
  }
}

test('database: concurrent writes commit and survive reopening', async t => {
  await withDatabase(async database => {
    const entries = Array.from({ length: 8 }, (_, index) => [`key-${index}`, { index }])
    await Promise.all(entries.map(([key, value]) => database.put(key, value)))
    t.deepEqual(await database.entries(), entries, 'all concurrent writes are committed')
    await database.update('key-0', { changed: true })
    await database.delete('key-1')
    await database.close()
    await database.open()
    t.deepEqual(await database.get('key-0'), { index: 0, changed: true }, 'updates persist after reopening')
    t.equal(await database.get('key-1'), null, 'deletions persist after reopening')
    t.deepEqual(await database.get('key-7'), { index: 7 }, 'other values remain intact')
  })
})

test('database: caught request aborts stay local to the operation', async t => {
  await withDatabase(async database => {
    await database.put('key', { original: true })
    for (const [operation, method] of [
      ['get', 'get'],
      ['put', 'put'],
      ['delete', 'delete'],
      ['update', 'get'],
      ['entries', 'openCursor']
    ]) {
      await observeRejections(t, async () => {
        await interceptRequest(database, method, (_store, transaction, request) => {
          const pending = request()
          transaction.abort()
          return pending
        }, async () => {
          await t.rejects(() => database[operation]('key', { changed: true }), undefined, `${operation} rejects an aborted request`)
        })
      })
      t.deepEqual(await database.get('key'), { original: true }, `${operation} abort leaves persisted data intact`)
    }
  })
})

test('database: asynchronous write failures reject without leaking promises', async t => {
  await withDatabase(async database => {
    await database.put('duplicate', { original: true })
    await database.put('key', { original: true })
    for (const operation of ['put', 'delete']) {
      await observeRejections(t, async () => {
        await interceptRequest(database, operation, store => {
          // Replace this operation with a native request that fails after
          // commit() is requested, without depending on disk or quota failures.
          return store.add({ key: 'duplicate', value: { changed: true } })
        }, async () => {
          await t.rejects(() => database[operation]('key', { changed: true }), undefined, `${operation} rejects the failed transaction`)
        })
      })
      t.deepEqual(await database.get('key'), { original: true }, `${operation} failure leaves the existing value intact`)
    }
  })
})

test('database: request success does not hide a later transaction abort', async t => {
  await withDatabase(async database => {
    await database.put('key', { original: true })
    for (const [operation, method] of [['get', 'get'], ['entries', 'openCursor']]) {
      let succeeded = false
      await observeRejections(t, async () => {
        await interceptRequest(database, method, (_store, transaction, request) => {
          const pending = request()
          pending.addEventListener('success', () => {
            succeeded = true
            transaction.abort()
          }, { once: true })
          return pending
        }, async () => {
          await t.rejects(() => database[operation]('key'), undefined, `${operation} rejects an abort after request success`)
        })
      })
      t.equal(succeeded, true, `${operation} request succeeded before the transaction aborted`)
    }
  })
})
