import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
function registration (filename, name) {
  const source = fs.readFileSync(new URL(`../../src/runtime/app/${filename}`, import.meta.url), 'utf8')
  const start = source.indexOf(`  fun ${name} (`)
  assert.ok(start > 0)
  const body = source.indexOf('{', start)
  let depth = 1
  let end = body + 1
  while (depth && end < source.length) {
    if (source[end] === '{') depth++
    if (source[end] === '}') depth--
    end++
  }
  return source.slice(start, end)
}

test('safe workflow reads recover within a capped budget and shared registries preserve independent application capacity', {
  skip: !stdlib || !json ? 'Requires Kotlin/JVM and JSON artifacts' : false, timeout: 90000
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-workflow-retry-'))
  fs.copyFileSync(new URL('../../src/runtime/app/workflow_policy.kt', import.meta.url), path.join(directory, 'Policy.kt'))
  fs.writeFileSync(path.join(directory, 'Main.kt'), `package oro.runtime.app
import org.json.JSONObject
class NativeStateException(val code:String):Exception(code)
interface ForegroundExecutor
interface DeferredReadExecutor
interface NativePushHandler
class Foreground {
 val executors=mutableMapOf<String,ForegroundExecutor>()
 ${registration('foreground.kt', 'registerExecutor')}
}
class Deferred {
 val executors=mutableMapOf<String,DeferredReadExecutor>()
 ${registration('deferred.kt', 'registerReadExecutor')}
}
class Push {
 val handlers=mutableMapOf<String,NativePushHandler>()
 ${registration('push.kt', 'registerHandler')}
}
fun rejected(work:()->Unit) { check(runCatching(work).isFailure) }
fun main() {
 for(reverse in listOf(false,true)) {
  val f=Foreground();val d=Deferred();val p=Push()
  val kinds=(0..15).map { "workflow.public"+it }+(0..15).map { "application.public"+it }
  for(kind in if(reverse) kinds.reversed() else kinds) {
   f.registerExecutor(kind,object:ForegroundExecutor{})
   d.registerReadExecutor(kind,object:DeferredReadExecutor{})
   p.registerHandler(kind,object:NativePushHandler{})
  }
  check(f.executors.size==32 && d.executors.size==32 && p.handlers.size==32)
  for(kind in listOf("workflow.extra","application.extra","workflow.public0","application.public0")) {
   rejected { f.registerExecutor(kind,object:ForegroundExecutor{}) }
   rejected { d.registerReadExecutor(kind,object:DeferredReadExecutor{}) }
   rejected { p.registerHandler(kind,object:NativePushHandler{}) }
  }
 }
 var clock=0L;var attempts=0;val waits=mutableListOf<Long>();val seen=mutableListOf<Int>()
 val recovered=WorkflowReadRetry.execute(10000,{clock},{waits.add(it);clock+=it},{attempt,_ -> seen.add(attempt)}) {
  attempts++;if(attempts<3) throw WorkflowReadFailure("EIO",true);"public recovery"
 }
 check(recovered=="public recovery" && attempts==3 && waits==listOf(1000L,2000L))
 check(seen.containsAll(listOf(1,2,3)))
 attempts=0;clock=0;waits.clear()
 rejected { WorkflowReadRetry.execute(10000,{clock},{waits.add(it);clock+=it},{_,_->}) {
  attempts++;throw WorkflowReadFailure("ETIMEDOUT",true)
 } }
 check(attempts==5 && waits==listOf(1000L,2000L,4000L,8000L))
 for(code in listOf("ETRUST","EACCES","EINVAL","EREDIRECT","E2BIG")) {
  attempts=0
  rejected { WorkflowReadRetry.execute(10000,{0L},{error("permanent failure retried")},{_,_->}) {
   attempts++;throw WorkflowReadFailure(code)
  } }
  check(attempts==1)
 }
 attempts=0;clock=0
 rejected { WorkflowReadRetry.execute(60000,{clock},{clock+=it},{_,_->}) { timeout ->
  attempts++;clock+=timeout;throw WorkflowReadFailure("ETIMEDOUT",true)
 } }
 check(attempts==2 && clock<=120000L)
 attempts=0
 rejected { WorkflowReadRetry.execute(10000,{0L},{throw NativeStateException("ABORT_ERR")},{_,_->}) {
  attempts++;throw WorkflowReadFailure("EIO",true)
 } }
 check(attempts==1)
 check(WorkflowPolicy.basePath("/gateway/team/")=="/gateway/team")
 check(WorkflowPolicy.basePath("/")=="")
 for(value in listOf("//evil.invalid","/../escape","/ok/../escape","/x%2fy","/a?b","/a#b","/a\\\\b","/a//b")) rejected { WorkflowPolicy.basePath(value) }
 println("workflow-read-recovery-budget-stop-permanent-failure-composition-and-prefix-bounds-passed")
}
`)
  const jar = path.join(directory, 'regression.jar')
  const classpath = [stdlib, json].join(path.delimiter)
  const compiled = spawnSync('kotlinc', [path.join(directory, 'Policy.kt'), path.join(directory, 'Main.kt'), '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.MainKt'], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /workflow-read-recovery-budget-stop-permanent-failure-composition-and-prefix-bounds-passed/)
})
