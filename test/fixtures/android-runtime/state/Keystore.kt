package android.security.keystore

import java.security.spec.AlgorithmParameterSpec

object KeyProperties {
  const val PURPOSE_ENCRYPT = 1
  const val PURPOSE_DECRYPT = 2
  const val KEY_ALGORITHM_AES = "AES"
  const val BLOCK_MODE_GCM = "GCM"
  const val ENCRYPTION_PADDING_NONE = "NoPadding"
}
class KeyGenParameterSpec(val alias: String, val bits: Int): AlgorithmParameterSpec {
  class Builder(private val alias: String, purpose: Int) {
    private var bits = 256
    fun setKeySize(value: Int) = apply { bits = value }
    fun setBlockModes(vararg value: String) = this
    fun setEncryptionPaddings(vararg value: String) = this
    fun build() = KeyGenParameterSpec(alias, bits)
  }
}
