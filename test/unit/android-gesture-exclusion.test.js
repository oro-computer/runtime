import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readdirSync, readFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import vm from 'node:vm'

test('gesture exclusion validates small edge regions before native IPC and preserves native errors', async () => {
  const calls = []
  let response = { active: true, osAcceptanceKnown: false }
  const context = vm.createContext({
    request: async (...args) => { calls.push(args); if (response instanceof Error) throw response; return response }
  })
  const source = readFileSync(new URL('../../api/window/appearance.js', import.meta.url), 'utf8')
    .replace(/^import .*\n/gm, '').replace(/^export default .*\n/gm, '').replaceAll('export async function', 'async function').replaceAll('export function', 'function')
  new vm.Script(`${source}\nglobalThis.api = { setGestureExclusion, getGestureExclusion }`).runInContext(context)
  const { api } = context
  for (const invalid of [null, {}, [{ edge: 'top', top: 0.1, height: 0.1 }], [{ edge: 'left', top: NaN, height: 0.1 }],
    [{ edge: 'left', top: 0.9, height: 0.2 }], [{ edge: 'left', top: 0.1, height: 0 }],
    [{ edge: 'left', top: 0.1, height: 0.1, password: 'never-serialized' }], Array(5).fill({ edge: 'left', top: 0, height: 0.01 })]) {
    await assert.rejects(api.setGestureExclusion(invalid), error => error.name === 'TypeError')
  }
  assert.equal(calls.length, 0)
  assert.equal((await api.setGestureExclusion([{ edge: 'left', top: 0.3, height: 0.1 }])).osAcceptanceKnown, false)
  assert.equal(calls[0][0], 'gestures')
  assert.equal(calls[0][1], 'set')
  await api.setGestureExclusion([])
  assert.equal(calls.at(-1)[2].regions.length, 0)
  response = Object.assign(new Error('Not active'), { code: 'ENOTREADY' })
  await assert.rejects(api.getGestureExclusion(), error => error === response)
})

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const available = stdlib && json && ['kotlinc', 'java'].every(command => spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)
test('native gesture policy bounds each edge, preserves mandatory regions and clears ownership/listeners', {
  skip: available ? false : 'Kotlin/JVM, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR are required'
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-gesture-exclusion-'))
  const fixture = fileURLToPath(new URL('../fixtures/android-runtime/gestures/', import.meta.url))
  const sources = readdirSync(fixture).filter(name => name.endsWith('.kt')).map(name => path.join(fixture, name))
  sources.push(fileURLToPath(new URL('../../src/runtime/app/gestures.kt', import.meta.url)))
  const jar = path.join(directory, 'gestures.jar')
  const classpath = [stdlib, json].join(path.delimiter)
  const built = spawnSync('kotlinc', [...sources, '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stderr)
  const result = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.RegressionKt'], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /gesture-ownership-policy-passed/)
})
