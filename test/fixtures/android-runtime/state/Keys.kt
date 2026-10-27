package oro.runtime.app

import android.security.keystore.KeyGenParameterSpec
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.security.Key
import java.security.KeyStoreSpi
import java.security.Provider
import java.security.SecureRandom
import java.security.cert.Certificate
import java.security.spec.AlgorithmParameterSpec
import java.util.Date
import java.util.Collections
import javax.crypto.KeyGenerator
import javax.crypto.KeyGeneratorSpi
import javax.crypto.SecretKey
import javax.crypto.spec.SecretKeySpec

/** Host-only public-test provider. Android hardware/OS Keystore is qualified on device separately. */
object FixtureKeys {
  lateinit var directory: File
  fun file(alias: String) = File(directory, alias).also { require(!alias.contains('/')) }
}
class FixtureProvider: Provider("AndroidKeyStore", "1", "Public native-state test keys") {
  init { put("KeyStore.AndroidKeyStore", FixtureKeyStore::class.java.name); put("KeyGenerator.AES", FixtureGenerator::class.java.name) }
}
class FixtureGenerator: KeyGeneratorSpi() {
  private lateinit var spec: KeyGenParameterSpec
  override fun engineInit(random: SecureRandom?) { error("Parameters required") }
  override fun engineInit(size: Int, random: SecureRandom?) { error("Parameters required") }
  override fun engineInit(params: AlgorithmParameterSpec, random: SecureRandom?) { spec = params as KeyGenParameterSpec }
  override fun engineGenerateKey(): SecretKey {
    val generator = KeyGenerator.getInstance("AES", "SunJCE"); generator.init(spec.bits)
    return generator.generateKey().also { FixtureKeys.file(spec.alias).writeBytes(it.encoded) }
  }
}
class FixtureKeyStore: KeyStoreSpi() {
  override fun engineLoad(input: InputStream?, password: CharArray?) {}
  override fun engineStore(output: OutputStream?, password: CharArray?) {}
  override fun engineGetKey(alias: String, password: CharArray?): Key? = FixtureKeys.file(alias).let {
    if (it.exists()) SecretKeySpec(it.readBytes(), "AES") else null
  }
  override fun engineDeleteEntry(alias: String) { FixtureKeys.file(alias).delete() }
  override fun engineContainsAlias(alias: String) = FixtureKeys.file(alias).exists()
  override fun engineAliases() = Collections.enumeration(FixtureKeys.directory.list()?.toList() ?: emptyList())
  override fun engineSize() = FixtureKeys.directory.list()?.size ?: 0
  override fun engineIsKeyEntry(alias: String) = engineContainsAlias(alias)
  override fun engineIsCertificateEntry(alias: String) = false
  override fun engineGetCreationDate(alias: String) = Date(0)
  override fun engineGetCertificate(alias: String): Certificate? = null
  override fun engineGetCertificateChain(alias: String): Array<Certificate>? = null
  override fun engineGetCertificateAlias(certificate: Certificate): String? = null
  override fun engineSetCertificateEntry(alias: String, certificate: Certificate) { error("Unsupported") }
  override fun engineSetKeyEntry(alias: String, key: ByteArray, chain: Array<Certificate>?) { error("Unsupported") }
  override fun engineSetKeyEntry(alias: String, key: Key, password: CharArray?, chain: Array<Certificate>?) { error("Unsupported") }
}
