import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync } from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const compiler = process.env.ORO_KOTLIN_COMPILER_CP
const stdlib = process.env.ORO_KOTLIN_STDLIB
const json = process.env.ORO_TEST_JSON_JAR
const java = process.env.ORO_TEST_JAVA || 'java'

test('Kotlin distributor validates wire boundaries and retains acknowledged, duplicate and interrupted outcomes', {
  skip: !(compiler && stdlib && json) ? 'Requires ORO_KOTLIN_COMPILER_CP, ORO_KOTLIN_STDLIB and ORO_TEST_JSON_JAR' : false,
  timeout: 60000
}, () => {
  const directory = mkdtempSync(path.join(os.tmpdir(), 'oro-push-kotlin-'))
  const context = path.join(directory, 'Context.kt')
  const cases = path.join(directory, 'Cases.kt')
  const output = path.join(directory, 'cases.jar')
  // Model Android's commit failure: memory changes before disk persistence.
  // This is a storage algorithm check, not Android service/IPC qualification.
  writeFileSync(context, `package android.content
class Preferences {
  val memory = mutableMapOf<String, String>()
  val disk = mutableMapOf<String, String>()
  var failCommit = false
  fun getString(key: String, fallback: String?): String? = memory[key] ?: fallback
  fun edit() = Editor(this)
  class Editor(val parent: Preferences) {
    val updates = mutableMapOf<String, String>()
    fun putString(key: String, value: String): Editor { updates[key] = value; return this }
    fun commit(): Boolean {
      parent.memory.putAll(updates)
      if (parent.failCommit) return false
      parent.disk.putAll(parent.memory)
      return true
    }
  }
}
class Context(val preferences: Preferences = Preferences()) {
  companion object { const val MODE_PRIVATE = 0 }
  fun getSharedPreferences(name: String, mode: Int) = preferences
  fun restart(): Context = Context(Preferences().also { it.memory.putAll(preferences.disk); it.disk.putAll(preferences.disk) })
}
`)
  writeFileSync(cases, `import android.content.Context
import dev.oro.push.Protocol
import dev.oro.push.Store
import org.json.JSONObject
import java.util.Base64
fun initial(context: Context) {
  val state = Store.empty()
  for (token in listOf("alpha", "beta")) state.getJSONArray("registrations")
    .put(JSONObject().put("token", token).put("topic", "up-" + token))
  Store.save(context, state)
}
fun message(id: String, topic: String = "up-alpha") = JSONObject().put("id", id).put("topic", topic)
  .put("message", Base64.getEncoder().encodeToString(byteArrayOf(0, -1, 4))).put("encoding", "base64")
fun main() {
  check(!Protocol.text("", 100) && !Protocol.text("bad\\n", 100))
  check(Protocol.text("é".repeat(50), 100) && !Protocol.text("é".repeat(51), 100))
  check(Protocol.origin("https://127.0.0.1:8787/") == "https://127.0.0.1:8787")
  for (url in listOf("http://localhost", "https://u:p@localhost", "https://localhost/path", "https://localhost?x", "https://localhost/#x"))
    check(runCatching { Protocol.origin(url) }.isFailure)
  val topics = (0 until 1000).map { Protocol.topic() }
  check(topics.toSet().size == 1000 && topics.all { Regex("up[A-Za-z0-9_-]{27}").matches(it) })
  for (size in listOf(1, 4096)) {
    val bytes = ByteArray(size) { (it % 256).toByte() }
    check(Protocol.payload(Base64.getEncoder().encodeToString(bytes), "base64").contentEquals(bytes))
  }
  check(runCatching { Protocol.payload("", "") }.isFailure)
  check(runCatching { Protocol.payload("x".repeat(4097), "") }.isFailure)
  check(runCatching { Protocol.payload("x", "unknown") }.isFailure)
  val pair = java.security.KeyPairGenerator.getInstance("EC").apply { initialize(java.security.spec.ECGenParameterSpec("secp256r1")) }.generateKeyPair()
  val public = pair.public as java.security.interfaces.ECPublicKey
  fun coordinate(value: java.math.BigInteger) = value.toByteArray().takeLast(32).toByteArray().let { ByteArray(32 - it.size) + it }
  val point = byteArrayOf(4) + coordinate(public.w.affineX) + coordinate(public.w.affineY)
  check(Protocol.vapid(Base64.getUrlEncoder().withoutPadding().encodeToString(point)))
  check(!Protocol.vapid("short") && !Protocol.vapid(Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(65).also { it[0] = 4 })))
  val context = Context()
  initial(context)
  Store.receive(context, message("one"))
  Store.receive(context, message("one"))
  check(Store.read(context).getJSONArray("pending").length() == 1)
  val restarted = context.restart()
  check(Store.read(restarted).getJSONArray("pending").length() == 1)
  check(Store.read(restarted).getString("cursor") == "one")
  Store.acknowledge(restarted, "beta", "one")
  check(Store.read(restarted).getJSONArray("pending").length() == 1)
  Store.acknowledge(restarted, "alpha", "one")
  Store.receive(restarted, message("one"))
  check(Store.read(restarted).getJSONArray("pending").length() == 0)
  Store.receive(restarted, message("one", "up-beta"))
  check(Store.read(restarted).getJSONArray("pending").length() == 1)
  synchronized(Store) {
    val state = Store.read(restarted)
    Store.remove(state, "beta")
    Store.save(restarted, state)
  }
  check(Store.read(restarted).getJSONArray("pending").length() == 0)
  Store.receive(restarted, message("unknown", "up-beta"))
  check(Store.read(restarted).getString("cursor") == "one")
  val full = Context()
  initial(full)
  for (index in 0 until Protocol.PENDING) Store.receive(full, message("event-" + index))
  check(runCatching { Store.receive(full, message("overflow")) }.isFailure)
  check(Store.read(full).getJSONArray("pending").length() == Protocol.PENDING)
  check(Store.read(full).getString("cursor") == "event-255")
  val failure = Context()
  initial(failure)
  failure.preferences.failCommit = true
  check(runCatching { Store.receive(failure, message("interrupted")) }.isFailure)
  check(Store.read(failure.restart()).getString("cursor") == "")
  check(Store.read(failure.restart()).getJSONArray("pending").length() == 0)
  failure.preferences.failCommit = false
  Store.receive(failure, message("interrupted"))
  check(Store.read(failure.restart()).getJSONArray("pending").length() == 1)
  val recent = Context()
  initial(recent)
  for (index in 0 until 513) {
    Store.receive(recent, message("ack-" + index))
    Store.acknowledge(recent, "alpha", "ack-" + index)
  }
  check(Store.read(recent).getJSONArray("recent").length() == 512)
  val malformed = message("invalid").put("message", "bad!")
  Store.receive(recent, malformed)
  check(Store.read(recent).getJSONArray("pending").length() == 0)
  Store.receive(recent, message("after-invalid"))
  check(Store.read(recent).getJSONArray("pending").length() == 1)
  val inactive = Context()
  initial(inactive)
  val now = System.currentTimeMillis()
  synchronized(Store) {
    val snapshot = Store.read(inactive)
    Store.registration(snapshot, "alpha")!!.put("lastAcknowledgedAt", now - 30L * 24 * 60 * 60 * 1000 - 1)
    Store.save(inactive, snapshot)
  }
  val ping = Store.expire(inactive, now)
  check(ping.size == 1 && ping[0].first == "NEW_ENDPOINT")
  check(Store.expire(inactive, now + 29999).isEmpty())
  Store.acknowledge(inactive, "alpha", ping[0].second.getString("endpointId"))
  check(Store.expire(inactive, now + 40000).isEmpty())
  synchronized(Store) {
    val snapshot = Store.read(inactive)
    Store.registration(snapshot, "alpha")!!.put("lastAcknowledgedAt", now - 30L * 24 * 60 * 60 * 1000 - 1)
    Store.save(inactive, snapshot)
  }
  check(Store.expire(inactive, now).size == 1)
  check(Store.expire(inactive, now + 30000).single().first == "UNREGISTERED")
  check(Store.registration(Store.read(inactive), "alpha") == null)
  println("Kotlin wire and durable state regressions passed")
}
`)
  const root = fileURLToPath(new URL('../../', import.meta.url))
  const classpath = [stdlib, json].join(path.delimiter)
  const compiled = spawnSync(java, ['-classpath', compiler, 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-no-stdlib', '-no-reflect',
    '-classpath', classpath, '-d', output, path.join(root, 'src/cli/push-distributor/Protocol.kt'), path.join(root, 'src/cli/push-distributor/Store.kt'), context, cases], {
    encoding: 'utf8', timeout: 45000
  })
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
  const result = spawnSync(java, ['-classpath', [output, classpath].join(path.delimiter), 'CasesKt'], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /Kotlin wire and durable state regressions passed/)
})
