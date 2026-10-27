import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { createRequire } from 'node:module'
import vm from 'node:vm'
const require = createRequire(import.meta.url)
const { generate } = require('../../src/cli/android-migration.cjs')
const fixture = new URL('../fixtures/migration/public/migration.json', import.meta.url)

test('retained adapters preserve fixed component names, job permission and opaque policy without app native sources', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-migration-generator-'))
  const source = path.join(directory, 'definitions.json')
  const output = path.join(directory, 'main')
  const policy = JSON.parse(fs.readFileSync(fixture, 'utf8'))
  fs.writeFileSync(source, JSON.stringify(policy))
  const manifest = generate(source, output, 'oro.runtime.qualification.migration.MainActivity')
  assert.match(manifest, /public\.oro\.migration\.LegacyJob/)
  assert.match(manifest, /android.permission.BIND_JOB_SERVICE/)
  assert.match(manifest, /activity-alias/)
  const generated = fs.readFileSync(path.join(output, 'java/oro-migration/public/oro/migration/LegacyJob.kt'), 'utf8')
  assert.match(generated, /RetainedJobService\("job"\)/)
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(output, 'assets/oro-migration.json'), 'utf8')), policy)
  for (const change of [p => { p.components[0].className = '../Escape' }, p => { p.components[0].className = 'oro.runtime.app.App' }, p => { p.protected[0].alias = 'oro.authentication.v1.other' }, p => { p.components[1].maximumJobId = 500000 }, p => { p.components[0].actions = ['unsafe"/>'] }, p => { p.components.push(p.components[0]) }]) {
    const bad = structuredClone(policy); change(bad); fs.writeFileSync(source, JSON.stringify(bad))
    assert.throws(() => generate(source, output, 'oro.runtime.qualification.migration.MainActivity'))
  }
})

test('migration JS forwards owned cancellation and preserves unavailable, stale and private result boundaries', async () => {
  const source = fs.readFileSync(new URL('../../api/migration.js', import.meta.url), 'utf8')
  const calls = []
  let result = { available: true }
  let currentPlatform = 'android'
  let protocol = 1
  const context = vm.createContext({ platform: () => currentPlatform, platformRequest: async (...args) => { calls.push(args); if (args[1] === 'status') return { historicalMigrationProtocol: protocol }; if (result instanceof Error) throw result; return result } })
  const body = source.replace(/^import .*\n/gm, '').replace('export default', 'globalThis.api =').replaceAll('export async function', 'async function')
  new vm.Script(body).runInContext(context)
  await context.api.getSupport()
  assert.equal(calls[0][0], 'authentication')
  assert.equal(calls[0][1], 'status')
  assert.equal(calls[1][1], 'migration.support')
  const signal = new AbortController().signal
  const input = { profile: 'public', sourceRef: 'public', sourceSha256: 'a'.repeat(64), userInitiated: true }
  await context.api.importProtected(input, { signal })
  assert.equal(calls.at(-1)[3], signal)
  assert.equal(calls.at(-1)[2], input)
  result = Object.assign(new Error('stale'), { code: 'ESTALE' })
  await assert.rejects(context.api.activateHandover({ id: 'public', generation: 'old' }), error => error.code === 'ESTALE')
  result = Object.assign(new Error('missing backend'), { code: 'ENOSYS' })
  assert.equal((await context.api.getSupport()).available, false)
  await assert.rejects(context.api.openProtected({ handle: 'public', userInitiated: true }), error => error.code === 'ENOSYS')
  for (currentPlatform of ['linux', 'macos', 'ios', 'win32']) {
    const count = calls.length
    const support = await context.api.getSupport()
    assert.equal(support.available, false)
    assert.equal(support.platform, currentPlatform)
    assert.equal(support.implementation, 'missing')
    assert.equal(support.reason, 'migration-backend-unimplemented')
    assert.equal(calls.length, count)
    await assert.rejects(context.api.openProtected({ handle: 'public', userInitiated: true }), error => error.code === 'ENOSYS' && error.platform === currentPlatform && error.reason === support.reason)
    assert.equal(calls.length, count)
  }
  currentPlatform = 'android'
  protocol = undefined
  const previous = calls.length
  assert.equal((await context.api.getSupport()).reason, 'migration-protocol-unavailable')
  assert.equal(calls.length, previous + 1)
  assert.equal(calls.at(-1)[1], 'status')
  await assert.rejects(context.api.openProtected({ handle: 'public', userInitiated: true }), error => error.code === 'ENOSYS')
  assert.equal(calls.length, previous + 2)
})
