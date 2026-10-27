import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createRequire } from 'node:module'
import { spawnSync } from 'node:child_process'

const require = createRequire(import.meta.url)
const { generate } = require('../../src/cli/android-migration.cjs')
const fixture = new URL('../fixtures/migration/source-selection/migration.json', import.meta.url)

test('one retained class selects guarded histories and bridge mode preserves policy bytes without emitting components', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-source-selection-'))
  const source = path.join(directory, 'migration.json')
  const policy = JSON.parse(fs.readFileSync(fixture, 'utf8'))
  fs.writeFileSync(source, JSON.stringify(policy))
  const bridge = path.join(directory, 'bridge'); const final = path.join(directory, 'final')
  assert.equal(generate(source, bridge, 'public.example.MainActivity', 'external'), '')
  assert.equal(fs.existsSync(path.join(bridge, 'java')), false)
  assert.match(generate(source, final, 'public.example.MainActivity'), /LegacyReceiver/)
  assert.deepEqual(fs.readFileSync(path.join(bridge, 'assets/oro-migration.json')), fs.readFileSync(path.join(final, 'assets/oro-migration.json')))
  assert.throws(() => generate(source, final, 'public.example.MainActivity', 'external'), /previously generated/)
  assert.throws(() => generate(source, bridge, 'public.example.MainActivity', 'other'), /mode/)
  for (const change of [
    p => { p.protected[1].provenance.legacy = 'missing' },
    p => { p.protected[1].provenance.markerKey = 'registry.v1' },
    p => { p.protected[1].provenance.digestFormat = 'plaintext' },
    p => { p.protected[1].provenance.format = 2147483648 },
    p => { p.protected[2].primary = 'selected' },
    p => { p.protected[2].fallback = 'records' },
    p => { p.protected[0].authentication = 'biometric' },
    p => { p.protected[0].key = '{sourceRef}' },
    p => { p.protected[2].sources = ['registry', 'records'] }
  ]) {
    const bad = structuredClone(policy); change(bad); fs.writeFileSync(source, JSON.stringify(bad))
    assert.throws(() => generate(source, bridge, 'public.example.MainActivity', 'external'))
  }
})

