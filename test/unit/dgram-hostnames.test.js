import test from 'node:test'
import assert from 'node:assert/strict'
import { dgramHostnameFixture } from '../fixtures/dgram-hostnames.js'

test('UDP bind, connect and send pass resolved address strings to native routes', async () => {
  for (const type of ['udp4', 'udp6']) {
    for (const method of ['bind', 'connect', 'send']) {
      for (const conduit of method === 'send' ? [false, true] : [false]) {
        const fixture = dgramHostnameFixture(type, conduit)
        const result = await fixture[method](fixture.socket, {
          address: 'lab.example', port: 53212, buffer: Buffer.from('bootstrap')
        })
        assert.equal(result.err, undefined)
        const family = type === 'udp6' ? 6 : 4
        assert.equal(fixture.calls[0].family, family)
        const native = fixture.calls.find(call => call.route === `udp.${method}`)
        assert.equal(native.address, family === 6 ? '2001:db8::1' : '192.0.2.1')
      }
    }
  }
})

test('UDP preserves literal IPv4 and IPv6 addresses without DNS queries', async () => {
  for (const address of ['192.0.2.1', '2001:db8::1']) {
    const fixture = dgramHostnameFixture(address.includes(':') ? 'udp6' : 'udp4')
    await fixture.send(fixture.socket, { address, port: 53212, buffer: Buffer.from('bootstrap') })
    assert.equal(fixture.calls.some(call => call.route === 'dns.lookup'), false)
    assert.equal(fixture.calls[0].address, address)
  }
})

test('UDP hostname lookup errors stop before native bind, connect or send', async () => {
  for (const method of ['bind', 'connect', 'send']) {
    const fixture = dgramHostnameFixture()
    fixture.error = new Error('getaddrinfo ENOTFOUND lab.example')
    let callbackError
    const result = await fixture[method](fixture.socket, {
      address: 'lab.example', port: 53212, buffer: Buffer.from('bootstrap')
    }, error => { callbackError = error })
    assert.equal(result.err, fixture.error)
    assert.equal(callbackError, fixture.error)
    assert.equal(fixture.calls.length, 1)
  }
})
