import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, readdirSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import vm from 'node:vm'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const sqlite = process.env.ORO_TEST_SQLITE_JAR
const slf4j = process.env.ORO_TEST_SLF4J_JAR
const available = stdlib && json && sqlite && slf4j && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('native records and coordinator use real SQLite CAS/rollback, encrypted isolation, migration and durable uncertain outcomes', {
  skip: available ? false : 'Kotlin/JVM, ORO_KOTLIN_STDLIB, ORO_TEST_JSON_JAR, ORO_TEST_SQLITE_JAR and ORO_TEST_SLF4J_JAR are required',
  timeout: 120000
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-state-'))
  const fixture = fileURLToPath(new URL('../fixtures/android-runtime/state', import.meta.url))
  const sources = readdirSync(fixture).filter(name => name.endsWith('.kt')).map(name => path.join(fixture, name))
  sources.push(fileURLToPath(new URL('../../src/runtime/app/native_records.kt', import.meta.url)))
  sources.push(fileURLToPath(new URL('../../src/runtime/app/native_coordination.kt', import.meta.url)))
  const jar = path.join(directory, 'state.jar')
  const classpath = [stdlib, json, sqlite, slf4j].join(path.delimiter)
  const built = spawnSync('kotlinc', [...sources, '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stderr)
  const run = (mode, root) => spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.RegressionKt', root, mode], {
    encoding: 'utf8', timeout: 30000
  })
  const matrix = run('matrix', path.join(directory, 'matrix'))
  assert.equal(matrix.status, 0, matrix.stdout + matrix.stderr)
  assert.match(matrix.stdout, /native-state-matrix-passed/)
  const seeded = run('seed', path.join(directory, 'restart'))
  assert.equal(seeded.status, 0, seeded.stderr)
  assert.match(seeded.stdout, /seeded-executing/)
  const recovered = run('recover', path.join(directory, 'restart'))
  assert.equal(recovered.status, 0, recovered.stdout + recovered.stderr)
  assert.match(recovered.stdout, /recovered-without-replay/)
})

test('foreground JS requires explicit intent and bounded references, forwards cancellation and preserves generation/status errors', async () => {
  const source = readFileSync(new URL('../../api/foreground.js', import.meta.url), 'utf8')
  const calls = []
  let response = { id: 'public', generation: '01234567-89ab-cdef-0123-456789abcdef', phase: 'starting' }
  const context = vm.createContext({
    request: async (...args) => { calls.push(args); if (response instanceof Error) throw response; return response }
  })
  const body = source.replace(/^import .*\n/gm, '').replace(/export default .*\n/, '').replaceAll('export async function', 'async function')
  new vm.Script(`${body}\nglobalThis.api = { getSupport, start, getStatus, list, stop, release }`).runInContext(context)
  const { api } = context
  const options = { id: 'public', kind: 'public.read', authorityRef: 'authority', accountRef: 'account', targetRef: 'target', operationRef: 'read', userInitiated: true }
  for (const bad of [null, { ...options, userInitiated: false }, { ...options, durationMs: 999 }, { ...options, durationMs: Infinity }, { ...options, durationMs: 1000.5 }, { ...options, password: 'never-serialized' }, { ...options, accountRef: '../other' }]) {
    await assert.rejects(api.start(bad), error => ['TypeError', 'RangeError'].includes(error.name))
  }
  assert.equal(calls.length, 0)
  const signal = new AbortController().signal
  const result = await api.start(options, { signal })
  assert.equal(result.phase, 'starting')
  assert.equal(calls[0][0], 'foreground')
  assert.equal(calls[0][1], 'start')
  assert.equal(calls[0][2].durationMs, 900000)
  assert.equal(calls[0][3], signal)
  await assert.rejects(api.stop('public', '-'.repeat(36)), error => error.name === 'TypeError')
  await api.stop('public', result.generation)
  assert.equal(calls.at(-1)[2].generation, result.generation)
  response = Object.assign(new Error('stale'), { code: 'ESTALE' })
  await assert.rejects(api.release('public', result.generation), error => error === response)
})

