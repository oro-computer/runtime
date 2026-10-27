import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
test('every Application startup timing call is accepted before workflows/extensions run', {
  skip: !stdlib || !json ? 'Requires Kotlin/JVM and JSON artifacts' : false
}, () => {
  const source = fs.readFileSync(new URL('../../src/runtime/app/app.kt', import.meta.url), 'utf8')
  const object = source.match(/internal object RuntimeStartupTimings \{[\s\S]*?\n\}/)?.[0]
  assert.ok(object)
  const phases = [...source.matchAll(/RuntimeStartupTimings\.measure\("([^"]+)"/g)].map(match => match[1])
  assert.ok(phases.includes('native-workflows'))
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-startup-timings-'))
  fs.writeFileSync(path.join(directory, 'Timing.kt'), 'package oro.runtime.app\nimport org.json.JSONObject\nimport org.json.JSONArray\n' + object)
  fs.writeFileSync(path.join(directory, 'Platform.kt'), 'package android.os\nobject SystemClock { fun elapsedRealtimeNanos()=System.nanoTime() }\nobject Trace { fun beginSection(name:String) {};fun endSection() {} }\n')
  fs.writeFileSync(path.join(directory, 'Main.kt'), [
    'package oro.runtime.app',
    'fun main() {',
    ...phases.map(phase => 'check(RuntimeStartupTimings.measure(' + JSON.stringify(phase) + ') { 17 } == 17)'),
    'check(RuntimeStartupTimings.list().length()>0)',
    'println("all-actual-application-startup-phases-accepted-passed")',
    '}'
  ].join('\n'))
  const jar = path.join(directory, 'timings.jar')
  const classpath = [stdlib, json].join(path.delimiter)
  const compiled = spawnSync('kotlinc', ['Timing.kt', 'Platform.kt', 'Main.kt', '-classpath', classpath, '-d', jar], { cwd: directory, encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.MainKt'], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /all-actual-application-startup-phases-accepted-passed/)
})
