import { readFileSync } from 'node:fs'
import vm from 'node:vm'
import Enumeration from '../../api/enumeration.js'

export function permissionsFixture (platform = 'android') {
  const filename = new URL('../../api/internal/permissions.js', import.meta.url)
  const source = readFileSync(filename, 'utf8')
  const end = source.indexOf('/**\n * An interface for querying and revoking permissions.')
  const body = source.slice(0, end)
    .replace(/^import .+\n/gm, '')
    .replace(/^export /gm, '')
  const calls = []
  const events = []
  const listeners = []
  const fixture = { state: 'prompt', error: null, calls, events }
  const context = vm.createContext({
    Event,
    EventTarget,
    CustomEvent,
    Enumeration,
    queueMicrotask,
    dispatchEvent: event => events.push(event),
    navigator: { permissions: { query: () => { throw new Error('Unexpected browser permission query') } } },
    os: { platform: () => platform },
    gc: { ref: () => {}, finalizer: Symbol('finalizer') },
    hooks: {
      onPermissionChange: listener => {
        listeners.push(listener)
        return () => {}
      }
    },
    ipc: {
      request: async (route, params) => {
        calls.push({ route, name: params.name })
        return fixture.error ? { err: fixture.error } : { data: { state: fixture.state } }
      }
    }
  })
  Object.assign(fixture, vm.runInContext(body + '\n;({ query, request })', context))
  fixture.change = state => {
    for (const listener of listeners) {
      listener({ detail: { name: 'local-network', state } })
    }
  }
  return fixture
}
