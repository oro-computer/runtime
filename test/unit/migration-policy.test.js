import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
const fixture = new URL('../fixtures/migration/public/migration.json', import.meta.url)

test('bundled historical selectors reject runtime alias injection, unbounded component ranges and invalid identity mappings', {
  skip: !process.env.ORO_KOTLIN_STDLIB || !process.env.ORO_TEST_JSON_JAR ? 'Requires Kotlin/JVM and JSON artifacts' : false
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-migration-policy-'))
  const policy = path.join(directory, 'Policy.kt')
  fs.copyFileSync(new URL('../../src/runtime/app/migration_policy.kt', import.meta.url), policy)
  const main = path.join(directory, 'Main.kt')
  fs.writeFileSync(main, `package oro.runtime.app
import org.json.JSONObject
import java.io.File
class NativeStateException(val code:String):Exception(code)
fun rejects(work:()->Unit) { check(runCatching(work).isFailure) }
fun main(args:Array<String>) {
 val text=File(args[0]).readText();val parsed=MigrationPolicy.parse(text)
 check(parsed.protected.size==4 && parsed.components.size==5)
 check(MigrationPolicy.expand("{package}:{sourceRef}","public.bundle","public-reference")=="public.bundle:public-reference")
 rejects { MigrationPolicy.expand("public.{sourceRef}","public.bundle","../other") }
 val source=JSONObject().put("generation","old-generation").put("nested",JSONObject().put("id","public-reference"))
 check(MigrationPolicy.pointer(source,"/nested/id")=="public-reference")
 rejects { MigrationPolicy.pointer(source,"/missing") }
 check(MigrationPolicy.generation(JSONObject(),source,"public-sha")=="public-sha")
 check(MigrationPolicy.generation(JSONObject().put("generationPointer","/generation"),source,"public-sha")=="old-generation")
 for(field in listOf("alias","ciphertextField")) {
  val bad=JSONObject(text);bad.getJSONArray("protected").getJSONObject(0).put(field,"oro.authentication.v1.forbidden/../")
  rejects { MigrationPolicy.parse(bad.toString()) }
 }
 val unknown=JSONObject(text).put("eval","code");rejects { MigrationPolicy.parse(unknown.toString()) }
 val range=JSONObject(text);range.getJSONArray("components").getJSONObject(1).put("maximumJobId",999999)
 rejects { MigrationPolicy.parse(range.toString()) }
 val auth=JSONObject(text);auth.getJSONArray("protected").getJSONObject(1).put("authentication","biometric")
 rejects { MigrationPolicy.parse(auth.toString()) }
 rejects { MigrationPolicy.parse("[".repeat(20)+"]".repeat(20)) }
 println("historical-policy-allowlist-bounds-identity-and-generation-passed")
}`)
  const classpath = [process.env.ORO_KOTLIN_STDLIB, process.env.ORO_TEST_JSON_JAR].join(path.delimiter)
  const jar = path.join(directory, 'policy.jar')
  const built = spawnSync('kotlinc', [policy, main, '-classpath', classpath, '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync('java', ['-cp', [jar, classpath].join(path.delimiter), 'oro.runtime.app.MainKt', fileURLToPath(fixture)], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
})
