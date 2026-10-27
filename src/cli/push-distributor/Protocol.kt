package dev.oro.push

import java.net.URI
import java.security.SecureRandom
import java.util.Base64

/** Wire limits for the development distributor, independent of runtime app state. */
object Protocol {
  const val CONNECTOR = "org.unifiedpush.android.connector."
  const val DISTRIBUTOR = "org.unifiedpush.android.distributor."
  const val REGISTRATIONS = 32
  const val PENDING = 256

  fun text (value: String?, maximum: Int): Boolean = value != null && value.isNotEmpty() &&
    value.toByteArray(Charsets.UTF_8).size <= maximum && value.none { it.code < 32 || it.code == 127 }

  fun origin (value: String): String {
    val uri = URI(value)
    require(uri.scheme == "https" && uri.host != null && uri.userInfo == null &&
      uri.query == null && uri.fragment == null && uri.rawPath in listOf(null, "", "/") &&
      uri.port in -1..65535 && uri.port != 0 && value.length <= 850) {
      "Use an HTTPS origin without a path, credentials or query"
    }
    return value.removeSuffix("/")
  }

  fun topic (): String = ByteArray(20).also { SecureRandom().nextBytes(it) }
    .let { "up" + Base64.getUrlEncoder().withoutPadding().encodeToString(it) }

  fun vapid (value: String?): Boolean = try {
    require(value != null && Regex("[A-Za-z0-9_-]{87}").matches(value))
    val bytes = Base64.getUrlDecoder().decode(value)
    require(bytes.size == 65 && bytes[0] == 4.toByte() && Base64.getUrlEncoder().withoutPadding().encodeToString(bytes) == value)
    val parameters = java.security.AlgorithmParameters.getInstance("EC").apply {
      init(java.security.spec.ECGenParameterSpec("secp256r1"))
    }.getParameterSpec(java.security.spec.ECParameterSpec::class.java)
    val prime = (parameters.curve.field as java.security.spec.ECFieldFp).p
    val x = java.math.BigInteger(1, bytes.copyOfRange(1, 33))
    val y = java.math.BigInteger(1, bytes.copyOfRange(33, 65))
    x < prime && y < prime && y.multiply(y).mod(prime) ==
      x.pow(3).add(parameters.curve.a.multiply(x)).add(parameters.curve.b).mod(prime)
  } catch (_: Exception) { false }

  fun payload (message: String, encoding: String): ByteArray {
    val bytes = when (encoding) {
      "base64" -> {
        require(message.length <= 5464) { "Payload too large" }
        Base64.getDecoder().decode(message)
      }
      "" -> message.toByteArray(Charsets.UTF_8)
      else -> throw IllegalArgumentException("Unsupported payload encoding")
    }
    require(bytes.size in 1..4096) { "Payload outside UnifiedPush limits" }
    return bytes
  }
}