test('native foreground controller preserves stop generations, cleanup quarantine, OS rejection and process-loss outcomes', {
  skip: available ? false : 'Kotlin/JVM and the native-state fixture JARs are required',
  timeout: 120000
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-foreground-'))
  const fixture = fileURLToPath(new URL('../fixtures/android-runtime/state', import.meta.url))
  const sources = readdirSync(fixture).filter(name => name.endsWith('.kt') && !['Context.kt', 'Regression.kt'].includes(name)).map(name => path.join(fixture, name))
  sources.push(...readdirSync(path.join(fixture, 'foreground')).filter(name => name.endsWith('.kt')).map(name => path.join(fixture, 'foreground', name)))
  sources.push(fileURLToPath(new URL('../../src/runtime/app/native_records.kt', import.meta.url)))
  sources.push(fileURLToPath(new URL('../../src/runtime/app/foreground.kt', import.meta.url)))
  const jar = path.join(directory, 'foreground.jar')
  const classpath = [stdlib, json, sqlite, slf4j].join(path.delimiter)
  const built = spawnSync('kotlinc', [...sources, '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stderr)
  const run = (mode, root) => spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.ForegroundRegressionKt', root, mode], {
    encoding: 'utf8', timeout: 40000
  })
  const matrix = run('matrix', path.join(directory, 'matrix'))
  assert.equal(matrix.status, 0, matrix.stdout + matrix.stderr)
  assert.match(matrix.stdout, /foreground-lifecycle-matrix-passed/)
  const seeded = run('seed', path.join(directory, 'restart'))
  assert.equal(seeded.status, 0, seeded.stdout + seeded.stderr)
  const recovered = run('recover', path.join(directory, 'restart'))
  assert.equal(recovered.status, 0, recovered.stdout + recovered.stderr)
  assert.match(recovered.stdout, /foreground-restart-no-replay/)
  const continuous = (mode, root) => spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.ContinuousRegressionKt', root, mode], {
    encoding: 'utf8', timeout: 40000
  })
  const monitoring = continuous('matrix', path.join(directory, 'monitoring'))
  assert.equal(monitoring.status, 0, monitoring.stdout + monitoring.stderr)
  assert.match(monitoring.stdout, /continuous-lifecycle-matrix-passed/)
  const continuousSeed = continuous('seed', path.join(directory, 'monitoring-restart'))
  assert.equal(continuousSeed.status, 0, continuousSeed.stdout + continuousSeed.stderr)
  const continuousRecover = continuous('recover', path.join(directory, 'monitoring-restart'))
  assert.equal(continuousRecover.status, 0, continuousRecover.stdout + continuousRecover.stderr)
  assert.match(continuousRecover.stdout, /continuous-restart-no-replay/)
})

test('continuous JS has a separate admission namespace, rejects finite duration and retains exact-generation controls', async () => {
  const source = readFileSync(new URL('../../api/foreground/continuous.js', import.meta.url), 'utf8')
  const calls = []
  const generation = '01234567-89ab-cdef-0123-456789abcdef'
  const context = vm.createContext({ request: async (...args) => { calls.push(args); return { generation, phase: 'starting' } } })
  const body = source.replace(/^import .*\n/gm, '').replace(/export default .*\n/, '').replaceAll('export async function', 'async function')
  new vm.Script(`${body}\nglobalThis.api = { getSupport, start, getStatus, list, stop, release }`).runInContext(context)
  const { api } = context
  const options = { id: 'monitor', kind: 'public.monitor', authorityRef: 'authority', accountRef: 'account', targetRef: 'target', operationRef: 'monitor', userInitiated: true }
  for (const bad of [null, { ...options, durationMs: 0 }, { ...options, userInitiated: false }, { ...options, password: 'not-serialized' }, { ...options, kind: '../arbitrary' }]) {
    await assert.rejects(api.start(bad), error => error.name === 'TypeError')
  }
  assert.equal(calls.length, 0)
  const signal = new AbortController().signal
  await api.start(options, { signal })
  assert.equal(calls[0][1], 'continuous.start')
  assert.equal('durationMs' in calls[0][2], false)
  assert.equal(calls[0][3], signal)
  await api.getSupport()
  await api.getStatus('monitor')
  await api.list()
  await api.stop('monitor', generation)
  await api.release('monitor', generation)
  assert.deepEqual(calls.map(call => call[1]), ['continuous.start', 'continuous.support', 'continuous.status', 'continuous.list', 'continuous.stop', 'continuous.release'])
  await assert.rejects(api.stop('monitor', 'stale'), error => error.name === 'TypeError')
})
