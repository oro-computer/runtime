// vim: set sw=2:
package oro.runtime.securestorage

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64

import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

import oro.runtime.debug.console

object SecureStorageManager {
  private const val PRIMARY_STORE_NAME = "oro_secure_storage"

  private const val PRIMARY_KEYSTORE_ALIAS = "OroSecureStorageKey"

  private const val PRIMARY_PREFIX = "oro"

  private const val KEYSTORE = "AndroidKeyStore"
  private const val TRANSFORMATION = "${KeyProperties.KEY_ALGORITHM_AES}/${KeyProperties.BLOCK_MODE_GCM}/${KeyProperties.ENCRYPTION_PADDING_NONE}"
  private const val IV_SIZE_BYTES = 12
  private const val TAG_LENGTH_BITS = 128

  @JvmStatic
  fun set(context: Context, scope: String, key: String, value: ByteArray): Boolean {
    return try {
      val storageKey = storageKey(PRIMARY_PREFIX, scope, key)
      val encoded = encrypt(value, PRIMARY_KEYSTORE_ALIAS)
      primaryPreferences(context).edit().putString(storageKey, encoded).apply()
      true
    } catch (err: Exception) {
      console.error("SecureStorageManager: failed to store value: ${err.message}")
      false
    }
  }

  @JvmStatic
  fun get(context: Context, scope: String, key: String): ByteArray? {
    val primaryKey = storageKey(PRIMARY_PREFIX, scope, key)

    val primaryEncoded = primaryPreferences(context).getString(primaryKey, null)
    if (!primaryEncoded.isNullOrEmpty()) {
      try {
        return decrypt(primaryEncoded, PRIMARY_KEYSTORE_ALIAS)
      } catch (err: Exception) {
        console.error("SecureStorageManager: failed to read value: ${err.message}")
      }
    }

    return null
  }

  @JvmStatic
  fun remove(context: Context, scope: String, key: String): Boolean {
    return try {
      val primaryKey = storageKey(PRIMARY_PREFIX, scope, key)

      primaryPreferences(context).edit().remove(primaryKey).apply()
      true
    } catch (err: Exception) {
      console.error("SecureStorageManager: failed to remove value: ${err.message}")
      false
    }
  }

  @JvmStatic
  fun clear(context: Context, scope: String): Boolean {
    return try {
      clearByPrefix(primaryPreferences(context), storagePrefix(PRIMARY_PREFIX, scope))
      true
    } catch (err: Exception) {
      console.error("SecureStorageManager: failed to clear values: ${err.message}")
      false
    }
  }

  @JvmStatic
  fun keys(context: Context, scope: String): Array<String> {
    return try {
      val keys = linkedSetOf<String>()
      collectKeys(primaryPreferences(context), storagePrefix(PRIMARY_PREFIX, scope), keys)
      keys.toTypedArray()
    } catch (err: Exception) {
      console.error("SecureStorageManager: failed to list keys: ${err.message}")
      emptyArray()
    }
  }

  private fun primaryPreferences(context: Context): SharedPreferences {
    return context.getSharedPreferences(PRIMARY_STORE_NAME, Context.MODE_PRIVATE)
  }

  private fun clearByPrefix(prefs: SharedPreferences, prefix: String) {
    val editor = prefs.edit()
    prefs.all.keys
      .filter { it.startsWith(prefix) }
      .forEach { editor.remove(it) }
    editor.apply()
  }

  private fun collectKeys(prefs: SharedPreferences, prefix: String, out: MutableSet<String>) {
    prefs.all.keys
      .filter { it.startsWith(prefix) }
      .mapNotNull { encodedKey ->
        val encoded = encodedKey.removePrefix(prefix)
        decodeComponent(encoded)
      }
      .forEach { out.add(it) }
  }

  private fun storageKey(prefix: String, scope: String, key: String): String {
    return storagePrefix(prefix, scope) + encodeComponent(key)
  }

  private fun storagePrefix(prefix: String, scope: String): String {
    return "${prefix}:${encodeComponent(scope)}:"
  }

  private fun encodeComponent(value: String): String {
    return Base64.encodeToString(value.toByteArray(Charsets.UTF_8), Base64.NO_WRAP)
  }

  private fun decodeComponent(value: String): String? {
    return try {
      val decoded = Base64.decode(value, Base64.NO_WRAP)
      String(decoded, Charsets.UTF_8)
    } catch (_: IllegalArgumentException) {
      null
    }
  }

  private fun encrypt(input: ByteArray, alias: String): String {
    val cipher = Cipher.getInstance(TRANSFORMATION)
    cipher.init(Cipher.ENCRYPT_MODE, getOrCreateSecretKey(alias))

    val iv = cipher.iv ?: throw IllegalStateException("Cipher IV missing")
    val encrypted = cipher.doFinal(input)

    val combined = ByteArray(iv.size + encrypted.size)
    System.arraycopy(iv, 0, combined, 0, iv.size)
    System.arraycopy(encrypted, 0, combined, iv.size, encrypted.size)

    return Base64.encodeToString(combined, Base64.NO_WRAP)
  }

  private fun decrypt(encoded: String, alias: String): ByteArray {
    val combined = Base64.decode(encoded, Base64.NO_WRAP)
    if (combined.size < IV_SIZE_BYTES) {
      throw IllegalArgumentException("Encrypted payload too small")
    }

    val iv = combined.copyOfRange(0, IV_SIZE_BYTES)
    val payload = combined.copyOfRange(IV_SIZE_BYTES, combined.size)

    val key = getSecretKeyOrNull(alias) ?: throw IllegalStateException("Missing secret key")

    val cipher = Cipher.getInstance(TRANSFORMATION)
    cipher.init(
      Cipher.DECRYPT_MODE,
      key,
      GCMParameterSpec(TAG_LENGTH_BITS, iv)
    )

    return cipher.doFinal(payload)
  }

  private fun getSecretKeyOrNull(alias: String): SecretKey? {
    return try {
      val keyStore = KeyStore.getInstance(KEYSTORE).apply { load(null) }
      keyStore.getKey(alias, null) as? SecretKey
    } catch (_: Exception) {
      null
    }
  }

  private fun getOrCreateSecretKey(alias: String): SecretKey {
    val existing = getSecretKeyOrNull(alias)
    if (existing != null) {
      return existing
    }

    val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
    val spec = KeyGenParameterSpec.Builder(
      alias,
      KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
    )
      .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
      .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
      .setRandomizedEncryptionRequired(true)
      .build()

    generator.init(spec)
    return generator.generateKey()
  }
}
