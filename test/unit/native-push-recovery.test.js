import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const available = stdlib && json && spawnSync('kotlinc', ['-version'], { stdio: 'ignore' }).status === 0
test('Android native push recovery scopes redacted snapshots and fences stale cleanup without resetting ciphertext', {
  skip: available ? false : 'Requires kotlinc, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR', timeout: 60000
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-push-recovery-'))
  const source = fs.readFileSync(new URL('../../src/runtime/app/push.kt', import.meta.url), 'utf8')
  const between = (start, end) => {
    const a = source.indexOf(start)
    const b = source.indexOf(end, a)
    assert.ok(a > 0 && b > a)
    return source.slice(a, b)
  }
  const methods = between('  private fun save (', '  /** Installation-wide native policy') +
    between('  /** Stops future delivery', '  /** Persists authenticated transport')
  const file = path.join(directory, 'Recovery.kt')
  fs.writeFileSync(file, `package oro.runtime.app
import org.json.JSONObject
import org.json.JSONArray
import java.util.UUID
class Context
class PlatformFailure(val code: String): Exception(code)
object NativePushJournal {
 fun alias(context: Context) = "public"
 fun commit(context: Context, editor: Preferences.Editor) { check(editor.commit()) }
}
class Preferences {
 val values = mutableMapOf<String,String>()
 val all get() = values.toMap()
 fun getString(key:String, fallback:String?):String? = values[key] ?: fallback
 fun edit() = Editor(this)
 class Editor(val owner:Preferences) {
  val changes = mutableMapOf<String,String?>()
  fun putString(key:String,value:String) = apply { changes[key] = value }
  fun remove(key:String) = apply { changes[key] = null }
  fun commit():Boolean { changes.forEach { (key,value) -> if(value==null) owner.values.remove(key) else owner.values[key]=value }; return true }
 }
}
object PushCryptography {
 var locked = false
 fun seal(value:JSONObject,key:String,create:Boolean,alias:String="public") = value.toString()
 fun open(value:String,key:String,alias:String="public"):JSONObject { if(locked) throw PlatformFailure("EKEYREVOKED"); return JSONObject(value) }
}
object NativePush {
 val preferences = Preferences()
 val active = mutableMapOf<String,Boolean>()
 var cancelled = 0
 var unavailable = false
 val sent = mutableListOf<String>()
 private fun store(context:Context) = preferences
 private fun keys(context:Context,prefix:String) = preferences.all.keys.filter { it.startsWith(prefix) }.sorted()
 private fun read(context:Context,key:String) = preferences.getString(key,null)?.let { PushCryptography.open(it,key) }
 private fun identifier(value:String):String { require(Regex("[a-zA-Z0-9_-]{1,128}").matches(value)); return value }
 private fun cancelSignal(value:Boolean) { cancelled++ }
 private fun send(context:Context,record:JSONObject,action:String) {
  check(preferences.getString("registration."+record.getString("id"),null)==null)
  if(unavailable) throw IllegalStateException("public-provider-unavailable")
  sent.add(record.getString("token"))
 }
 ${methods}
 fun seed(context:Context,id:String,account:String="account",legacy:Boolean=false) {
  val record = JSONObject().put("id",id).put("kind","public.alert").put("authorityRef","authority").put("accountRef",account)
   .put("phase","registered").put("targetRef","target").put("token",UUID.randomUUID().toString())
   .put("endpoint","public-test-private-endpoint").put("keys",JSONObject().put("auth","public-test-secret"))
  if(legacy) preferences.edit().putString("registration.$"+"id",record.toString()).commit()
  else save(context,"registration.$"+"id",record)
 }
}
fun rejected(code:String,body:()->Unit) { try {body(); error("Expected failure") } catch(error:PlatformFailure) {check(error.code==code)} }
fun main() {
 val context = Context()
 NativePush.seed(context,"public",legacy=true)
 NativePush.seed(context,"other","different")
 val original = NativePush.preferences.values.getValue("registration.public")
 val rows = NativePush.discoverRegistrations(context,"authority","account","public.alert")
 check(rows.length()==1)
 val saved = rows.getJSONObject(0)
 check(saved.keySet()==setOf("id","kind","phase","revision"))
 check(!rows.toString().contains("public-test-secret") && !rows.toString().contains("endpoint"))
 check(NativePush.preferences.values.getValue("registration.public")==original)
 val revision = saved.getString("revision")
 rejected("EAGAIN") { NativePush.unregisterIfCurrent(context,"authority","different","public.alert","public",revision) }
 NativePush.seed(context,"public")
 rejected("EAGAIN") { NativePush.unregisterIfCurrent(context,"authority","account","public.alert","public",revision) }
 val current = NativePush.discoverRegistrations(context,"authority","account","public.alert").getJSONObject(0).getString("revision")
 NativePush.active["public"] = true
 check(NativePush.unregisterIfCurrent(context,"authority","account","public.alert","public",current))
 check(NativePush.cancelled==1 && NativePush.sent.size==1)
 check(!NativePush.unregisterIfCurrent(context,"authority","account","public.alert","public",current))
 NativePush.seed(context,"public")
 rejected("EAGAIN") { NativePush.unregisterIfCurrent(context,"authority","account","public.alert","public",current) }
 val before = NativePush.preferences.values.toMap()
 PushCryptography.locked = true
 rejected("EKEYREVOKED") { NativePush.discoverRegistrations(context,"authority","account","public.alert") }
 check(NativePush.preferences.values==before)
 PushCryptography.locked = false
 val final = NativePush.discoverRegistrations(context,"authority","account","public.alert").getJSONObject(0).getString("revision")
 NativePush.unavailable = true
 rejected("EUNKNOWN") { NativePush.unregisterIfCurrent(context,"authority","account","public.alert","public",final) }
 check(NativePush.discoverRegistrations(context,"authority","account","public.alert").length()==0)
 check(NativePush.preferences.values.containsKey("registration.other"))
 println("native-push-redacted-scoped-generation-fenced-recovery-passed")
}`.replaceAll('"registration.$"+"id"', '"registration.$id"'))
  const jar = path.join(directory, 'recovery.jar')
  const cp = [stdlib, json].join(path.delimiter)
  const built = spawnSync('kotlinc', [file, '-classpath', cp, '-d', jar], { encoding: 'utf8', timeout: 45000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync('java', ['-cp', [jar, cp].join(path.delimiter), 'oro.runtime.app.RecoveryKt'], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /native-push-redacted-scoped-generation-fenced-recovery-passed/)
})
