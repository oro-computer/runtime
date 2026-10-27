package oro.runtime.qualification.pushjournal

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import org.json.JSONObject
import oro.runtime.app.NativePush
import oro.runtime.app.NativePushJournal
import oro.runtime.app.PushCryptography
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.Executors

object PublicJournalFixture {
  external fun run (action: String, payload: String): String
}
private fun qa(context: Context) = context.getSharedPreferences("public.journal.qa", Context.MODE_PRIVATE)
private fun hash(file: File): String = MessageDigest.getInstance("SHA-256").digest(file.readBytes())
  .joinToString("") { "%02x".format(it.toInt() and 255) }
private fun journal(context: Context): File = File(File(context.applicationInfo.dataDir, "shared_prefs"),
  NativePushJournal.name(NativePushJournal.epoch(context)) + ".xml")

/** Counts only unexpected local UNREGISTER attempts; there is no server or distributor enrollment. */
class PublicDistributor : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    if (intent.action == "org.unifiedpush.android.distributor.UNREGISTER")
      check(qa(context).edit().putInt("unregisters", qa(context).getInt("unregisters", 0) + 1).commit())
  }
}

/** Public fault injection only. DUMP permission prevents ordinary applications driving this fixture. */
class JournalReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val pending = goAsync()
    val executor = Executors.newSingleThreadExecutor()
    executor.execute {
      val result = try {
        val mode = intent.getStringExtra("mode") ?: ""
        val options = JSONObject(intent.getStringExtra("options") ?: "{}")
        when (mode) {
          "inspect", "quarantine", "busy-status", "release" -> JSONObject(PublicJournalFixture.run(mode,
            if (mode == "quarantine") options.getString("revision") + "|" + options.getString("recoveryId") + "|" +
              if (options.optBoolean("confirmed")) "confirmed" else "denied" else ""))
          "seed-missing" -> {
            check(NativePushJournal.epoch(context).isEmpty())
            check(!PushCryptography.hasStorageKey())
            val value = JSONObject().put("id", "public-missing").put("kind", "public.wait")
              .put("authorityRef", "authority").put("accountRef", "account").put("targetRef", "target")
            val encrypted = PushCryptography.seal(value, "registration.public-missing", true, "oro.public.journal.source")
            check(context.getSharedPreferences("oro.push.v1", Context.MODE_PRIVATE).edit()
              .putString("registration.public-missing", encrypted).commit())
            check(qa(context).edit().putString("legacyCipher", encrypted).putString("legacyHash", hash(journal(context))).commit())
            JSONObject().put("seeded", true).put("legacyKeyPresent", PushCryptography.hasStorageKey())
          }
          "mutate" -> {
            check(context.getSharedPreferences("oro.push.v1", Context.MODE_PRIVATE).edit().putLong("seen.public", 1).commit())
            check(qa(context).edit().putString("legacyHash", hash(journal(context))).commit())
            JSONObject().put("mutated", true)
          }
          "seed-busy" -> {
            val epoch = NativePushJournal.epoch(context)
            check(epoch.isNotEmpty())
            val record = JSONObject().put("id", "public-busy").put("kind", "public.wait")
              .put("authorityRef", "authority").put("accountRef", "account").put("targetRef", "target")
              .put("phase", "registered").put("enrollmentPending", true).put("enrollmentAttempts", 0)
              .put("token", "public-token").put("generation", "public-generation").put("distributor", context.packageName)
              .put("keys", PushCryptography.keys())
            val method = NativePush.javaClass.getDeclaredMethod("save", Context::class.java, String::class.java, JSONObject::class.java)
              .apply { isAccessible = true }
            method.invoke(NativePush, context, "registration.public-busy", record)
            check(qa(context).edit().putString("workingEpoch", epoch).commit())
            NativePush.start(context)
            JSONObject().put("seeded", true).put("newKeyPresent", PushCryptography.hasStorageKey("oro.push.storage.v1.$epoch"))
          }
          "corrupt-busy" -> {
            val original = File(context.noBackupFilesDir, "public-original-working.xml")
            check(!original.exists())
            journal(context).copyTo(original)
            check(context.getSharedPreferences(NativePushJournal.name(NativePushJournal.epoch(context)), Context.MODE_PRIVATE)
              .edit().putString("registration.public-busy", "not-json").commit())
            check(qa(context).edit().putString("workingHash", hash(journal(context))).commit())
            JSONObject().put("corrupted", true)
          }
          "seed-xml" -> {
            val epoch = NativePushJournal.epoch(context)
            val file = journal(context)
            check(!file.exists() && !PushCryptography.hasStorageKey("oro.push.storage.v1.$epoch"))
            file.parentFile!!.mkdirs()
            file.writeText("public malformed XML")
            check(qa(context).edit().putString("xmlEpoch", epoch).putString("xmlHash", hash(file)).commit())
            JSONObject().put("seeded", true)
          }
          "verify" -> {
            val legacy = File(File(context.applicationInfo.dataDir, "shared_prefs"), "oro.push.v1.xml")
            check(hash(legacy) == qa(context).getString("legacyHash", null))
            check(!PushCryptography.hasStorageKey())
            qa(context).getString("workingEpoch", null)?.let { epoch ->
              val file = File(legacy.parentFile, "oro.push.v1.$epoch.xml")
              check(hash(file) == qa(context).getString("workingHash", null))
              check(PushCryptography.hasStorageKey("oro.push.storage.v1.$epoch"))
              check(File(context.noBackupFilesDir, "public-original-working.xml").exists())
            }
            qa(context).getString("xmlEpoch", null)?.let { epoch ->
              check(hash(File(legacy.parentFile, "oro.push.v1.$epoch.xml")) == qa(context).getString("xmlHash", null))
              check(!PushCryptography.hasStorageKey("oro.push.storage.v1.$epoch"))
            }
            check(qa(context).getInt("unregisters", 0) == 0)
            JSONObject().put("originalsRetained", true).put("originalAliasesUnchanged", true).put("remoteUnregisterAttempts", 0)
          }
          else -> throw IllegalArgumentException()
        }
      } catch (_: Exception) { JSONObject().put("failed", true) }
      result.put("requestId", intent.getStringExtra("requestId"))
      try { check(qa(context).edit().putString("receipt", result.toString()).commit()) }
      finally { executor.shutdown(); pending.finish() }
    }
  }
}
