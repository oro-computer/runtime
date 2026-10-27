package oro.runtime.app

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.biometrics.BiometricManager
import android.hardware.biometrics.BiometricPrompt
import android.os.Build
import android.os.CancellationSignal
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyInfo
import android.security.keystore.KeyPermanentlyInvalidatedException
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONObject
import java.net.URI
import java.security.Key
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.util.concurrent.atomic.AtomicBoolean
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.GCMParameterSpec

/** Authentication gates the cryptographic operation, never a JavaScript boolean. */
internal object ProtectedAuthentication {
  private const val PREFIX = "oro.authentication.v1."
  private val active = AtomicBoolean(false)
  private fun enabled (context: Context): Boolean = Build.VERSION.SDK_INT >= 30 &&
    App.getInstance().getUserConfigValue("permissions_allow_biometrics") == "true" &&
    context.checkSelfPermission(Manifest.permission.USE_BIOMETRIC) == PackageManager.PERMISSION_GRANTED

  private fun policy (options: JSONObject): String {
    val value = options.optString("policy", "biometric")
    require(value in listOf("biometric", "biometric-or-device"))
    return value
  }

  private fun authenticators (policy: String): Int = BiometricManager.Authenticators.BIOMETRIC_STRONG or
    if (policy == "biometric-or-device") BiometricManager.Authenticators.DEVICE_CREDENTIAL else 0

  private fun keyAuthenticators (policy: String): Int = KeyProperties.AUTH_BIOMETRIC_STRONG or
    if (policy == "biometric-or-device") KeyProperties.AUTH_DEVICE_CREDENTIAL else 0

  fun status (context: Context, options: JSONObject = JSONObject()): JSONObject {
    val selected = policy(options)
    val available = enabled(context)
    val code = if (available) (context.getSystemService(Context.BIOMETRIC_SERVICE) as BiometricManager)
      .canAuthenticate(authenticators(selected)) else -1
    return JSONObject().put("supported", Build.VERSION.SDK_INT >= 30).put("enabled", available)
      .put("canAuthenticate", available && code == BiometricManager.BIOMETRIC_SUCCESS)
      .put("statusCode", code).put("policy", selected).put("cryptographicOperationsOnly", true)
      .put("serverVerificationRequired", true).put("busy", active.get()).put("historicalMigrationProtocol", 1)
  }

  private fun identifier (value: String): String {
    require(Regex("[a-zA-Z0-9_-]{1,128}").matches(value))
    return value
  }

  private fun alias (context: Context, kind: String, id: String): String {
    val hash = MessageDigest.getInstance("SHA-256").digest("${context.packageName}:$id".toByteArray())
    return PREFIX + kind + "." + Base64.encodeToString(hash, Base64.NO_WRAP or Base64.URL_SAFE or Base64.NO_PADDING)
  }

