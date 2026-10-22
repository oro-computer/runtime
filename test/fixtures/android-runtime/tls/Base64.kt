package android.util
object Base64 {
  const val DEFAULT = 0
  const val NO_WRAP = 2
  fun decode(value: String, flags: Int): ByteArray = java.util.Base64.getDecoder().decode(value)
  fun encodeToString(value: ByteArray, flags: Int): String = java.util.Base64.getEncoder().encodeToString(value)
}
