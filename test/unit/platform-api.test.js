import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

test('platform ESM exports preserve routing and support errors without an unreleased native entry point', () => {
  const program = `
import assert from 'node:assert/strict'
import fs from 'node:fs'
import vm from 'node:vm'
const calls = []
let result = { schema: 1, platform: 'linux', features: {} }
let failure = null
const context = vm.createContext({})
const operation = new vm.SyntheticModule(['default'], function () {
  this.setExport('default', async (...args) => { calls.push(args); if (failure) throw failure; return result })
}, { context })
const ipc = new vm.SyntheticModule(['default'], function () {
  this.setExport('default', { request: async (...args) => { calls.push(args); return failure ? { err: failure } : { data: result } } })
}, { context })
const platform = new vm.SourceTextModule(fs.readFileSync('api/platform.js', 'utf8'), { context, identifier: 'platform' })
assert.equal(fs.existsSync('api/native.js'), false)
assert.equal(fs.existsSync('api/native.d.ts'), false)
await platform.link(specifier => {
  if (specifier === './internal/platform-request.js') return operation
  if (specifier === './ipc.js') return ipc
  throw new Error('Unexpected platform dependency: ' + specifier)
})
await platform.evaluate()
const api = platform.namespace
for (const method of ['request', 'getSupport', 'getCapabilities']) {
  assert.equal(api.default[method], api[method])
}
assert.equal(await api.getCapabilities(), result)
assert.deepEqual(calls.at(-1), ['runtime.platform.support'])
result = { schema: 1, platform: 'ios', feature: 'foreground.continuous', implementation: 'restricted', reason: 'no-unbounded-background-execution' }
assert.equal(await api.getSupport('foreground.continuous'), result)
assert.equal(calls.at(-1)[0], 'runtime.platform.support')
assert.equal(calls.at(-1)[1].feature, 'foreground.continuous')
const payload = { public: true }
const signal = new AbortController().signal
await api.request('public.metadata', 'read', payload, { signal })
assert.equal(calls.at(-1)[0], 'extension')
assert.equal(calls.at(-1)[1], 'read')
assert.equal(calls.at(-1)[2].extension, 'public.metadata')
assert.equal(calls.at(-1)[2].payload, payload)
assert.equal(calls.at(-1)[3], signal)
const before = calls.length
for (const work of [() => api.request('../escape', 'read'), () => api.request('public', 'bad/action'), () => api.request('public', 'read', []), () => api.getSupport(), () => api.getSupport('../escape')]) {
  await assert.rejects(work, { name: 'TypeError' })
}
assert.equal(calls.length, before)
for (const code of ['ENOSYS', 'EACCES', 'ABORT_ERR', 'ESTALE']) {
  failure = Object.assign(new Error('public fixture'), { code })
  await assert.rejects(api.request('public.metadata', 'read'), error => error === failure)
  await assert.rejects(api.getSupport('extension'), error => error === failure)
  await assert.rejects(api.getCapabilities(), error => error === failure)
}
console.log('platform-module-routing-and-support-errors-passed')
`
  const result = spawnSync(process.execPath, ['--experimental-vm-modules', '--input-type=module', '-e', program], {
    cwd: fileURLToPath(new URL('../../', import.meta.url)), encoding: 'utf8', timeout: 15000
  })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /platform-module-routing-and-support-errors-passed/)
})
