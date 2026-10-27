package oro.runtime.app

import android.app.Instrumentation
import android.app.PendingIntent
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.security.KeyStore
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.KeyGenerator

/** Test APK only: no production histories, identities, credentials or network requests. */
class SourceInstrumentation : Instrumentation() {
  override fun onCreate(arguments: Bundle?) { super.onCreate(arguments); start() }
  private fun expected(code: String, operation: () -> Unit) {
    try { operation(); error("Expected $code") }
    catch (error: NativeStateException) { check(error.code == code) { "Expected $code, got ${error.code}" } }
  }
  private fun coldReceiver(event: String, count: Int) {
    val proof = PendingIntent.getActivity(targetContext, 0,
      Intent().setComponent(ComponentName(targetContext.packageName, targetContext.packageName + ".MainActivity")),
      PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    targetContext.sendBroadcast(Intent("public.oro.migration.MESSAGE")
      .setComponent(ComponentName(targetContext.packageName, "public.oro.migration.LegacyReceiver"))
      .putExtra("token", "public-token").putExtra("id", event).putExtra("pi", proof))
    val until = System.currentTimeMillis() + 20000
    while (System.currentTimeMillis() < until) {
      val receipts = NativeMigration.entries(targetContext, "receipt.")
      if (receipts.size == count) {
        check(receipts.all { it.second.getString("outcome") == "unknown" })
        proof.cancel(); return
      }
      Thread.sleep(100)
    }
    error("Cold receiver did not preserve unknown input")
  }
  override fun onStart() {
    val result = Bundle()
    try {
      check(targetContext.packageName == "oro.runtime.qualification.sourcehistory")
      val preferences = targetContext.getSharedPreferences("qualification.history", Context.MODE_PRIVATE)
      val profile = NativeMigration.definitions().protected.getValue("records")
      val records = NativeMigration.recordSource(targetContext, profile, "public-target")
      check(records.status().revision == 0L && !preferences.contains("registry.public-target")) {
        "This regression requires an unused isolated fixture; existing state was preserved"
      }
      val key = KeyGenerator.getInstance("AES", "AndroidKeyStore").apply {
        init(KeyGenParameterSpec.Builder("qualification.history.registry", KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
          .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
      }.generateKey()
      val plain = JSONObject().put("clientId", "public-target").put("generation", "public-generation-1")
        .put("authority", "public-authority").put("account", "public-account").put("sender", targetContext.packageName).put("token", "public-token")
      val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, key); updateAAD("public-target".toByteArray()) }
      val flags = Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING
      val raw = JSONObject().put("iv", Base64.encodeToString(cipher.iv, flags))
        .put("body", Base64.encodeToString(cipher.doFinal(plain.toString().toByteArray()), flags)).toString()
      check(preferences.edit().putString("registry.public-target", raw).commit())
      val component = NativeMigration.definitions().components.getValue("job")
      val (old, _, oldDefinition) = NativeMigration.sourcePlain(targetContext, component, "public-target")
      check(old.toString() == plain.toString() && oldDefinition.getString("id") == "registry")
      check(records.status().revision == 0L)
      coldReceiver("public-cold-legacy", 1)
      val pairs = JSONArray().put(JSONArray().put("registry.public-target").put(raw)).toString()
      val digest = MessageDigest.getInstance("SHA-256").digest(pairs.toByteArray()).joinToString("") { "%02x".format(it.toInt() and 255) }
      val legacy = NativeMigration.definitions().protected.getValue("registry")
      check(MigrationPolicy.legacyDigest(preferences.all, legacy, targetContext.packageName) == digest)
      val marker = JSONObject().put("format", 1).put("sourceSha256", digest).toString().toByteArray()
      plain.put("cursor", "native-progress")
      records.apply(0, mapOf("registry.public-target" to plain.toString().toByteArray(), "migration.v1" to marker))
      val (native, _, nativeDefinition) = NativeMigration.sourcePlain(targetContext, component, "public-target")
      check(native.getString("cursor") == "native-progress" && nativeDefinition.getString("id") == "records")
      coldReceiver("public-cold-native", 2)
      check(preferences.edit().putString("registry.public-target", raw + " ").commit())
      expected("EEXIST") { NativeMigration.source(targetContext, "selected", "public-target") }
      expected("EEXIST") { NativeMigration.sourceNames(targetContext, "selected") }
      check(preferences.edit().putString("registry.public-target", raw).commit())
      records.apply(records.status().revision, mapOf("registry.public-target" to null))
      expected("ENOENT") { NativeMigration.source(targetContext, "selected", "public-target") }
      check(NativeMigration.sourceNames(targetContext, "selected").second == listOf("migration.v1"))
      records.apply(records.status().revision, mapOf("registry.public-target" to plain.toString().toByteArray(), "migration.v1" to "{}".toByteArray()))
      expected("EBADMSG") { NativeMigration.source(targetContext, "selected", "public-target") }
      records.apply(records.status().revision, mapOf("migration.v1" to marker))
      val keys = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
      keys.deleteEntry(NativeRecords.alias(records.scope, records.migrationEpoch()))
      expected("EKEYREVOKED") { NativeMigration.source(targetContext, "selected", "public-target") }
      check(preferences.getString("registry.public-target", null) == raw && records.status().revision > 0)
      result.putBoolean("passed", true)
      result.putString("scope", "AES-decrypt-native-preference-cold-receivers-provenance-tombstone-marker-key-loss-originals")
      finish(0, result)
    } catch (error: Throwable) {
      result.putBoolean("passed", false); result.putString("error", error.javaClass.simpleName + ": " + error.message)
      finish(1, result)
    }
  }
}