const jars = ['ORO_KOTLIN_STDLIB', 'ORO_TEST_JSON_JAR', 'ORO_TEST_SQLITE_JAR', 'ORO_TEST_SLF4J_JAR'].map(key => process.env[key])
test('actual historical source reader preserves native authority, provenance, tombstones, key loss and transition checks', {
  skip: jars.some(value => !value) ? 'Requires configured Kotlin/JVM, JSON and SQLite artifacts' : false,
  timeout: 90000
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-source-reader-'))
  const state = fileURLToPath(new URL('../fixtures/android-runtime/state/', import.meta.url))
  const source = fs.readFileSync(new URL('../../src/runtime/app/migration.kt', import.meta.url), 'utf8')
  const retained = fs.readFileSync(new URL('../../src/runtime/app/retained_components.kt', import.meta.url), 'utf8')
  const revision = retained.slice(retained.indexOf('  private fun policyRevision('), retained.indexOf('  private fun publicState(')).replace('private fun policyRevision', 'fun policyRevision')
  const canonical = source.slice(source.indexOf('  private fun canonical('), source.indexOf('  private fun saveImport('))
  const start = source.indexOf('  internal data class Source(')
  const end = source.indexOf('  internal fun origin(', start)
  assert.ok(start > 0 && end > start)
  const context = path.join(directory, 'Context.kt')
  fs.writeFileSync(context, `package android.content
import java.io.File
class Preferences {
 val values=linkedMapOf<String,Any>();var beforeRead:(()->Unit)?=null;var beforeAll:(()->Unit)?=null
 val all:Map<String,*> get(){beforeAll?.invoke();return values.toMap()}
 fun getString(key:String,fallback:String?):String? { beforeRead?.invoke();return values[key] as String? ?: fallback }
}
open class Context(val root:File) {
 companion object{const val MODE_PRIVATE=0}
 val applicationContext get()=this;val packageName="oro.runtime.public.state";val noBackupFilesDir get()=File(root,"no-backup")
 private val preferences=mutableMapOf<String,Preferences>()
 fun getSharedPreferences(name:String,mode:Int)=preferences.getOrPut(name){Preferences()}
}`)
  const main = path.join(directory, 'Main.kt')
  fs.writeFileSync(main, `package oro.runtime.app
import android.content.Context
import android.util.Base64
import java.io.File
import java.util.UUID
import java.security.Security
import java.security.KeyStore
import org.json.JSONObject
import org.json.JSONArray
object ProtectedAuthentication {
 fun legacyPlain(context:Context,definition:JSONObject,reference:String,envelope:JSONObject)=envelope.getString("clear").toByteArray()
}
internal object NativeMigration {
 lateinit var policy:MigrationPolicy.Definitions
 fun definitions()=policy
 private fun store(context:Context)=NativeRecords.runtime(context,"migration.v1")
 private val gate=Any()
 fun <T> locked(work:()->T):T=synchronized(gate){work()}
 ${source.slice(start, end)}
 ${canonical}
}
internal object RetainedPolicy { ${revision} }
fun expected(code:String,work:()->Unit){try{work();error("Expected "+code)}catch(error:NativeStateException){check(error.code==code){"Expected "+code+", got "+error.code}}}
fun main(args:Array<String>){
 val root=File(args[0]).apply{mkdirs()};FixtureKeys.directory=File(root,"keys").apply{mkdirs()};Security.addProvider(FixtureProvider())
 val text=File(args[1]).readText();NativeMigration.policy=MigrationPolicy.parse(text)
 val context=Context(root);val prefs=context.getSharedPreferences("qualification.history",0)
 val clear=JSONObject().put("clientId","public-target").put("generation","v1").toString()
 val original=JSONObject().put("clear",clear).toString()
 prefs.values["registry.public-target"]=original;prefs.values["unrelated"]=true
 val primary=NativeMigration.policy.protected.getValue("records")
 val legacy=NativeMigration.policy.protected.getValue("registry")
 val records=NativeMigration.recordSource(context,primary,"public-target")
 check(records.status().revision==0L)
 val beforeKeys=FixtureKeys.directory.list()!!.toSet()
 check(NativeMigration.source(context,"selected","public-target").raw==original)
 check(NativeMigration.sourceNames(context,"selected").first.getString("id")=="registry")
 check(records.status().revision==0L && beforeKeys==FixtureKeys.directory.list()!!.toSet())
 val component=NativeMigration.policy.components.getValue("receiver")
 val legacyRevision=RetainedPolicy.policyRevision(context,component,"public-target")
 val digest=MigrationPolicy.legacyDigest(prefs.all,legacy,context.packageName)
 val pairs=JSONArray().put(JSONArray().put("registry.public-target").put(original)).toString()
 check(digest==MigrationPolicy.digest(pairs.toByteArray()))
 val marker=JSONObject().put("format",1).put("sourceSha256",digest).toString().toByteArray()
 val staticBefore=NativeMigration.profileConfiguration("selected").toString()
 records.apply(0,mapOf("registry.public-target" to clear.toByteArray(),"migration.v1" to marker))
 val selected=NativeMigration.source(context,"selected","public-target")
 val nativeRevision=RetainedPolicy.policyRevision(context,component,"public-target")
 check(nativeRevision!=legacyRevision && selected.epoch==records.scope+":"+records.migrationEpoch())
 check(selected.definition.getString("id")=="records" && Base64.decode(selected.raw,2).toString(Charsets.UTF_8)==clear)
 check(NativeMigration.sourceNames(context,"selected").second.toSet()==setOf("registry.public-target","migration.v1"))
 records.apply(records.status().revision,mapOf("registry.public-target" to JSONObject(clear).put("cursor","progress").toString().toByteArray()))
 check(RetainedPolicy.policyRevision(context,component,"public-target")==nativeRevision)
 prefs.values["registry.public-target"]=original+" "
 expected("EEXIST"){NativeMigration.source(context,"selected","public-target")}
 expected("EEXIST"){NativeMigration.sourceNames(context,"selected")}
 prefs.values["registry.public-target"]=original
 records.apply(records.status().revision,mapOf("registry.public-target" to null))
 expected("ENOENT"){NativeMigration.source(context,"selected","public-target")}
 check(NativeMigration.sourceNames(context,"selected").second==listOf("migration.v1"))
 records.apply(records.status().revision,mapOf("migration.v1" to null))
 expected("EBADMSG"){NativeMigration.source(context,"selected","public-target")}
 records.apply(records.status().revision,mapOf("migration.v1" to "{}".toByteArray()))
 expected("EBADMSG"){NativeMigration.source(context,"selected","public-target")}
 records.apply(records.status().revision,mapOf("migration.v1" to marker,"registry.public-target" to clear.toByteArray()))
 var allReads=0
 prefs.beforeAll={allReads++;if(allReads==2)prefs.values["registry.public-target"]=original+" "}
 expected("ESTALE"){NativeMigration.source(context,"selected","public-target")}
 prefs.beforeAll=null;prefs.values["registry.public-target"]=original
 KeyStore.getInstance("AndroidKeyStore").apply{load(null);deleteEntry(NativeRecords.alias(records.scope,records.migrationEpoch()))}
 expected("EKEYREVOKED"){NativeMigration.source(context,"selected","public-target")}
 check(NativeMigration.profileConfiguration("selected").toString()==staticBefore)
 check(prefs.values["registry.public-target"]==original)
 val race=Context(File(root,"race").apply{mkdirs()});val old=race.getSharedPreferences("qualification.history",0)
 old.values["registry.public-target"]=original
 old.beforeRead={old.beforeRead=null;NativeMigration.recordSource(race,primary,"public-target").apply(0,emptyMap())}
 expected("ESTALE"){NativeMigration.source(race,"selected","public-target")}
 expected("EBADMSG"){NativeMigration.source(race,"selected","public-target")}
 prefs.values["registry.bad"]=false;expected("EBADMSG"){MigrationPolicy.legacyDigest(prefs.all,legacy,context.packageName)}
 prefs.values.remove("registry.bad")
 for(index in 0..31)prefs.values["registry.more-"+index]="public"
 expected("ENOSPC"){MigrationPolicy.legacyDigest(prefs.all,legacy,context.packageName)}
 val unicode=linkedMapOf("registry.😀" to "slash/quote\\"", "registry.\\uE000" to "public")
 val ordered=JSONArray().put(JSONArray().put("registry.\\uE000").put("public")).put(JSONArray().put("registry.😀").put("slash/quote\\""))
 check(MigrationPolicy.legacyDigest(unicode,legacy,context.packageName)==MigrationPolicy.digest(ordered.toString().toByteArray()))
 for(change in listOf<(JSONObject)->Unit>(
  {it.getJSONArray("protected").getJSONObject(1).getJSONObject("provenance").put("legacy","missing")},
  {it.getJSONArray("protected").getJSONObject(2).put("primary","selected")},
  {it.getJSONArray("protected").getJSONObject(1).getJSONObject("provenance").put("markerKey","registry.v1")})){
  val bad=JSONObject(text);change(bad);check(runCatching{MigrationPolicy.parse(bad.toString())}.isFailure)
 }
 println("source-selection-native-authority-provenance-tombstones-key-loss-and-races-passed")
}`)
  const sources = fs.readdirSync(state).filter(name => name.endsWith('.kt') && !['Regression.kt', 'Context.kt'].includes(name)).map(name => path.join(state, name))
  sources.push(context, main, fileURLToPath(new URL('../../src/runtime/app/native_records.kt', import.meta.url)), fileURLToPath(new URL('../../src/runtime/app/migration_policy.kt', import.meta.url)))
  const classpath = jars.join(path.delimiter); const jar = path.join(directory, 'reader.jar')
  const built = spawnSync('kotlinc', [...sources, '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.MainKt', path.join(directory, 'state'), fileURLToPath(fixture)], { encoding: 'utf8', timeout: 20000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /source-selection-native-authority-provenance-tombstones-key-loss-and-races-passed/)
})
