package oro.runtime.qualification.extensions

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.CancellationSignal
import org.json.JSONArray
import org.json.JSONObject
import oro.runtime.app.*
import java.security.KeyStore
import java.util.UUID

/** Public state/executor fixture; DUMP-protected receiver and isolated package only. */
class StateQualificationExtension : NativeExtension {
  override val name = "qualification.state"
  override fun onCreate (app: App) {
    require(app.packageName.startsWith("oro.runtime.qualification."))
    for (controller in listOf(ForegroundTasks, ContinuousTasks)) {
      for (kind in listOf("qualification.wait", "qualification.complete")) {
        controller.registerExecutor(kind, ForegroundExecutor { context, task, signal ->
          val preferences = context.getSharedPreferences(if (controller === ContinuousTasks) "oro.public.continuous" else "oro.public.foreground", Context.MODE_PRIVATE)
          object : AuthorizedForegroundExecution {
            override fun execute (cancellation: CancellationSignal): ForegroundOutcome {
              check(preferences.edit().putString("running", task.id).putInt("runs", preferences.getInt("runs", 0) + 1).commit())
              if (kind == "qualification.complete") return ForegroundOutcome.COMPLETED
              while (true) { cancellation.throwIfCanceled(); Thread.sleep(20) }
            }
            override fun close () {
              check(preferences.edit().putString("closed", task.id).putInt("closes", preferences.getInt("closes", 0) + 1).commit())
            }
          }
        })
      }
    }
  }
  override fun request (call: NativeExtensionCall, action: String, options: JSONObject) {
    require(options.length() == 0)
    call.foregroundActivity()
    Thread {
      try { call.success(PublicNativeState.run(call.context, action))
      } catch (error: NativeStateException) { call.failure(error.code)
      } catch (_: Exception) { call.failure("EIO") }
    }.start()
  }
}

internal object PublicNativeState {
  private fun expect (code: String, action: () -> Unit) {
    try { action(); error("Expected public failure") } catch (error: NativeStateException) { check(error.code == code) }
  }
  fun run (context: Context, action: String): JSONObject {
    require(context.packageName.startsWith("oro.runtime.qualification."))
    val checks = mutableListOf<String>()
    val identity = NativeRecordIdentity("public-authority", "public-account", "public-identity")
    if (action in setOf("seed", "recover")) {
      val store = NativeRecords.open(context, "qualification.restart", identity)
      val coordinator = NativeCoordinator(store)
      if (action == "seed") {
        check(coordinator.get("restart") == null)
        val prepared = coordinator.prepare("restart", "public-test", store.read().revision)
        coordinator.begin(prepared.ticket)
        checks.add("durable-executing-intent-seeded")
      } else {
        val old = checkNotNull(coordinator.get("restart"))
        check(old.phase == "executing" && !old.ownerProcessMatches)
        val recovered = coordinator.recover(old.ticket)
        val done = coordinator.resolve(recovered.ticket, true, mapOf("public" to "public-result".toByteArray()), "public-receipt".toByteArray())
        check(done.phase == "committed" && done.receipt!!.toString(Charsets.UTF_8) == "public-receipt")
        coordinator.release(done.ticket)
        checks.addAll(listOf("new-process-retains-executing", "explicit-recovery-no-replay", "atomic-record-and-receipt", "terminal-release"))
      }
      return JSONObject().put("checks", JSONArray(checks))
    }
    require(action == "run")
    val store = NativeRecords.open(context, "qualification.records", identity.copy(identityRef = UUID.randomUUID().toString()))
    check(store.read().revision == 0L && !store.status().keyPresent)
    val bytes = "Public native-only record %2F 🙂\u0000".toByteArray(Charsets.UTF_8)
    var current = store.apply(0, mapOf("public" to bytes), "public-first")
    check(current.records["public"]!!.contentEquals(bytes)); checks.add("real-keystore-sqlite-roundtrip")
    expect("ESTALE") { store.apply(0, mapOf("public" to byteArrayOf(1))) }; checks.add("stale-cas-refused")
    val coordinator = NativeCoordinator(store)
    val prepared = coordinator.prepare("public-slot", "public-operation", current.revision)
    val executing = coordinator.begin(prepared.ticket)
    expect("EINVAL") { coordinator.abort(executing.ticket) }
    val unknown = coordinator.unknown(executing.ticket)
    expect("ESTALE") { coordinator.commit(executing.ticket, mapOf("late" to bytes)) }
    val result = coordinator.resolve(unknown.ticket, true, mapOf("confirmed" to bytes), bytes)
    check(result.receipt!!.contentEquals(bytes) && store.read().records["confirmed"]!!.contentEquals(bytes))
    checks.addAll(listOf("no-false-abort-after-begin", "uncertain-outcome-retained", "late-commit-refused", "atomic-native-receipt"))
    coordinator.release(result.ticket)
    current = store.read()
    current = store.migrate("public-migration", "a".repeat(64), current.revision, mapOf("imported" to bytes))
    current = store.migrate("public-migration", "a".repeat(64), current.revision, mapOf("imported" to byteArrayOf(0)))
    check(current.records["imported"]!!.contentEquals(bytes))
    expect("EEXIST") { store.migrate("public-migration", "b".repeat(64), current.revision, emptyMap()) }
    checks.add("atomic-idempotent-import-marker")
    NativeRecords.database(context).use { db ->
      db.execSQL("CREATE TRIGGER public_reject BEFORE INSERT ON scopes WHEN NEW.scope = '${store.scope}' BEGIN SELECT RAISE(ABORT, 'public-fault'); END")
    }
    try { expect("EIO") { store.apply(current.revision, mapOf("public" to byteArrayOf(1))) }
    } finally { NativeRecords.database(context).use { it.execSQL("DROP TRIGGER public_reject") } }
    check(store.read().records["public"]!!.contentEquals(bytes)); checks.add("failed-transaction-preserves-records")
    val envelope = NativeRecords.database(context).use { db -> db.rawQuery("SELECT envelope FROM scopes WHERE scope = ?", arrayOf(store.scope)).use {
      it.moveToFirst(); it.getBlob(0)
    } }
    val corrupt = envelope.copyOf().also { it[it.lastIndex] = (it.last().toInt() xor 1).toByte() }
    NativeRecords.database(context).use { it.execSQL("UPDATE scopes SET envelope = ? WHERE scope = ?", arrayOf(corrupt, store.scope)) }
    try {
      expect("EBADMSG") { store.read() }
      NativeRecords.database(context).use { db -> db.rawQuery("SELECT envelope FROM scopes WHERE scope = ?", arrayOf(store.scope)).use {
        it.moveToFirst(); check(it.getBlob(0).contentEquals(corrupt))
      } }
    } finally { NativeRecords.database(context).use { it.execSQL("UPDATE scopes SET envelope = ? WHERE scope = ?", arrayOf(envelope, store.scope)) } }
    checks.add("corrupt-ciphertext-preserved")
    val other = NativeRecords.open(context, "qualification.records", identity.copy(accountRef = "other-public-account"))
    val otherBefore = other.read()
    other.apply(otherBefore.revision, mapOf("other" to bytes))
    val keyId = NativeRecords.database(context).use { db -> db.rawQuery("SELECT key_id FROM scopes WHERE scope = ?", arrayOf(store.scope)).use {
      it.moveToFirst(); it.getString(0)
    } }
    KeyStore.getInstance("AndroidKeyStore").apply { load(null); deleteEntry(NativeRecords.alias(store.scope, keyId)) }
    expect("EKEYREVOKED") { store.read() }
    check(store.status().requiresRecovery)
    val beforeReset = store.status().revision
    check(store.reset(beforeReset).revision == beforeReset + 1)
    check(other.read().records["other"]!!.contentEquals(bytes))
    checks.addAll(listOf("missing-key-no-auto-replacement", "explicit-identity-local-reset", "other-identity-preserved"))
    NativeRecords.collectRetiredKeys(context)
    return JSONObject().put("checks", JSONArray(checks))
  }
}

