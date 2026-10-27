package oro.runtime.app

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONObject
import java.math.BigInteger
import java.nio.ByteBuffer
import java.security.AlgorithmParameters
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.SecureRandom
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECFieldFp
import java.security.spec.ECPoint
import java.security.spec.ECPublicKeySpec
import java.security.spec.PKCS8EncodedKeySpec
import javax.crypto.Cipher
import javax.crypto.KeyAgreement
import javax.crypto.KeyGenerator
import javax.crypto.Mac
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/** RFC 8291 single-record decryption and authenticated native push storage. */
internal object PushCryptography {
  private const val ALIAS = "oro.push.storage.v1"
  fun encode (value: ByteArray): String = Base64.encodeToString(value,
    Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
  fun decode (value: String): ByteArray {
    require(value.length <= 16384 && Regex("[A-Za-z0-9_-]*").matches(value))
    return Base64.decode(value, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING).also {
      require(encode(it) == value)
    }
  }

  fun hasStorageKey (alias: String = ALIAS): Boolean = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }.containsAlias(alias)

  private fun storageKey (create: Boolean, alias: String): SecretKey {
    val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    (store.getKey(alias, null) as? SecretKey)?.let { return it }
    if (!create || store.containsAlias(alias)) throw PlatformFailure("EKEYREVOKED")
    return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
      init(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
    }.generateKey()
  }

  @JvmOverloads
  fun seal (value: JSONObject, identity: String, create: Boolean, alias: String = ALIAS): String {
    val cipher = Cipher.getInstance("AES/GCM/NoPadding")
    cipher.init(Cipher.ENCRYPT_MODE, storageKey(create, alias))
    cipher.updateAAD(identity.toByteArray(Charsets.UTF_8))
    val bytes = value.toString().toByteArray(Charsets.UTF_8)
    return try {
      JSONObject().put("iv", encode(cipher.iv)).put("body", encode(cipher.doFinal(bytes))).toString()
    } finally { bytes.fill(0) }
  }

  @JvmOverloads
  fun open (value: String, identity: String, alias: String = ALIAS): JSONObject {
    val record = JSONObject(value)
    val iv = decode(record.getString("iv"))
    val body = decode(record.getString("body"))
    require(iv.size == 12 && body.size >= 16)
    val cipher = Cipher.getInstance("AES/GCM/NoPadding")
    cipher.init(Cipher.DECRYPT_MODE, storageKey(false, alias), GCMParameterSpec(128, iv))
    cipher.updateAAD(identity.toByteArray(Charsets.UTF_8))
    val bytes = cipher.doFinal(body)
    return try { JSONObject(bytes.toString(Charsets.UTF_8)) } finally { bytes.fill(0) }
  }

  private fun coordinate (value: BigInteger): ByteArray {
    val bytes = value.toByteArray()
    return ByteArray(32).also {
      val size = minOf(32, bytes.size)
      bytes.copyInto(it, 32 - size, bytes.size - size)
    }
  }

  fun keys (): JSONObject {
    val pair = KeyPairGenerator.getInstance("EC").apply {
      initialize(ECGenParameterSpec("secp256r1"))
    }.generateKeyPair()
    val public = pair.public as ECPublicKey
    return JSONObject().put("privateKey", encode(pair.private.encoded))
      .put("p256dh", encode(byteArrayOf(4) + coordinate(public.w.affineX) + coordinate(public.w.affineY)))
      .put("auth", encode(ByteArray(16).also { SecureRandom().nextBytes(it) }))
  }

  private fun publicKey (encoded: ByteArray): java.security.PublicKey {
    require(encoded.size == 65 && encoded[0] == 4.toByte())
    val parameters = AlgorithmParameters.getInstance("EC").apply { init(ECGenParameterSpec("secp256r1")) }
      .getParameterSpec(ECParameterSpec::class.java)
    val prime = (parameters.curve.field as ECFieldFp).p
    val x = BigInteger(1, encoded.copyOfRange(1, 33))
    val y = BigInteger(1, encoded.copyOfRange(33, 65))
    require(x < prime && y < prime && y.modPow(BigInteger.valueOf(2), prime) ==
      (x.modPow(BigInteger.valueOf(3), prime) + parameters.curve.a * x + parameters.curve.b).mod(prime))
    return KeyFactory.getInstance("EC").generatePublic(ECPublicKeySpec(ECPoint(x, y), parameters))
  }

  fun validatePublicKey (encoded: String) { publicKey(decode(encoded)) }

  private fun hmac (key: ByteArray, value: ByteArray): ByteArray =
    Mac.getInstance("HmacSHA256").apply { init(SecretKeySpec(key, "HmacSHA256")) }.doFinal(value)

  /** Only authenticated aes128gcm payloads of at most 4096 bytes are accepted. */
  fun decrypt (message: ByteArray, keys: JSONObject): ByteArray {
    require(message.size in 103..4096)
    val header = ByteBuffer.wrap(message)
    val salt = ByteArray(16).also { header.get(it) }
    val recordSize = header.int.toLong() and 0xffffffffL
    require((header.get().toInt() and 255) == 65 && recordSize > message.size - 86)
    val sender = ByteArray(65).also { header.get(it) }
    require(sender[0] == 4.toByte())
    val factory = KeyFactory.getInstance("EC")
    val public = publicKey(sender)
    val privateBytes = decode(keys.getString("privateKey"))
    val private = try { factory.generatePrivate(PKCS8EncodedKeySpec(privateBytes)) } finally { privateBytes.fill(0) }
    val shared = KeyAgreement.getInstance("ECDH").apply {
      init(private)
      doPhase(public, true)
    }.generateSecret()
    val auth = decode(keys.getString("auth"))
    require(auth.size == 16)
    val extracted = hmac(auth, shared)
    val combined = hmac(extracted, "WebPush: info\u0000".toByteArray(Charsets.US_ASCII) +
      decode(keys.getString("p256dh")) + sender + byteArrayOf(1))
    val key = hmac(salt, combined)
    val contentKey = hmac(key, "Content-Encoding: aes128gcm\u0000\u0001".toByteArray(Charsets.US_ASCII)).copyOf(16)
    val nonce = hmac(key, "Content-Encoding: nonce\u0000\u0001".toByteArray(Charsets.US_ASCII)).copyOf(12)
    val clear = try {
      Cipher.getInstance("AES/GCM/NoPadding").apply {
        init(Cipher.DECRYPT_MODE, SecretKeySpec(contentKey, "AES"), GCMParameterSpec(128, nonce))
      }.doFinal(message.copyOfRange(86, message.size))
    } finally {
      shared.fill(0)
      auth.fill(0)
      extracted.fill(0)
      combined.fill(0)
      key.fill(0)
      contentKey.fill(0)
      nonce.fill(0)
    }
    return try {
      var end = clear.size - 1
      while (end >= 0 && clear[end] == 0.toByte()) end--
      require(end >= 0 && clear[end] == 2.toByte())
      clear.copyOfRange(0, end)
    } finally { clear.fill(0) }
  }
}