  private fun bytes (value: String, minimum: Int, maximum: Int): ByteArray {
    require(value.length <= ((maximum + 2) / 3) * 4 && Regex("[a-zA-Z0-9+/]*={0,2}").matches(value))
    val result = Base64.decode(value, Base64.NO_WRAP)
    require(result.size in minimum..maximum && encode(result) == value)
    return result
  }
  private fun encode (value: ByteArray): String = Base64.encodeToString(value, Base64.NO_WRAP)
  private fun store (): KeyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }

  private fun parameters (alias: String, purpose: Int, policy: String): KeyGenParameterSpec.Builder =
    KeyGenParameterSpec.Builder(alias, purpose).setUserAuthenticationRequired(true)
      .setUserAuthenticationParameters(0, keyAuthenticators(policy))
      .setInvalidatedByBiometricEnrollment(policy == "biometric")

  private fun verifyPolicy (key: Key, policy: String) {
    val info = if (key is SecretKey) SecretKeyFactory.getInstance(key.algorithm, "AndroidKeyStore")
      .getKeySpec(key, KeyInfo::class.java) as KeyInfo else KeyFactory.getInstance(key.algorithm, "AndroidKeyStore")
      .getKeySpec(key, KeyInfo::class.java)
    // Keystore2 reports zero for per-operation keys; older providers report -1.
    if (!info.isUserAuthenticationRequired || info.userAuthenticationValidityDurationSeconds !in -1..0 ||
        info.userAuthenticationType != keyAuthenticators(policy)) throw PlatformFailure("EACCES")
  }

  private fun newKeyAllowed (store: KeyStore) {
    if (store.aliases().asSequence().count { it.startsWith(PREFIX) } >= 64) throw PlatformFailure("ENOSPC")
  }

  private fun authority (value: String): String {
    require(value.length in 1..2048 && value.none { it.isWhitespace() || it.code < 32 })
    val uri = URI(value)
    require(uri.scheme == "https" && !uri.host.isNullOrEmpty() && uri.userInfo == null &&
      uri.rawQuery == null && uri.rawFragment == null && uri.path in listOf("", "/") && uri.port in -1..65535 && uri.port != 0)
    return URI("https", null, uri.host.lowercase(), if (uri.port == 443) -1 else uri.port, null, null, null).toASCIIString()
  }

  /** Runs on an owned worker; system UI and document validation remain on the main thread. */
  private fun authorizeCipher(call: PlatformCall, cancellation: CancellationSignal, cipher: Cipher, selected: String, title: String) {
    val result = java.util.concurrent.CompletableFuture<Unit>()
    val signal = CancellationSignal()
    cancellation.setOnCancelListener { signal.cancel(); result.completeExceptionally(PlatformFailure("ABORT_ERR")) }
    PlatformOperations.main.post {
      try {
        if (call.done.get() || cancellation.isCanceled) throw PlatformFailure("ABORT_ERR")
        val owner = call.foreground()
        val builder = BiometricPrompt.Builder(owner.activity).setTitle(title).setAllowedAuthenticators(authenticators(selected))
        if (selected == "biometric") builder.setNegativeButton("Cancel", owner.activity.mainExecutor) { _, _ ->
          result.completeExceptionally(PlatformFailure("ABORT_ERR"))
        }
        builder.build().authenticate(BiometricPrompt.CryptoObject(cipher), signal, owner.activity.mainExecutor,
          object : BiometricPrompt.AuthenticationCallback() {
            override fun onAuthenticationError(code: Int, message: CharSequence) {
              result.completeExceptionally(PlatformFailure(when (code) {
                BiometricPrompt.BIOMETRIC_ERROR_CANCELED, BiometricPrompt.BIOMETRIC_ERROR_USER_CANCELED -> "ABORT_ERR"
                BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT, BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT_PERMANENT -> "EACCES"
                else -> "ENOTREADY"
              }))
            }
            override fun onAuthenticationSucceeded(value: BiometricPrompt.AuthenticationResult) {
              try {
                if (call.done.get() || cancellation.isCanceled) throw PlatformFailure("ABORT_ERR")
                call.foreground()
                if (value.cryptoObject?.cipher !== cipher) throw PlatformFailure("EACCES")
                result.complete(Unit)
              } catch (failure: PlatformFailure) { result.completeExceptionally(failure) }
            }
          })
      } catch (failure: Exception) { result.completeExceptionally(failure) }
    }
    try { result.get()
    } catch (failure: java.util.concurrent.ExecutionException) { throw (failure.cause ?: failure)
    } finally { cancellation.setOnCancelListener(null); signal.cancel() }
    cancellation.throwIfCanceled()
    if (call.done.get()) throw PlatformFailure("ABORT_ERR")
  }

  internal fun legacyPlain(context: Context, definition: JSONObject, reference: String, envelope: JSONObject): ByteArray {
    if (definition.getString("authentication") != "none") throw PlatformFailure("EACCES")
    val cipher = legacyCipher(context, definition, reference, envelope)
    cipher.updateAAD(MigrationPolicy.expand(definition.getString("aad"), context.packageName, reference).toByteArray(Charsets.UTF_8))
    return cipher.doFinal(legacyBytes(definition, envelope))
  }

  private fun <T> historicalEnvelope(operation: () -> T): T = try { operation()
  } catch (_: org.json.JSONException) { throw PlatformFailure("EBADMSG")
  } catch (_: IllegalArgumentException) { throw PlatformFailure("EBADMSG")
  } catch (_: java.security.InvalidAlgorithmParameterException) { throw PlatformFailure("EBADMSG") }

  private fun legacyBytes(definition: JSONObject, envelope: JSONObject): ByteArray = historicalEnvelope {
    val value = envelope.getString(definition.getString("ciphertextField"))
    if (definition.getString("encoding") == "base64") bytes(value, 16, 65552) else {
      require(value.length <= 87404 && Regex("[A-Za-z0-9_-]+={0,2}").matches(value))
      Base64.decode(value, Base64.URL_SAFE or Base64.NO_WRAP).also { require(it.size in 16..65552) }
    }
  }

  private fun legacyCipher(context: Context, definition: JSONObject, reference: String, envelope: JSONObject): Cipher = historicalEnvelope {
    val name = MigrationPolicy.expand(definition.getString("alias"), context.packageName, reference)
    val key = store().getKey(name, null) as? SecretKey ?: throw PlatformFailure("EKEYREVOKED")
    if (definition.getString("authentication") == "none") {
      val info = SecretKeyFactory.getInstance(key.algorithm, "AndroidKeyStore").getKeySpec(key, KeyInfo::class.java) as KeyInfo
      if (info.isUserAuthenticationRequired) throw PlatformFailure("EACCES")
    } else verifyPolicy(key, definition.getString("authentication"))
    val ivText = envelope.getString("iv")
    val iv = if (definition.getString("encoding") == "base64") bytes(ivText, 12, 12)
      else Base64.decode(ivText, Base64.NO_WRAP or Base64.URL_SAFE).also { require(it.size == 12) }
    Cipher.getInstance("AES/GCM/NoPadding").apply {
      init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, iv))
    }
  }

  /** Imports through two distinct CryptoObjects when the historical key is authentication-bound. */
  internal fun rewrapLegacy(call: PlatformCall, signal: CancellationSignal, definition: JSONObject,
    reference: String, envelope: JSONObject, id: String, nativePlain: ByteArray? = null): JSONObject {
    if (!enabled(call.app)) throw PlatformFailure("ENOSYS")
    if (!active.compareAndSet(false, true)) throw PlatformFailure("EBUSY")
    var clear = ByteArray(0)
    try {
      if (nativePlain != null) {
        require(definition.optString("format") in setOf("native-records-v1", "preferences-json") && definition.getString("authentication") == "none")
        clear = nativePlain.copyOf()
      } else {
        val encrypted = legacyBytes(definition, envelope)
        val source = legacyCipher(call.app, definition, reference, envelope)
        if (definition.getString("authentication") != "none")
          authorizeCipher(call, signal, source, definition.getString("authentication"), "Unlock historical saved information")
        signal.throwIfCanceled()
        source.updateAAD(MigrationPolicy.expand(definition.getString("aad"), call.app.packageName, reference).toByteArray(Charsets.UTF_8))
        clear = source.doFinal(encrypted)
      }
      require(clear.size <= 65536)
      val selected = definition.getString("policy")
      val name = alias(call.app, "secret", id)
      val keys = store()
      if (!keys.containsAlias(name)) {
        newKeyAllowed(keys)
        KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
          init(parameters(name, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT, selected)
            .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
          generateKey()
        }
      }
      val key = keys.getKey(name, null) as SecretKey
      verifyPolicy(key, selected)
      val target = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, key) }
      authorizeCipher(call, signal, target, selected, "Protect migrated saved information")
      target.updateAAD("oro.secret.v1:${call.app.packageName}:$id:$selected".toByteArray(Charsets.UTF_8))
      val encrypted = target.doFinal(clear)
      return JSONObject().put("schema", 1).put("id", id).put("policy", selected)
        .put("iv", encode(target.iv)).put("ciphertext", encode(encrypted))
    } finally { clear.fill(0); active.set(false) }
  }

  internal fun openMigrated(call: PlatformCall, signal: CancellationSignal, record: JSONObject): ByteArray {
    if (!enabled(call.app)) throw PlatformFailure("ENOSYS")
    if (!active.compareAndSet(false, true)) throw PlatformFailure("EBUSY")
    try {
      val id = identifier(record.getString("id")); val selected = policy(record)
      require(record.opt("schema") == 1)
      val key = store().getKey(alias(call.app, "secret", id), null) as? SecretKey ?: throw PlatformFailure("EKEYREVOKED")
      verifyPolicy(key, selected)
      val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply {
        init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, bytes(record.getString("iv"), 12, 12)))
      }
      authorizeCipher(call, signal, cipher, selected, "Unlock migrated saved information")
      cipher.updateAAD("oro.secret.v1:${call.app.packageName}:$id:$selected".toByteArray(Charsets.UTF_8))
      return cipher.doFinal(bytes(record.getString("ciphertext"), 16, 65552))
    } finally { active.set(false) }
  }

  fun request (call: PlatformCall, action: String, options: JSONObject) {
    if (action.startsWith("migration.")) { NativeMigration.request(call, action.removePrefix("migration."), options); return }
    if (action == "status") { call.success(status(call.app, options)); return }
    if (!enabled(call.app)) throw PlatformFailure("ENOSYS")
    val owner = call.foreground()
    val selected = policy(options)
    val id = identifier(options.getString("id"))
    val kind = options.optString("kind", "secret")
    require(kind in listOf("secret", "signing"))
    if (action == "removeKey") {
      if (active.get()) throw PlatformFailure("EBUSY")
      store().deleteEntry(alias(call.app, kind, id))
      call.success()
      return
    }
    require(action in listOf("seal", "open", "createSigningKey", "signChallenge"))
    if (!active.compareAndSet(false, true)) throw PlatformFailure("EBUSY")
    val signal = CancellationSignal()
    var input = ByteArray(0)
    call.setCancellation { input.fill(0); try { signal.cancel() } finally { active.set(false) } }
    try {
      val keyStore = store()
      var cipher: Cipher? = null
      var signature: Signature? = null
      var publicKey = ""
      var signedPayload = ""
      val keyAlias = alias(call.app, if (action in listOf("seal", "open")) "secret" else "signing", id)
      if (action in listOf("seal", "open")) {
        if (!keyStore.containsAlias(keyAlias)) {
          if (action == "open") throw PlatformFailure("ENOENT")
          newKeyAllowed(keyStore)
          KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(parameters(keyAlias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT, selected)
              .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
              .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
            generateKey()
          }
        }
        val key = keyStore.getKey(keyAlias, null) as SecretKey
        verifyPolicy(key, selected)
        cipher = Cipher.getInstance("AES/GCM/NoPadding")
        if (action == "seal") {
          input = bytes(options.getString("value"), 0, 65536)
          cipher.init(Cipher.ENCRYPT_MODE, key)
        } else {
          val record = options.getJSONObject("sealed")
          require(record.getInt("schema") == 1 && record.getString("id") == id && record.getString("policy") == selected)
          input = bytes(record.getString("ciphertext"), 16, 65552)
          cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, bytes(record.getString("iv"), 12, 12)))
        }
      } else {
        val audience = authority(options.getString("authority"))
        val account = identifier(options.getString("accountRef"))
        val expiry = options.get("expiresAt")
        require(expiry is Number && expiry.toDouble() == expiry.toLong().toDouble() &&
          expiry.toLong() - System.currentTimeMillis() in 1..300000L)
        val challenge = options.getString("challenge")
        bytes(challenge, 16, 1024)
        signedPayload = JSONObject().put("schema", 1).put("purpose", "oro-account-authentication")
          .put("application", call.app.packageName).put("authority", audience).put("accountRef", account)
          .put("keyId", id).put("challenge", challenge).put("expiresAt", expiry.toLong()).toString()
        input = signedPayload.toByteArray(Charsets.UTF_8)
        if (!keyStore.containsAlias(keyAlias)) {
          if (action != "createSigningKey") throw PlatformFailure("ENOENT")
          newKeyAllowed(keyStore)
          KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore").apply {
            initialize(parameters(keyAlias, KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY, selected)
              .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1")).setDigests(KeyProperties.DIGEST_SHA256).build())
            generateKeyPair()
          }
        }
        val key = keyStore.getKey(keyAlias, null) as PrivateKey
        verifyPolicy(key, selected)
        publicKey = encode(keyStore.getCertificate(keyAlias).publicKey.encoded)
        signature = Signature.getInstance("SHA256withECDSA").apply { initSign(key) }
      }
      val crypto = if (cipher != null) BiometricPrompt.CryptoObject(cipher) else BiometricPrompt.CryptoObject(signature!!)
      val builder = BiometricPrompt.Builder(owner.activity).setTitle(
        if (action == "seal") "Protect saved information" else if (action == "open") "Unlock saved information" else "Confirm sign-in")
        .setAllowedAuthenticators(authenticators(selected))
      if (selected == "biometric") builder.setNegativeButton("Cancel", owner.activity.mainExecutor) { _, _ -> call.failure("ABORT_ERR") }
      val prompt = builder.build()
      prompt.authenticate(crypto, signal, owner.activity.mainExecutor, object : BiometricPrompt.AuthenticationCallback() {
        override fun onAuthenticationError (code: Int, message: CharSequence) {
          call.failure(when (code) {
            BiometricPrompt.BIOMETRIC_ERROR_USER_CANCELED, BiometricPrompt.BIOMETRIC_ERROR_CANCELED -> "ABORT_ERR"
            BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT, BiometricPrompt.BIOMETRIC_ERROR_LOCKOUT_PERMANENT -> "EACCES"
            else -> "ENOTREADY"
          })
        }
        override fun onAuthenticationSucceeded (result: BiometricPrompt.AuthenticationResult) {
          if (call.done.get()) return
          try {
            call.foreground()
            if (cipher != null) {
              if (result.cryptoObject?.cipher !== cipher) throw PlatformFailure("EACCES")
              cipher.updateAAD("oro.secret.v1:${call.app.packageName}:$id:$selected".toByteArray(Charsets.UTF_8))
              val output = cipher.doFinal(input)
              try {
                call.success(if (action == "open") JSONObject().put("value", encode(output)) else
                  JSONObject().put("schema", 1).put("id", id).put("policy", selected)
                    .put("iv", encode(cipher.iv)).put("ciphertext", encode(output)))
              } finally { output.fill(0) }
            } else {
              if (result.cryptoObject?.signature !== signature || options.getLong("expiresAt") <= System.currentTimeMillis()) {
                throw PlatformFailure("EACCES")
              }
              signature!!.update(input)
              call.success(JSONObject().put("algorithm", "ECDSA_P256_SHA256").put("publicKey", publicKey)
                .put("payload", encode(signedPayload.toByteArray(Charsets.UTF_8))).put("signature", encode(signature.sign())))
            }
          } catch (error: PlatformFailure) { call.failure(error.code)
          } catch (_: javax.crypto.AEADBadTagException) { call.failure("EBADMSG")
          } catch (_: Exception) { call.failure("EIO")
          } finally { input.fill(0) }
        }
      })
    } catch (_: KeyPermanentlyInvalidatedException) { throw PlatformFailure("EKEYREVOKED")
    } catch (_: java.security.UnrecoverableKeyException) { throw PlatformFailure("EKEYREVOKED") }
  }
}
