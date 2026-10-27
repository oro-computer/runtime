import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const jars = ['ORO_KOTLIN_STDLIB', 'ORO_TEST_JSON_JAR', 'ORO_TEST_SQLITE_JAR', 'ORO_TEST_SLF4J_JAR'].map(key => process.env[key])
test('protected import bodies commit atomically at 64 KiB, survive lost replies and reject partial or foreign-origin reads', {
  skip: jars.some(value => !value) ? 'Requires configured Kotlin/JVM, JSON and SQLite test artifacts' : false,
  timeout: 90000
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-migration-storage-'))
  const fixture = fileURLToPath(new URL('../fixtures/android-runtime/state/', import.meta.url))
  const source = fs.readFileSync(new URL('../../src/runtime/app/migration.kt', import.meta.url), 'utf8')
  const start = source.indexOf('  private fun saveImport(')
  const end = source.indexOf('  private fun imported(', start)
  assert.ok(start > 0 && end > start)
  const canonicalStart = source.indexOf('  private fun canonical(')
  const helpers = source.slice(canonicalStart, start)
  const update = source.slice(source.indexOf('  internal fun update('), source.indexOf('  internal fun entries('))
  const sources = fs.readdirSync(fixture).filter(name => name.endsWith('.kt') && name !== 'Regression.kt').map(name => path.join(fixture, name))
  sources.push(fileURLToPath(new URL('../../src/runtime/app/native_records.kt', import.meta.url)))
  const main = path.join(directory, 'Main.kt')
  fs.writeFileSync(main, `package oro.runtime.app
import android.content.Context
import android.util.Base64
import android.database.sqlite.DatabaseFault
import org.json.JSONObject
import org.json.JSONArray
import java.util.UUID
import java.io.File
import java.security.Security
object MigrationPolicy { fun digest(value:ByteArray)=java.security.MessageDigest.getInstance("SHA-256").digest(value).joinToString(""){"%02x".format(it)} }
object Imports {
 private fun store(context:Context)=NativeRecords.runtime(context,"migration.v1")
 private fun owner(value:JSONObject,origin:String) { if(value.getString("documentOrigin")!=origin)throw NativeStateException("EACCES") }
 ${helpers}
 ${source.slice(start, end)}
 ${update}
 fun save(context:Context,id:String,bytes:ByteArray)=saveImport(context,id,JSONObject().put("documentOrigin","oro://public")
  .put("sealed",JSONObject().put("id",id).put("schema",1).put("policy","biometric").put("iv","public-iv").put("ciphertext",Base64.encodeToString(bytes,2))))
 fun open(context:Context,id:String,origin:String="oro://public")=Base64.decode(sealedImport(context,id,origin).second.getString("ciphertext"),2)
 fun snapshot(context:Context)=store(context).read()
 fun damage(context:Context,id:String) { val before=store(context).read();store(context).apply(before.revision,mapOf("import-body.$id.1" to null)) }
}
fun expected(code:String,work:()->Unit){try{work();error("Expected "+code)}catch(error:NativeStateException){check(error.code==code)}}
fun main(args:Array<String>){
 val root=File(args[0]).apply{mkdirs()};FixtureKeys.directory=File(root,"keys").apply{mkdirs()};Security.addProvider(FixtureProvider())
 check(Imports.revision(JSONObject("{\\"b\\":2,\\"a\\":1}"))==Imports.revision(JSONObject("{\\"a\\":1,\\"b\\":2}")))
 for(size in listOf(16,32768,32769,65552)){
  val context=Context(File(root,"size-"+size).apply{mkdirs()});val bytes=ByteArray(size){(it%251).toByte()}
  Imports.save(context,"public",bytes);check(Imports.open(context,"public").contentEquals(bytes))
  val snapshot=Imports.snapshot(context);check(snapshot.revision==1L && snapshot.records.values.all{it.size<=65536})
  check(!JSONObject(snapshot.records.getValue("import.public").toString(Charsets.UTF_8)).getJSONObject("sealed").has("ciphertext"))
  Imports.save(context,"public",ByteArray(size){99});check(Imports.open(context,"public").contentEquals(bytes));check(Imports.snapshot(context).revision==1L)
  expected("EACCES"){Imports.open(context,"public","oro://foreign")}
  if(size>32768){Imports.damage(context,"public");expected("EBADMSG"){Imports.open(context,"public")}}
 }
 val context=Context(File(root,"faults").apply{mkdirs()});val bytes=ByteArray(65552){7}
 DatabaseFault.beforeCommit=true;expected("EUNKNOWN"){Imports.save(context,"public",bytes)};check(Imports.snapshot(context).records.isEmpty())
 DatabaseFault.afterCommit=true;expected("EUNKNOWN"){Imports.save(context,"public",bytes)};check(Imports.open(context,"public").contentEquals(bytes))
 val revision=Imports.snapshot(context).revision;Imports.save(context,"public",bytes);check(Imports.snapshot(context).revision==revision)
 val stale=Context(File(root,"stale").apply{mkdirs()});var calls=0
 expected("ESTALE"){Imports.update(stale,"public"){calls++;throw NativeStateException("ESTALE")}};check(calls==1)
 var attempts=0
 val updated=Imports.update(stale,"public"){
  attempts++;if(attempts==1)Imports.save(stale,"concurrent",ByteArray(16){1})
  JSONObject().put("attempt",attempts)
 }
 check(attempts==2 && updated.getInt("attempt")==2)
 println("atomic-protected-import-bounds-canonical-identity-origin-partial-body-and-lost-reply-passed")
}`)
  sources.push(main)
  const classpath = jars.join(path.delimiter)
  const jar = path.join(directory, 'migration.jar')
  const built = spawnSync('kotlinc', [...sources, '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const run = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.MainKt', path.join(directory, 'state')], { encoding: 'utf8', timeout: 20000 })
  assert.equal(run.status, 0, run.stdout + run.stderr)
  assert.match(run.stdout, /atomic-protected-import-bounds-canonical-identity-origin-partial-body-and-lost-reply-passed/)
})
