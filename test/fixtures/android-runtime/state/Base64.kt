package android.util

object Base64 {
  const val NO_WRAP = 2
  fun encodeToString(value: ByteArray, flags: Int) = java.util.Base64.getEncoder().encodeToString(value)
  fun decode(value: String, flags: Int) = java.util.Base64.getDecoder().decode(value)
}
