import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
test('bundled workflow policy bounds reads, validates scope before planning effects and excludes arbitrary code', {
  skip: !stdlib || !json ? 'Requires Kotlin/JVM and JSON artifacts' : false
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-workflow-policy-'))
  const source = fs.readFileSync(new URL('../../src/runtime/app/workflow_policy.kt', import.meta.url), 'utf8')
  const definition = fs.readFileSync(new URL('../fixtures/workflows/read-notify/workflows.json', import.meta.url), 'utf8')
  fs.writeFileSync(path.join(directory, 'Policy.kt'), source)
  fs.writeFileSync(path.join(directory, 'workflows.json'), definition)
  fs.writeFileSync(path.join(directory, 'Main.kt'), [
    'package oro.runtime.app',
    'import org.json.JSONObject',
    'import org.json.JSONArray',
    'import java.io.File',
    'class NativeStateException(val code:String):Exception(code)',
    'fun rejected(work:()->Unit) { check(runCatching(work).isFailure) }',
    'fun main(args:Array<String>) {',
    ' val text=File(args[0]).readText();val policies=WorkflowPolicy.parse(text);check(policies.size==3)',
    ' val policy=policies[0];check(policy.pollIntervalMs==5000)',
    ' val prefix=JSONObject(text);prefix.getJSONArray("workflows").getJSONObject(2).getJSONObject("request").put("allowApprovedBasePath",true)',
    ' check(WorkflowPolicy.parse(prefix.toString())[2].allowApprovedBasePath)',
    ' prefix.getJSONArray("workflows").getJSONObject(0).getJSONObject("request").put("allowApprovedBasePath",true)',
    ' rejected { WorkflowPolicy.parse(prefix.toString()) }',
    ' check(policies[2].approvedPath=="/public/alerts" && policies[2].url.isEmpty())',
    ' val unsafe=JSONObject(text);unsafe.getJSONArray("workflows").getJSONObject(2).getJSONObject("request").put("path","//unapproved.invalid/events")',
    ' rejected { WorkflowPolicy.parse(unsafe.toString()) }',
    ' rejected { WorkflowPolicy.parse(JSONObject(text).put("schema",1.5).toString()) }',
    ' val scope=JSONObject().put("authorityRef","public-authority").put("accountRef","public-account").put("targetRef","public-target")',
    ' val event=JSONObject().put("id","public-event").put("accountRef","public-account").put("targetRef","public-target").put("title","Public unicode 🎈").put("body","Public body")',
    ' val response=JSONObject().put("alerts",JSONArray().put(event)).put("cursor","public-cursor")',
    ' val plan=policy.plan(response.toString().toByteArray(),scope)',
    ' check(plan.events.size==1 && plan.cursor=="public-cursor")',
    ' check(plan.events[0].presentation.getString("title")=="Public unicode 🎈")',
    ' check(plan.events[0].presentation.getJSONObject("publicVersion").getString("title")=="Public workflow check")',
    ' rejected { policy.plan(response.toString().toByteArray(),JSONObject(scope.toString()).put("accountRef","different")) }',
    ' rejected { policy.plan(JSONObject(response.toString()).put("alerts",JSONArray().put(event).put(event)).toString().toByteArray(),scope) }',
    ' rejected { policy.plan(JSONObject(response.toString()).put("alerts",JSONArray((0..32).map { event })).toString().toByteArray(),scope) }',
    ' rejected { policy.plan(byteArrayOf(-64,-128),scope) }',
    ' rejected { policy.plan(("[".repeat(33)+"]".repeat(33)).toByteArray(),scope) }',
    ' rejected { WorkflowPolicy.parse(JSONObject(text).put("eval","code").toString()) }',
    ' val changed=JSONObject(text);changed.getJSONArray("workflows").getJSONObject(0).getJSONObject("request").put("url","http://localhost/events")',
    ' rejected { WorkflowPolicy.parse(changed.toString()) }',
    ' val revised=JSONObject(text);revised.getJSONArray("workflows").getJSONObject(0).getJSONObject("request").put("url","https://other.invalid/events")',
    ' check(WorkflowPolicy.parse(revised.toString())[0].revision!=policy.revision)',
    ' val noScope=JSONObject(text);noScope.getJSONArray("workflows").getJSONObject(1).getJSONObject("response").put("checks",JSONArray())',
    ' rejected { WorkflowPolicy.parse(noScope.toString()) }',
    ' val fractional=JSONObject(text);fractional.getJSONArray("workflows").getJSONObject(0).getJSONObject("request").put("timeoutMs",1.5)',
    ' rejected { WorkflowPolicy.parse(fractional.toString()) }',
    ' println("bounded-bundled-workflow-scope-identity-private-public-planning-and-policy-revision-passed")',
    '}'
  ].join('\n'))
  const jar = path.join(directory, 'policy.jar')
  const classpath = [stdlib, json].join(path.delimiter)
  const compiled = spawnSync('kotlinc', [path.join(directory, 'Policy.kt'), path.join(directory, 'Main.kt'), '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.MainKt', path.join(directory, 'workflows.json')], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /bounded-bundled-workflow-scope-identity-private-public-planning-and-policy-revision-passed/)
})
