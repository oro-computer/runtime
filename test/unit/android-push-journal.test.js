import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const xml = process.env.ORO_TEST_XML_JAR
test('Android journal inspection preserves malformed XML and failed-commit memory without replacing keys', {
  skip: !stdlib || !json || !xml ? 'Requires Kotlin, JSON and XML pull parser artifacts' : false
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-android-push-journal-'))
  const source = fs.readFileSync(new URL('../../src/runtime/app/push_journal.kt', import.meta.url), 'utf8')
    .replace('private external fun control (options: String, ciphertext: ByteArray): String',
      'private fun control (options: String, ciphertext: ByteArray): String = PublicLedgerSink.call(options, ciphertext)')
  const files = {
    'Journal.kt': source,
    'Keep.kt': 'package androidx.annotation\nannotation class Keep\n',
    'Xml.kt': 'package android.util\nobject Xml { fun newPullParser():org.xmlpull.v1.XmlPullParser = org.kxml2.io.KXmlParser() }\n',
    'Context.kt': `package android.content
import java.io.File
class Information(val dataDir:String)
class Context(val root:File) {
 companion object { const val MODE_PRIVATE=0 }
 val applicationContext get() = this
 val packageName = "public.application"
 val noBackupFilesDir get() = File(root,"no_backup")
 val applicationInfo get() = Information(root.path)
 val preferences = mutableMapOf<String,SharedPreferences>()
 var preferenceReads = 0
 fun getSharedPreferences(name:String,mode:Int):SharedPreferences { preferenceReads++; return preferences.getOrPut(name) { SharedPreferences() } }
}
class SharedPreferences {
 val all = mutableMapOf<String,Any>()
 var success = true
 fun edit() = Editor(this)
 class Editor(val owner:SharedPreferences) {
  val updates = mutableMapOf<String,Any>()
  fun putString(key:String,value:String) = apply { updates[key]=value }
  fun commit():Boolean { owner.all.putAll(updates); return owner.success }
 }
}
`,
    'Keys.kt': 'package android.security.keystore\nclass KeyPermanentlyInvalidatedException:Exception()\n',
    'Main.kt': `package oro.runtime.app
import android.content.Context
import org.json.JSONObject
import java.io.File
import java.io.DataInputStream
import java.io.ByteArrayInputStream
class PlatformFailure(val code:String):Exception(code)
object App { fun getInstance()=this; fun getNativeBuildIdentity()="{\\"nativePushJournalRecoveryProtocol\\":1}" }
object PushCryptography {
 var present = false
 var reads = 0
 fun hasStorageKey(alias:String):Boolean { reads++; return present }
 fun open(value:String,key:String,alias:String):JSONObject { require(value=="public-ciphertext");return JSONObject() }
}
object PublicLedgerSink {
 var epoch = ""
 var changed = false
 var captured = byteArrayOf()
 var calls = 0
 fun call(source:String,bytes:ByteArray):String {
  calls++;captured=bytes.copyOf()
  val options=JSONObject(source)
  val result=when(options.getString("action")) {
   "epoch" -> JSONObject().put("epoch",epoch)
   "inspect" -> options
   else -> JSONObject().put("changed",changed)
  }
  return JSONObject().put("data",result).toString()
 }
}
fun fails(code:String,work:()->Unit) { try { work();error("accepted") } catch(error:PlatformFailure) { check(error.code==code) } }
fun main(args:Array<String>) {
 val root=File(args[0]);val context=Context(root)
 val file=File(root,"shared_prefs/oro.push.v1.xml");file.parentFile.mkdirs()
 file.writeText("<map><string name=\\"registration.public\\">public-ciphertext</string></map>")
 val original=file.readBytes()
 val inspected=NativePushJournal.inspect(context)
 check(inspected.getString("reason")=="missing-key") { inspected.toString() }
 check(file.readBytes().contentEquals(original) && context.preferenceReads==0)
 val snapshot=DataInputStream(ByteArrayInputStream(PublicLedgerSink.captured))
 check(snapshot.readInt()==1 && snapshot.readBoolean())
 val copy=ByteArray(snapshot.readInt());snapshot.readFully(copy);check(copy.contentEquals(original))
 PushCryptography.present=true
 check(NativePushJournal.inspect(context).getString("reason").isEmpty())
 val preferences=NativePushJournal.preferences(context)
 preferences.success=false
 fails("EUNKNOWN") { NativePushJournal.commit(context, preferences.edit().putString("registration.public","new-ciphertext")) }
 fails("EUNKNOWN") { NativePushJournal.preferences(context) }
 check(NativePushJournal.inspect(context).getString("reason")=="uncertain-write")
 check(String(PublicLedgerSink.captured).contains("new-ciphertext"))
 val before=PublicLedgerSink.calls
 fails("EACCES") { NativePushJournal.quarantine(context,JSONObject(),true) }
 check(PublicLedgerSink.calls==before)
 val consent=JSONObject().put("installationScopeConfirmed",true).put("archiveAndStopLocalDeliveryConfirmed",true)
 NativePushJournal.quarantine(context,consent,true)
 fails("EUNKNOWN") { NativePushJournal.preferences(context) }
 PublicLedgerSink.changed=true
 NativePushJournal.quarantine(context,consent,true)
 file.copyTo(File(root,"original-ciphertext.xml"))
 file.writeText("public malformed XML")
 val malformed=file.readBytes();val reads=context.preferenceReads
 check(NativePushJournal.inspect(context).getString("reason")=="journal-format")
 fails("EBADMSG") { NativePushJournal.preferences(context) }
 check(file.readBytes().contentEquals(malformed) && context.preferenceReads==reads)
 println("android-push-journal-raw-xml-memory-quarantine-and-no-key-provisioning-passed")
}
`
  }
  for (const [name, text] of Object.entries(files)) fs.writeFileSync(path.join(directory, name), text)
  const jar = path.join(directory, 'journal.jar')
  const classpath = [stdlib, json, xml].join(path.delimiter)
  const compiled = spawnSync('kotlinc', [...Object.keys(files).map(name => path.join(directory, name)), '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.MainKt', directory], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /android-push-journal-raw-xml-memory-quarantine-and-no-key-provisioning-passed/)
})