class StateQualificationReceiver : BroadcastReceiver() {
  override fun onReceive (context: Context, intent: Intent) {
    if (!context.packageName.startsWith("oro.runtime.qualification.")) return
    val operation = intent.action?.removePrefix("oro.runtime.qualification.STATE.") ?: return
    val pending = goAsync()
    Thread {
      try {
        val result = when (operation) {
          "RUN" -> PublicNativeState.run(context, "run")
          "SEED" -> PublicNativeState.run(context, "seed")
          "RECOVER" -> PublicNativeState.run(context, "recover")
          "FOREGROUND" -> {
            val preferences = context.getSharedPreferences("oro.public.foreground", Context.MODE_PRIVATE)
            JSONObject().put("running", preferences.getString("running", "")).put("closed", preferences.getString("closed", ""))
              .put("runs", preferences.getInt("runs", 0)).put("closes", preferences.getInt("closes", 0))
              .put("tasks", ForegroundTasks.list(context))
          }
          "CONTINUOUS" -> {
            val preferences = context.getSharedPreferences("oro.public.continuous", Context.MODE_PRIVATE)
            JSONObject().put("running", preferences.getString("running", "")).put("closed", preferences.getString("closed", ""))
              .put("runs", preferences.getInt("runs", 0)).put("closes", preferences.getInt("closes", 0))
              .put("tasks", ContinuousTasks.list(context))
          }
          "STOP_CONTINUOUS" -> {
            val manager = context.getSystemService(android.app.NotificationManager::class.java)
            val notification = manager.activeNotifications.single { it.id == ContinuousTasks.NOTIFICATION }.notification
            notification.actions.single { it.title.toString() == "Stop" }.actionIntent.send()
            JSONObject().put("osStopPendingIntentSent", true)
          }
          "STOP" -> {
            val manager = context.getSystemService(android.app.NotificationManager::class.java)
            val notification = manager.activeNotifications.single { it.id == ForegroundTasks.NOTIFICATION }.notification
            notification.actions.single { it.title.toString() == "Stop" }.actionIntent.send()
            JSONObject().put("osStopPendingIntentSent", true)
          }
          else -> error("Unknown public fixture operation")
        }
        pending.resultCode = -1; pending.resultData = result.toString()
      } catch (_: Exception) { pending.resultCode = 0; pending.resultData = "public-state-check-failed"
      } finally { pending.finish() }
    }.start()
  }
}
