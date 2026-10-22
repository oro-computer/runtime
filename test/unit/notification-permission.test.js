import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { runInNewContext } from 'node:vm'

const source = readFileSync(new URL('../../api/notification.js', import.meta.url), 'utf8')
const start = source.indexOf('  static async requestPermission (')
const end = source.indexOf('\n  #onclick', start)
assert.ok(start >= 0 && end > start)

function notification (permissions) {
  const state = { permission: 'default' }
  const Notification = runInNewContext(`(class Notification { ${source.slice(start, end)} })`, {
    permissions, state, isLinux: false, NativeNotification: null, AbortController
  })
  return { Notification, state }
}

for (const outcome of ['denied', 'granted', 'prompt']) {
  test(`Notification.requestPermission settles the ${outcome} dialog outcome`, async () => {
    let signal
    const { Notification, state } = notification({
      async query () { return { state: 'prompt' } },
      async request (descriptor, options) {
        assert.equal(descriptor.name, 'notifications')
        signal = options.signal
        return { state: outcome }
      }
    })
    const result = await Promise.race([
      Notification.requestPermission(),
      new Promise((resolve, reject) => { setTimeout(() => reject(new Error('Permission result did not settle')), 100) })
    ])
    assert.equal(result, outcome === 'prompt' ? 'default' : outcome)
    assert.equal(state.permission, result)
    assert.equal(signal.aborted, true)
  })
}

test('Notification.requestPermission releases observers when the native request fails', async () => {
  let signal
  const failure = new Error('Native request failed')
  const { Notification } = notification({
    async query () { return { state: 'prompt' } },
    async request (descriptor, options) {
      signal = options.signal
      throw failure
    }
  })
  await assert.rejects(Notification.requestPermission(), error => error === failure)
  assert.equal(signal.aborted, true)
})

test('Notification.requestPermission refreshes an existing grant without another dialog', async () => {
  let signal
  const { Notification, state } = notification({
    async query (descriptor, options) {
      signal = options.signal
      return { state: 'granted' }
    },
    async request () { throw new Error('Existing permission must not be requested again') }
  })
  assert.equal(await Notification.requestPermission(), 'granted')
  assert.equal(state.permission, 'granted')
  assert.equal(signal.aborted, true)
})
