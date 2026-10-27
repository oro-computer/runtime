package oro.runtime.app

import android.content.Context
import android.database.sqlite.DatabaseFault
import java.io.File
import java.security.KeyStore
import java.security.Security
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicInteger

private fun expect(code: String, action: () -> Unit) {
  try { action(); error("Expected $code") } catch (error: NativeStateException) { check(error.code == code) { "Expected $code, got ${error.code}" } }
}
fun main(args: Array<String>) {
  val root = File(args[0]).apply { mkdirs() }
  FixtureKeys.directory = File(root, "keys").apply { mkdirs() }; Security.addProvider(FixtureProvider())
  val context = Context(root)
  val identity = NativeRecordIdentity("public-server", "public-account", "public-key-epoch")
  val store = NativeRecords.open(context, "qualification.state", identity)
  val coordinator = NativeCoordinator(store)
  if (args[1] == "seed") {
    val prepared = coordinator.prepare("restart", "public-external-operation", 0)
    coordinator.begin(prepared.ticket)
    println("seeded-executing"); return
  }
  if (args[1] == "recover") {
    val old = coordinator.get("restart")!!
    check(old.phase == "executing" && !old.ownerProcessMatches)
    val unknown = coordinator.recover(old.ticket)
    val complete = coordinator.resolve(unknown.ticket, true, mapOf("result" to "public".toByteArray()), "public-receipt".toByteArray())
    check(complete.phase == "committed" && complete.receipt!!.toString(Charsets.UTF_8) == "public-receipt")
    check(store.read().records["result"]!!.toString(Charsets.UTF_8) == "public")
    println("recovered-without-replay"); return
  }
  check(store.read().revision == 0L && FixtureKeys.directory.list()!!.isEmpty())
  val value = "public %2F 🙂\u0000 bytes".toByteArray(Charsets.UTF_8)
  val first = store.apply(0, mapOf("value" to value), "first")
  check(first.revision == 1L && first.records["value"]!!.contentEquals(value))
  first.records["value"]!!.fill(0); check(store.read().records["value"]!!.contentEquals(value))
  expect("ENOSPC") { store.apply(first.revision, mapOf("oversized" to ByteArray(65537))) }
  expect("ENOSPC") { store.apply(first.revision, (1..129).associate { "record-$it" to byteArrayOf(1) }) }
  check(store.read().revision == first.revision)
  expect("ESTALE") { store.apply(0, mapOf("value" to "wrong".toByteArray())) }
  check(store.read().records["value"]!!.contentEquals(value))
  val successes = AtomicInteger(); val ready = CountDownLatch(1)
  val threads = (0 until 6).map { Thread {
    ready.await()
    try { store.apply(1, mapOf("race" to byteArrayOf(1))); successes.incrementAndGet() }
    catch (error: NativeStateException) { check(error.code in setOf("ESTALE", "EBUSY")) }
  }.apply { start() } }
  ready.countDown(); threads.forEach { it.join() }; check(successes.get() == 1)
  var revision = store.read().revision
  val migrated = store.migrate("legacy", "a".repeat(64), revision, mapOf("import" to value))
  val repeat = store.migrate("legacy", "a".repeat(64), migrated.revision, mapOf("import" to "ignored".toByteArray()))
  check(repeat.records["import"]!!.contentEquals(value))
  expect("EEXIST") { store.migrate("legacy", "b".repeat(64), repeat.revision, emptyMap()) }
  revision = store.read().revision
  DatabaseFault.beforeCommit = true
  expect("EUNKNOWN") { store.apply(revision, mapOf("value" to "not-committed".toByteArray()), "before") }
  check(store.read().revision == revision && store.read().records["value"]!!.contentEquals(value))
  DatabaseFault.afterCommit = true
  expect("EUNKNOWN") { store.apply(revision, mapOf("durable" to value), "lost-reply") }
  check(store.read().commitId == "lost-reply" && store.read().records["durable"]!!.contentEquals(value))
  val prepared = coordinator.prepare("slot", "public-operation", store.read().revision)
  val executing = coordinator.begin(prepared.ticket)
  expect("EINVAL") { coordinator.abort(executing.ticket) }
  val uncertain = coordinator.unknown(executing.ticket)
  expect("ESTALE") { coordinator.commit(executing.ticket, mapOf("late" to value)) }
  val committed = coordinator.resolve(uncertain.ticket, true, mapOf("committed" to value), value)
  check(store.read().records["committed"]!!.contentEquals(value) && committed.receipt!!.contentEquals(value))
  expect("EEXIST") { coordinator.prepare("slot", "other", committed.ticket.revision) }
  val lostPrepared = coordinator.prepare("lost-reply", "public-operation", store.read().revision)
  val lostExecuting = coordinator.begin(lostPrepared.ticket)
  DatabaseFault.afterCommit = true
  expect("EUNKNOWN") { coordinator.commit(lostExecuting.ticket, mapOf("lost-commit" to value), value) }
  val known = coordinator.get("lost-reply")!!
  check(known.phase == "committed" && known.receipt!!.contentEquals(value))
  check(store.read().records["lost-commit"]!!.contentEquals(value))
  coordinator.release(known.ticket)
  val other = NativeRecords.open(context, "qualification.state", identity.copy(accountRef = "other-account"))
  expect("EACCES") { NativeCoordinator(other).release(committed.ticket) }
  expect("ESTALE") { coordinator.release(committed.ticket) }
  coordinator.release(coordinator.get("slot")!!.ticket)
  val replaced = coordinator.prepare("slot", "replacement", store.read().revision)
  expect("ESTALE") { coordinator.abort(committed.ticket.copy(revision = replaced.ticket.revision)) }
  val aborted = coordinator.abort(replaced.ticket); coordinator.release(aborted.ticket)
  val otherValue = other.apply(0, mapOf("other" to value))
  val metadata = NativeRecords.database(context).use { db -> db.rawQuery("SELECT key_id FROM scopes WHERE scope = ?", arrayOf(store.scope)).use { it.moveToFirst(); it.getString(0) } }
  KeyStore.getInstance("AndroidKeyStore").apply { load(null); deleteEntry(NativeRecords.alias(store.scope, metadata)) }
  check(store.status().requiresRecovery)
  expect("EKEYREVOKED") { store.read() }
  expect("EKEYREVOKED") { store.apply(store.status().revision, emptyMap()) }
  val recovered = store.reset(store.status().revision)
  check(recovered.records.isEmpty() && recovered.revision > revision)
  check(other.read().records["other"]!!.contentEquals(otherValue.records["other"]!!))
  NativeRecords.collectRetiredKeys(context)
  val databaseBytes = File(context.noBackupFilesDir, "oro-native-state-v1.db").readBytes().toString(Charsets.ISO_8859_1)
  check(!databaseBytes.contains("public-account") && !databaseBytes.contains("public %2F"))
  println("native-state-matrix-passed")
}
