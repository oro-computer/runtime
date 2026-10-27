import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const stdlib = process.env.ORO_KOTLIN_STDLIB
test('native workflow approval serializes dialogs and settles cancellation and stale documents', {
  skip: !stdlib ? 'Requires Kotlin/JVM and ORO_KOTLIN_STDLIB' : false
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-workflow-consent-'))
  const source = fs.readFileSync(new URL('../../src/runtime/app/native_workflows.kt', import.meta.url), 'utf8')
  const start = source.indexOf('internal object NativeConsent {')
  assert.ok(start > 0)
  const files = {
    'Consent.kt': 'package oro.runtime.app\n' + source.slice(start),
    'Cancellation.kt': `package android.os
class CancellationSignal {
 @Volatile var isCanceled=false
 private var listener:(()->Unit)?=null
 @Synchronized fun setOnCancelListener(next:(()->Unit)?) { listener=next;if(isCanceled) next?.invoke() }
 @Synchronized fun cancel() { isCanceled=true;listener?.invoke() }
}`,
    'Dialog.kt': `package androidx.appcompat.app
class AlertDialog {
 companion object { @Volatile var last:AlertDialog?=null }
 var positive:((Any?,Int)->Unit)?=null
 var negative:((Any?,Int)->Unit)?=null
 var dismissed:((Any?)->Unit)?=null
 var visible=false
 fun setOnDismissListener(next:(Any?)->Unit) { dismissed=next }
 fun show() { visible=true;last=this }
 fun dismiss() { if(visible) { visible=false;dismissed?.invoke(null) } }
 class Builder(activity:Any) {
  private val value=AlertDialog()
  fun setTitle(title:String)=this
  fun setMessage(message:String)=this
  fun setPositiveButton(text:String,next:(Any?,Int)->Unit)=apply { value.positive=next }
  fun setNegativeButton(text:String,next:(Any?,Int)->Unit)=apply { value.negative=next }
  fun create()=value
 }
}`,
    'Main.kt': `package oro.runtime.app
import java.util.concurrent.*
import java.util.concurrent.atomic.AtomicBoolean
import androidx.appcompat.app.AlertDialog
class PlatformFailure(val code:String):RuntimeException(code)
class AppActivity
class Owner(val activity:Any=AppActivity())
class PlatformCall {
 val done=AtomicBoolean(false)
 @Volatile var valid=true
 fun foreground():Owner { if(!valid) throw PlatformFailure("ENOTREADY");return Owner() }
}
object PlatformOperations {
 val main=Queue()
 class Queue {
  val pending=LinkedBlockingQueue<()->Unit>()
  fun post(work:()->Unit) { pending.put(work) }
  fun run() { (pending.poll(5,TimeUnit.SECONDS) ?: error("UI continuation missing"))() }
 }
}
fun main() {
 val workers=Executors.newFixedThreadPool(2)
 try {
  val call=PlatformCall();val signal=android.os.CancellationSignal()
  val first=workers.submit<Boolean> { NativeConsent.approve(call,signal,"public","https://public.invalid") }
  PlatformOperations.main.run();check(AlertDialog.last!!.visible)
  val competing=workers.submit<Boolean> { NativeConsent.approve(PlatformCall(),android.os.CancellationSignal(),"public","public") }
  PlatformOperations.main.run()
  try { competing.get(5,TimeUnit.SECONDS);error("concurrent dialog admitted") }
  catch(error:ExecutionException) { check((error.cause as PlatformFailure).code=="EBUSY") }
  PlatformOperations.main.run();check(AlertDialog.last!!.visible)
  signal.cancel();check(!first.get(5,TimeUnit.SECONDS))
  PlatformOperations.main.run();PlatformOperations.main.run();check(!AlertDialog.last!!.visible)
  val stale=PlatformCall();val next=workers.submit<Boolean> { NativeConsent.approve(stale,android.os.CancellationSignal(),"public","public") }
  PlatformOperations.main.run();stale.valid=false
  AlertDialog.last!!.positive!!(null,1);check(!next.get(5,TimeUnit.SECONDS));PlatformOperations.main.run()
  val accepted=workers.submit<Boolean> { NativeConsent.approve(PlatformCall(),android.os.CancellationSignal(),"public","public") }
  PlatformOperations.main.run();AlertDialog.last!!.positive!!(null,1)
  check(accepted.get(5,TimeUnit.SECONDS));PlatformOperations.main.run();check(!AlertDialog.last!!.visible)
  println("workflow-native-dialog-capacity-cancellation-document-fence-and-release-passed")
 } finally { workers.shutdownNow() }
}`
  }
  for (const [name, source] of Object.entries(files)) fs.writeFileSync(path.join(directory, name), source)
  const jar = path.join(directory, 'consent.jar')
  const compiled = spawnSync('kotlinc', [...Object.keys(files).map(name => path.join(directory, name)), '-classpath', stdlib, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, stdlib].join(path.delimiter), 'oro.runtime.app.MainKt'], { encoding: 'utf8', timeout: 20000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /workflow-native-dialog-capacity-cancellation-document-fence-and-release-passed/)
})
