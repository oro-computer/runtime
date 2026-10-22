import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { runInNewContext } from 'node:vm'

const source = readFileSync(new URL('../../api/internal/init.js', import.meta.url), 'utf8')
const start = source.indexOf('class RuntimeQueuedResponses extends ConcurrentQueue {')
const end = source.indexOf('\nhooks.onLoad(', start)
assert.ok(start >= 0 && end > start)

for (const platform of ['linux', 'ios', 'android', 'win32']) {
  test(`queued ${platform} responses retain their owning transport for documents and workers`, async () => {
    const events = []
    const messages = []
    const calls = []
    const queues = [new Map([['same', 'application']]), new Map([['same', 'extension'], ['worker', 'worker bytes']])]
    function request (command, params, options) {
      assert.equal(command, 'queuedResponse')
      const extension = options.useExtensionIPCIfAvailable
      calls.push([params.id, extension])
      const queue = queues[Number(extension)]
      if (!queue.has(params.id)) return { err: { name: 'NotFoundError', message: 'Missing response' } }
      const data = queue.get(params.id)
      queue.delete(params.id)
      return { data }
    }
    const runtime = runInNewContext(`${source.slice(start, end)}; new RuntimeQueuedResponses()`, {
      ConcurrentQueue: class { dispatchEvent (event) { events.push(event) } },
      ipc: { primordials: { platform }, request, sendSync: request },
      RuntimeWorker: { pool: new Map([['owned', { deref: () => ({ postMessage: message => messages.push(message) }) }]]) },
      CustomEvent: class { constructor (type, options) { this.type = type; this.detail = options.detail } },
      ErrorEvent: class { constructor (type, options) { this.type = type; this.error = options.error } },
      globalThis: { dispatchEvent: event => events.push(event) }
    })

    await Promise.all([
      runtime.dispatch('same', '-1', { source: 'fs.watch' }, [], { useExtensionIPCIfAvailable: true }),
      runtime.dispatch('same', '-1', { source: 'fs.watch' }, [])
    ])
    assert.deepEqual(events.map(event => event.detail.data), ['extension', 'application'])
    assert.deepEqual(calls, [['same', true], ['same', false]])
    await runtime.dispatch('worker', '-1', {}, [], { workerId: 'owned', useExtensionIPCIfAvailable: true })
    assert.equal(messages[0].__runtime_worker_event.detail.data, 'worker bytes')

    queues[0].set('missing', 'foreign bytes')
    await runtime.dispatch('missing', '-1', {}, [], { useExtensionIPCIfAvailable: true })
    assert.equal(events.at(-1).type, 'error')
    assert.equal(events.at(-1).error.name, 'NotFoundError')
    assert.equal(queues[0].get('missing'), 'foreign bytes', 'a missing native response cannot consume another transport')
    await runtime.dispatch('missing', '-1', {}, [])
    assert.equal(events.at(-1).detail.data, 'foreign bytes', 'failure does not block subsequent delivery')
  })
}
