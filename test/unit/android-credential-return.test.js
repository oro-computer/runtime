import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readdirSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const available = stdlib && json && ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('password provider returns wait for resume and retain document ownership, cancellation and bounded cleanup', {
  skip: !available ? 'Kotlin, JVM, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR are required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-credential-return-'))
  const fixtures = fileURLToPath(new URL('../fixtures/android-runtime/credentials/', import.meta.url))
  const sources = readdirSync(fixtures).filter(name => name.endsWith('.kt')).map(name => path.join(fixtures, name))
  sources.push(...['platform_operations', 'credentials'].map(name =>
    fileURLToPath(new URL(`../../src/runtime/app/${name}.kt`, import.meta.url))))
  const jar = path.join(directory, 'credentials.jar')
  const compiled = spawnSync('kotlinc', [...sources, '-classpath', [stdlib, json].join(path.delimiter), '-d', jar], {
    encoding: 'utf8', timeout: 60000
  })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib, json].join(path.delimiter), 'oro.runtime.app.RegressionKt'], {
    encoding: 'utf8', timeout: 15000
  })
  assert.equal(result.status, 0, result.stderr)
})
