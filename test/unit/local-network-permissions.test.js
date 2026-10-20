import test from 'node:test'
import assert from 'node:assert/strict'
import { permissionsFixture } from '../fixtures/local-network-permissions.js'

test('local-network query and request use native permissions on Android', async () => {
  const fixture = permissionsFixture()
  const status = await fixture.query({ name: 'local-network' })
  assert.equal(status.state, 'prompt')
  fixture.state = 'granted'
  const granted = await fixture.request({ name: 'local-network' })
  assert.equal(granted.state, 'granted')
  assert.deepEqual(fixture.calls, [
    { route: 'permissions.query', name: 'local-network' },
    { route: 'permissions.request', name: 'local-network' }
  ])
  assert.equal(fixture.events[0].detail.state, 'granted')
  fixture.change('denied')
  assert.equal(status.state, 'denied')
  assert.equal(granted.state, 'denied')
})

test('local-network permission returns denial and propagates native failures', async () => {
  const fixture = permissionsFixture()
  fixture.state = 'denied'
  assert.equal((await fixture.request({ name: 'local-network' })).state, 'denied')
  const error = new Error('Permission service unavailable')
  fixture.error = error
  await assert.rejects(fixture.query({ name: 'local-network' }), candidate => candidate === error)
  await assert.rejects(fixture.request({ name: 'local-network' }), candidate => candidate === error)
})

test('local-network permission fails explicitly on other platforms', async () => {
  for (const platform of ['linux', 'darwin', 'ios', 'win32']) {
    const fixture = permissionsFixture(platform)
    await assert.rejects(fixture.query({ name: 'local-network' }), /only supported on Android/)
    await assert.rejects(fixture.request({ name: 'local-network' }), /only supported on Android/)
    assert.equal(fixture.calls.length, 0)
  }
})
