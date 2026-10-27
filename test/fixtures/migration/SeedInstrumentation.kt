package public.oro.migration

import android.app.Activity
import android.app.Instrumentation
import android.app.PendingIntent
import android.app.job.JobInfo
import android.app.job.JobScheduler
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.hardware.biometrics.BiometricManager
import android.hardware.biometrics.BiometricPrompt
import android.os.Bundle
import android.os.CancellationSignal
import android.os.PersistableBundle
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONObject
import java.security.KeyStore
import java.security.MessageDigest
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey

/** Test APK only. It can write only these isolated public-history fixtures. */
class SeedInstrumentation : Instrumentation() {
  private var action = ""
  override fun onCreate(arguments: Bundle?) { super.onCreate(arguments); action = arguments?.getString("action") ?: "inspect"; start() }
  private fun store() = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
  private fun key(alias: String, protected: Boolean): SecretKey {
    val existing = store().getKey(alias, null) as? SecretKey
    if (existing != null) return existing
    val spec = KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
      .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
    if (protected) spec.setUserAuthenticationRequired(true).setUserAuthenticationParameters(0,
      KeyProperties.AUTH_BIOMETRIC_STRONG or KeyProperties.AUTH_DEVICE_CREDENTIAL).setInvalidatedByBiometricEnrollment(false)
    return KeyGenerator.getInstance("AES", "AndroidKeyStore").apply { init(spec.build()) }.generateKey()
  }
  private fun encrypt(key: SecretKey, aad: String, bytes: ByteArray, protected: Boolean): Cipher {
    val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, key) }
    if (protected) {
      val activity = startActivitySync(Intent().setComponent(ComponentName(targetContext.packageName,
        targetContext.packageName + ".MainActivity")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
      val approved = CompletableFuture<Unit>()
      runOnMainSync {
        BiometricPrompt.Builder(activity).setTitle("Seed public historical test data")
          .setAllowedAuthenticators(BiometricManager.Authenticators.BIOMETRIC_STRONG or BiometricManager.Authenticators.DEVICE_CREDENTIAL)
          .build().authenticate(BiometricPrompt.CryptoObject(cipher), CancellationSignal(), activity.mainExecutor,
            object : BiometricPrompt.AuthenticationCallback() {
              override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                if (result.cryptoObject?.cipher !== cipher) approved.completeExceptionally(IllegalStateException("Wrong public crypto operation"))
                else approved.complete(Unit)
              }
              override fun onAuthenticationError(code: Int, message: CharSequence) { approved.completeExceptionally(IllegalStateException("Public prompt failed")) }
            })
      }
      approved.get(85, TimeUnit.SECONDS)
    }
    cipher.updateAAD(aad.toByteArray(Charsets.UTF_8))
    return cipher
  }
  private fun schedule() {
    val job = JobInfo.Builder(23101, ComponentName("oro.runtime.qualification.migration", "public.oro.migration.LegacyJob"))
      .setPersisted(true).setMinimumLatency(3600000).setExtras(PersistableBundle().apply { putString("clientId", "public-target") }).build()
    check(targetContext.getSystemService(JobScheduler::class.java).schedule(job) == JobScheduler.RESULT_SUCCESS)
  }
  override fun onStart() {
    val result = Bundle()
    try {
      require(targetContext.packageName == "oro.runtime.qualification.migration")
      val preferences = targetContext.getSharedPreferences("qualification.history", Context.MODE_PRIVATE)
      when(action) {
        "metadata" -> check(preferences.edit().putString("shortcut.public-target",
          JSONObject().put("destination", "public-target").put("label", "Public historical shortcut").toString()).commit())
        "seed" -> {
          val registry = JSONObject().put("clientId", "public-target").put("authority", "public-authority")
            .put("account", "public-account").put("generation", "public-generation-1")
            .put("sender", targetContext.packageName).put("token", "public-legacy-token")
          val cipher = encrypt(key("qualification.history.registry", false), "public-target", registry.toString().toByteArray(), false)
          val body = cipher.doFinal(registry.toString().toByteArray(Charsets.UTF_8))
          val flags = Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING
          val envelope = JSONObject().put("iv", Base64.encodeToString(cipher.iv, flags)).put("body", Base64.encodeToString(body, flags))
          check(preferences.edit().putString("registry.public-target", envelope.toString()).commit())
          val privateCipher = encrypt(key("qualification.history.private", true), targetContext.packageName + ":public-private",
            "public historical value 🎈\u0000".toByteArray(Charsets.UTF_8), true)
          val encrypted = privateCipher.doFinal("public historical value 🎈\u0000".toByteArray(Charsets.UTF_8))
          check(preferences.edit().putString("protected", JSONObject().put("iv", Base64.encodeToString(privateCipher.iv, Base64.NO_WRAP))
            .put("ciphertext", Base64.encodeToString(encrypted, Base64.NO_WRAP)).toString()).commit())
          schedule()
        }
        "schedule" -> schedule()
        "seed-native" -> {
          val records = oro.runtime.app.NativeRecords.open(targetContext, "qualification.history",
            oro.runtime.app.NativeRecordIdentity("public-legacy", targetContext.packageName, "installation-1"))
          val snapshot = records.read()
          try {
            val value = JSONObject().put("clientId", "public-target").put("authority", "public-authority")
              .put("account", "public-account").put("generation", "public-native-generation-1").put("revoked", false)
            records.apply(snapshot.revision, mapOf("registry.public-target" to value.toString().toByteArray()), "public-seed-native-v1")
          } finally { snapshot.records.values.forEach { it.fill(0) } }
          val job = JobInfo.Builder(23109, ComponentName(targetContext.packageName, "public.oro.migration.NativeRecordJob"))
            .setMinimumLatency(3600000).setExtras(PersistableBundle().apply { putString("clientId", "public-target") }).build()
          check(targetContext.getSystemService(JobScheduler::class.java).schedule(job) == JobScheduler.RESULT_SUCCESS)
        }
        "native-progress", "native-revoke", "native-epoch" -> {
          val records = oro.runtime.app.NativeRecords.open(targetContext, "qualification.history",
            oro.runtime.app.NativeRecordIdentity("public-legacy", targetContext.packageName, "installation-1"))
          val snapshot = records.read()
          try {
            val bytes = snapshot.records.getValue("registry.public-target")
            if (action == "native-epoch") {
              val empty = records.reset(snapshot.revision)
              records.apply(empty.revision, mapOf("registry.public-target" to bytes), "public-native-epoch")
            } else {
              val value = JSONObject(bytes.toString(Charsets.UTF_8))
              if (action == "native-revoke") value.put("revoked", true) else value.put("cursor", "public-progress")
              records.apply(snapshot.revision, mapOf("registry.public-target" to value.toString().toByteArray()), "public-" + action)
            }
          } finally { snapshot.records.values.forEach { it.fill(0) } }
          val job = JobInfo.Builder(23109, ComponentName(targetContext.packageName, "public.oro.migration.NativeRecordJob"))
            .setMinimumLatency(3600000).setExtras(PersistableBundle().apply { putString("clientId", "public-target") }).build()
          check(targetContext.getSystemService(JobScheduler::class.java).schedule(job) == JobScheduler.RESULT_SUCCESS)
        }
        "corrupt", "restore" -> {
          if (action == "restore") check(preferences.edit().putString("protected", preferences.getString("protected-backup", null)).commit())
          else {
            val original = preferences.getString("protected", null) ?: error("Public source absent")
            val value = JSONObject(original); val body = Base64.decode(value.getString("ciphertext"), Base64.NO_WRAP)
            body[0] = (body[0].toInt() xor 1).toByte()
            value.put("ciphertext", Base64.encodeToString(body, Base64.NO_WRAP))
            check(preferences.edit().putString("protected-backup", original).putString("protected", value.toString()).commit())
          }
        }
        "lose-key" -> store().deleteEntry("qualification.history.private")
        "send" -> {
          val intent = Intent("public.oro.migration.MESSAGE").setComponent(ComponentName(targetContext.packageName, "public.oro.migration.LegacyReceiver"))
            .putExtra("token", "public-legacy-token").putExtra("id", "public-retained-message-1")
            .putExtra("bytesMessage", "public opaque legacy ciphertext".toByteArray())
          val proof = PendingIntent.getBroadcast(targetContext, 123, Intent("public.oro.migration.PROOF").setPackage(targetContext.packageName), PendingIntent.FLAG_IMMUTABLE)
          intent.putExtra("pi", proof)
          val delivered = CompletableFuture<Unit>()
          targetContext.sendOrderedBroadcast(intent, null, object : android.content.BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) { delivered.complete(Unit) }
          }, android.os.Handler(android.os.Looper.getMainLooper()), Activity.RESULT_OK, null, null)
          delivered.get(12, TimeUnit.SECONDS)
        }
        "inspect" -> {}
        else -> error("Unknown public fixture action")
      }
      val inventory = JSONObject()
      for (name in listOf("protected", "registry.public-target")) preferences.getString(name, null)?.let {
        inventory.put(name, MessageDigest.getInstance("SHA-256").digest(it.toByteArray()).joinToString("") { byte -> "%02x".format(byte.toInt() and 255) })
      }
      inventory.put("privateKeyPresent", store().containsAlias("qualification.history.private"))
        .put("registryKeyPresent", store().containsAlias("qualification.history.registry"))
      val native = oro.runtime.app.NativeRecords.open(targetContext, "qualification.history",
        oro.runtime.app.NativeRecordIdentity("public-legacy", targetContext.packageName, "installation-1")).read()
      try { inventory.put("nativeRevision", native.revision).put("nativeCommitId", native.commitId ?: "")
      } finally { native.records.values.forEach { it.fill(0) } }
      val job = targetContext.getSystemService(JobScheduler::class.java).getPendingJob(23101)
      inventory.put("jobClass", job?.service?.className ?: "").put("jobId", job?.id ?: -1)
      result.putString("publicEvidence", inventory.toString()); finish(Activity.RESULT_OK, result)
    } catch (error: Exception) { result.putString("publicFailure", error.javaClass.simpleName); finish(Activity.RESULT_CANCELED, result) }
  }
}
