import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/internal/init.js', import.meta.url), 'utf8')
const start = source.indexOf('class RuntimeQueuedResponses extends ConcurrentQueue')
const end = source.indexOf('\nhooks.onLoad(', start)
assert.ok(start >= 0 && end > start)

for (const platform of ['android', 'win32', 'linux', 'ios']) {
  test(`queued binary responses preserve bytes and order on ${platform}`, async () => {
    const events = []
    const calls = []
    const payload = Uint8Array.from([0, 255, 128, 65]).buffer
    const context = vm.createContext({
      ConcurrentQueue: class {},
      RuntimeWorker: { pool: new Map() },
      CustomEvent: class {
        constructor (type, { detail }) { this.type = type; this.detail = detail }
      },
      dispatchEvent: event => events.push(event),
      ipc: {
        primordials: { platform },
        async request (command, params, options) {
          calls.push(['async', command, params.id, options.responseType])
          await new Promise(resolve => setTimeout(resolve, params.id === 1 ? 10 : 0))
          return { data: payload }
        },
        sendSync (command, params, options) {
          assert.ok(!['android', 'win32'].includes(platform), 'Chromium documents cannot read binary sync XHR')
          calls.push(['sync', command, params.id, options.responseType])
          return { data: payload }
        }
      }
    })
    const queue = vm.runInContext(`${source.slice(start, end)}; new RuntimeQueuedResponses()`, context)
    await Promise.all([queue.dispatch(1, '-1', {}, {}), queue.dispatch(2, '-1', {}, {})])
    assert.deepEqual(events.map(event => event.detail.id), [1, 2])
    for (const event of events) {
      assert.equal(event.type, 'data')
      assert.deepEqual(new Uint8Array(event.detail.data), new Uint8Array(payload))
    }
    assert.deepEqual(calls.map(call => call.slice(1)), [
      ['queuedResponse', 1, 'arraybuffer'], ['queuedResponse', 2, 'arraybuffer']
    ])
  })
}
