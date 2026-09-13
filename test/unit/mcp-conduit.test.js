import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const read = file => readFileSync(new URL(`../../api/${file}`, import.meta.url), 'utf8')
const conduit = read('conduit.js')
const codec = conduit.slice(conduit.indexOf('  encodeOption ('), conduit.indexOf('  send ('))
const mcp = read('mcp/index.js')
const transport = mcp.slice(mcp.indexOf('const textDecoder ='), mcp.indexOf('function normalizeMetadata ('))

test('MCP keeps server bearer tokens separate from Conduit correlation tokens', () => {
  const source = mcp.slice(mcp.indexOf('function buildRouteOptions ('), mcp.indexOf('function decodeConduitResult ('))
  const buildRouteOptions = vm.runInNewContext(`${source}; buildRouteOptions`, { IPCSearchParams: URLSearchParams })
  const anonymous = buildRouteOptions('mcp.server.start', {}, 'mcp-1', '2')
  const authenticated = buildRouteOptions('mcp.server.start', { token: 'configured-bearer-token' }, 'mcp-3', '2')
  assert.equal(anonymous.token, undefined, 'transport does not enable server bearer authentication')
  assert.equal(anonymous['ipc-token'], 'mcp-1')
  assert.equal(authenticated.token, 'configured-bearer-token', 'explicit bearer authentication is preserved')
  assert.equal(authenticated['ipc-token'], 'mcp-3')
})

test('MCP matches out-of-order Conduit replies without numeric token coercion', async () => {
  const clients = []
  const sent = []
  const timers = new Set()
  const ids = [1n, 18446744073709551615n, 9007199254740993n, 7n]
  const context = vm.createContext({
    EventTarget,
    MessageEvent,
    TextEncoder,
    TextDecoder,
    Uint8Array,
    clients,
    sent,
    rand64: () => ids.shift(),
    IPCSearchParams: URLSearchParams,
    assertObject: value => value && typeof value === 'object' ? value : {},
    Result: { from: value => value },
    ipc: { request () { assert.fail('Connected requests must use Conduit') } },
    setTimeout: callback => { timers.add(callback); return callback },
    clearTimeout: callback => timers.delete(callback)
  })
  vm.runInContext(`
    class Conduit extends EventTarget {
      constructor (options) {
        super()
        this.id = options.id
        this.isActive = true
        clients.push(this)
      }
      ${codec}
      send (options) { sent.push(options); return true }
    }
    const textEncoder = new TextEncoder()
    ${transport}
  `, context)
  const request = vm.runInContext('runtimeRequest', context)
  const responses = Promise.all([
    request('mcp.server.start', {}),
    request('mcp.server.start', { token: 'configured-bearer-token' }),
    request('mcp.server.stop', {})
  ])
  const client = clients[0]
  for (const [index, options] of [...sent.entries()].reverse()) {
    const payload = new TextEncoder().encode(JSON.stringify({
      source: options.route,
      ...(index === 1 ? { err: { message: 'Invalid schema' } } : { data: { index } })
    }))
    const frame = client.encodeMessage({ token: options['ipc-token'], route: options.route }, payload)
    client.dispatchEvent(new MessageEvent('message', { data: frame.buffer }))
  }
  // Expire any unmatched replies immediately so a regression cannot hang the test.
  for (const timeout of timers) timeout()
  const results = await responses
  assert.equal(results[0].data.index, 0)
  assert.equal(results[1].err.message, 'Invalid schema')
  assert.equal(results[2].data.index, 2)
  assert.equal(timers.size, 0, 'all reply deadlines were cleared')
})
