import { readFileSync } from 'node:fs'
import { isIPv4 } from 'node:net'
import vm from 'node:vm'

export function dgramHostnameFixture (type = 'udp4', useConduit = false) {
  const source = readFileSync(new URL('../../api/dgram.js', import.meta.url), 'utf8')
  const constants = source.slice(source.indexOf('const BIND_STATE_UNBOUND'), source.indexOf('const MAX_PORT'))
  const defaultAddress = source.slice(source.indexOf('function getDefaultAddress ('), source.indexOf('function getAddressFamily ('))
  const functions = source.slice(source.indexOf('async function bind ('), source.indexOf('async function close ('))
  const calls = []
  const fixture = { calls, error: null }
  const record = async (route, options) => {
    calls.push({ route, address: options.address })
    return { data: {} }
  }
  const context = vm.createContext({
    isIPv4,
    isFunction: value => typeof value === 'function',
    noop: () => {},
    console,
    dns: {
      lookup: async (host, options) => {
        calls.push({ route: 'dns.lookup', host, family: options.family })
        if (fixture.error) throw fixture.error
        return { address: options.family === 6 ? '2001:db8::1' : '192.0.2.1', family: options.family }
      }
    },
    ipc: { request: record, write: record },
    dc: { channel: () => ({ publish: () => {} }) },
    getSendBufferSize: async () => ({ data: { size: 65536 } }),
    getRecvBufferSize: async () => ({ data: { size: 65536 } })
  })
  Object.assign(fixture, vm.runInContext(`${constants}\n${defaultAddress}\n${functions}\n;({ bind, connect, send })`, context))
  fixture.socket = {
    id: '1',
    type,
    state: { bindState: 2, connectState: 0 },
    address: () => ({ port: 12345, address: type === 'udp6' ? '::' : '0.0.0.0' }),
    setSendBufferSize: async () => {},
    setRecvBufferSize: async () => {},
    conduit: {
      isActive: useConduit,
      send: options => {
        calls.push({ route: options.route, address: options.address })
        return true
      }
    }
  }
  return fixture
}
